#include "ai.h"
#include "population.h"
#include "camera.h"
#include "plan.h"
#include "stocks.h"
#include "trade.h"

#include <sstream>

#include "modules/Buildings.h"
#include "modules/Gui.h"
#include "modules/Translation.h"
#include "modules/Units.h"

#include "df/abstract_building_inn_tavernst.h"
#include "df/abstract_building_libraryst.h"
#include "df/abstract_building_templest.h"
#include "df/activity_entry.h"
#include "df/activity_event.h"
#include "df/activity_event_participants.h"
#include "df/assign_trade_status.h"
#include "df/building_civzonest.h"
#include "df/building_farmplotst.h"
#include "df/building_stockpilest.h"
#include "df/building_tradedepotst.h"
#include "df/building_workshopst.h"
#include "df/caravan_state.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/entity_buy_prices.h"
#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/entity_position_raw.h"
#include "df/entity_raw.h"
#include "df/entity_sell_prices.h"
#include "df/general_ref_building_civzone_assignedst.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/general_ref_unit_workerst.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/history_event_hist_figure_diedst.h"
#include "df/incident.h"
#include "df/item_weaponst.h"
#include "df/itemdef_weaponst.h"
#include "df/job.h"
#include "df/manager_order.h"
#include "df/occupation.h"
#include "df/reaction.h"
#include "df/squad.h"
#include "df/squad_ammo_spec.h"
#include "df/squad_order_kill_listst.h"
#include "df/squad_order_trainst.h"
#include "df/squad_position.h"
#include "df/squad_schedule_order.h"
#include "df/squad_uniform_spec.h"
#include "df/ui.h"
#include "df/ui_sidebar_menus.h"
#include "df/uniform_category.h"
#include "df/unit_misc_trait.h"
#include "df/unit_relationship_type.h"
#include "df/unit_skill.h"
#include "df/unit_soul.h"
#include "df/unit_wound.h"
#include "df/viewscreen_layer_assigntradest.h"
#include "df/viewscreen_layer_noblelistst.h"
#include "df/viewscreen_locationsst.h"
#include "df/viewscreen_tradelistst.h"
#include "df/viewscreen_tradegoodsst.h"
#include "df/world.h"
#include "df/world_site.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(standing_orders_forbid_used_ammo);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(ui_building_assign_units);
REQUIRE_GLOBAL(ui_building_item_cursor);
REQUIRE_GLOBAL(ui_sidebar_menus);
REQUIRE_GLOBAL(world);

const static struct LaborInfo
{
    std::vector<df::unit_labor> list;
    std::set<df::unit_labor> tool;
    std::map<df::unit_labor, df::job_skill> skill;
    std::set<df::unit_labor> idle;
    std::set<df::unit_labor> medical;
    std::map<df::unit_labor, std::set<std::string>> stocks;
    std::set<df::unit_labor> hauling;
    std::map<df::unit_labor, int32_t> min, max, min_pct, max_pct;
    std::set<df::job_type> wont_work_job;

    LaborInfo()
    {
        FOR_ENUM_ITEMS(unit_labor, ul)
        {
            if (ul != unit_labor::NONE)
            {
                list.push_back(ul);
                min[ul] = 2;
                max[ul] = 8;
                min_pct[ul] = 0;
                max_pct[ul] = 40;
                if (ENUM_KEY_STR(unit_labor, ul).find("HAUL") != std::string::npos)
                {
                    hauling.insert(ul);
                }
            }
        }

        tool.insert(unit_labor::MINE);
        tool.insert(unit_labor::CUTWOOD);
        tool.insert(unit_labor::HUNT);

        FOR_ENUM_ITEMS(job_skill, js)
        {
            df::unit_labor ul = ENUM_ATTR(job_skill, labor, js);
            if (ul != unit_labor::NONE)
            {
                skill[ul] = js;
            }
        }

        idle.insert(unit_labor::PLANT);
        idle.insert(unit_labor::HERBALIST);
        idle.insert(unit_labor::FISH);
        idle.insert(unit_labor::DETAIL);

        medical.insert(unit_labor::DIAGNOSE);
        medical.insert(unit_labor::SURGERY);
        medical.insert(unit_labor::BONE_SETTING);
        medical.insert(unit_labor::SUTURING);
        medical.insert(unit_labor::DRESSING_WOUNDS);
        medical.insert(unit_labor::FEED_WATER_CIVILIANS);

        stocks[unit_labor::PLANT].insert("food");
        stocks[unit_labor::PLANT].insert("drink");
        stocks[unit_labor::PLANT].insert("cloth");
        stocks[unit_labor::HERBALIST].insert("food");
        stocks[unit_labor::HERBALIST].insert("drink");
        stocks[unit_labor::HERBALIST].insert("cloth");
        stocks[unit_labor::FISH].insert("food");

        hauling.insert(unit_labor::FEED_WATER_CIVILIANS);
        hauling.insert(unit_labor::RECOVER_WOUNDED);

        wont_work_job.insert(job_type::AttendParty);
        wont_work_job.insert(job_type::Rest);
        wont_work_job.insert(job_type::UpdateStockpileRecords);

        min[unit_labor::DETAIL] = 4;
        min[unit_labor::PLANT] = 4;
        min[unit_labor::HERBALIST] = 1;

        max[unit_labor::FISH] = 1;

        min_pct[unit_labor::DETAIL] = 5;
        min_pct[unit_labor::PLANT] = 30;
        min_pct[unit_labor::FISH] = 1;
        min_pct[unit_labor::HERBALIST] = 10;

        max_pct[unit_labor::DETAIL] = 60;
        max_pct[unit_labor::PLANT] = 80;
        max_pct[unit_labor::FISH] = 10;
        max_pct[unit_labor::HERBALIST] = 30;

        for (auto it = hauling.begin(); it != hauling.end(); it++)
        {
            min_pct[*it] = 30;
            max_pct[*it] = 100;
        }
    }
} labors;

static int32_t days_since(int32_t year, int32_t tick)
{
    return (*cur_year - year) * 12 * 28 + (*cur_year_tick - tick) / 1200;
}

Population::Population(AI *ai) :
    ai(ai),
    citizen(),
    military(),
    pet(),
    pet_check(),
    visitor(),
    resident(),
    update_counter(0),
    onupdate_handle(nullptr),
    seen_death(0),
    deathwatch_handle(nullptr),
    medic(),
    workers(),
    seen_badwork()
{
}

Population::~Population()
{
}

command_result Population::startup(color_ostream &)
{
    *standing_orders_forbid_used_ammo = 0;
    return CR_OK;
}

command_result Population::onupdate_register(color_ostream &)
{
    onupdate_handle = events.onupdate_register("df-ai pop", 360, 10, [this](color_ostream & out) { update(out); });
    deathwatch_handle = events.onupdate_register("df-ai pop deathwatch", 1, 1, [this](color_ostream & out) { deathwatch(out); });
    return CR_OK;
}

command_result Population::onupdate_unregister(color_ostream &)
{
    events.onupdate_unregister(onupdate_handle);
    events.onupdate_unregister(deathwatch_handle);
    return CR_OK;
}

void Population::update(color_ostream & out)
{
    update_counter++;
    switch (update_counter % 10)
    {
    case 0:
        update_trading(out);
        break;
    case 1:
        update_citizenlist(out);
        break;
    case 2:
        update_nobles(out);
        break;
    case 3:
        update_jobs(out);
        break;
    case 4:
        update_military(out);
        break;
    case 5:
        update_pets(out);
        break;
    case 6:
        update_deads(out);
        break;
    case 7:
        update_caged(out);
        break;
    case 8:
        update_locations(out);
        break;
    case 9:
        if (ai->eventsJson.is_open())
        {
            Json::Value payload(Json::objectValue);
            payload["citizen"] = Json::UInt(citizen.size());
            payload["military"] = Json::UInt(military.size());
            payload["pet"] = Json::UInt(pet.size());
            payload["visitor"] = Json::UInt(visitor.size());
            payload["resident"] = Json::UInt(resident.size());
            ai->event("population", payload);
        }
        break;
    }
}

void Population::deathwatch(color_ostream & out)
{
    if (world->history.events2.size() == seen_death)
    {
        return;
    }

    auto it = world->history.events2.begin();
    std::advance(it, seen_death);
    for (; it != world->history.events2.end(); it++)
    {
        auto d = virtual_cast<df::history_event_hist_figure_diedst>(*it);

        if (!d || d->site != ui->site_id)
        {
            continue;
        }

        ai->debug(out, "[RIP] " + AI::describe_event(d));
    }

    seen_death = world->history.events2.size();
}

void Population::new_citizen(color_ostream & out, int32_t id)
{
    citizen.insert(id);
    ai->plan->new_citizen(out, id);
}

void Population::del_citizen(color_ostream & out, int32_t id)
{
    citizen.erase(id);
    military.erase(id);
    ai->plan->del_citizen(out, id);
}

void Population::update_trading(color_ostream & out)
{
    bool any_traders = ai->trade->can_move_goods();

    if (any_traders && did_trade)
    {
        return;
    }

    if (!any_traders)
    {
        did_trade = false;
    }

    if (set_up_trading(out, any_traders))
    {
        if (any_traders)
        {
            ai->debug(out, "[trade] Requested broker at depot.");
        }
        else
        {
            ai->debug(out, "[trade] Dismissed broker from depot: no traders");
        }
        return;
    }

    if (!any_traders)
    {
        return;
    }

    df::entity_position *broker_pos = position_with_responsibility(entity_position_responsibility::TRADE);
    if (!broker_pos)
    {
        ai->debug(out, "[trade] Could not find broker position!");
        return;
    }

    auto broker_assignment = std::find_if(ui->main.fortress_entity->positions.assignments.begin(), ui->main.fortress_entity->positions.assignments.end(), [broker_pos](df::entity_position_assignment *asn) -> bool { return asn->position_id == broker_pos->id; });
    if (broker_assignment == ui->main.fortress_entity->positions.assignments.end())
    {
        ai->debug(out, "[trade] Could not find broker assignment!");
        return;
    }

    df::historical_figure *broker_hf = df::historical_figure::find((*broker_assignment)->histfig);
    if (!broker_hf)
    {
        ai->debug(out, "[trade] Could not find broker!");
        return;
    }

    df::unit *broker = df::unit::find(broker_hf->unit_id);
    if (!broker)
    {
        ai->debug(out, "[trade] Could not find broker unit!");
        return;
    }

    if (!broker->job.current_job)
    {
        ai->debug(out, "[trade] Waiting for the broker: " + AI::describe_unit(broker));
        return;
    }

    if (broker->job.current_job->job_type != job_type::TradeAtDepot)
    {
        ai->debug(out, "[trade] Waiting for the broker to do their job: " + AI::describe_job(broker->job.current_job) + " - " + AI::describe_unit(broker));
        return;
    }

    room *depot = ai->plan->find_room(room_type::tradedepot, [broker](room *r) -> bool { return r->include(broker->pos); });
    if (!depot)
    {
        ai->debug(out, "[trade] Broker en route to depot. Currently at " + ai->plan->describe_room(ai->plan->find_room_at(broker->pos)));
        return;
    }

    df::caravan_state *caravan = nullptr;
    for (auto it = ui->caravans.begin(); it != ui->caravans.end(); it++)
    {
        if ((*it)->trade_state == df::caravan_state::AtDepot)
        {
            caravan = *it;
        }
        else if ((*it)->trade_state == df::caravan_state::Approaching)
        {
            ai->debug(out, "[trade] Waiting for the traders from " + AI::describe_name(df::historical_entity::find((*it)->entity)->name) + " to arrive at the depot...");
        }
    }

    if (!caravan)
    {
        return;
    }

    int32_t waiting_for_items = 0;
    for (auto j = world->job_list.next; j; j = j->next)
    {
        if (j->item->job_type == job_type::BringItemToDepot)
        {
            waiting_for_items++;
        }
    }

    if (waiting_for_items)
    {
        if (caravan->time_remaining < 1000)
        {
            ai->debug(out, stl_sprintf("[trade] Waiting for %d more items to arrive at the depot, but time is running out. Trading with what we have.", waiting_for_items));
        }
        else
        {
            ai->debug(out, stl_sprintf("[trade] Waiting for %d more items to arrive at the depot.", waiting_for_items));
            return;
        }
    }

    if (perform_trade(out))
    {
        did_trade = true;

        if (set_up_trading(out, false))
        {
            ai->debug(out, "[trade] Dismissed broker from depot: finished trade");
        }
    }
}

void Population::update_citizenlist(color_ostream & out)
{
    std::set<int32_t> old = citizen;

    visitor.clear();
    resident.clear();

    // add new fort citizen to our list
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        df::creature_raw *race = df::creature_raw::find(u->race);
        if (Units::isCitizen(u) && !Units::isBaby(u))
        {
            if (old.count(u->id))
            {
                old.erase(u->id);
            }
            else
            {
                new_citizen(out, u->id);

                if (ai->eventsJson.is_open())
                {
                    Json::Value payload(Json::objectValue);
                    payload["id"] = Json::Int(u->id);
                    payload["name"] = DF2UTF(AI::describe_name(u->name, false));
                    payload["name_english"] = DF2UTF(AI::describe_name(u->name, true));
                    payload["birth_year"] = Json::Int(u->birth_year);
                    payload["birth_time"] = Json::Int(u->birth_time);
                    if (race)
                    {
                        payload["race"] = race->creature_id;
                        payload["caste"] = race->caste[u->caste]->caste_id;
                    }
                    payload["sex"] = u->sex == 0 ? "female" : u->sex == 1 ? "male" : "unknown";
                    ai->event("new citizen", payload);
                }
            }
        }
        else if (u->flags1.bits.dead || u->flags1.bits.merchant || u->flags1.bits.forest || u->flags2.bits.slaughter)
        {
            // ignore
        }
        else if (u->flags2.bits.visitor)
        {
            visitor.insert(u->id);
        }
        else if (Units::isOwnCiv(u) && !Units::isOwnGroup(u) && race && race->caste[u->caste]->flags.is_set(caste_raw_flags::CAN_LEARN))
        {
            resident.insert(u->id);
        }
    }

    // del those who are no longer here
    for (auto it = old.begin(); it != old.end(); it++)
    {
        // u.counters.death_tg.flags.discovered dead/missing
        del_citizen(out, *it);

        if (ai->eventsJson.is_open())
        {
            Json::Value payload(Json::objectValue);
            payload["id"] = Json::Int(*it);
            if (df::unit *u = df::unit::find(*it))
            {
                payload["name"] = DF2UTF(AI::describe_name(u->name, false));
                payload["name_english"] = DF2UTF(AI::describe_name(u->name, true));
                payload["birth_year"] = Json::Int(u->birth_year);
                payload["birth_time"] = Json::Int(u->birth_time);
                if (df::incident *i = df::incident::find(u->counters.death_id))
                {
                    payload["death_year"] = Json::Int(i->event_year);
                    payload["death_time"] = Json::Int(i->event_time);
                    payload["death_cause"] = ENUM_KEY_STR(death_type, i->death_cause);
                }
                if (df::creature_raw *race = df::creature_raw::find(u->race))
                {
                    payload["race"] = race->creature_id;
                    payload["caste"] = race->caste[u->caste]->caste_id;
                }
                payload["sex"] = u->sex == 0 ? "female" : u->sex == 1 ? "male" : "unknown";
            }
            ai->event("del citizen", payload);
        }
    }
}

void Population::update_jobs(color_ostream &)
{
    for (auto j = world->job_list.next; j; j = j->next)
    {
        if (j->item->flags.bits.suspend && !j->item->flags.bits.repeat)
        {
            j->item->flags.bits.suspend = 0;
        }
    }
}

void Population::update_deads(color_ostream & out)
{
    for (auto it = world->units.all.begin(); it != world->units.all.end(); it++)
    {
        df::unit *u = *it;
        if (u->flags3.bits.ghostly)
        {
            ai->stocks->queue_slab(out, u->hist_figure_id);
        }
    }
}

void Population::update_caged(color_ostream & out)
{
    int32_t count = 0;
    for (auto it = world->items.other[items_other_id::CAGE].begin(); it != world->items.other[items_other_id::CAGE].end(); it++)
    {
        df::item *cage = *it;
        if (!cage->flags.bits.on_ground)
        {
            continue;
        }
        for (auto ref = cage->general_refs.begin(); ref != cage->general_refs.end(); ref++)
        {
            if (virtual_cast<df::general_ref_contains_itemst>(*ref))
            {
                df::item *i = (*ref)->getItem();
                if (i->flags.bits.dump && !i->flags.bits.forbid)
                {
                    continue;
                }
                count++;
                i->flags.bits.dump = 1;
                i->flags.bits.forbid = 0;
            }
            else if (virtual_cast<df::general_ref_contains_unitst>(*ref))
            {
                df::unit *u = (*ref)->getUnit();
                if (Units::isOwnCiv(u))
                {
                    // TODO rescue caged dwarves
                }
                else
                {
                    size_t waiting_items = 0;

                    for (auto ii = u->inventory.begin(); ii != u->inventory.end(); ii++)
                    {
                        if (auto owner = Items::getOwner((*ii)->item))
                        {
                            ai->debug(out, "pop: cannot strip item " + AI::describe_item((*ii)->item) + " owned by " + AI::describe_unit(owner));
                            continue;
                        }
                        waiting_items++;
                        if ((*ii)->item->flags.bits.dump && !(*ii)->item->flags.bits.forbid)
                        {
                            continue;
                        }
                        count++;
                        (*ii)->item->flags.bits.dump = 1;
                        (*ii)->item->flags.bits.forbid = 0;
                        ai->debug(out, "pop: marked item " + AI::describe_item((*ii)->item) + " for dumping");
                    }

                    if (!waiting_items)
                    {
                        room *r = ai->plan->find_room(room_type::pitcage, [](room *r) -> bool { return r->dfbuilding(); });
                        if (r && ai->plan->spiral_search(r->pos(), 1, 1, [cage](df::coord t) -> bool { return t == cage->pos; }).isValid())
                        {
                            assign_unit_to_zone(u, virtual_cast<df::building_civzonest>(r->dfbuilding()));
                            ai->debug(out, "pop: marked " + AI::describe_unit(u) + " for pitting");
                            military_random_squad_attack_unit(out, u);
                        }
                    }
                    else
                    {
                        ai->debug(out, stl_sprintf("pop: waiting for %s to be stripped for pitting (%d items remain)", AI::describe_unit(u).c_str(), waiting_items));
                    }
                }
            }
        }
    }
    if (count > 0)
    {
        ai->debug(out, stl_sprintf("pop: dumped %d items from cages", count));
    }
}

void Population::update_military(color_ostream & out)
{
    // check for new soldiers, allocate barracks
    std::vector<int32_t> newsoldiers;

    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        if (Units::isCitizen(u))
        {
            if (u->military.squad_id == -1)
            {
                if (military.erase(u->id))
                {
                    ai->plan->freesoldierbarrack(out, u->id);
                }
            }
            else
            {
                if (!military.count(u->id))
                {
                    military[u->id] = u->military.squad_id;
                    newsoldiers.push_back(u->id);
                }
            }
        }
    }

    // enlist new soldiers if needed
    std::vector<df::unit *> maydraft;
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        std::vector<Units::NoblePosition> positions;
        if (Units::isCitizen(u) && !Units::isChild(u) && !Units::isBaby(u) && u->mood == mood_type::None && u->military.squad_id == -1 && !Units::getNoblePositions(&positions, u))
        {
            bool hasTool = false;
            for (auto it = labors.tool.begin(); it != labors.tool.end(); it++)
            {
                if (u->status.labors[*it])
                {
                    hasTool = true;
                    break;
                }
            }
            if (hasTool)
            {
                continue;
            }
            maydraft.push_back(u);
        }
    }
    size_t axes = 0, picks = 0;
    for (auto it = world->items.other[items_other_id::WEAPON].begin(); it != world->items.other[items_other_id::WEAPON].end(); it++)
    {
        df::item_weaponst *weapon = virtual_cast<df::item_weaponst>(*it);
        if (!weapon || !weapon->subtype || !weapon->subtype->flags.is_set(weapon_flags::HAS_EDGE_ATTACK))
        {
            continue;
        }

        if (weapon->getMeleeSkill() == job_skill::AXE)
        {
            axes++;
        }
        else if (weapon->getMeleeSkill() == job_skill::MINING)
        {
            picks++;
        }
    }
    while (military.size() < maydraft.size() / 5 && military.size() + 1 < axes && military.size() + 1 < picks)
    {
        df::unit *ns = military_find_new_soldier(out, maydraft);
        if (!ns)
        {
            break;
        }
        military[ns->id] = ns->military.squad_id;
        newsoldiers.push_back(ns->id);
    }

    for (auto it = newsoldiers.begin(); it != newsoldiers.end(); it++)
    {
        ai->plan->getsoldierbarrack(out, *it);
    }
}

// with a population of 200:
const static int32_t wanted_tavern_keeper = 4;
const static int32_t wanted_tavern_keeper_min = 1;
const static int32_t wanted_tavern_performer = 8;
const static int32_t wanted_tavern_performer_min = 0;
const static int32_t wanted_library_scholar = 16;
const static int32_t wanted_library_scholar_min = 0;
const static int32_t wanted_library_scribe = 2;
const static int32_t wanted_library_scribe_min = 0;
const static int32_t wanted_temple_performer = 4;
const static int32_t wanted_temple_performer_min = 0;

void Population::update_locations(color_ostream & out)
{
    // not urgent, wait for next cycle.
    if (!AI::is_dwarfmode_viewscreen())
        return;

    // accept all petitions
    while (!ui->petitions.empty())
    {
        AI::feed_key(interface_key::D_PETITIONS);
        AI::feed_key(interface_key::OPTION1);
        AI::feed_key(interface_key::LEAVESCREEN);
    }

#define INIT_NEED(name) int32_t need_##name = std::max(wanted_##name * int32_t(citizen.size()) / 200, wanted_##name##_min)
    INIT_NEED(tavern_keeper);
    INIT_NEED(tavern_performer);
    INIT_NEED(library_scholar);
    INIT_NEED(library_scribe);
    INIT_NEED(temple_performer);
#undef INIT_NEED

    if (room *tavern = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::tavern && r->dfbuilding(); }))
    {
        df::building *bld = tavern->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_inn_tavernst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto it = loc->occupations.begin(); it != loc->occupations.end(); it++)
            {
                if ((*it)->unit_id != -1)
                {
                    if ((*it)->type == occupation_type::TAVERN_KEEPER)
                    {
                        need_tavern_keeper--;
                    }
                    else if ((*it)->type == occupation_type::PERFORMER)
                    {
                        need_tavern_performer--;
                    }
                }
            }
            if (need_tavern_keeper > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::TAVERN_KEEPER);
            }
            if (need_tavern_performer > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::PERFORMER);
            }
        }
    }

    if (room *library = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::library && r->dfbuilding(); }))
    {
        df::building *bld = library->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_libraryst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto it = loc->occupations.begin(); it != loc->occupations.end(); it++)
            {
                if ((*it)->unit_id != -1)
                {
                    if ((*it)->type == occupation_type::SCHOLAR)
                    {
                        need_library_scholar--;
                    }
                    else if ((*it)->type == occupation_type::SCRIBE)
                    {
                        need_library_scribe--;
                    }
                }
            }
            if (need_library_scholar > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::SCHOLAR);
            }
            if (need_library_scribe > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::SCRIBE);
            }
        }
    }

    if (room *temple = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::temple && r->dfbuilding(); }))
    {
        df::building *bld = temple->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_templest>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto it = loc->occupations.begin(); it != loc->occupations.end(); it++)
            {
                if ((*it)->unit_id != -1)
                {
                    if ((*it)->type == occupation_type::PERFORMER)
                    {
                        need_temple_performer--;
                    }
                }
            }
            if (need_temple_performer > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::PERFORMER);
            }
        }
    }
}

void Population::assign_occupation(color_ostream & out, df::building *, df::abstract_building *loc, df::occupation_type occ)
{
    AI::feed_key(interface_key::D_LOCATIONS);

    auto view = strict_virtual_cast<df::viewscreen_locationsst>(Gui::getCurViewscreen(true));
    if (!view)
    {
        ai->debug(out, "[ERROR] expected viewscreen_locationsst");
        return;
    }

    while (view->locations[view->location_idx] != loc)
    {
        AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
    }

    AI::feed_key(interface_key::STANDARDSCROLL_RIGHT);

    while (true)
    {
        if (view->occupations[view->occupation_idx]->unit_id == -1 &&
            view->occupations[view->occupation_idx]->type == occ)
        {
            break;
        }
        AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
    }

    AI::feed_key(interface_key::SELECT);

    df::unit *chosen = nullptr;
    int32_t best = std::numeric_limits<int32_t>::max();
    for (auto it = view->units.begin(); it != view->units.end(); it++)
    {
        df::unit *u = *it;

        if (!u || u->military.squad_id != -1)
        {
            continue;
        }

        std::vector<Units::NoblePosition> positions;
        Units::getNoblePositions(&positions, u);
        if (!positions.empty())
        {
            continue;
        }

        bool has_occupation = false;
        for (auto o = world->occupations.all.begin(); o != world->occupations.all.end(); o++)
        {
            if ((*o)->unit_id == u->id)
            {
                has_occupation = true;
                break;
            }
        }

        if (has_occupation)
        {
            continue;
        }

        int32_t score = unit_totalxp(u);
        if (!chosen || score < best)
        {
            chosen = u;
            best = score;
        }
    }

    if (!chosen)
    {
        ai->debug(out, "pop: could not find unit for occupation " + ENUM_KEY_STR(occupation_type, occ) + " at " + AI::describe_name(*loc->getName(), true));

        AI::feed_key(interface_key::LEAVESCREEN);

        AI::feed_key(interface_key::LEAVESCREEN);

        return;
    }

    ai->debug(out, "pop: assigning occupation " + ENUM_KEY_STR(occupation_type, occ) + " at " + AI::describe_name(*loc->getName(), true) + " to " + AI::describe_unit(chosen));

    while (true)
    {
        if (view->units[view->unit_idx] == chosen)
        {
            break;
        }
        AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
    }

    AI::feed_key(interface_key::SELECT);

    AI::feed_key(interface_key::LEAVESCREEN);
}

void Population::military_random_squad_attack_unit(color_ostream & out, df::unit *u)
{
    df::squad *squad = nullptr;
    int32_t best = std::numeric_limits<int32_t>::min();
    for (auto sqid = ui->main.fortress_entity->squads.begin(); sqid != ui->main.fortress_entity->squads.end(); sqid++)
    {
        df::squad *sq = df::squad::find(*sqid);

        int32_t score = 0;
        for (auto sp = sq->positions.begin(); sp != sq->positions.end(); sp++)
        {
            if ((*sp)->occupant != -1)
            {
                score++;
            }
        }
        score -= int32_t(sq->orders.size());

        if (!squad || best < score)
        {
            squad = sq;
            best = score;
        }
    }
    if (!squad)
    {
        return;
    }

    military_squad_attack_unit(out, squad, u);
}

bool Population::military_all_squads_attack_unit(color_ostream & out, df::unit *u)
{
    bool any = false;
    for (auto sqid = ui->main.fortress_entity->squads.begin(); sqid != ui->main.fortress_entity->squads.end(); sqid++)
    {
        if (military_squad_attack_unit(out, df::squad::find(*sqid), u))
            any = true;
    }
    return any;
}

bool Population::military_squad_attack_unit(color_ostream & out, df::squad *squad, df::unit *u)
{
    if (Units::isOwnCiv(u))
    {
        return false;
    }

    for (auto it = squad->orders.begin(); it != squad->orders.end(); it++)
    {
        if (auto so = strict_virtual_cast<df::squad_order_kill_listst>(*it))
        {
            if (std::find(so->units.begin(), so->units.end(), u->id) != so->units.end())
            {
                return false;
            }
        }
    }

    df::squad_order_kill_listst *so = df::allocate<df::squad_order_kill_listst>();
    so->units.push_back(u->id);
    so->title = AI::describe_unit(u);
    squad->orders.push_back(so);
    ai->debug(out, "sending " + AI::describe_name(squad->name, true) + " to attack " + AI::describe_unit(u));
    return true;
}

df::entity_position *Population::military_find_captain_pos()
{
    for (auto it = ui->main.fortress_entity->positions.own.begin(); it != ui->main.fortress_entity->positions.own.end(); it++)
    {
        if ((*it)->flags.is_set(entity_position_flags::MILITARY_SCREEN_ONLY))
        {
            return *it;
        }
    }
    return nullptr;
}

// returns an unit newly assigned to a military squad
df::unit *Population::military_find_new_soldier(color_ostream & out, const std::vector<df::unit *> & unitlist)
{
    df::unit *ns = nullptr;
    int32_t best = std::numeric_limits<int32_t>::max();
    for (auto it = unitlist.begin(); it != unitlist.end(); it++)
    {
        df::unit *u = *it;
        if (u->military.squad_id == -1)
        {
            int32_t score = unit_totalxp(u);
            if (!ns || score < best)
            {
                ns = u;
                best = score;
            }
        }
    }
    if (!ns)
    {
        return nullptr;
    }

    int32_t squad_id = military_find_free_squad();
    df::squad *squad = df::squad::find(squad_id);
    auto pos = std::find_if(squad->positions.begin(), squad->positions.end(), [](df::squad_position *p) -> bool { return p->occupant == -1; });
    if (pos == squad->positions.end())
    {
        return nullptr;
    }

    (*pos)->occupant = ns->hist_figure_id;
    ns->military.squad_id = squad_id;
    ns->military.squad_position = pos - squad->positions.begin();

    for (auto it = ui->main.fortress_entity->positions.assignments.begin(); it != ui->main.fortress_entity->positions.assignments.end(); it++)
    {
        if ((*it)->squad_id == squad_id)
        {
            return ns;
        }
    }

    if (ui->main.fortress_entity->assignments_by_type[entity_position_responsibility::MILITARY_STRATEGY].empty())
    {
        assign_new_noble(out, position_with_responsibility(entity_position_responsibility::MILITARY_STRATEGY), ns, squad_id);
    }
    else
    {
        assign_new_noble(out, military_find_captain_pos(), ns, squad_id);
    }

    return ns;
}

// return a squad index with an empty slot
int32_t Population::military_find_free_squad()
{
    int32_t squad_sz = 8;
    if (military.size() < 4 * 6)
        squad_sz = 6;
    if (military.size() < 3 * 4)
        squad_sz = 4;

    for (auto sqid = ui->main.fortress_entity->squads.begin(); sqid != ui->main.fortress_entity->squads.end(); sqid++)
    {
        int32_t count = 0;
        for (auto it = military.begin(); it != military.end(); it++)
        {
            if (it->second == *sqid)
            {
                count++;
            }
        }
        if (count < squad_sz)
        {
            return *sqid;
        }
    }

    // create a new squad using the UI
    AI::feed_key(interface_key::D_MILITARY);
    AI::feed_key(interface_key::D_MILITARY_CREATE_SQUAD);
    AI::feed_key(interface_key::STANDARDSCROLL_UP);
    AI::feed_key(interface_key::SELECT);
    AI::feed_key(interface_key::LEAVESCREEN);

    // get the squad and its id
    int32_t squad_id = ui->main.fortress_entity->squads.back();
    df::squad *squad = df::squad::find(squad_id);

    squad->cur_alert_idx = 1; // train
    squad->uniform_priority = 2;
    squad->carry_food = 2;
    squad->carry_water = 2;

    const static struct uniform_category_item_type
    {
        std::vector<std::pair<df::uniform_category, df::item_type>> vec;
        uniform_category_item_type()
        {
            vec.push_back(std::make_pair(uniform_category::body, item_type::ARMOR));
            vec.push_back(std::make_pair(uniform_category::head, item_type::HELM));
            vec.push_back(std::make_pair(uniform_category::pants, item_type::PANTS));
            vec.push_back(std::make_pair(uniform_category::gloves, item_type::GLOVES));
            vec.push_back(std::make_pair(uniform_category::shoes, item_type::SHOES));
            vec.push_back(std::make_pair(uniform_category::shield, item_type::SHIELD));
            vec.push_back(std::make_pair(uniform_category::weapon, item_type::WEAPON));
        }
    } item_type;
    // uniform
    for (auto pos = squad->positions.begin(); pos != squad->positions.end(); pos++)
    {
        for (auto it = item_type.vec.begin(); it != item_type.vec.end(); it++)
        {
            if ((*pos)->uniform[it->first].empty())
            {
                df::squad_uniform_spec *sus = df::allocate<df::squad_uniform_spec>();
                sus->color = -1;
                sus->item_filter.item_type = it->second;
                sus->item_filter.material_class = it->first == uniform_category::weapon ? entity_material_category::None : entity_material_category::Armor;
                sus->item_filter.mattype = -1;
                sus->item_filter.matindex = -1;
                (*pos)->uniform[it->first].push_back(sus);
            }
        }
        (*pos)->flags.bits.exact_matches = 1;
    }

    if (ui->main.fortress_entity->squads.size() % 3 == 0)
    {
        // ranged squad
        for (auto pos = squad->positions.begin(); pos != squad->positions.end(); pos++)
        {
            (*pos)->uniform[uniform_category::weapon][0]->indiv_choice.bits.ranged = 1;
        }
        df::squad_ammo_spec *sas = df::allocate<df::squad_ammo_spec>();
        sas->item_filter.item_type = item_type::AMMO;
        sas->item_filter.item_subtype = 0; // XXX bolts
        sas->item_filter.material_class = entity_material_category::None;
        sas->amount = 500;
        sas->flags.bits.use_combat = 1;
        sas->flags.bits.use_training = 1;
        squad->ammunition.push_back(sas);
    }
    else
    {
#if 0
        // we don't want all the axes being used up by the military.
        std::vector<int32_t> weapons;
        for (auto it = ui->main.fortress_entity->entity_raw->equipment.weapon_id.begin(); it != ui->main.fortress_entity->entity_raw->equipment.weapon_id.end(); it++)
        {
            df::itemdef_weaponst *idef = df::itemdef_weaponst::find(*it);
            if (idef->skill_melee != job_skill::MINING && idef->skill_melee != job_skill::AXE && idef->skill_ranged == job_skill::NONE && !idef->flags.is_set(weapon_flags::TRAINING))
            {
                weapons.push_back(*it);
            }
        }
        if (weapons.empty())
#endif
        {
            for (auto pos = squad->positions.begin(); pos != squad->positions.end(); pos++)
            {
                (*pos)->uniform[uniform_category::weapon][0]->indiv_choice.bits.melee = 1;
            }
        }
#if 0
        else
        {
            int32_t n = int32_t(ui->main.fortress_entity->squads.size());
            n -= n / 3 + 1;
            n *= 10;
            for (auto pos = squad->positions.begin(); pos != squad->positions.end(); pos++)
            {
                (*pos)->uniform[uniform_category::weapon][0]->item_filter.item_subtype = weapons[n % weapons.size()];
                n++;
            }
        }
#endif
    }

    return squad_id;
}

bool Population::set_up_trading(color_ostream & out, bool should_be_trading)
{
    room *r = ai->plan->find_room(room_type::tradedepot);
    if (!r)
    {
        return false;
    }
    df::building_tradedepotst *bld = virtual_cast<df::building_tradedepotst>(r->dfbuilding());
    if (!bld || bld->getBuildStage() < bld->getMaxBuildStage())
    {
        return false;
    }
    if (bld->trade_flags.bits.trader_requested == should_be_trading)
    {
        return false;
    }

    if (!AI::is_dwarfmode_viewscreen())
    {
        return false;
    }

    int32_t start_x, start_y, start_z;
    Gui::getViewCoords(start_x, start_y, start_z);

    AI::feed_key(interface_key::D_BUILDJOB);

    df::coord pos = r->pos();
    Gui::revealInDwarfmodeMap(pos, true);
    Gui::setCursorCoords(pos.x, pos.y, pos.z);

    AI::feed_key(interface_key::CURSOR_LEFT);
    AI::feed_key(interface_key::BUILDJOB_DEPOT_REQUEST_TRADER);
    if (should_be_trading)
    {
        AI::feed_key(interface_key::BUILDJOB_DEPOT_BRING);
        if (auto bring = virtual_cast<df::viewscreen_layer_assigntradest>(Gui::getCurViewscreen(true)))
        {
            ai->debug(out, stl_sprintf("[trade] Checking %d possible trade items...", bring->lists[0].size()));
            for (size_t i = 0; i < bring->lists[0].size(); i++)
            {
                auto info = bring->info.at(bring->lists[0].at(i));
                if (info->unk) // TODO: determine if this field is really "banned by mandate".
                {
                    if (info->status == df::assign_trade_status::Pending)
                    {
                        info->status = df::assign_trade_status::RemovePending;
                    }
                    else if (info->status == df::assign_trade_status::Trading)
                    {
                        info->status = df::assign_trade_status::RemoveTrading;
                    }
                }
                else if (info->status == df::assign_trade_status::None && ai->stocks->willing_to_trade_item(out, info->item))
                {
                    info->status = df::assign_trade_status::AddPending;
                    ai->debug(out, "[trade] Bringing item to trade depot: " + AI::describe_item(info->item));
                }
            }
            AI::feed_key(interface_key::LEAVESCREEN);
        }
    }
    AI::feed_key(interface_key::LEAVESCREEN);

    ai->camera->ignore_pause(start_x, start_y, start_z);

    return true;
}

bool Population::perform_trade(color_ostream & out)
{
    room *r = ai->plan->find_room(room_type::tradedepot);
    if (!r)
    {
        return false;
    }
    df::building_tradedepotst *bld = virtual_cast<df::building_tradedepotst>(r->dfbuilding());
    if (!bld || bld->getBuildStage() < bld->getMaxBuildStage())
    {
        return false;
    }

    if (!AI::is_dwarfmode_viewscreen())
    {
        return false;
    }

    int32_t start_x, start_y, start_z;
    Gui::getViewCoords(start_x, start_y, start_z);

    AI::feed_key(interface_key::D_BUILDJOB);

    df::coord pos = r->pos();
    Gui::revealInDwarfmodeMap(pos, true);
    Gui::setCursorCoords(pos.x, pos.y, pos.z);

    AI::feed_key(interface_key::CURSOR_LEFT);
    AI::feed_key(interface_key::BUILDJOB_DEPOT_TRADE);
    if (auto wait = virtual_cast<df::viewscreen_tradelistst>(Gui::getCurViewscreen(true)))
    {
        if (wait->caravans.size() == 1)
        {
            wait->logic();
        }
        else
        {
            AI::feed_key(wait, interface_key::SELECT);
        }
    }
    if (auto trade = virtual_cast<df::viewscreen_tradegoodsst>(Gui::getCurViewscreen(true)))
    {
        if (perform_trade(out, trade))
        {
            ai->camera->ignore_pause(start_x, start_y, start_z);

            return true;
        }
    }
    else
    {
        ai->debug(out, "[trade] Opening the trade screen failed. Trying again soon.");
        AI::feed_key(interface_key::LEAVESCREEN);
    }

    ai->camera->ignore_pause(start_x, start_y, start_z);

    return false;
}

bool Population::perform_trade(color_ostream & out, df::viewscreen_tradegoodsst *trade)
{
    trade->render(); // for some reason, this populates the item list.

    if (trade->is_unloading)
    {
        ai->debug(out, "[trade] Waiting for caravan to unload. Trying again soon.");

        AI::feed_key(trade, interface_key::LEAVESCREEN);
        AI::feed_key(interface_key::LEAVESCREEN);

        return false;
    }

    df::creature_raw *creature = df::creature_raw::find(trade->entity->race);

    ai->debug(out, "[trade] Scanning goods offered by " + trade->merchant_name + " from " + trade->merchant_entity + "...");

    std::vector<size_t> want_items;

    for (auto it = trade->trader_items.begin(); it != trade->trader_items.end(); it++)
    {
        if (ai->stocks->want_trader_item(out, *it))
        {
            want_items.push_back(it - trade->trader_items.begin());
        }
    }

    std::sort(want_items.begin(), want_items.end(), [this, trade](size_t a, size_t b) -> bool { return ai->stocks->want_trader_item_more(trade->trader_items.at(a), trade->trader_items.at(b)); });

    int32_t max_offer_value = 0;

    for (auto it = trade->broker_items.begin(); it != trade->broker_items.end(); it++)
    {
        max_offer_value += ai->trade->item_or_container_price_for_caravan(*it, trade->caravan, trade->entity, creature, (*it)->getStackSize(), trade->caravan->buy_prices, trade->caravan->sell_prices);
    }

    ai->debug(out, stl_sprintf("[trade] We have %d dorfbux of items we're willing to trade.", max_offer_value));

    int32_t request_value = 0;
    int32_t offer_value = 0;

    for (auto it = want_items.begin(); it != want_items.end(); it++)
    {
        df::item *item = trade->trader_items.at(*it);

        bool can_afford_any = false;

        for (int32_t qty = item->getStackSize(); qty > 0; qty--)
        {
            int32_t item_value = ai->trade->item_or_container_price_for_caravan(trade->trader_items.at(*it), trade->caravan, trade->entity, creature, qty, trade->caravan->buy_prices, trade->caravan->sell_prices);

            if ((request_value + item_value) * 11 / 10 >= max_offer_value)
            {
                if (qty > 1)
                {
                    ai->debug(out, stl_sprintf("[trade] Cannot afford %d of item, trying again with fewer: ", qty) + AI::describe_item(item));
                }
                continue;
            }

            can_afford_any = true;
            request_value += item_value;
            trade->trader_selected.at(*it) = 1;
            trade->trader_count.at(*it) = qty;
            ai->debug(out, stl_sprintf("[trade] Requesting %d of item: ", qty) + AI::describe_item(item));
            ai->debug(out, stl_sprintf("[trade] Requested: %d Offered: %d", request_value, offer_value));

            for (size_t i = 0; request_value * 11 / 10 >= offer_value && i < trade->broker_items.size(); i++)
            {
                if (!trade->broker_selected.at(i) || trade->broker_count.at(i) != trade->broker_items.at(i)->getStackSize())
                {
                    auto offer_item = trade->broker_items.at(i);

                    int32_t current_count = trade->broker_selected.at(i) ? trade->broker_count.at(i) : 0;
                    int32_t existing_offer_value = current_count ? ai->trade->item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, current_count, trade->caravan->buy_prices, trade->caravan->sell_prices) : 0;

                    int32_t over_offer_qty = trade->broker_items.at(i)->getStackSize();
                    for (int32_t offer_qty = over_offer_qty - 1; offer_qty > 0; offer_qty--)
                    {
                        int32_t new_offer_value = ai->trade->item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, offer_qty, trade->caravan->buy_prices, trade->caravan->sell_prices);
                        if (offer_value - existing_offer_value + new_offer_value > request_value * 11 / 10)
                        {
                            over_offer_qty = offer_qty;
                        }
                        else
                        {
                            break;
                        }
                    }
                    int32_t new_offer_value = ai->trade->item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, over_offer_qty, trade->caravan->buy_prices, trade->caravan->sell_prices);
                    trade->broker_selected.at(i) = 1;
                    trade->broker_count.at(i) = over_offer_qty;
                    offer_value = offer_value - existing_offer_value + new_offer_value;
                    ai->debug(out, stl_sprintf("[trade] Offering %d%s of item: ", over_offer_qty - current_count, current_count ? " more" : "") + AI::describe_item(offer_item));
                    ai->debug(out, stl_sprintf("[trade] Requested: %d Offered: %d", request_value, offer_value));
                }
            }

            break;
        }

        if (!can_afford_any)
        {
            ai->debug(out, "[trade] Cannot afford any of item, stopping: " + AI::describe_item(item));
            break;
        }
    }

    if (offer_value == 0)
    {
        ai->debug(out, "[trade] Cancelling trade.");

        AI::feed_key(trade, interface_key::LEAVESCREEN);
        AI::feed_key(interface_key::LEAVESCREEN);

        return true;
    }

    ai->debug(out, "[trade] Making offer...");

    AI::feed_key(trade, interface_key::TRADE_TRADE);
    trade->logic();
    trade->render();

    std::string reply, mood;
    ai->trade->read_trader_reply(reply, mood);

    ai->debug(out, "[trade] Trader reply: " + reply);

    if (!trade->counteroffer.empty())
    {
        for (auto it = trade->counteroffer.begin(); it != trade->counteroffer.end(); it++)
        {
            ai->debug(out, "[trade] Trader requests item: " + AI::describe_item(*it));
        }
        ai->debug(out, "[trade] Accepting counter-offer.");

        AI::feed_key(trade, interface_key::SELECT);

        AI::feed_key(trade, interface_key::TRADE_TRADE);
        trade->logic();
        trade->render();

        ai->trade->read_trader_reply(reply, mood);

        ai->debug(out, "[trade] Trader reply: " + reply);
    }

    if (std::find(trade->broker_selected.begin(), trade->broker_selected.end(), 1) == trade->broker_selected.end())
    {
        ai->debug(out, "[trade] Offer was accepted.");
    }
    else
    {
        ai->debug(out, "[trade] Trader mood: " + mood);
        ai->debug(out, "[trade] Offer was rejected. Giving up.");
    }

    AI::feed_key(trade, interface_key::LEAVESCREEN);
    AI::feed_key(interface_key::LEAVESCREEN);

    return true;
}

bool Population::unit_hasmilitaryduty(df::unit *u)
{
    if (u->military.squad_id == -1)
    {
        return false;
    }
    df::squad *squad = df::squad::find(u->military.squad_id);
    std::vector<df::squad_schedule_order *> & curmonth = squad->schedule[squad->cur_alert_idx][*cur_year_tick / 28 / 1200]->orders;
    return !curmonth.empty() && (curmonth.size() != 1 || curmonth[0]->min_count != 0);
}

int32_t Population::unit_totalxp(df::unit *u)
{
    int32_t t = 0;
    for (auto sk = u->status.current_soul->skills.begin(); sk != u->status.current_soul->skills.end(); sk++)
    {
        int32_t rat = (*sk)->rating;
        t += 400 * rat + 100 * rat * (rat + 1) / 2 + (*sk)->experience;
    }
    return t;
}

df::entity_position *Population::position_with_responsibility(df::entity_position_responsibility responsibility)
{
    for (auto it = ui->main.fortress_entity->positions.own.begin(); it != ui->main.fortress_entity->positions.own.end(); it++)
    {
        if ((*it)->responsibilities[responsibility])
        {
            return *it;
        }
    }
    return nullptr;
}

void Population::update_nobles(color_ostream & out)
{
    if (!config.manage_nobles)
    {
        check_noble_appartments(out);
        return;
    }

    std::vector<df::unit *> cz;
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        std::vector<Units::NoblePosition> positions;
        if (Units::isCitizen(u) && !Units::isChild(u) && !Units::isBaby(u) && u->mood == mood_type::None && u->military.squad_id == -1 && !Units::getNoblePositions(&positions, u))
        {
            cz.push_back(u);
        }
    }
    std::sort(cz.begin(), cz.end(), [this](df::unit *a, df::unit *b) -> bool
    {
        return unit_totalxp(a) > unit_totalxp(b);
    });
    df::historical_entity *ent = ui->main.fortress_entity;

    if (ent->assignments_by_type[entity_position_responsibility::MANAGE_PRODUCTION].empty() && !cz.empty())
    {
        ai->debug(out, "assigning new manager: " + AI::describe_unit(cz.back()));
        // TODO do check population caps, ...
        assign_new_noble(out, position_with_responsibility(entity_position_responsibility::MANAGE_PRODUCTION), cz.back());
        cz.pop_back();
    }

    if (ent->assignments_by_type[entity_position_responsibility::ACCOUNTING].empty() && !cz.empty())
    {
        for (auto it = cz.rbegin(); it != cz.rend(); it++)
        {
            if (!(*it)->status.labors[unit_labor::MINE])
            {
                ai->debug(out, "assigning new bookkeeper: " + AI::describe_unit(*it));
                assign_new_noble(out, position_with_responsibility(entity_position_responsibility::ACCOUNTING), *it);
                ui->bookkeeper_settings = 4;
                cz.erase(it.base() - 1);
                break;
            }
        }
    }

    if (ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].empty() && ai->plan->find_room(room_type::infirmary, [](room *r) -> bool { return r->status != room_status::plan; }) && !cz.empty())
    {
        ai->debug(out, "assigning new chief medical dwarf: " + AI::describe_unit(cz.back()));
        assign_new_noble(out, position_with_responsibility(entity_position_responsibility::HEALTH_MANAGEMENT), cz.back());
        cz.pop_back();
    }

    if (!ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].empty())
    {
        df::entity_position_assignment *asn = ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].at(0);
        df::historical_figure *hf = df::historical_figure::find(asn->histfig);
        df::unit *doctor = df::unit::find(hf->unit_id);
        // doc => healthcare
        for (auto it = labors.medical.begin(); it != labors.medical.end(); it++)
        {
            doctor->status.labors[*it] = true;
        }
    }

    if (ent->assignments_by_type[entity_position_responsibility::TRADE].empty() && !cz.empty())
    {
        ai->debug(out, "assigning new broker: " + AI::describe_unit(cz.back()));
        assign_new_noble(out, position_with_responsibility(entity_position_responsibility::TRADE), cz.back());
        cz.pop_back();
    }

    check_noble_appartments(out);
}

void Population::check_noble_appartments(color_ostream & out)
{
    std::set<int32_t> noble_ids;

    for (auto asn = ui->main.fortress_entity->positions.assignments.begin(); asn != ui->main.fortress_entity->positions.assignments.end(); asn++)
    {
        df::entity_position *pos = binsearch_in_vector(ui->main.fortress_entity->positions.own, (*asn)->position_id);
        if (pos->required_office > 0 || pos->required_dining > 0 || pos->required_tomb > 0)
        {
            if (df::historical_figure *hf = df::historical_figure::find((*asn)->histfig))
            {
                noble_ids.insert(hf->unit_id);
            }
        }
    }

    ai->plan->attribute_noblerooms(out, noble_ids);
}

df::entity_position_assignment *Population::assign_new_noble(color_ostream & out, df::entity_position *pos, df::unit *unit, int32_t squad_id)
{
    if (pos == nullptr)
    {
        ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as unknown position");
        return nullptr;
    }
    if (!AI::is_dwarfmode_viewscreen())
    {
        ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as " + pos->code + ": not on dwarfmode viewscreen");
        return nullptr;
    }
    AI::feed_key(interface_key::D_NOBLES);
    if (auto view = strict_virtual_cast<df::viewscreen_layer_noblelistst>(Gui::getCurViewscreen(true)))
    {
        for (auto it = view->assignments.begin(); it != view->assignments.end(); it++)
        {
            auto assign = *it;
            if (assign != nullptr && assign->position_id == pos->id && assign->histfig == -1 && assign->squad_id == squad_id)
            {
                AI::feed_key(interface_key::SELECT);
                for (auto c = view->candidates.begin(); c != view->candidates.end(); c++)
                {
                    if ((*c)->unit == unit)
                    {
                        AI::feed_key(interface_key::SELECT);
                        AI::feed_key(interface_key::LEAVESCREEN);
                        return assign;
                    }
                    AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
                }
                AI::feed_key(interface_key::LEAVESCREEN);
                AI::feed_key(interface_key::LEAVESCREEN);
                ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as " + pos->code + ": unit is not candidate");
                return nullptr;
            }
            AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
        }
        AI::feed_key(interface_key::LEAVESCREEN);
        ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as " + pos->code + ": could not find position");
        return nullptr;
    }
    ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as " + pos->code + ": nobles screen did not appear");
    return nullptr;
}

void Population::update_pets(color_ostream & out)
{
    int32_t needmilk = 0;
    int32_t needshear = 0;
    for (auto mo = world->manager_orders.begin(); mo != world->manager_orders.end(); mo++)
    {
        if ((*mo)->job_type == job_type::MilkCreature)
        {
            needmilk -= (*mo)->amount_left;
        }
        else if ((*mo)->job_type == job_type::ShearCreature)
        {
            needshear -= (*mo)->amount_left;
        }
    }

    std::map<df::caste_raw *, std::set<std::pair<int32_t, df::unit *>>> forSlaughter;

    std::map<int32_t, pet_flags> np = pet;
    for (auto it = pet_check.begin(); it != pet_check.end(); it++)
    {
        np[*it]; // make sure existing pasture assignments are checked
    }
    pet_check.clear();
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        if (!Units::isOwnCiv(u) || Units::isOwnGroup(u) || Units::isOwnRace(u) || u->cultural_identity != -1)
        {
            continue;
        }
        if (u->flags1.bits.dead || u->flags1.bits.merchant || u->flags1.bits.forest || u->flags2.bits.visitor || u->flags2.bits.slaughter)
        {
            continue;
        }

        df::creature_raw *race = df::creature_raw::find(u->race);
        df::caste_raw *cst = race->caste[u->caste];

        if (cst->flags.is_set(caste_raw_flags::CAN_LEARN))
        {
            continue;
        }

        int32_t age = days_since(u->birth_year, u->birth_time);

        if (pet.count(u->id))
        {
            if (cst->body_size_2.back() <= age && // full grown
                u->profession != profession::TRAINED_HUNTER && // not trained
                u->profession != profession::TRAINED_WAR && // not trained
                u->relationship_ids[df::unit_relationship_type::Pet] == -1) // not owned
            {

                if (std::find_if(u->body.wounds.begin(), u->body.wounds.end(), [](df::unit_wound *w) -> bool { return std::find_if(w->parts.begin(), w->parts.end(), [](df::unit_wound::T_parts *p) -> bool { return p->flags2.bits.gelded; }) != w->parts.end(); }) != u->body.wounds.end() || cst->gender == -1)
                {
                    // animal can't reproduce, can't work, and will provide maximum butchering reward. kill it.
                    u->flags2.bits.slaughter = true;
                    ai->debug(out, stl_sprintf("marked %dy%dd old %s:%s for slaughter (can't reproduce)", age / 12 / 28, age % (12 * 28), race->creature_id.c_str(), cst->caste_id.c_str()));
                    continue;
                }

                forSlaughter[cst].insert(std::make_pair(age, u));
            }

            if (pet.at(u->id).bits.milkable && !Units::isBaby(u) && !Units::isChild(u))
            {
                bool have = false;
                for (auto mt = u->status.misc_traits.begin(); mt != u->status.misc_traits.end(); mt++)
                {
                    if ((*mt)->id == misc_trait_type::MilkCounter)
                    {
                        have = true;
                        break;
                    }
                }
                if (!have)
                {
                    needmilk++;
                }
            }

            if (pet.at(u->id).bits.shearable && !Units::isBaby(u) && !Units::isChild(u))
            {
                bool found = false;
                for (auto stl = cst->shearable_tissue_layer.begin(); stl != cst->shearable_tissue_layer.end(); stl++)
                {
                    for (auto bpi = (*stl)->bp_modifiers_idx.begin(); bpi != (*stl)->bp_modifiers_idx.end(); bpi++)
                    {
                        if (u->appearance.bp_modifiers[*bpi] >= (*stl)->length)
                        {
                            needshear++;
                            found = true;
                            break;
                        }
                    }
                    if (found)
                        break;
                }
            }

            np.erase(u->id);
            continue;
        }

        pet_flags flags;
        flags.bits.milkable = 0;
        flags.bits.shearable = 0;
        flags.bits.hunts_vermin = 0;
        flags.bits.grazer = 0;

        if (cst->flags.is_set(caste_raw_flags::MILKABLE))
        {
            flags.bits.milkable = 1;
        }

        if (!cst->shearable_tissue_layer.empty())
        {
            flags.bits.shearable = 1;
        }

        if (cst->flags.is_set(caste_raw_flags::HUNTS_VERMIN))
        {
            flags.bits.hunts_vermin = 1;
        }

        if (cst->flags.is_set(caste_raw_flags::GRAZER))
        {
            flags.bits.grazer = 1;

            if (df::building_civzonest *bld = virtual_cast<df::building_civzonest>(ai->plan->getpasture(out, u->id)))
            {
                assign_unit_to_zone(u, bld);
                // TODO monitor grass levels
            }
            else if (u->relationship_ids[df::unit_relationship_type::Pet] == -1 && !cst->flags.is_set(caste_raw_flags::CAN_LEARN))
            {
                // TODO slaughter best candidate, keep this one
                u->flags2.bits.slaughter = 1;
                ai->debug(out, stl_sprintf("marked %dy%dd old %s:%s for slaughter (no pasture)", age / 12 / 28, age % (12 * 28), race->creature_id.c_str(), cst->caste_id.c_str()));
            }
        }

        pet[u->id] = flags;
    }

    for (auto p = np.begin(); p != np.end(); p++)
    {
        ai->plan->freepasture(out, p->first);
        pet.erase(p->first);
    }

    for (auto cst = forSlaughter.begin(); cst != forSlaughter.end(); cst++)
    {
        // we have reproductively viable animals, but there are more than 5 of
        // this sex (full-grown). kill the oldest ones for meat/leather/bones.

        if (cst->second.size() > 5)
        {
            // remove the youngest 5
            auto it = cst->second.begin();
            std::advance(it, 5);
            cst->second.erase(cst->second.begin(), it);

            for (auto it = cst->second.begin(); it != cst->second.end(); it++)
            {
                int32_t age = it->first;
                df::unit *u = it->second;
                df::creature_raw *race = df::creature_raw::find(u->race);
                u->flags2.bits.slaughter = 1;
                ai->debug(out, stl_sprintf("marked %dy%dd old %s:%s for slaughter (too many adults)", age / 12 / 28, age % (12 * 28), race->creature_id.c_str(), cst->first->caste_id.c_str()));
            }
        }
    }

    if (needmilk > 30)
        needmilk = 30;
    if (needmilk > 0)
        ai->stocks->legacy_add_manager_order(out, "MilkCreature", needmilk);

    if (needshear > 30)
        needshear = 30;
    if (needshear > 0)
        ai->stocks->legacy_add_manager_order(out, "ShearCreature", needshear);
}

void Population::assign_unit_to_zone(df::unit *u, df::building_civzonest *bld)
{
    if (auto ref = Units::getGeneralRef(u, general_ref_type::BUILDING_CIVZONE_ASSIGNED))
    {
        if (ref->getBuilding() == bld)
        {
            // already assigned to the correct zone
            return;
        }
    }

    int32_t start_x, start_y, start_z;
    Gui::getViewCoords(start_x, start_y, start_z);
    AI::feed_key(interface_key::D_CIVZONE);
    if (ui->main.mode != ui_sidebar_mode::Zones)
    {
        // we probably aren't on the main dwarf fortress screen
        return;
    }
    Gui::revealInDwarfmodeMap(df::coord(bld->x1 + 1, bld->y1, bld->z), true);
    Gui::setCursorCoords(bld->x1 + 1, bld->y1, bld->z);
    AI::feed_key(interface_key::CURSOR_LEFT);
    while (ui_sidebar_menus->zone.selected != bld)
    {
        AI::feed_key(interface_key::CIVZONE_NEXT);
    }
    if (Buildings::isPitPond(bld))
    {
        AI::feed_key(interface_key::CIVZONE_POND_OPTIONS);
    }
    else if (Buildings::isPenPasture(bld))
    {
        AI::feed_key(interface_key::CIVZONE_PEN_OPTIONS);
    }
    if (std::find(ui_building_assign_units->begin(), ui_building_assign_units->end(), u) != ui_building_assign_units->end())
    {
        while (ui_building_assign_units->at(*ui_building_item_cursor) != u)
        {
            AI::feed_key(interface_key::SECONDSCROLL_DOWN);
        }
        AI::feed_key(interface_key::SELECT);
    }
    AI::feed_key(interface_key::LEAVESCREEN);
    AI::feed_key(interface_key::LEAVESCREEN);
    ai->camera->ignore_pause(start_x, start_y, start_z);
}

std::string Population::status()
{
    return stl_sprintf("%d citizen, %d military, %d pet, %d visitor, %d resident", citizen.size(), military.size(), pet.size(), visitor.size(), resident.size());
}

std::string Population::report()
{
    std::ostringstream s;

    auto do_unit = [this, &s](int32_t id)
    {
        auto u = df::unit::find(id);

        s << "- " << AI::describe_unit(u);

        if (u == nullptr)
        {
            s << "\n";
            return;
        }

        int32_t age = days_since(u->birth_year, u->birth_time);
        s << " (age " << (age / 12 / 28) << "y" << (age % (12 * 28)) << "d)\n";

        if (room *r = ai->plan->find_room_at(Units::getPosition(u)))
        {
            s << "  " << ai->plan->describe_room(r) << "\n";
        }

        if (u->job.current_job != nullptr)
        {
            s << "  " << AI::describe_job(u->job.current_job) << "\n";
        }
        else
        {
            bool any = false;
            for (auto it = world->activities.all.begin(); it != world->activities.all.end(); it++)
            {
                for (auto e = (*it)->events.begin(); e != (*it)->events.end(); e++)
                {
                    if (auto p = (*e)->getParticipantInfo())
                    {
                        if (std::find(p->units.begin(), p->units.end(), u->id) != p->units.end())
                        {
                            std::string name;
                            (*e)->getName(u->id, &name);
                            if (any)
                            {
                                s << " / ";
                            }
                            else
                            {
                                s << "  ";
                                any = true;
                            }
                            s << name;
                        }
                    }
                }
            }
            if (any)
            {
                s << "\n";
            }
        }
    };

    s << "## Citizens\n";
    for (auto it = citizen.begin(); it != citizen.end(); it++)
    {
        do_unit(*it);
    }
    s << "\n";
    s << "## Military\n";
    for (auto sqid = ui->main.fortress_entity->squads.begin(); sqid != ui->main.fortress_entity->squads.end(); sqid++)
    {
        df::squad *sq = df::squad::find(*sqid);

        s << "### " << AI::describe_name(sq->name, false) << ", " << AI::describe_name(sq->name, true) << "\n";

        s << "#### Members\n";
        for (auto sp = sq->positions.begin(); sp != sq->positions.end(); sp++)
        {
            if ((*sp)->occupant == -1)
            {
                s << "- (vacant)\n";
            }
            else
            {
                auto hf = df::historical_figure::find((*sp)->occupant);
                do_unit(hf ? hf->unit_id : -1);
            }
        }
        s << "\n";

        s << "#### Targets\n";
        for (auto o = sq->orders.begin(); o != sq->orders.end(); o++)
        {
            std::string description;
            (*o)->getDescription(&description);
            s << "- " << description << "\n";
        }
        if (sq->orders.empty())
        {
            s << "(none)\n";
        }
        s << "\n";
    }
    s << "\n";
    s << "## Pets\n";
    for (auto it = pet.begin(); it != pet.end(); it++)
    {
        do_unit(it->first);

        bool first = true;

        if (it->second.bits.milkable)
        {
            s << "  milkable";
            first = false;
        }

        if (it->second.bits.shearable)
        {
            s << (first ? "  " : ", ") << "shearable";
            first = false;
        }

        if (it->second.bits.hunts_vermin)
        {
            s << (first ? "  " : ", ") << "hunts vermin";
            first = false;
        }

        if (it->second.bits.grazer)
        {
            s << (first ? "  " : ", ") << "grazer";
            first = false;
        }

        if (!first)
        {
            s << "\n";
        }
    }
    s << "\n";
    s << "## Visitors\n";
    for (auto it = visitor.begin(); it != visitor.end(); it++)
    {
        do_unit(*it);
    }
    s << "\n";
    s << "## Residents\n";
    for (auto it = resident.begin(); it != resident.end(); it++)
    {
        do_unit(*it);
    }
    s << "\n";
    s << "## Deaths\n";
    for (auto it = world->history.events2.begin(); it != world->history.events2.end(); it++)
    {
        auto d = virtual_cast<df::history_event_hist_figure_diedst>(*it);

        if (!d || d->site != ui->site_id)
        {
            continue;
        }

        s << "- " << AI::describe_event(d) << "\n";
    }
    s << "\n";

    return s.str();
}

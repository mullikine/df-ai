#pragma once

#include "dfhack_shared.h"

#include <array>

#include "df/embark_finder_option.h"

constexpr int32_t embark_options_count = df::enum_traits<df::embark_finder_option>::last_item_value + 1;

struct Config
{
    Config();
    void load(color_ostream & out);
    void save(color_ostream & out);
    template<typename T>
    void set(color_ostream & out, T & saved, const T & value)
    {
        if (saved == value)
        {
            return;
        }
        saved = value;
        save(out);
    }
    template<typename T>
    void set(color_ostream & out, volatile T & saved, const T & value)
    {
        set(out, saved, (const volatile T &)value);
    }

    bool random_embark;
    std::string random_embark_world;
    bool write_console;
    bool write_log;
    bool record_movie;
    bool no_quit;
    std::array<int32_t, embark_options_count> embark_options;
    int32_t world_size;
    bool camera;
    bool fps_meter;
    std::string manage_labors;
    bool manage_nobles;
    uint8_t cancel_announce;
    volatile bool lockstep;
    bool allow_pause;
};

extern Config config;

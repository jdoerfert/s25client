// Copyright (c) 2005 - 2017 Settlers Freaks (sf-team at siedler25.org)
//
// This file is part of Return To The Roots.
//
// Return To The Roots is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// Return To The Roots is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Return To The Roots. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include "gameTypes/AIInfo.h"

namespace AI { namespace jd {
    struct Config
    {
        struct Player
        {
            /// The minimal GF number for anything to happen.
            unsigned short MINIMAL_GF = 500;

            /// TODO
            unsigned short SKIP_GF_CHANCE = 5;

            Player(AI::Level level)
            {
                MINIMAL_GF *= (AI::Level::LAST - level + 1);
                SKIP_GF_CHANCE *= (AI::Level::LAST - level + 1);
            }
        } player;

        struct Actions
        {
            /// TODO
            static constexpr unsigned short EMERGENCY_IDENTIFICATION_CHANCE = 50;

            /// TODO
            static constexpr unsigned short EMERGENCY_MIN_WOOD_IN_STORE = 15;

            /// TODO
            static constexpr unsigned short EMERGENCY_MIN_BOARDS_IN_STORE = 15;

            /// TODO
            static constexpr unsigned short EMERGENCY_MIN_STONES_IN_STORE = 15;

            /// TODO
            static constexpr unsigned short GEOLOGIST_MIN_GF = 1000;

            /// TODO
            static constexpr unsigned short GEOLOGIST_CHANCE = 60;

            /// TODO
            static constexpr unsigned short GEOLOGIST_MIN_AMOUNT = 1;

            /// TODO
            static constexpr unsigned short GEOLOGIST_PREFERRED_AMOUNT = 2;

            /// TODO
            static constexpr unsigned short GOODS_ON_FLAG_REQUIRE_DONKEY = 4;

            unsigned short MIN_ROAD_PRODUCTIVITY = 10;

            Actions(AI::Level) {}
        } actions;

        struct View
        {
            /// TODO
            static constexpr unsigned short FOG_ESTIMATE_RADIUS = 4;

            /// TODO
            static constexpr unsigned short FOG_ESTIMATE_RATIO = 60;

            /// TODO
            static constexpr unsigned short NUM_WARES_EQUAL_PRODUCTION = 8;
        };

        struct Score
        {
            /// TODO
            static constexpr unsigned short RESOURCE_IN_RANGE_FACTOR = 64;

            /// TODO
            static constexpr unsigned short BQ_NOTHING = 0;

            /// TODO
            static constexpr unsigned short BQ_FLAG = 0;

            /// TODO
            static constexpr unsigned short BQ_HUT = 10;

            /// TODO
            static constexpr unsigned short BQ_HOUSE = 2 * BQ_HUT;

            /// TODO
            static constexpr unsigned short BQ_CASTLE = BQ_HOUSE + BQ_HUT;

            /// TODO
            static constexpr unsigned short BQ_MINE = 8 * BQ_CASTLE;

            /// TODO
            static constexpr unsigned short BQ_HARBOR = 128 * BQ_CASTLE;

            /// TODO
            static constexpr unsigned short SECONDARY_ROAD_FACTOR = 2;

            /// TODO
            static constexpr unsigned short REQUIRED_MINIMAL_DISTANCE_TO_ENEMY[4] = {0, 0, 0, 0};

            /// TODO
            static constexpr unsigned short REQUIRED_REMAINING_DISTANCE_TO_ENEMY[4] = {100, 100, 70, 0};

            struct Penalties
            {
                /// TODO
                static constexpr unsigned short ROUTE_SEGMENT = 8;

                /// TODO
                static constexpr unsigned short ROUTE_DIRECTION_CHANGE = 0;

                /// TODO
                static constexpr unsigned short ROUTE_DIRECTION_NORTHWEST_OR_SOUTHEAST = 0;

                /// TODO
                static constexpr unsigned short ROUTE_MISSING_FLAG = 64;

                /// TODO
                static constexpr unsigned short FARM_LAND_ROAD = 128;

                /// TODO
                static constexpr unsigned short FARM_LAND_BUILDING = 1024;

                /// TODO
                static constexpr unsigned short TREE_LAND = 32;

                /// TODO
                static constexpr unsigned short CLOSE_TO_POINT_DISTANCE = 1 << 12;

                /// TODO
                static constexpr unsigned short SAFE_ZONE_MILITARY_RADIUS_FACTOR = 2;

                /// TODO
                static constexpr unsigned short STONE_ON_NEIGHBOUR = 32;

                /// TODO
                static constexpr unsigned short TREE_ON_NEIGHBOUR = 4;

                /// TODO
                static constexpr unsigned short BORDER_ON_NEIGHBOUR = 8;
            };
        };

        struct Buildings
        {
            /// TODO
            static constexpr unsigned short METALWORKS_TOOL_REQUEST_AMOUNT = 8;
        };

        struct Counters
        {
            /// TODO
            unsigned short ADJUST_MINE = 10;

            /// TODO
            unsigned short ADJUST_MILITARY_BUILDINGS = 50;

            /// TODO
            unsigned short DESTROY_UNUSED_ROADS = 3000;

            /// TODO
            unsigned short RECONNECT_FLAGS = 5000;

            /// TODO
            unsigned UPGRADE_ROAD_FACTOR = 10;

            /// TODO
            unsigned COIN_ADJUSTMENT = 1000;
        } counters;

        Config(AI::Level level) : player(level), actions(level) {}
    };
}} // namespace AI::jd

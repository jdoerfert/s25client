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

#include "Utils.h"

namespace AI {
namespace jd {

void tprintf(const char* format)
{
    std::cout << format;
}

std::size_t construction::Building::hash() const noexcept
{
    std::size_t h1 = quality;
    std::size_t h2 = std::hash<MapPoint>{}(originPt);
    return h1 ^ (h2 << 8);
}
bool construction::Building::isEqual(const construction::Building &other) const noexcept
{
    return quality == other.quality && originPt == other.originPt;
}
StreamTy& construction::Building::operator>>(StreamTy& out) const noexcept {
    return out << "Building[" << type << "@" << originPt << "]";
}

std::size_t construction::Route::hash() const noexcept
{
  std::size_t h = 0;
    for(auto& d : directions)
        h = ((h * 31) + d.native_value());
    return h;
}
bool construction::Route::isEqual(const construction::Route &other) const noexcept
{
    return directions == other.directions;
}

StreamTy& construction::Route::operator>>(StreamTy& out) const noexcept
{
    out << "Route{";
    for(auto& dir : directions)
    {
        ENUM_PRINT_BEGIN(dir.native_value());
        ENUM_PRINT_NS(out, Direction, WEST);
        ENUM_PRINT_NS(out, Direction, NORTHWEST);
        ENUM_PRINT_NS(out, Direction, NORTHEAST);
        ENUM_PRINT_NS(out, Direction, EAST);
        ENUM_PRINT_NS(out, Direction, SOUTHEAST);
        ENUM_PRINT_NS(out, Direction, SOUTHWEST);
        ENUM_PRINT_END();
        out << " - ";
    }
    out << "}";
    return out;
}

std::size_t construction::Road::hash() const noexcept
{
    std::size_t h1 = std::hash<MapPoint>{}(originPt);
    std::size_t h2 = 0;// std::hash<Route>{}(route);
    return h1 ^ (h2 << 8);
}
bool construction::Road::isEqual(const construction::Road &other) const noexcept
{
    return originPt == other.originPt && route == other.route;
}

StreamTy& construction::Road::operator>>(StreamTy &out) const noexcept
{
    out << "Road[" << originPt << "| " << route << "]";
    return out;
}

StreamTy& operator<<(StreamTy& out, const MapPoint& pt)
{
    out << "(" << pt.x << "|" << pt.y << ")";
    return out;
}

StreamTy& operator<<(StreamTy& out, const RoadNote& roadNote)
{
    out << "road ";
    if(roadNote.type == RoadNote::ConstructionFailed)
        out << "not ";
    construction::Road road{roadNote.pos, roadNote.route};
    out << "constructuted; " << road;
    return out;
}

StreamTy& operator<<(StreamTy& out, const AIResource& aiResource)
{
    ENUM_PRINT_BEGIN(aiResource);
    ENUM_PRINT_NS(out, AIResource, WOOD);
    ENUM_PRINT_NS(out, AIResource, STONES);
    ENUM_PRINT_NS(out, AIResource, GOLD);
    ENUM_PRINT_NS(out, AIResource, IRONORE);
    ENUM_PRINT_NS(out, AIResource, COAL);
    ENUM_PRINT_NS(out, AIResource, GRANITE);
    ENUM_PRINT_NS(out, AIResource, PLANTSPACE);
    ENUM_PRINT_NS(out, AIResource, BORDERLAND);
    ENUM_PRINT_NS(out, AIResource, FISH);
    ENUM_PRINT_NS(out, AIResource, MULTIPLE);
    ENUM_PRINT_NS(out, AIResource, BLOCKED);
    ENUM_PRINT_NS(out, AIResource, NOTHING);
    ENUM_PRINT_END();
    return out;
}

StreamTy& operator<<(StreamTy& out, const BuildingType buildingType)
{
    ENUM_PRINT_BEGIN(buildingType);
    ENUM_PRINT_W_PREFIX(out, BLD_, HEADQUARTERS);
    ENUM_PRINT_W_PREFIX(out, BLD_, BARRACKS);
    ENUM_PRINT_W_PREFIX(out, BLD_, GUARDHOUSE);
    ENUM_PRINT_W_PREFIX(out, BLD_, NOTHING2);
    ENUM_PRINT_W_PREFIX(out, BLD_, WATCHTOWER);
    ENUM_PRINT_W_PREFIX(out, BLD_, NOTHING3);
    ENUM_PRINT_W_PREFIX(out, BLD_, NOTHING4);
    ENUM_PRINT_W_PREFIX(out, BLD_, NOTHING5);
    ENUM_PRINT_W_PREFIX(out, BLD_, NOTHING6);
    ENUM_PRINT_W_PREFIX(out, BLD_, FORTRESS);
    ENUM_PRINT_W_PREFIX(out, BLD_, GRANITEMINE);
    ENUM_PRINT_W_PREFIX(out, BLD_, COALMINE);
    ENUM_PRINT_W_PREFIX(out, BLD_, IRONMINE);
    ENUM_PRINT_W_PREFIX(out, BLD_, GOLDMINE);
    ENUM_PRINT_W_PREFIX(out, BLD_, LOOKOUTTOWER);
    ENUM_PRINT_W_PREFIX(out, BLD_, NOTHING7);
    ENUM_PRINT_W_PREFIX(out, BLD_, CATAPULT);
    ENUM_PRINT_W_PREFIX(out, BLD_, WOODCUTTER);
    ENUM_PRINT_W_PREFIX(out, BLD_, FISHERY);
    ENUM_PRINT_W_PREFIX(out, BLD_, QUARRY);
    ENUM_PRINT_W_PREFIX(out, BLD_, FORESTER);
    ENUM_PRINT_W_PREFIX(out, BLD_, SLAUGHTERHOUSE);
    ENUM_PRINT_W_PREFIX(out, BLD_, HUNTER);
    ENUM_PRINT_W_PREFIX(out, BLD_, BREWERY);
    ENUM_PRINT_W_PREFIX(out, BLD_, ARMORY);
    ENUM_PRINT_W_PREFIX(out, BLD_, METALWORKS);
    ENUM_PRINT_W_PREFIX(out, BLD_, IRONSMELTER);
    ENUM_PRINT_W_PREFIX(out, BLD_, CHARBURNER);
    ENUM_PRINT_W_PREFIX(out, BLD_, PIGFARM);
    ENUM_PRINT_W_PREFIX(out, BLD_, STOREHOUSE);
    ENUM_PRINT_W_PREFIX(out, BLD_, NOTHING9);
    ENUM_PRINT_W_PREFIX(out, BLD_, MILL);
    ENUM_PRINT_W_PREFIX(out, BLD_, BAKERY);
    ENUM_PRINT_W_PREFIX(out, BLD_, SAWMILL);
    ENUM_PRINT_W_PREFIX(out, BLD_, MINT);
    ENUM_PRINT_W_PREFIX(out, BLD_, WELL);
    ENUM_PRINT_W_PREFIX(out, BLD_, SHIPYARD);
    ENUM_PRINT_W_PREFIX(out, BLD_, FARM);
    ENUM_PRINT_W_PREFIX(out, BLD_, DONKEYBREEDER);
    ENUM_PRINT_W_PREFIX(out, BLD_, HARBORBUILDING);
    ENUM_PRINT_END();
    return out;
}

constexpr int64_t Score::LB;
constexpr int64_t Score::UB;

}
}

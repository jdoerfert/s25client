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

#include <cstdint>
#include <functional>
#include <iostream>
#include <random>
#include <vector>
#include <set>
#include <cassert>

#include "s25util/warningSuppression.h"

#include <boost/optional/optional_io.hpp>

#include "Timer.h"
#include "ai/AIResource.h"
#include "notifications/RoadNote.h"
#include "world/NodeMapBase.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/Direction.h"
#include "gameTypes/MapCoordinates.h"
#include "gameData/BuildingConsts.h"
#include "gameData/JobConsts.h"

namespace AI {
namespace jd {

template<class T>
constexpr const T& clamp( const T& v, const T& lo, const T& hi )
{
    assert( !(hi < lo) );
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

using StreamTy = decltype(std::cout);

template<typename T>
struct Base
{
    friend StreamTy& operator<<(StreamTy& out, const T& obj) noexcept {
      return obj >> out;
    }
    virtual StreamTy& operator>>(StreamTy& out) const noexcept = 0;
    virtual bool isEqual(const T& other) const noexcept = 0;
    virtual std::size_t hash() const noexcept = 0;
    bool operator==(const T& other) const noexcept { return isEqual(other); }
    bool operator!=(const T& other) const noexcept { return !(*this == other); }
};

// {{{ Debugging Utilities

StreamTy& operator<<(StreamTy& out, const MapPoint& pt);
StreamTy& operator<<(StreamTy& out, const RoadNote& roadNote);
StreamTy& operator<<(StreamTy& out, const AIResource &);
StreamTy& operator<<(StreamTy& out, const BuildingType);

void tprintf(const char* format);

template<typename T, typename... Targs>
void tprintf(const char* format, const T& value, const Targs&... Fargs)
{
    for(; *format != '\0'; format++)
    {
        if(*format == '%')
        {
            std::cout << value;
            tprintf(format + 1, Fargs...);
            return;
        }
        std::cout << *format;
    }
}

#if 0
#define SAY(msg, ...) (tprintf((_TAG_ " " msg), __VA_ARGS__), std::cout << std::endl);
#else
#define SAY(msg, ...) (void)msg;
#endif

#define ENUM_PRINT_BEGIN(var) \
    switch(var)               \
    {
#define ENUM_PRINT_W_PREFIX(out, prefix, val) \
    case prefix##val:                         \
        out << #val;                          \
        break;
#define ENUM_PRINT(out, val) \
    case val:                \
        out << #val;         \
        break;
#define ENUM_PRINT_NS(out, ns, val) \
    case ns::val:                   \
        out << #val;                \
        break;

#define ENUM_PRINT_END() }

struct RAIISayTimer :public Timer
{
#define _TAG_ "Timer"
    RAIISayTimer(std::string msg) : Timer(true), msg(msg) {
    //printf("[%s] Enter %s\n", _TAG_, msg.c_str());
    }
    ~RAIISayTimer()
    {

    // integral duration: requires duration_cast
      //if (std::chrono::duration_cast<std::chrono::milliseconds>(getElapsed()).count() > 5)
    //printf("[%s] %ld %s\n", _TAG_, std::chrono::duration_cast<std::chrono::milliseconds>(getElapsed()).count(), msg.c_str());
  }
  std::string msg;
#undef _TAG_
};

// }}}

static inline boost::optional<GoodType> job2tool(Job job) { return JOB_CONSTS[job].tool; }

struct Score {

  Score() : value(0) {}
  Score(int64_t value) : value(value) {}

  static Score invalid() { return Score{std::numeric_limits<int64_t>::min()}; }

  bool isValid() const { return value != std::numeric_limits<int64_t>::min(); }
  int64_t getValue() const { assert(isValid()); return value; }
  Score &invalidate() { value = std::numeric_limits<int64_t>::min(); return *this;}

  bool operator<(const Score &other) const {
    if (!isValid())
      return false;
    if (!other.isValid())
      return true;
    return getValue() < other.getValue();
  }
  bool operator>(const Score &other) const {
    if (!isValid())
      return false;
    if (!other.isValid())
      return true;
    return getValue() > other.getValue();
  }
  Score& operator+=(const Score &other) {
    if (!isValid())
      return *this;
    if (!other.isValid()) {
      invalidate();
      return *this;
    }
    value = clamp(value + other.value, LB, UB);
    if (value == LB)
      invalidate();
    return *this;
  }
  Score& operator-=(const Score &other) {
    if (!isValid())
      return *this;
    if (!other.isValid()) {
      invalidate();
      return *this;
    }
    value = clamp(value - other.value, LB, UB);
    if (value == LB)
      invalidate();
    return *this;
  }

  friend StreamTy& operator<<(StreamTy& out, const Score &obj) noexcept {
    if (obj.isValid())
      out << obj.getValue();
    else
      out << "[no score]";
    return out;
  };

  private:
  static constexpr int64_t LB = -1 * (1<<24);
  static constexpr int64_t UB = +1 * (1<<24);
  int64_t value = 0;
};

// {{{ Construction Utility Classes

namespace construction {

    struct Building : public Base<Building>
    {
        Building(BuildingType buildingType, MapPoint originPt, const MapBase& mapBase)
            : type(buildingType), quality(BUILDING_SIZE[buildingType])
        {
            if(originPt.isValid())
                updateOrigin(originPt, mapBase);
        }

        void updateOrigin(MapPoint pt, const MapBase &mapBase)
        {
            originPt = pt;
            flagPt = mapBase.GetNeighbour(originPt, Direction::SOUTHEAST);
        }

        StreamTy& operator>>(StreamTy& out) const noexcept override;
        bool isEqual(const Building& other) const noexcept override;
        std::size_t hash() const noexcept override;

        const BuildingType type;
        const BuildingQuality quality;
        MapPoint originPt;
        MapPoint flagPt;
    };

    struct Route : public Base<Route>
    {
        Route() : Base<Route>() {};
        Route(const std::vector<Direction>& directions) : Base<Route>(), directions(directions){};

        StreamTy& operator>>(StreamTy& out) const noexcept override;
        bool isEqual(const Route& other) const noexcept override;
        std::size_t hash() const noexcept override;

       std::vector<Direction> directions;
    };

    struct Road : public Base<Road>
    {
        Road(MapPoint originPt, Route route) : Base<Road>(), originPt(originPt), route(route) {}

        StreamTy& operator>>(StreamTy& out) const noexcept override;
        bool isEqual(const Road& other) const noexcept override;
        std::size_t hash() const noexcept override;

        const MapPoint originPt;
        Route route;
    };
} // namespace construction

// }}}


// {{{ Talking

static constexpr const char* NAMES[] = {
  "Mica Mefford",       "Dorene Whitworth",    "Roselyn Flury",      "Stephani Palladino", "Richelle Covington", "Alonzo Bastian",
  "Daniell Blanton",    "Donella Darbonne",    "Edith Maselli",      "Antonia Lanning",    "Emelina Ortis",      "Keeley Reid",
  "Jamika Balsamo",     "Leif Covert",         "Filomena Cordero",   "Corinne Branscome",  "Laveta Buford",      "Scot Wiedman",
  "Emery Pinkston",     "Burl Ptak",           "Eldon Licata",       "Marilee Chipman",    "Tom Muntz",          "Barbera Mossman",
  "Cassey Hutchens",    "Ralph Place",         "Vernia Cleaves",     "Augustus Delawder",  "Isela Ahlquist",     "Daron Cantley",
  "Emmett Warburton",   "Kiesha Hausler",      "Juliette Sickels",   "Huong Chenoweth",    "Cora Bowlby",        "Alexia Trivett",
  "Aleta Moodie",       "Brittney Haar",       "Santa Bellew",       "Laquita Dineen",     "Katie Hilario",      "Stacia Clements",
  "Letitia Alford",     "Eartha Bussey",       "Tillie Yelle",       "Joslyn Galaz",       "Alishia Pleiman",    "Hope Brittan",
  "Lyda Matthes",       "Tamesha Cumpston",    "Tresa Olah",         "Tyrell Hutchcraft",  "Yoko Buskirk",       "Cinderella Prins",
  "Jeanice Thibert",    "Kylie Freel",         "Delores Yedinak",    "Shamika Peguero",    "Twila Mitchem",      "Maire Myerson",
  "Elmira Sedberry",    "Jodee Scurlock",      "Pandora Gregoire",   "Carmon Reider",      "Theda Faver",        "Tashina Pellot",
  "Conception Bennett", "Carolee Peay",        "Armanda Detrick",    "Tu Hendry",          "Virgen Labat",       "Gidget Schiller",
  "Alfonso Nivens",     "Krystin Cavalier",    "Gudrun Vandergriff", "Cher Randle",        "Shonta Copas",       "Jamel Signor",
  "Elma Corkill",       "Yulanda Wheelwright", "Leo Cranmer",        "Michael Cassity",    "Mariel Salyers",     "Cathryn Inge",
  "Lonny Lawson",       "Elwanda Steinfeldt",  "Taunya Naples",      "Jonathan Nedeau",    "Micheal Resler",     "Edwin Cazarez",
  "Silvia Woodell",     "Lizbeth Brightwell",  "Merideth Beckles",   "Josie Bingaman",     "Arnulfo Marinelli",  "Easter Gaudin",
  "Lizeth Dobbins",     "Rossie Penner",       "Rogelio Adame",      "Machelle Guenther"};
static constexpr unsigned NUMNAMES = sizeof(NAMES) / sizeof(NAMES[0]);

template<typename RNG>
const char* getRandomName(RNG& rng)
{
    static std::uniform_int_distribution<typename RNG::result_type> dist(0, NUMNAMES);
    return NAMES[dist(rng)];
}

// }}}

} // namespace jd
} // namespace AI

// {{{ Hashing utility

#define STD_HASH_VIA_HASH_MEMBER(TYPE)                                                               \
    namespace std {                                                                                  \
        template<>                                                                                   \
        struct hash<TYPE>                                                                            \
        {                                                                                            \
            std::size_t operator()(TYPE const& obj) const noexcept { return obj.hash(); }            \
        };                                                                                           \
        template<>                                                                                   \
        struct equal_to<TYPE>                                                                        \
        {                                                                                            \
            constexpr bool operator()(const TYPE& lhs, const TYPE& rhs) const { return lhs == rhs; } \
        };                                                                                           \
    }

//STD_HASH_VIA_HASH_MEMBER(ai::jd::construction::Route)
//STD_HASH_VIA_HASH_MEMBER(ai::jd::construction::Road)

namespace std {
    template<>
    struct hash<MapPoint>
    {
        std::size_t operator()(MapPoint const& pt) const noexcept
        {
            std::size_t h1 = std::hash<MapCoord>{}(pt.x);
            std::size_t h2 = std::hash<MapCoord>{}(pt.y);
            return h1 ^ (h2 << (sizeof(std::size_t) * 4));
        }
    };
    template<>
    struct less<MapPoint>
    {
        bool operator()(const MapPoint& lhs, const MapPoint& rhs) const { return lhs.x == rhs.x ? lhs.y < rhs.y : lhs.x < rhs.x; }
    };
    template<>
    struct greater<MapPoint>
    {
        bool operator()(const MapPoint& lhs, const MapPoint& rhs) const { return lhs.x == rhs.x ? lhs.y > rhs.y : lhs.x > rhs.x; }
    };
    template<>
    struct less<std::pair<MapPoint, MapPoint>>
    {
        bool operator()(const std::pair<MapPoint, MapPoint>& lhs, const std::pair<MapPoint, MapPoint>& rhs) const
        {
            return lhs.first == rhs.first ? std::less<MapPoint>{}(lhs.second, rhs.second) : std::less<MapPoint>{}(lhs.first, rhs.first);
        }
    };
    template<typename  T>
    struct less<std::pair<T, MapPoint>>
    {
        bool operator()(const std::pair<T, MapPoint>& lhs, const std::pair<T, MapPoint>& rhs) const
        {
            return lhs.first == rhs.first ? std::less<MapPoint>{}(lhs.second, rhs.second) : std::less<T>{}(lhs.first, rhs.first);
        }
    };
    template<typename  T>
    struct greater<std::pair<T, MapPoint>>
    {
        bool operator()(const std::pair<T, MapPoint>& lhs, const std::pair<T, MapPoint>& rhs) const
        {
            return lhs.first == rhs.first ? std::greater<MapPoint>{}(lhs.second, rhs.second) : std::greater<T>{}(lhs.first, rhs.first);
        }
    };

    //template<>
    //struct hash<RoadNote>
    //{
        //std::size_t operator()(RoadNote const& roadNote) const noexcept
        //{
            //return std::hash<ai::jd::construction::Road>{}({roadNote.pos, roadNote.route});
        //}
    //};

} // namespace std

template<typename ContainerT, typename PredicateT>
void discard_if(ContainerT& items, PredicateT predicate)
{
    for(auto it = items.begin(); it != items.end();)
    {
        if(predicate(*it))
            it = items.erase(it);
        else
            ++it;
    }
}

// }}}

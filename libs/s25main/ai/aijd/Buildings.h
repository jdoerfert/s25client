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

#include "Utils.h"
#include "ai/AIPlayer.h"
#include "notifications/Subscription.h"

#include <set>
#include <unordered_set>
#include <vector>

namespace AI { namespace jd {

    struct View;

    template<typename T>
    struct Range {
        static bool isa(unsigned u)
        {
            return u >= T::FIRST && u < T::LAST;
        }
    };

    struct ResourceInfo : Range<ResourceInfo>
    {
        enum Kind
        {
            FIRST,
            TREE = FIRST,
            STONE,
            GOLD,
            IRONORE,
            COAL,
            GRANITE,
            PLANTSPACE,
            BORDERLAND,
            FISH,
            ANIMAL,
            WATER,
            LAST,
        };

        friend StreamTy& operator<<(StreamTy& out, Kind kind) noexcept
        {
            ENUM_PRINT_BEGIN(kind);
            ENUM_PRINT(out, TREE);
            ENUM_PRINT(out, STONE);
            ENUM_PRINT(out, GOLD);
            ENUM_PRINT(out, IRONORE);
            ENUM_PRINT(out, COAL);
            ENUM_PRINT(out, GRANITE);
            ENUM_PRINT(out, PLANTSPACE);
            ENUM_PRINT(out, BORDERLAND);
            ENUM_PRINT(out, FISH);
            ENUM_PRINT(out, ANIMAL);
            ENUM_PRINT(out, WATER);
            ENUM_PRINT(out, LAST);
            ENUM_PRINT_END();
            return out;
        }
    };

    struct CommodityInfo : Range<CommodityInfo>

    {
        enum Kind
        {
            FIRST = ResourceInfo::LAST,
            WOOD = FIRST,
            BOARDS,
            STONES,
            COINS,
            COAL,
            GOLD,
            SWORD,
            SHIELD,
            IRONORE,
            IRON,
            GRAIN,
            FOOD,
            FISH = FOOD,
            MEAT = FOOD,
            BREAD = FOOD,
            PIG,
            FLOUR,
            WATER,
            BEER,
            TONGS,
            AXE,
            SAW,
            PICK_AXE,
            HAMMER,
            SHOVEL,
            CRUCIBLE,
            ROD_AND_LINE,
            SCYTHE,
            CLEAVER,
            ROLLING_PIN,
            BOW,
            BOAT,
            LAST,
        };

        friend StreamTy& operator<<(StreamTy& out, Kind kind) noexcept
        {
            ENUM_PRINT_BEGIN(kind);
            ENUM_PRINT(out, WOOD);
            ENUM_PRINT(out, BOARDS);
            ENUM_PRINT(out, STONES);
            ENUM_PRINT(out, PIG);
            ENUM_PRINT(out, GRAIN);
            ENUM_PRINT(out, FLOUR);
            ENUM_PRINT(out, FOOD);
            ENUM_PRINT(out, WATER);
            ENUM_PRINT(out, BEER);
            ENUM_PRINT(out, COAL);
            ENUM_PRINT(out, IRONORE);
            ENUM_PRINT(out, GOLD);
            ENUM_PRINT(out, IRON);
            ENUM_PRINT(out, COINS);
            ENUM_PRINT(out, TONGS);
            ENUM_PRINT(out, AXE);
            ENUM_PRINT(out, SAW);
            ENUM_PRINT(out, PICK_AXE);
            ENUM_PRINT(out, HAMMER);
            ENUM_PRINT(out, SHOVEL);
            ENUM_PRINT(out, CRUCIBLE);
            ENUM_PRINT(out, ROD_AND_LINE);
            ENUM_PRINT(out, SCYTHE);
            ENUM_PRINT(out, CLEAVER);
            ENUM_PRINT(out, ROLLING_PIN);
            ENUM_PRINT(out, BOW);
            ENUM_PRINT(out, SWORD);
            ENUM_PRINT(out, SHIELD);
            ENUM_PRINT(out, BOAT);
            ENUM_PRINT(out, LAST);
            ENUM_PRINT_END();
            return out;
        }

        static constexpr unsigned NUM = LAST - FIRST;
    };

    struct ConceptInfo : Range<ConceptInfo>

    {
        enum Kind
        {
            FIRST = CommodityInfo::LAST,
            LOOKOUTTOWER = FIRST,
            HEADQUATERS,
            FISHERY,
            FORESTER,
            TREE_SPACE,
            FARM_SPACE,
            MINE_SPACE,
            STOREHOUSE,
            METALWORKS,
            BURNSITE,
            MILITARY,
            CATAPULT,
            HIDDEN,
            DONKEY,
            HUNTER,
            NEAR_UNOWNED,
            NEAR_BORDER,
            UNOWNED,
            OWNED,
            ENEMY,
            LAST,
        };

        friend StreamTy& operator<<(StreamTy& out, Kind kind) noexcept
        {
            ENUM_PRINT_BEGIN(kind);
            ENUM_PRINT(out, LOOKOUTTOWER);
            ENUM_PRINT(out, HEADQUATERS);
            ENUM_PRINT(out, FISHERY);
            ENUM_PRINT(out, FORESTER);
            ENUM_PRINT(out, TREE_SPACE);
            ENUM_PRINT(out, FARM_SPACE);
            ENUM_PRINT(out, MINE_SPACE);
            ENUM_PRINT(out, STOREHOUSE);
            ENUM_PRINT(out, METALWORKS);
            ENUM_PRINT(out, BURNSITE);
            ENUM_PRINT(out, MILITARY);
            ENUM_PRINT(out, CATAPULT);
            ENUM_PRINT(out, HIDDEN);
            ENUM_PRINT(out, DONKEY);
            ENUM_PRINT(out, HUNTER);
            ENUM_PRINT(out, NEAR_UNOWNED);
            ENUM_PRINT(out, NEAR_BORDER);
            ENUM_PRINT(out, UNOWNED);
            ENUM_PRINT(out, OWNED);
            ENUM_PRINT(out, ENEMY);
            ENUM_PRINT(out, LAST);
            ENUM_PRINT_END();
            return out;
        }

        static constexpr unsigned NUM = LAST - FIRST;
    };

    struct BuildingEffect
    {
        enum Type
        {
            NONE = 0,
            LOCAL = 1,
            GLOBAL = 2,
        };

        enum Kind
        {
            PRODUCTION,
            CONSUMPTION,
            AFFECTION,
            AVERSION,
        };

        unsigned getRadius() const
        {
            assert(isLocal());
            return radius;
        }

        bool isValid() const { return type != NONE; }
        bool isLocal() const { return type & LOCAL; }
        bool isGlobal() const { return type & GLOBAL; }

        bool isOfKind(Kind k) const { return kind == k; }
        bool isProduction() const { return kind == PRODUCTION; }
        bool isConsumption() const { return kind == CONSUMPTION; }
        bool isPreference() const { return isAffection() || isAversion(); }
        bool isAffection() const { return kind == AFFECTION; }
        bool isAversion() const { return kind == AVERSION; }

        int getAmount() const { return amount; }
        unsigned getThing() const { return thing; }

        template<typename InfoT>
        bool isKind() const
        {
            return type != NONE && thing >= InfoT::FIRST && thing < InfoT::LAST;
        }

        template<typename InfoT>
        typename InfoT::Kind getKind() const
        {
            assert(isKind<InfoT>());
            return typename InfoT::Kind(thing);
        }

        Kind kind = PRODUCTION;
        Type type = NONE;
        unsigned thing = 0;
        int radius = 0;
        int amount = 1;

        BuildingEffect() = default;
        BuildingEffect(Kind kind, Type type, unsigned thing, unsigned radius, unsigned amount)
            : kind(kind), type(type), thing(thing), radius(radius), amount(amount)
        {}
    };

    struct BuildingInfo
    {
        bool isValid() const { return valid; }
        BuildingType getBuildingType() const { return buildingType; }
        std::string getName() const { return BUILDING_NAMES[buildingType]; }
        unsigned getBoardCosts() const { return BUILDING_COSTS[0][buildingType].boards; }
        unsigned getStoneCosts() const { return BUILDING_COSTS[0][buildingType].stones; }
        BuildingQuality getBuildingQuality() const { return BUILDING_SIZE[buildingType]; }
        boost::optional<Job> getJob() const { return BLD_WORK_DESC[buildingType].job; }

        bool canBePlacedAt(View& view, MapPoint pt) const;
        Score getProximityScore(View& view, MapPoint pt) const;
        void checkResources(View& view, MapPoint bldgPt, bool &requiredResourcesAvailable, int factor , bool globalOnly) const;
        void updateResources(View& view, MapPoint bldgPt, bool &requiredResourcesAvailable, int factor, bool globalOnly) const;
        Score getLocationScore(View& view, MapPoint pt) const;

        static const BuildingInfo& get(BuildingType buildingType) { return buildingInfos[buildingType]; }

        template<typename CallbackType>
        bool foreachEffect(CallbackType callback) const
        {
            for(const BuildingEffect& effect : effects)
                if(!callback(effect))
                    return false;
            return true;
        }
        template<typename CallbackType>
        bool foreachEffectOfKind(BuildingEffect::Kind kind, CallbackType callback) const
        {
            for(const BuildingEffect& effect : effects)
                if(effect.isOfKind(kind))
                    if(!callback(effect))
                        return false;
            return true;
        }
        template<typename CallbackType>
        bool foreachConsumption(CallbackType callback) const
        {
            return foreachEffectOfKind<CallbackType>(BuildingEffect::CONSUMPTION, callback);
        }
        template<typename CallbackType>
        bool foreachProduction(CallbackType callback) const
        {
            return foreachEffectOfKind<CallbackType>(BuildingEffect::PRODUCTION, callback);
        }
        template<typename CallbackType>
        bool foreachAffection(CallbackType callback) const
        {
            return foreachEffectOfKind<CallbackType>(BuildingEffect::AFFECTION, callback);
        }
        template<typename CallbackType>
        bool foreachAversion(CallbackType callback) const
        {
            return foreachEffectOfKind<CallbackType>(BuildingEffect::AVERSION, callback);
        }
        template<typename CallbackType>
        bool foreachPreference(CallbackType callback) const
        {
            return foreachAffection<CallbackType>(callback) && foreachAversion<CallbackType>(callback);
        }

        template<typename T>
        const BuildingEffect& getLocalConsumption(typename T::Kind kind) const
        {
            for(const BuildingEffect& effect : effects)
                if(effect.isConsumption() && effect.isLocal() && effect.isKind<T>() && effect.getKind<T>() == kind)
                    return effect;
            assert(0);
        }
        template<typename T>
        const BuildingEffect& getConsumption(typename T::Kind kind) const
        {
            for(const BuildingEffect& effect : effects)
                if(effect.isConsumption() && effect.isKind<T>() && effect.getKind<T>() == kind)
                    return effect;
            __builtin_unreachable();
        }

    private:
        BuildingInfo(BuildingType buildingType);

        bool valid = true;
        const BuildingType buildingType;

        void addProduction(BuildingEffect::Type type, unsigned thing, unsigned radius, unsigned amount = 1)
        {
            effects.push_back(BuildingEffect(BuildingEffect::PRODUCTION, type, thing, radius, amount));
        }
        void addLocalProduction(unsigned thing, unsigned radius, unsigned amount = 1)
        {
            addProduction(BuildingEffect::LOCAL, thing, radius, amount);
        }
        void addGlobalProduction(unsigned thing, unsigned amount = 1) { addProduction(BuildingEffect::GLOBAL, thing, 0, amount); }
        void addLocalAndGlobalProduction(unsigned thing, unsigned radius, unsigned amount = 1)
        {
            addProduction(BuildingEffect::LOCAL, thing, radius, amount);
            addProduction(BuildingEffect::GLOBAL, thing, radius, amount);
        }
        void addConsumption(BuildingEffect::Type type, unsigned thing, unsigned radius, unsigned amount = 1)
        {
            effects.push_back(BuildingEffect(BuildingEffect::CONSUMPTION, type, thing, radius, amount));
            while (radius) {
              radius /= 2;
              addAffection(thing, radius, amount);
            }
        }
        void addLocalConsumption(unsigned thing, unsigned radius, unsigned amount = 1)
        {
            addConsumption(BuildingEffect::LOCAL, thing, radius, amount);
        }
        void addGlobalConsumption(unsigned thing, unsigned amount = 1) { addConsumption(BuildingEffect::GLOBAL, thing, 0, amount); }
        void addLocalAndGlobalConsumption(unsigned thing, unsigned radius, unsigned amount = 1)
        {
            addConsumption(BuildingEffect::LOCAL, thing, radius, amount);
            addConsumption(BuildingEffect::GLOBAL, thing, radius, amount);
        }
        void addAffection(unsigned thing, unsigned radius, unsigned amount = 1)
        {
            effects.push_back(BuildingEffect(BuildingEffect::AFFECTION, BuildingEffect::LOCAL, thing, radius, amount));
        }
        void addAversion(unsigned thing, unsigned radius, unsigned amount = 1)
        {
            effects.push_back(BuildingEffect(BuildingEffect::AVERSION, BuildingEffect::LOCAL, thing, radius, amount));
        }

        std::vector<BuildingEffect> effects;

        static std::array<BuildingInfo, NUM_BUILDING_TYPES> buildingInfos;
    };

    struct Building
    {
        Building(BuildingType buildingType, MapPoint pt);

        BuildingType getBuildingType() const { return buildingInfo.getBuildingType(); }

        void init(View& view);
        void check(View& view);
        void destroy(View& view);

        bool hasRequiredResources() const { return hasResources; }

    private:
        bool hasResources = true;
        const BuildingInfo& buildingInfo;
        const MapPoint pt;
    };

    static inline CommodityInfo::Kind goodType2CommodityInfoKind(GoodType goodType)
    {
        switch(goodType)
        {
            case GD_BEER: return CommodityInfo::BEER;
            case GD_TONGS: return CommodityInfo::TONGS;
            case GD_HAMMER: return CommodityInfo::HAMMER;
            case GD_AXE: return CommodityInfo::AXE;
            case GD_SAW: return CommodityInfo::SAW;
            case GD_PICKAXE: return CommodityInfo::PICK_AXE;
            case GD_SHOVEL: return CommodityInfo::SHOVEL;
            case GD_CRUCIBLE: return CommodityInfo::CRUCIBLE;
            case GD_RODANDLINE: return CommodityInfo::ROD_AND_LINE;
            case GD_SCYTHE: return CommodityInfo::SCYTHE;
            case GD_WATEREMPTY: return CommodityInfo::LAST;
            case GD_WATER: return CommodityInfo::WATER;
            case GD_CLEAVER: return CommodityInfo::CLEAVER;
            case GD_ROLLINGPIN: return CommodityInfo::ROLLING_PIN;
            case GD_BOW: return CommodityInfo::BOW;
            case GD_BOAT: return CommodityInfo::BOAT;
            case GD_SWORD: return CommodityInfo::SWORD;
            case GD_IRON: return CommodityInfo::IRON;
            case GD_FLOUR: return CommodityInfo::FLOUR;
            case GD_FISH: return CommodityInfo::FISH;
            case GD_BREAD: return CommodityInfo::BREAD;
            case GD_SHIELDROMANS: return CommodityInfo::SHIELD;
            case GD_WOOD: return CommodityInfo::WOOD;
            case GD_BOARDS: return CommodityInfo::BOARDS;
            case GD_STONES: return CommodityInfo::STONES;
            case GD_SHIELDVIKINGS: return CommodityInfo::SHIELD;
            case GD_SHIELDAFRICANS: return CommodityInfo::SHIELD;
            case GD_GRAIN: return CommodityInfo::GRAIN;
            case GD_COINS: return CommodityInfo::COINS;
            case GD_GOLD: return CommodityInfo::GOLD;
            case GD_IRONORE: return CommodityInfo::IRONORE;
            case GD_COAL: return CommodityInfo::COAL;
            case GD_MEAT: return CommodityInfo::MEAT;
            case GD_HAM: return CommodityInfo::PIG;
            case GD_SHIELDJAPANESE: return CommodityInfo::SHIELD;
            case GD_NOTHING:;
                // case GD_INVALID: return CommodityInfo::LAST;
        };
        return CommodityInfo::LAST;
    }

}} // namespace AI::jd

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

#include "Buildings.h"
#include "Config.h"
#include "View.h"

#include "buildings/nobMilitary.h"
#include "gameTypes/BuildingType.h"

#include "gameData/BuildingProperties.h"
#include "gameData/MilitaryConsts.h"

#define _TAG_ "[BUILDINGS]"

using namespace AI;
using namespace jd;

static unsigned getMilitaryRadius(BuildingType buildingType)
{
    switch(buildingType)
    {
        case BLD_BARRACKS: return MILITARY_RADIUS[0];
        case BLD_GUARDHOUSE: return MILITARY_RADIUS[1];
        case BLD_WATCHTOWER: return MILITARY_RADIUS[2];
        case BLD_FORTRESS: return MILITARY_RADIUS[3];
        default: break;
    }
    assert(0);
    return 0;
}
static unsigned getMilitaryRadius(BuildingQuality buildingQuality)
{
    switch(buildingQuality)
    {
        case BQ_HUT: return getMilitaryRadius(BLD_GUARDHOUSE);
        case BQ_HOUSE: return getMilitaryRadius(BLD_WATCHTOWER);
        case BQ_CASTLE: return getMilitaryRadius(BLD_FORTRESS);
        default: return getMilitaryRadius(BLD_BARRACKS);
    }
    return 0;
}
static unsigned getRequiredMinimalDistanceToEnemy(BuildingType buildingType)
{
    switch(buildingType)
    {
        case BLD_BARRACKS: return Config::Score::REQUIRED_MINIMAL_DISTANCE_TO_ENEMY[0];
        case BLD_GUARDHOUSE: return Config::Score::REQUIRED_MINIMAL_DISTANCE_TO_ENEMY[1];
        case BLD_WATCHTOWER: return Config::Score::REQUIRED_MINIMAL_DISTANCE_TO_ENEMY[2];
        case BLD_FORTRESS: return Config::Score::REQUIRED_MINIMAL_DISTANCE_TO_ENEMY[3];
        default: break;
    }
    assert(0);
    return 0;
}
static unsigned getRequiredRemainingDistanceToEnemy(BuildingType buildingType)
{
    switch(buildingType)
    {
        case BLD_BARRACKS: return Config::Score::REQUIRED_REMAINING_DISTANCE_TO_ENEMY[0];
        case BLD_GUARDHOUSE: return Config::Score::REQUIRED_REMAINING_DISTANCE_TO_ENEMY[1];
        case BLD_WATCHTOWER: return Config::Score::REQUIRED_REMAINING_DISTANCE_TO_ENEMY[2];
        case BLD_FORTRESS: return Config::Score::REQUIRED_REMAINING_DISTANCE_TO_ENEMY[3];
        default: break;
    }
    assert(0);
    return 0;
}

BuildingInfo::BuildingInfo(BuildingType buildingType) : buildingType(buildingType)
{
    switch(buildingType)
    {
        case BLD_NOTHING2:
        case BLD_NOTHING3:
        case BLD_NOTHING4:
        case BLD_NOTHING5:
        case BLD_NOTHING6:
        case BLD_NOTHING7:
        case BLD_NOTHING9: valid = false; break;
        case BLD_HEADQUARTERS:
            addLocalAndGlobalProduction(ConceptInfo::STOREHOUSE, 12, 20);
            addLocalAndGlobalProduction(ConceptInfo::HEADQUATERS, 4, 1);
            break;
        case BLD_BARRACKS:
        case BLD_GUARDHOUSE:
            addAversion(ConceptInfo::ENEMY, 20, -1);
            addAversion(ConceptInfo::MINE_SPACE, 6, 1024);
            // Fallthrough
        case BLD_WATCHTOWER:
        case BLD_FORTRESS:
        {
            unsigned militaryRadius = getMilitaryRadius(buildingType);
            addLocalAndGlobalProduction(ConceptInfo::MILITARY, militaryRadius / 2);
            addLocalConsumption(ConceptInfo::NEAR_UNOWNED, 0, 8);
            addLocalConsumption(ConceptInfo::UNOWNED, militaryRadius, 8);
            // addAversion(ConceptInfo::MILITARY, 8, 64);
            addAversion(ConceptInfo::MILITARY, 0, -1);
            addAversion(ConceptInfo::OWNED, 7, 64);
            addAffection(ConceptInfo::HIDDEN, militaryRadius / 2, 64);
            addAffection(ResourceInfo::BORDERLAND, 2, 1 << 13);
            addAffection(ResourceInfo::BORDERLAND, 4, 1 << 10);
            addAffection(ResourceInfo::BORDERLAND, 6, 1 << 6);
            addAffection(ConceptInfo::UNOWNED, 5, 1 << 10);
            break;
        }
        case BLD_GRANITEMINE:
            addLocalAndGlobalConsumption(ResourceInfo::GRANITE, 2, 4);
            addGlobalConsumption(CommodityInfo::FOOD, 1);
            addLocalAndGlobalProduction(CommodityInfo::STONES, 0, 1);
            break;
        case BLD_COALMINE:
            addLocalAndGlobalConsumption(ResourceInfo::COAL, 2, 4);
            addGlobalConsumption(CommodityInfo::FOOD, 1);
            addLocalAndGlobalProduction(CommodityInfo::COAL, 0, 1);
            break;
        case BLD_IRONMINE:
            addLocalAndGlobalConsumption(ResourceInfo::IRONORE, 2, 4);
            // addGlobalProduction(CommodityInfo::IRON, 0);
            addGlobalConsumption(CommodityInfo::FOOD, 1);
            addLocalAndGlobalProduction(CommodityInfo::IRONORE, 0, 1);
            break;
        case BLD_GOLDMINE:
            addLocalAndGlobalConsumption(ResourceInfo::GOLD, 2, 2);
            // addGlobalProduction(CommodityInfo::COINS, 0);
            addGlobalConsumption(CommodityInfo::FOOD, 1);
            addLocalAndGlobalProduction(CommodityInfo::GOLD, 0, 1);
            break;
        case BLD_LOOKOUTTOWER:
            addLocalProduction(ConceptInfo::LOOKOUTTOWER, 10, 1);
            addLocalConsumption(ConceptInfo::HIDDEN, 10, 12);
            // addLocalConsumption(ResourceInfo::BORDERLAND, 4, 1);
            addAffection(ResourceInfo::BORDERLAND, 4, 1024);
            addAffection(ConceptInfo::HIDDEN, 10, 1024);
            addAffection(ConceptInfo::ENEMY, 20, 1024);
            addAversion(ConceptInfo::LOOKOUTTOWER, 0, -1);
            break;
        case BLD_CATAPULT:
            addLocalConsumption(ConceptInfo::ENEMY, 8, 1);
            addLocalProduction(ConceptInfo::CATAPULT, 8, 1);
            addAffection(ResourceInfo::BORDERLAND, 4, 64);
            addAffection(ConceptInfo::LOOKOUTTOWER, 5, 1);
            addAversion(ConceptInfo::CATAPULT, 0, -1);
            break;
        case BLD_WOODCUTTER:
            addGlobalConsumption(ResourceInfo::TREE, 1);
            addLocalConsumption(ResourceInfo::TREE, 5, 8);
            // addLocalConsumption(ResourceInfo::TREE, 7, 6);
            addAffection(ResourceInfo::TREE, 3, 4096);
            addLocalAndGlobalProduction(CommodityInfo::WOOD, 0, 1);
            break;
        case BLD_FISHERY:
            addLocalConsumption(ResourceInfo::FISH, 0, 2);
            addGlobalConsumption(ResourceInfo::FISH, 1);
            // addLocalAndGlobalConsumption(ResourceInfo::FISH, 8, 12);
            addLocalProduction(ConceptInfo::FISHERY, 6, 1);
            addGlobalProduction(CommodityInfo::FISH);
            addAffection(ResourceInfo::FISH, 4, 1 << 12);
            addAversion(ResourceInfo::BORDERLAND, 3, 1 << 15);
            addAversion(ConceptInfo::FISHERY, 0, -1);
            break;
        case BLD_QUARRY:
            addLocalConsumption(ResourceInfo::STONE, 5, 2);
            addLocalConsumption(ResourceInfo::STONE, 8, 2);
            addAffection(ResourceInfo::BORDERLAND, 8, 1 << 10);
            addGlobalProduction(CommodityInfo::STONES);
            break;
        case BLD_FORESTER:
            addLocalProduction(ConceptInfo::FORESTER, 4, 1);
            addLocalAndGlobalProduction(
              ResourceInfo::TREE, 0, BuildingInfo::get(BLD_WOODCUTTER).getLocalConsumption<ResourceInfo>(ResourceInfo::TREE).getAmount());
            addLocalAndGlobalConsumption(ResourceInfo::PLANTSPACE, 2, 4);
            addLocalProduction(ConceptInfo::TREE_SPACE, 3, 10);
            addAffection(CommodityInfo::WOOD, 2, 1024);
            addAffection(ResourceInfo::TREE, 3, 128);
            addAversion(ConceptInfo::FARM_SPACE, 4, -1);
            addAversion(ConceptInfo::TREE_SPACE, 4, 64);
            addAversion(ConceptInfo::STOREHOUSE, 4, 64);
            addAversion(ResourceInfo::BORDERLAND, 2, -1);
            addAversion(ConceptInfo::FORESTER, 0, -1);
            break;
        case BLD_SLAUGHTERHOUSE:
            addGlobalConsumption(CommodityInfo::PIG, 1);
            addGlobalProduction(CommodityInfo::MEAT, 1);
            addAffection(CommodityInfo::PIG, 4, 128);
            break;
        case BLD_HUNTER:
            addLocalConsumption(ResourceInfo::ANIMAL, 12, 5);
            addAversion(ConceptInfo::HUNTER, 5, -1);
            addGlobalProduction(CommodityInfo::MEAT);
            break;
        case BLD_BREWERY:
            addGlobalConsumption(CommodityInfo::WATER, 1);
            addGlobalConsumption(CommodityInfo::GRAIN, 1);
            // addGlobalConsumption(CommodityInfo::SWORD, 1);
            // addGlobalConsumption(CommodityInfo::SHIELD, 1);
            addGlobalProduction(CommodityInfo::BEER, 3);
            addAffection(ConceptInfo::STOREHOUSE, 4, 128);
            break;
        case BLD_ARMORY:
            addGlobalConsumption(CommodityInfo::IRON, 1);
            addGlobalConsumption(CommodityInfo::COAL, 1);
            addGlobalConsumption(CommodityInfo::BEER, 1);
            addGlobalProduction(CommodityInfo::SHIELD, 1);
            addGlobalProduction(CommodityInfo::SWORD, 1);
            addAffection(CommodityInfo::COAL, 7, 128);
            addAffection(CommodityInfo::IRON, 7, 128);
            break;
        case BLD_METALWORKS:
            addGlobalConsumption(CommodityInfo::IRON, 1);
            addGlobalConsumption(CommodityInfo::COAL, 1);
            addGlobalProduction(ConceptInfo::METALWORKS, 6);
            addAffection(CommodityInfo::COAL, 7, 128);
            addAffection(CommodityInfo::IRON, 7, 128);
            break;
        case BLD_IRONSMELTER:
            addGlobalConsumption(CommodityInfo::COAL, 1);
            addGlobalConsumption(CommodityInfo::IRONORE, 1);
            addLocalAndGlobalProduction(CommodityInfo::IRON, 0, 3);
            addAffection(CommodityInfo::COAL, 7, 128);
            addAffection(CommodityInfo::IRONORE, 7, 128);
            addAffection(ResourceInfo::COAL, 7, 128);
            addAffection(ResourceInfo ::IRONORE, 7, 128);
            break;
        case BLD_CHARBURNER:
            addGlobalConsumption(CommodityInfo::WOOD, 1);
            addGlobalConsumption(CommodityInfo::GRAIN, 1);
            addLocalAndGlobalProduction(CommodityInfo::COAL, 0, 1);
            addAffection(CommodityInfo::COAL, 7, 128);
            addAffection(CommodityInfo::IRONORE, 7, 128);

            addLocalAndGlobalConsumption(ResourceInfo::PLANTSPACE, 3, 18);
            addLocalProduction(ConceptInfo::FARM_SPACE, 4, 1);
            addAversion(ConceptInfo::TREE_SPACE, 1, 256);
            addAversion(ConceptInfo::FARM_SPACE, 2, 128);
            addAversion(ConceptInfo::HEADQUATERS, 0, -1);
            addAversion(ResourceInfo::BORDERLAND, 3, 8);
            break;
        case BLD_PIGFARM:
            addGlobalConsumption(CommodityInfo::GRAIN, 1);
            addGlobalConsumption(CommodityInfo::WATER, 1);
            addLocalAndGlobalProduction(CommodityInfo::PIG, 0, 1);
            break;
        case BLD_STOREHOUSE:
            addLocalAndGlobalProduction(ConceptInfo::STOREHOUSE, 12, 30);
            addAffection(ConceptInfo::MILITARY, 4, 1024);
            addAversion(ConceptInfo::STOREHOUSE, 1, -1);
            addAversion(ResourceInfo::BORDERLAND, 2, -1);
            break;
        case BLD_MILL:
            addGlobalConsumption(CommodityInfo::GRAIN, 1);
            addLocalAndGlobalProduction(CommodityInfo::FLOUR, 0, 1);
            break;
        case BLD_BAKERY:
            addGlobalConsumption(CommodityInfo::WATER, 1);
            addGlobalConsumption(CommodityInfo::FLOUR, 1);
            addGlobalProduction(CommodityInfo::BREAD, 1);
            addAffection(CommodityInfo::GRAIN, 5, 128);
            break;
        case BLD_SAWMILL:
            addGlobalConsumption(CommodityInfo::WOOD, 2);
            addGlobalProduction(CommodityInfo::BOARDS);
            break;
        case BLD_MINT:
            addGlobalConsumption(CommodityInfo::COAL, 1);
            addGlobalConsumption(CommodityInfo::GOLD, 1);
            addGlobalProduction(CommodityInfo::COINS, 1);
            break;
        case BLD_WELL:
            addGlobalConsumption(ResourceInfo::WATER, 1);
            addGlobalProduction(CommodityInfo::WATER, 2);
            addLocalConsumption(ResourceInfo::WATER, 4, 32);
            addAffection(ConceptInfo::STOREHOUSE, 4, 128);
            break;
        case BLD_SHIPYARD: /* TODO */ break;
        case BLD_FARM:
            addLocalAndGlobalConsumption(ResourceInfo::PLANTSPACE, 4, 8);
            addLocalProduction(ConceptInfo::FARM_SPACE, 3, 1);
            addAversion(ConceptInfo::TREE_SPACE, 4, 2048);
            addAversion(ConceptInfo::FARM_SPACE, 3, 1 << 8);
            addAversion(ConceptInfo::HEADQUATERS, 0, -1);
            addAversion(ResourceInfo::BORDERLAND, 4, -1);
            addAversion(ConceptInfo::MINE_SPACE, 3, -128);
            addAffection(ResourceInfo::PLANTSPACE, 4, 1 << 9);
            addLocalAndGlobalProduction(CommodityInfo::GRAIN, 0, 1);
            break;
        case BLD_DONKEYBREEDER:
            addGlobalConsumption(CommodityInfo::GRAIN, 1);
            addGlobalConsumption(CommodityInfo::WATER, 1);
            addGlobalProduction(ConceptInfo::DONKEY, 8);
            break;
        case BLD_HARBORBUILDING: break;
    }

    if(!BuildingProperties::IsMilitary(buildingType) && buildingType != BLD_QUARRY && buildingType != BLD_WOODCUTTER)
        addAversion(ResourceInfo::BORDERLAND, 3, 128);
}

Score BuildingInfo::getProximityScore(View& view, MapPoint bldgPt) const
{
    Score score(0);
    auto ConsumptionCallback = [&](const BuildingEffect& consumption) {
        int required = consumption.getAmount();
        if(consumption.isGlobal() && view.globalResources[consumption.getThing()] < required)
        {
            SAY("2 Require % % but globally we have only %", required, consumption.getThing(),
                view.globalResources[consumption.getThing()]);
            switch(consumption.getThing())
            {
                case CommodityInfo::GRAIN:
                case CommodityInfo::FOOD:
                case CommodityInfo::SHIELD:
                case CommodityInfo::SWORD:
                case CommodityInfo::COAL:
                case CommodityInfo::GOLD:
                case CommodityInfo::BEER:
                    if(view.globalResources[consumption.getThing()] + 1 >= required)
                    {
                        SAY("Ignored due ot importance", "");
                        break;
                    }
                default:;
                    // score = Score::invalid();
                    // return false;
            };
        }

        if(!consumption.isLocal())
            return true;

        int available = view.sumLocalTrackerValues(bldgPt, consumption.getRadius(), consumption.getThing(),
                                                   /* requireReachable */ ResourceInfo::isa(consumption.getThing())
                                                     && consumption.getThing() != ResourceInfo::BORDERLAND);
        if(required > available) //&& !BuildingProperties::IsMilitary(getBuildingType()))
        {
            SAY("Require % % but found only % in a % radius around %", consumption.getAmount(), consumption.getThing(), available,
                consumption.getRadius(), bldgPt);
            score = Score::invalid();
            return false;
        }

        score += available * Config::Score::RESOURCE_IN_RANGE_FACTOR;
        return true;
    };
    foreachConsumption(ConsumptionCallback);
    if(!score.isValid())
        return Score::invalid();

    auto PreferenceCallback = [&](const BuildingEffect& preference) {
        assert(preference.isLocal());
        int available = view.sumLocalTrackerValues(bldgPt, preference.getRadius(), preference.getThing());
        SAY("% has % with radius % (%), available % around %", getBuildingType(), preference.isAversion() ? "aversion" : "affection",
            preference.getRadius(), preference.getThing(), available, bldgPt);

        if(preference.getAmount() == -1)
        {
            SAY("% has % with radius %, available % around %", getBuildingType(), preference.isAversion() ? "aversion" : "affection",
                preference.getRadius(), available, bldgPt);
            if(preference.isAversion() && available > 0)
            {
                score = Score::invalid();
                return false;
            }
            if(preference.isAffection() && available <= 0)
            {
                score = Score::invalid();
                return false;
            }
        } else
        {
            score += preference.getAmount() * (preference.isAversion() ? -1 : 1) * available;
        }
        return true;
    };
    foreachPreference(PreferenceCallback);

    return score;
}

void BuildingInfo::updateResources(View& view, MapPoint bldgPt, bool& hasResources, int factor, bool globalOnly) const
{
    auto ProductionCallback = [&](const BuildingEffect& production) {
        if(production.isGlobal())
        {
            view.globalResources[production.getThing()] += production.getAmount() * factor;
        }

        if(!production.isLocal() || globalOnly)
            return true;
        SAY("[%] % produces % % locally %", bldgPt, getBuildingType(), production.getAmount(), production.getThing(),
            production.getRadius());

        view.addToLocalTracker(bldgPt, production.getRadius(), production.getThing(), production.getAmount() * factor);
        return true;
    };
    foreachProduction(ProductionCallback);
}
void BuildingInfo::checkResources(View& view, MapPoint bldgPt, bool& hasResources, int factor, bool globalOnly) const
{
    hasResources = true;
    auto ConsumptionCallback = [&](const BuildingEffect& consumption) {
        if(consumption.isGlobal())
        {
            view.globalResources[consumption.getThing()] -= consumption.getAmount() * factor;
        }

        if(!consumption.isLocal() || globalOnly)
            return true;

        std::vector<std::pair<MapPoint, int>> availablePoints;

        auto Check = [&](MapPoint pt, unsigned) {
            int available = view[pt].getValue(consumption.getThing());
            if(available > 0)
                availablePoints.push_back({pt, available});
            return false;
        };
        view.CheckPointsInRadius(bldgPt, consumption.getRadius(), Check, /* includePt */ false);

        bool change = true;
        int required = consumption.getAmount();
        while(change && required > 0)
        {
            change = false;
            for(auto& it : availablePoints)
            {
                if(it.second <= 0)
                    continue;
                --it.second;
                change = true;
                if(--required <= 0)
                    break;
            }
        }

        hasResources &= (required == 0);

        if(required)
        {
            if(consumption.isKind<ResourceInfo>())
                SAY("[%] % did miss % of the % %", bldgPt, getBuildingType(), required, consumption.getAmount(),
                    consumption.getKind<ResourceInfo>());
            if(consumption.isKind<CommodityInfo>())
                SAY("[%] % did miss % of the % %", bldgPt, getBuildingType(), required, consumption.getAmount(),
                    consumption.getKind<CommodityInfo>());
            if(consumption.isKind<ConceptInfo>())
                SAY("[%] % did miss % of the % %", bldgPt, getBuildingType(), required, consumption.getAmount(),
                    consumption.getKind<ConceptInfo>());
        }

        for(auto& it : availablePoints)
            view[it.first].setValue(consumption.getThing(), it.second);
        return true;
    };
    foreachConsumption(ConsumptionCallback);
}

bool BuildingInfo::canBePlacedAt(View& view, MapPoint bldgPt) const
{
    if(!canUseBq(view.getBuildingQuality(bldgPt), getBuildingQuality()))
        return false;

    if(BuildingProperties::IsMilitary(buildingType) && view[bldgPt].militaryInProximity)
        return false;

    return true;
}

Score BuildingInfo::getLocationScore(View& view, MapPoint bldgPt) const
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    if(!canBePlacedAt(view, bldgPt))
    {
        SAY("% cannot be placed @ %", getBuildingType(), bldgPt);
        return Score::invalid();
    }

    Score proximityScore = getProximityScore(view, bldgPt);
    SAY("% has  proximity score % @ %", getBuildingType(), proximityScore, bldgPt);
    if(!proximityScore.isValid())
    {
        SAY("% has invalid proximity score @ %", getBuildingType(), bldgPt);
        return Score::invalid();
    }

    if(BuildingProperties::IsMilitary(getBuildingType()))
    {
        //if(buildingType == BLD_BARRACKS && view.sumLocalTrackerValues(bldgPt, 4, ConceptInfo::ENEMY))
            //return proximityScore;

        std::set<MapPoint> militaryBuildingSites;
        for(BuildingType buildingType : {BLD_BARRACKS, BLD_GUARDHOUSE, BLD_WATCHTOWER, BLD_FORTRESS})
        {
            for(auto& it : view.buildings[buildingType])
            {
                const noBase* no = view.aiPlayer.gwb.GetNO(it.first);
                if(no->GetType() == NOP_BUILDINGSITE)
                    militaryBuildingSites.insert(it.first);
                else if(no->GetType() == NOP_BUILDING && reinterpret_cast<const nobMilitary*>(no)->IsNewBuilt())
                    militaryBuildingSites.insert(it.first);
            }
        }

        auto Check = [&](MapPoint pt, unsigned) {
            if(!view[pt].militaryInProximity && canUseBq(view.getBuildingQuality(pt), BQ_HOUSE) && view.potentialBuildingSites.count(pt))
                return true;
            if(militaryBuildingSites.count(pt))
                return true;
            if(view.buildings[BLD_BARRACKS].count(pt) || view.buildings[BLD_GUARDHOUSE].count(pt)
               || view.buildings[BLD_WATCHTOWER].count(pt) || view.buildings[BLD_FORTRESS].count(pt))
                return true;
            return false;
        };

        if(buildingType < BLD_WATCHTOWER && !view.CheckPointsInRadius(bldgPt, 7, Check, /* includePt */ true))
        {
            return proximityScore;
        }

        unsigned numEnemy = 0;
        bool newMines = false;
        unsigned militaryRadius = getMilitaryRadius(getBuildingQuality());
        for(const MapPoint& circlePt : view.GetPointsInRadiusWithCenter(bldgPt, militaryRadius))
        {
            if(militaryBuildingSites.count(circlePt) && view.CalcDistance(circlePt, bldgPt) <= militaryRadius)
            {
                SAY("Military building site % too close to %!", circlePt, bldgPt);
                return Score::invalid();
            }

            Location& location = view[circlePt];
            numEnemy += location.getValue(ConceptInfo::ENEMY);
            if(!location.getValue(ConceptInfo::OWNED) && location.getValue(ConceptInfo::MINE_SPACE))
            {
                if(getBuildingType() < BLD_WATCHTOWER)
                    proximityScore -= 512;
                proximityScore += Config::Score::BQ_MINE;
                newMines = true;
            }
            if(view.aiPlayer.gwb.IsWaterPoint(circlePt))
                proximityScore -= 32;
        }
        if(newMines && getBuildingType() < BLD_WATCHTOWER)
            proximityScore -= 2048;
        if(getBuildingType() == BLD_GUARDHOUSE)
            proximityScore -= 512 * numEnemy;
        if(getBuildingType() == BLD_WATCHTOWER)
            proximityScore -= 64 * numEnemy;
        if(getBuildingType() == BLD_FORTRESS)
            proximityScore += 32 * numEnemy;
        if(getBuildingType() == BLD_BARRACKS && numEnemy > 4)
            return proximityScore += 32 * numEnemy;

        MapPoint hqPt = view.aiPlayer.player.GetHQPos();
        for(const MapPoint& enemyHQPt : view.enemyHQs)
        {
            RoadRequest::Constraints hqConstraints{hqPt, enemyHQPt};
            int hqEnemyHQDistance = view.getConnectionLength(hqConstraints);
            if(hqEnemyHQDistance == -1)
                hqEnemyHQDistance = view.CalcDistance(hqPt, enemyHQPt);
            int requiredMinimalDistance = getRequiredMinimalDistanceToEnemy(getBuildingType());
            int requiredRemainingDistance = getRequiredRemainingDistanceToEnemy(getBuildingType());
            int requiredRemainingDistanceForBldg = (requiredRemainingDistance * hqEnemyHQDistance / 100);
            int requiredMinDistanceForBldg = (requiredMinimalDistance * hqEnemyHQDistance / 100);

            RoadRequest::Constraints constraints{bldgPt, enemyHQPt};
            // constraints.maximalCost = requiredMinDistanceForBldg;
            int bldgEnemyHQDistance = view.getConnectionLength(constraints);
            SAY("[%][%] ENEMY DISTANCE: hq-enemyHQ % | bldg-enemyHQ % || min: %, remaining: %", bldgPt, getBuildingType(),
                hqEnemyHQDistance, bldgEnemyHQDistance, requiredMinDistanceForBldg, requiredRemainingDistanceForBldg);
            // if(bldgEnemyHQDistance < 0)
            //{
            // if(requiredMinimalDistance)
            // return Score::invalid();
            ////proximityScore -= Config::Score::Penalties::SAFE_ZONE_MILITARY_RADIUS_FACTOR * getMilitaryRadius(getBuildingType());
            // continue;
            //}

            bldgEnemyHQDistance -= militaryRadius;
            if(!newMines && bldgEnemyHQDistance > requiredMinDistanceForBldg && requiredMinDistanceForBldg)
                return Score::invalid();
            if(!newMines && bldgEnemyHQDistance < requiredRemainingDistanceForBldg && requiredRemainingDistanceForBldg)
                return Score::invalid();
            // if (requiredMinDistanceForBldg)
            // break;
            SAY("[%][%] OK", bldgPt, getBuildingType());
        }

        // proximityScore -= militaryRadius * 326;

    } else if(!BuildingProperties::IsMine(buildingType))
    {
        unsigned numMilitary = 0;
        unsigned numBorder = 0;
        auto Check = [&](MapPoint pt, unsigned) {
            if(!view[pt].militaryInProximity && canUseBq(view.getBuildingQuality(pt), BQ_HUT) && view.potentialBuildingSites.count(pt))
                return (++numMilitary) > 3;
            if(view.buildings[BLD_BARRACKS].count(pt) || view.buildings[BLD_GUARDHOUSE].count(pt)
               || view.buildings[BLD_WATCHTOWER].count(pt) || view.buildings[BLD_FORTRESS].count(pt))
                return true;
            numBorder += view[pt].getLocalResource(ResourceInfo::BORDERLAND);
            return false;
        };
        if(!view.CheckPointsInRadius(bldgPt, 5, Check, /* includePt */ true))
        {
            if(numBorder && numMilitary)
            {
                SAY("Blocks military expansion %!", bldgPt);
                return Score::invalid();
            }
        }

        proximityScore -= Config::Score::Penalties::FARM_LAND_BUILDING * view.sumLocalTrackerValues(bldgPt, 2, ConceptInfo::FARM_SPACE);
        proximityScore -= Config::Score::Penalties::TREE_LAND * view.sumLocalTrackerValues(bldgPt, 2, ConceptInfo::TREE_SPACE);
    }

    return proximityScore;
}

static void updateMilitaryInProximity(View& view, MapPoint pt, BuildingType buildingType, int diff)
{
    if(BuildingProperties::IsMilitary(buildingType) || buildingType == BLD_HEADQUARTERS || buildingType == BLD_HARBORBUILDING)
    {
        for(const MapPoint& circlePt : view.GetPointsInRadiusWithCenter(pt, 4))
        {
            Location& circleLocation = view[circlePt];
            circleLocation.militaryInProximity += diff;
        }
    }
}

Building::Building(BuildingType buildingType, MapPoint pt) : buildingInfo(BuildingInfo::get(buildingType)), pt(pt) {}

void Building::init(View& view)
{
    // SAY("Init % @ %", getBuildingType(), pt);
    updateMilitaryInProximity(view, pt, getBuildingType(), 1);
    buildingInfo.updateResources(view, pt, hasResources, 1, false);
}

void Building::check(View& view)
{
    // SAY("Check % @ %", getBuildingType(), pt);
    buildingInfo.checkResources(view, pt, hasResources, 1, false);
}

void Building::destroy(View& view)
{
    // SAY("Destroy % @ %", getBuildingType(), pt);
    buildingInfo.checkResources(view, pt, hasResources, -1, true);
    buildingInfo.updateResources(view, pt, hasResources, -1, true);
    // BuildingType buildingType = getBuildingType();
    // updateMilitaryInProximity(view, pt, buildingType, -1);

#if 0
    auto ConsumptionCallback = [&](const BuildingEffect& consumption) {
        if(!consumption.isLocal())
            return true;
        if (!ResourceInfo::isa(consumption.getThing()) || consumption.getThing() == ResourceInfo::TREE)
          return true;
        for(const MapPoint& circlePt : view.GetPointsInRadiusWithCenter(pt, (consumption.getRadius() * 3) / 2))
            view[circlePt].setValue(consumption.getThing(), 0);
        return true;
    };
    buildingInfo.foreachConsumption(ConsumptionCallback);
#endif
}

constexpr unsigned short Config::Score::REQUIRED_REMAINING_DISTANCE_TO_ENEMY[];

std::array<BuildingInfo, NUM_BUILDING_TYPES> BuildingInfo::buildingInfos = {
  BuildingInfo(BLD_HEADQUARTERS), BuildingInfo(BLD_BARRACKS),       BuildingInfo(BLD_GUARDHOUSE),    BuildingInfo(BLD_NOTHING2),
  BuildingInfo(BLD_WATCHTOWER),   BuildingInfo(BLD_NOTHING3),       BuildingInfo(BLD_NOTHING4),      BuildingInfo(BLD_NOTHING5),
  BuildingInfo(BLD_NOTHING6),     BuildingInfo(BLD_FORTRESS),       BuildingInfo(BLD_GRANITEMINE),   BuildingInfo(BLD_COALMINE),
  BuildingInfo(BLD_IRONMINE),     BuildingInfo(BLD_GOLDMINE),       BuildingInfo(BLD_LOOKOUTTOWER),  BuildingInfo(BLD_NOTHING7),
  BuildingInfo(BLD_CATAPULT),     BuildingInfo(BLD_WOODCUTTER),     BuildingInfo(BLD_FISHERY),       BuildingInfo(BLD_QUARRY),
  BuildingInfo(BLD_FORESTER),     BuildingInfo(BLD_SLAUGHTERHOUSE), BuildingInfo(BLD_HUNTER),        BuildingInfo(BLD_BREWERY),
  BuildingInfo(BLD_ARMORY),       BuildingInfo(BLD_METALWORKS),     BuildingInfo(BLD_IRONSMELTER),   BuildingInfo(BLD_CHARBURNER),
  BuildingInfo(BLD_PIGFARM),      BuildingInfo(BLD_STOREHOUSE),     BuildingInfo(BLD_NOTHING9),      BuildingInfo(BLD_MILL),
  BuildingInfo(BLD_BAKERY),       BuildingInfo(BLD_SAWMILL),        BuildingInfo(BLD_MINT),          BuildingInfo(BLD_WELL),
  BuildingInfo(BLD_SHIPYARD),     BuildingInfo(BLD_FARM),           BuildingInfo(BLD_DONKEYBREEDER), BuildingInfo(BLD_HARBORBUILDING)};

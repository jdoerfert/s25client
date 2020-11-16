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

#include "View.h"
#include "Config.h"

#include "notifications/BuildingNote.h"
#include "notifications/NodeNote.h"
#include "notifications/PlayerNodeNote.h"
#include "notifications/ResourceNote.h"

#include "buildings/nobMilitary.h"
#include "buildings/nobUsual.h"
#include "figures/nofCarrier.h"
#include "pathfinding/PathConditionReachable.h"
#include "nodeObjs/noAnimal.h"
#include "nodeObjs/noFlag.h"
#include "nodeObjs/noGranite.h"
#include "nodeObjs/noSign.h"
#include "nodeObjs/noTree.h"
#include "gameTypes/MapTypes.h"
#include "gameData/BuildingProperties.h"

#include <algorithm>
#include <queue>
#include <unordered_map>

#define _TAG_ "[VIEW]"

using namespace AI;
using namespace jd;

View::View(AIPlayer& aiPlayer) : NodeMapBase<Location>(), aiPlayer(aiPlayer)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    const MapExtent& size = aiPlayer.gwb.GetSize();
    Resize(size);

    registerNotificationHandlers();

    update(/* full */ true);

    AIInterface& aii = aiPlayer.getAIInterface();
    std::map<MapPoint, unsigned> mountainScore;
    for(int y = 0; y < size.y; ++y)
    {
        for(int x = 0; x < size.x; ++x)
        {
            switch(aii.GetBuildingQualityAnyOwner(MapPoint(x, y)))
            {
                case BQ_MINE: ++mountainScore[MapPoint(x, y)]; break;
                case BQ_HARBOR:
                    // TODO
                    break;
                default: break;
            }
        }
    }
    for(auto& pair : mountainScore)
    {
        for(const MapPoint& circlePt : GetPointsInRadiusWithCenter(pair.first, 10))
        {
            auto it = mountainScore.find(circlePt);
            if(it != mountainScore.end())
                ++it->second;
        }
    }
    while(!mountainScore.empty())
    {
        auto pair = std::max_element(mountainScore.begin(), mountainScore.end(),
                                     [](auto& lhs, auto& rhs) { return lhs.second < rhs.second; });
        mountains.push_back(*pair);
        for(const MapPoint& circlePt : GetPointsInRadiusWithCenter(pair->first, 30))
            mountainScore.erase(circlePt);
        if(mountains.size() > 10)
            break;
    }

    MapPoint hqPt = aiPlayer.player.GetHQPos();
    for(auto& it : mountains)
    {
        SAY("MOUNTAIN @ %, size %", it.first, it.second);
        precomputeConnections(it.first);
        SAY("MOUNTAIN @ %, size %, distance %", it.first, it.second, getConnectionLength({bldg2flag(hqPt), it.first}));
    }

    unsigned playerId = aiPlayer.GetPlayerId();
    unsigned numPlayers = aiPlayer.gwb.GetNumPlayers();
    for(unsigned u = 0; u < numPlayers; ++u)
    {
        if(u == playerId)
            continue;
        const GamePlayer& otherPlayer = aiPlayer.gwb.GetPlayer(u);
        if(otherPlayer.IsAlly(playerId))
            friendHQs.push_back(otherPlayer.GetHQPos());
        else
        {
            enemies.push_back(u);
            enemyHQs.push_back(otherPlayer.GetHQPos());
            SAY("ENEMY % @ %", u, otherPlayer.GetHQPos());
            precomputeConnections(otherPlayer.GetHQPos());
        }
    }
    enemyMilitaryBuildings.resize(numPlayers);

    for(const MapPoint& pt : friendHQs)
        SAY("Friend @ %, distance %", pt, CalcDistance(pt, hqPt))
    for(const MapPoint& pt : enemyHQs)
        SAY("Enemy @ %, distance %", pt, CalcDistance(pt, hqPt))
    for(const MapPoint& pt : enemyHQs)
    {
        int dis = getConnectionLength({hqPt, pt});
        SAY("Enemy @ %, distance %", pt, dis)
    }
    for(const MapPoint& pt : enemyHQs)
    {
        int dis = getConnectionLength({hqPt, pt});
        SAY("Enemy @ %, distance %", pt, dis)
        assert(dis != -1);
    }
}

void View::registerNotificationHandlers()
{
    unsigned char playerId = aiPlayer.GetPlayerId();
    NotificationManager& notifications = aiPlayer.gwb.GetNotifications();
    notificationHandles.emplace_back(notifications.subscribe<BuildingNote>([=](const BuildingNote& note) {
        if(note.player != playerId)
            return;
        SAY("[%][NOTE] Building % type %", note.pos, note.bld, note.type);
        switch(note.type)
        {
            case BuildingNote::Constructed: break;
            case BuildingNote::Destroyed: registerLostBuilding(note.pos, note.bld); break;
            case BuildingNote::Captured: requiresFullUpdate = true; break;
            case BuildingNote::Lost: break;
            case BuildingNote::NoRessources: registerOutOfResourceBuilding(note.pos, note.bld); break;
            case BuildingNote::LuaOrder: break;
            case BuildingNote::LostLand: requiresFullUpdate = true; break;
        };
        // for(const MapPoint& circlePt : GetPointsInRadiusWithCenter(note.pos, 3))
        // updateLocation(circlePt);
    }));

    notificationHandles.emplace_back(notifications.subscribe<NodeNote>([=](const NodeNote& note) {
        if(note.type == NodeNote::Altitude)
            return;
        // SAY("[%][NOTE] Node", note.pos);
        // updateLocation(note.pos);
    }));

    notificationHandles.emplace_back(notifications.subscribe<PlayerNodeNote>([=](const PlayerNodeNote& note) {
        if(note.player != playerId)
            return;
        if(note.type == PlayerNodeNote::Visibility)
            requiresFullUpdate = true;
    }));

    notificationHandles.emplace_back(notifications.subscribe<RoadNote>([=](const RoadNote& note) {
        if(note.player != playerId)
            return;
        SAY("[%] Road node %: % ", note.pos, note.type, construction::Route(note.route));

        std::set<MapPoint> affectedPoints;
        MapPoint curPt = note.pos;
        for(unsigned u = 0, e = note.route.size(); u < e; ++u)
        {
            curPt = GetNeighbour(curPt, note.route[u]);
            for(const MapPoint& circlePt : GetPointsInRadiusWithCenter(curPt, 3))
                affectedPoints.insert(circlePt);
        }
        // for(const MapPoint& pt : affectedPoints)
        // updateLocation(pt);
    }));
}

void View::clearMapInfo(bool full)
{
    flagPts.clear();
    singletonFlagPts.clear();
    enemyLand.clear();
    potentialBuildingSites.clear();

    for(unsigned u = 0; u < NUM_BUILDING_TYPES; ++u)
    {
        for(auto& it : buildings[u])
            it.second.destroy(*this);
        buildings[u].clear();
    }

    if(full)
    {
        globalResources.clear();
        storehouseResources.clear();
        assert(!globalResources.any());
    }

    clearCaches(full);
}

void View::clearCaches(bool full)
{
    cachedConnectionScores.clear();
    cachedBuildingScores.clear();
    if(full)
        cachedLocationScores.clear();
}

void View::update(bool full)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    full |= requiresFullUpdate;
    requiresFullUpdate = false;
    numFullUpdate += full;

    clearMapInfo(full);

    const MapExtent frameMin = full ? MapExtent{0, 0} : visibleframeMin;
    const MapExtent frameMax = full ? GetSize() : visibleFrameMax;
    // SAY("Update view %: % - %", full ? "[FULL]" : "[Visible]", frameMin, frameMax);

    for(unsigned y = frameMin.y; y < frameMax.y; ++y)
        for(unsigned x = frameMin.x; x < frameMax.x; ++x)
        {
            MapPoint pt(x, y);
            updateLocation(pt, full);
        }

    for(unsigned u = 0; u < NUM_BUILDING_TYPES; ++u)
    {
        for(auto& it : buildings[u])
        {
            if(full)
            {
                --globalResources[ConceptInfo::STOREHOUSE];
                //--globalResources[ConceptInfo::METALWORKS];
            }
            it.second.init(*this);

            if((*this)[it.first].getValue(ConceptInfo::FARM_SPACE))
                potentialBuildingSites.erase(it.first);
            if((*this)[it.first].getValue(ConceptInfo::TREE_SPACE))
                potentialBuildingSites.erase(it.first);
        }
    }
    std::set<MapPoint> ignoredBuildings;
    for(unsigned u = 0; u < NUM_BUILDING_TYPES; ++u)
    {
        ignoredBuildings.clear();
        if(BuildingProperties::IsMine(BuildingType(u)) || u == BLD_MINT || u == BLD_BREWERY || u == BLD_METALWORKS)
        {
            const std::list<nobUsual*>& nobBuildings = aiPlayer.getAIInterface().GetBuildings(BuildingType(u));
            for(auto* nobBuilding : nobBuildings)
                if(nobBuilding->IsProductionDisabled())
                    ignoredBuildings.insert(nobBuilding->GetPos());
        }
        for(auto& it : buildings[u])
        {
            if(ignoredBuildings.count(it.first))
                continue;
            it.second.check(*this);
        }
    }

    for(unsigned enemyId : enemies)
    {
        enemyMilitaryBuildings[enemyId].clear();
        for(nobMilitary* enemyMilitaryBuilding :
            aiPlayer.gwb.GetPlayer(enemyId).GetBuildingRegister().GetMilitaryBuildings())
        {
            if((*this)[enemyMilitaryBuilding->GetPos()].visible)
                enemyMilitaryBuildings[enemyId].insert(enemyMilitaryBuilding);
        }
        // discard_if(enemyMilitaryBuildings[enemyId],
        //[&](auto& it) { return !aii.IsBuildingOnNode(it.first, it.second->GetBuildingType()); });
    }

#if 0
    AIInterface& aii = aiPlayer.getAIInterface();
    auto GetInBounds = [&](MapPoint pt, const MapExtent& min, const MapExtent& max) {
        return MapPoint(std::max(std::min((int)pt.x, (int)max.x - 1), (int)min.x),
                        std::max(std::min((int)pt.y, (int)max.y - 1), (int)min.y));
    };

    auto GetInFrame = [&](MapPoint pt) { return GetInBounds(pt, frameMin, frameMax); };
    auto GetInMap = [&](MapPoint pt) { return GetInBounds(pt, MapExtent{0, 0}, GetSize()); };

    // Guess into the fog of war and update the visible frame (for full updates).
    const int radius = Config::View::FOG_ESTIMATE_RADIUS;
    for(int y = frameMin.y - radius; y < frameMax.y + radius; ++y)
    {
        for(int x = frameMin.x - radius; x < frameMax.x + radius; ++x)
        {
            MapPoint pt(x, y);
            if(GetInMap(pt) != pt)
                continue;

            Location& location = (*this)[pt];
            if(location.visible)
                continue;

            bool anyVisible = false;
            for(int diffY : {-radius, 0, radius})
                for(int diffX : {-radius, 0, radius})
                    anyVisible |= (*this)[GetInFrame(MapPoint(x + diffX, y + diffY))].visible;
            if(!anyVisible)
                continue;

            Tracker resources;
            unsigned numVisiblePts = 0;
            for(MapPoint circlePt : GetPointsInRadiusWithCenter(pt, radius))
            {
                Location& circleLocation = (*this)[circlePt];
                if(!circleLocation.visible)
                    continue;

                ++numVisiblePts;
                resources += circleLocation.getLocalResources();
            }

            for(unsigned u = 0; u < Tracker::size(); ++u)
                if(resources.values[u] > int((Config::View::FOG_ESTIMATE_RATIO * numVisiblePts) / 100))
                    location.addValue(u, 1);
        }
    }
#endif
}

void View::updateLocation(MapPoint pt, bool globals)
{
    AIInterface& aii = aiPlayer.getAIInterface();
    Location& location = (*this)[pt];
    location.pt = pt;
    location.visible = aii.IsVisible(pt);

    BuildingQuality buildingQuality = aii.GetBuildingQualityAnyOwner(pt);
    if(buildingQuality == BQ_MINE)
        setLocalTracker(pt, 1, ConceptInfo::MINE_SPACE, 1);

    if(!location.visible && buildingQuality != BQ_NOTHING && !aiPlayer.gwb.IsWaterPoint(pt))
    {
        location.setValue(ConceptInfo::HIDDEN, 1);
        if(globals)
            ++globalResources[ConceptInfo::HIDDEN];
        return;
    }

    if(!location.visible)
        return;

    visibleframeMin.x = std::min(visibleframeMin.x, (unsigned short)pt.x);
    visibleframeMin.y = std::min(visibleframeMin.y, (unsigned short)pt.y);
    visibleFrameMax.x = std::max(visibleFrameMax.x, (unsigned short)pt.x);
    visibleFrameMax.y = std::max(visibleFrameMax.y, (unsigned short)pt.y);

    updateResource(pt, globals);

    if(!location.getValue(ConceptInfo::OWNED))
        return;

    if(aii.IsObjectTypeOnNode(pt, NodalObjectType::NOP_FLAG))
        flagPts.insert(pt);

    unsigned numConnection = getConnectedRoads(pt);
    MapPoint bldgPt = flag2bldg(pt);
    if(numConnection == 0
       || (numConnection == 1
           && (aii.IsObjectTypeOnNode(bldgPt, NodalObjectType::NOP_BUILDINGSITE)
               || (aii.IsObjectTypeOnNode(bldgPt, NodalObjectType::NOP_BUILDING)
                   && !aii.IsBuildingOnNode(bldgPt, BLD_HEADQUARTERS)
                   && !aii.IsBuildingOnNode(bldgPt, BLD_STOREHOUSE)))))
        singletonFlagPts.insert(pt);
}

void View::updateResource(MapPoint pt, bool globals)
{
    potentialBuildingSites.erase(pt);

    Location& location = (*this)[pt];

    const MapNode& mapNode = aiPlayer.gwb.GetNode(pt);
    bool isVital = aiPlayer.gwb.IsOfTerrain(pt, [](const TerrainDesc& desc) { return desc.IsVital(); });

    unsigned water = location.getValue(ResourceInfo::WATER);
    for(unsigned u = ResourceInfo::FIRST; u < ConceptInfo::LAST; ++u)
        location.setValue(u, 0);
    location.setValue(ResourceInfo::WATER, water);

    BuildingQuality buildingQuality = mapNode.bq;
    NodalObjectType nodalObjectType = mapNode.obj ? mapNode.obj->GetType() : NOP_NOTHING;
    if(!aiPlayer.gwb.IsOnRoad(pt))
    {
        if(nodalObjectType == NOP_NOTHING || nodalObjectType == NOP_ENVIRONMENT)
        {
            if(buildingQuality > BuildingQuality::BQ_FLAG)
                potentialBuildingSites.insert(location.pt);

            if(buildingQuality > BuildingQuality::BQ_HUT && isVital)
            {
                location.setLocalResource(ResourceInfo::PLANTSPACE, 1);
                if(globals)
                    globalResources[ResourceInfo::PLANTSPACE] += 1;
            }
        } else if(nodalObjectType == NOP_FIRE)
        {
            if(globals)
                ++globalResources[ConceptInfo::BURNSITE];
        }
    }

    bool isOwn = aiPlayer.gwb.IsWaterPoint(pt) || (mapNode.owner == (aiPlayer.playerId + 1));
    location.setValue(ConceptInfo::OWNED, isOwn);
    location.setValue(ConceptInfo::UNOWNED, !isOwn);
    if(isOwn)
    {
        if(globals)
            ++globalResources[ConceptInfo::OWNED];
        if(nodalObjectType == NOP_BUILDING || nodalObjectType == NOP_BUILDINGSITE)
        {
            BuildingType buildingType = static_cast<const noBaseBuilding*>(mapNode.obj)->GetBuildingType();
            if(!buildings[buildingType].count(pt))
                registerNewBuilding(pt, buildingType);
        }
    }
    if(!isOwn)
        addToLocalTracker(pt, 10, ConceptInfo::NEAR_UNOWNED, 1);

    if(!isOwn && mapNode.owner && aiPlayer.getAIInterface().IsPlayerAttackable(mapNode.owner - 1))
    {
        location.setValue(ConceptInfo::ENEMY, 1);
        enemyLand.push_back(pt);
        if(globals)
            globalResources[ConceptInfo::ENEMY] += 1;
    }

    if(mapNode.boundary_stones[BorderStonePos::OnPoint] == (aiPlayer.playerId + 1))
    {
        addToLocalTracker(pt, 4, ConceptInfo::NEAR_BORDER, 1);
        location.setLocalResource(ResourceInfo::BORDERLAND, 1);
        if(globals)
            globalResources[ResourceInfo::BORDERLAND] += 1;
    }

    Resource subres = mapNode.resources;
    if(subres.has(Resource::Fish))
    {
        std::vector<MapPoint> walkablePoints;

        for(const MapPoint& circlePt : GetPointsInRadiusWithCenter(pt, 2))
        {
            if(aiPlayer.gwb.IsOfTerrain(circlePt, [](const TerrainDesc& desc) { return desc.Is(ETerrain::Walkable); }))
                walkablePoints.push_back(circlePt);
        }
        int amount = subres.getAmount() * 2;
        auto CheckPt = [&](const MapPoint& circlePt, unsigned) {
            if(!canUseBq(aiPlayer.gwb.GetNode(circlePt).bq, BQ_HUT))
                return false;
            addToLocalTracker(circlePt, 0, ResourceInfo::FISH, 1);
            return --amount <= 0;
        };
        int lastAmount = amount + 1;
        while(amount > 0 && lastAmount != amount)
        {
            lastAmount = amount;
            for(const MapPoint& walkablePoint : walkablePoints)
                if(amount > 0)
                    CheckPointsInRadius(walkablePoint, 6, CheckPt, true);
        }
        if(globals && amount <= 0)
        {
            globalResources[ResourceInfo::FISH] += subres.getAmount();
        }
    } else if(location.visitedByGeologist)
    {
        ResourceInfo::Kind kind = ResourceInfo::LAST;
        switch(subres.getType())
        {
            case Resource::Iron: kind = ResourceInfo::IRONORE; break;
            case Resource::Gold: kind = ResourceInfo::GOLD; break;
            case Resource::Coal: kind = ResourceInfo::COAL; break;
            case Resource::Granite: kind = ResourceInfo::GRANITE; break;
            case Resource::Water: kind = ResourceInfo::WATER; break;
            case Resource::Fish: break;
            case Resource::Nothing: break;
        };
        if(kind != ResourceInfo::LAST)
        {
            location.setLocalResource(kind, subres.getAmount());
            if(globals)
                globalResources[kind] += subres.getAmount();
        }
    }

    if(nodalObjectType == NOP_GRANITE)
    {
        location.setLocalResource(ResourceInfo::STONE, aiPlayer.gwb.GetSpecObj<noGranite>(pt)->GetSize());
        bool AnyClose = false;
        for(const auto& it : buildings[BLD_QUARRY])
        {
            AnyClose = CalcDistance(pt, it.first) < 9;
            if(AnyClose)
                break;
        }
        if(!AnyClose)
            if(globals)
                globalResources[ResourceInfo::STONE] += 1;
    }

    if(nodalObjectType == NOP_TREE && static_cast<const noTree*>(mapNode.obj)->ProducesWood())
    {
        location.setValue(ResourceInfo::TREE, 1);
        bool AnyClose = false;
        for(const auto& it : buildings[BLD_WOODCUTTER])
        {
            AnyClose = CalcDistance(pt, it.first) < 6;
            if(AnyClose)
                break;
        }
        // SAY("[%] Tree found, woodcutter close: %", pt, AnyClose);
        if(!AnyClose)
            if(globals)
                globalResources[ResourceInfo::TREE] += 1;
    }

    for(const noBase* fig : aiPlayer.gwb.GetFigures(pt))
        if(fig->GetType() == NOP_ANIMAL)
            if(static_cast<const noAnimal*>(fig)->CanHunted())
            {
                location.setValue(ResourceInfo::ANIMAL, 1);
                if(globals)
                    globalResources[ResourceInfo::ANIMAL] += 2;
            }

    // if(location.getLocalResources().any())
    // SAY("[%][UPDATE] local resources: %", pt, location.getLocalResources());
}

BuildingQuality View::getBuildingQuality(MapPoint pt)
{
    return aiPlayer.gwb.GetBQ(pt, aiPlayer.GetPlayerId());
}

bool View::wasVisitedByGeologist(MapPoint pt)
{
    Location& location = (*this)[pt];
    if(location.visitedByGeologist)
        return true;

    const auto* sign = aiPlayer.gwb.GetSpecObj<noSign>(pt);
    if(!sign)
        return false;

    return location.visitedByGeologist = true;
}

bool View::isRoadOrFlag(MapPoint pt)
{
    return aiPlayer.gwb.IsOnRoad(pt) || flagPts.count(pt);
}

int View::sumLocalTrackerValues(MapPoint pt, int radius, unsigned idx, bool requireReachable)
{
    if(radius == 0)
        return (*this)[pt].getValue(idx);

    int sum = 0;
    for(const MapPoint& circlePt : GetPointsInRadiusWithCenter(pt, radius))
    {
        if(!(*this)[circlePt].getValue(idx))
            continue;
        // if(!requireReachable || pt == circlePt || aiPlayer.gwb.FindHumanPath(pt, circlePt, 20) != 0xFF)
        if(!requireReachable || pt == circlePt || getConnectionLength({pt, circlePt}) != -1)
            sum += (*this)[circlePt].getValue(idx);
    }
    return sum;
}
void View::setLocalTracker(MapPoint pt, int radius, unsigned idx, int value)
{
    if(radius == 0)
    {
        (*this)[pt].getValue(idx) = value;
        return;
    }

    for(MapPoint circlePt : GetPointsInRadiusWithCenter(pt, radius))
        (*this)[circlePt].getValue(idx) = value;
}
void View::addToLocalTracker(MapPoint pt, int radius, unsigned idx, int difference)
{
    if(radius == 0)
    {
        (*this)[pt].getValue(idx) += difference;
        return;
    }

    for(MapPoint circlePt : GetPointsInRadiusWithCenter(pt, radius))
        (*this)[circlePt].getValue(idx) += difference;
}
int View::getGlobalCommodity(CommodityInfo::Kind kind)
{
    return globalResources[kind];
}

void View::registerLostBuilding(MapPoint pt, BuildingType buildingType)
{
    SAY("[%][LOST %] TODO", pt, buildingType);

    auto it = buildings[buildingType].find(pt);
    if(it == buildings[buildingType].end())
    {
        SAY("DESTROYED BUILDING WAS NOT FOUND (%:%)!", pt, buildingType);
        // assert(0);
        return;
    }
    // Building& building = it->second;
    // assert(building.getBuildingType() == buildingType);

    // building.destroy(*this);
    // buildings[buildingType].erase(it);
    requiresFullUpdate = true;
}

void View::registerNewBuilding(MapPoint bldgPt, BuildingType buildingType)
{
    SAY("[%][NEW %] YAY!", bldgPt, buildingType);

    for(auto& it : buildings[buildingType])
        SAY("--- % %", it.first, it.second.getBuildingType());

    auto pair = buildings[buildingType].insert({bldgPt, Building(buildingType, bldgPt)});
    assert(pair.second);
    // pair.first->second.init(*this);

    potentialBuildingSites.erase(bldgPt);
}

void View::registerOutOfResourceBuilding(MapPoint bldgPt, BuildingType buildingType)
{
    SAY("registerOutOfResourceBuilding: % %", bldgPt, buildingType);

    auto ConsumptionCallback = [&](const BuildingEffect& consumption) {
        if(!consumption.isLocal())
            return true;
        for(const MapPoint& circlePt : GetPointsInRadiusWithCenter(bldgPt, consumption.getRadius()))
        {
            Location& circleLocation = (*this)[circlePt];
            circleLocation.setValue(consumption.getThing(), 0);
        }

        return true;
    };

    BuildingInfo buildingInfo = BuildingInfo::get(buildingType);
    buildingInfo.foreachConsumption(ConsumptionCallback);

    auto ConsumptionRequest = [&](const BuildingEffect& consumption) {
        if(!consumption.isGlobal())
            return true;
        --globalResources[consumption.getThing()];
        return true;
    };
    buildingInfo.foreachConsumption(ConsumptionRequest);
}

bool View::canBuildFlag(MapPoint pt, bool orIsFlag)
{
    BuildingQuality buildingQuality = getBuildingQuality(pt);
    if((orIsFlag && flagPts.count(pt)) || canUseBq(buildingQuality, BQ_FLAG))
        return true;
    if(buildingQuality != BQ_MINE)
        return false;
    for(const MapPoint& circlePt : GetPointsInRadiusWithCenter(pt, 2))
    {
        if(flagPts.count(circlePt))
            return false;
    }
    return true;
}

Score View::getCloseByPenalty(MapPoint pt, BuildingQuality toBeBuild, MapPoint prevPt)
{
    Score penalty(0);

    auto BQScore = [](BuildingQuality buildingQuality) {
        switch(buildingQuality)
        {
            case BQ_NOTHING: return Config::Score::BQ_NOTHING;
            case BQ_FLAG: return Config::Score::BQ_FLAG;
            case BQ_HUT: return Config::Score::BQ_HUT;
            case BQ_HOUSE: return Config::Score::BQ_HOUSE;
            case BQ_CASTLE: return Config::Score::BQ_CASTLE;
            case BQ_MINE: return Config::Score::BQ_MINE;
            case BQ_HARBOR: return Config::Score::BQ_HARBOR;
        };
        __builtin_unreachable();
    };

    auto Diff = [&](BuildingQuality oldBQ, BuildingQuality newBQ) { penalty += BQScore(oldBQ) - BQScore(newBQ); };
    auto Reduction = [&](MapPoint rPt, BuildingQuality newBQ) {
        if(toBeBuild == BQ_NOTHING && (rPt == prevPt || landIsUsed(rPt)))
            return;
        // if (toBeBuild == BQ_FLAG && rPt != pt && prevPt.isValid() && CalcDistance(rPt, prevPt) < 2)
        // return;
        Diff(getBuildingQuality(rPt), newBQ);
    };
    auto CastleReduction = [&](MapPoint castlePt, BuildingQuality newBQ) {
        BuildingQuality castlePtBQ = getBuildingQuality(castlePt);
        if(castlePtBQ == BQ_CASTLE)
            Reduction(castlePt, newBQ);
    };

    Reduction(pt, BQ_NOTHING);

    penalty += Config::Score::Penalties::FARM_LAND_ROAD * sumLocalTrackerValues(pt, 0, ConceptInfo::FARM_SPACE);
    penalty += Config::Score::Penalties::TREE_LAND * sumLocalTrackerValues(pt, 0, ConceptInfo::TREE_SPACE);

    MapPoint wPt = GetNeighbour(pt, Direction::WEST);
    MapPoint ePt = GetNeighbour(pt, Direction::EAST);
    MapPoint nePt = GetNeighbour(pt, Direction::NORTHEAST);
    MapPoint nwPt = GetNeighbour(pt, Direction::NORTHWEST);
    MapPoint swPt = GetNeighbour(pt, Direction::SOUTHWEST);
    MapPoint sePt = GetNeighbour(pt, Direction::SOUTHEAST);
    switch(toBeBuild)
    {
        case BQ_NOTHING:
            CastleReduction(wPt, BQ_HOUSE);
            CastleReduction(sePt, BQ_HOUSE);
            CastleReduction(swPt, BQ_HOUSE);
            break;
        case BQ_FLAG:
            CastleReduction(ePt, BQ_HOUSE);
            CastleReduction(sePt, BQ_HOUSE);
            CastleReduction(swPt, BQ_HOUSE);
            Reduction(wPt, BQ_NOTHING);
            Reduction(nePt, BQ_NOTHING);
            Reduction(GetNeighbour(nwPt, Direction::WEST), BQ_FLAG);
            Reduction(GetNeighbour(nwPt, Direction::NORTHWEST), BQ_FLAG);
            Reduction(GetNeighbour(nwPt, Direction::NORTHEAST), BQ_FLAG);
            break;
        case BQ_HUT:
        case BQ_HOUSE:
        case BQ_CASTLE:
            penalty += getCloseByPenalty(bldg2flag(pt), BQ_FLAG, prevPt);
            CastleReduction(GetNeighbour(nwPt, Direction::WEST), toBeBuild == BQ_CASTLE ? BQ_FLAG : BQ_HOUSE);
            CastleReduction(GetNeighbour(nwPt, Direction::NORTHWEST), toBeBuild == BQ_CASTLE ? BQ_FLAG : BQ_HOUSE);
            CastleReduction(GetNeighbour(nwPt, Direction::NORTHEAST), toBeBuild == BQ_CASTLE ? BQ_FLAG : BQ_HOUSE);
            CastleReduction(GetNeighbour(nePt, Direction::EAST), BQ_HOUSE);
            CastleReduction(GetNeighbour(nePt, Direction::NORTHEAST), BQ_HOUSE);
            CastleReduction(GetNeighbour(ePt, Direction::EAST), BQ_HOUSE);
            CastleReduction(GetNeighbour(swPt, Direction::WEST), BQ_HOUSE);
            CastleReduction(GetNeighbour(swPt, Direction::SOUTHWEST), BQ_HOUSE);
            CastleReduction(GetNeighbour(wPt, Direction::WEST), BQ_HOUSE);
            break;
        case BQ_MINE: penalty += getCloseByPenalty(bldg2flag(pt), BQ_FLAG, prevPt);
        case BQ_HARBOR: break;
    };

    for(const MapPoint neighbourPt : {wPt, ePt, nePt, nwPt, swPt, sePt})
    {
        if((*this)[neighbourPt].getLocalResource(ResourceInfo::STONE))
            penalty += Config::Score::Penalties::STONE_ON_NEIGHBOUR;
        else if((*this)[neighbourPt].getLocalResource(ResourceInfo::TREE))
            penalty += Config::Score::Penalties::TREE_ON_NEIGHBOUR;
        else if((*this)[neighbourPt].getLocalResource(ResourceInfo::BORDERLAND))
            penalty += Config::Score::Penalties::BORDER_ON_NEIGHBOUR;
    }

    // SAY("closeby penalty for % @ % is %", toBeBuild, pt, penalty);
    return penalty;
}

Score View::getConnectionPenalty(const RoadRequest::Constraints& constraints)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    Score& score = cachedConnectionScores[constraints.fromPt];
    if(score.isValid() && score.getValue() == 0)
    {
        std::vector<Direction> directions;
        MapPoint connectionPt = findConnectionPoint(constraints, directions, score);
        if(!connectionPt.isValid())
            return Score::invalid();
    }
    return score;
}

Score View::getBuildingScore(MapPoint bldgPt, const BuildingInfo& buildingInfo)
{
    if(!bldgPt.isValid())
        return Score::invalid();

    RAIISayTimer RST(__PRETTY_FUNCTION__);
    Score& score = cachedBuildingScores[bldgPt][buildingInfo.getBuildingType()];
    if(score.isValid() && score.getValue() == 0)
    {
        Score& locationScore = cachedLocationScores[bldgPt];
        if(score.isValid() && score.getValue() == 0)
        {
            locationScore = buildingInfo.getLocationScore(*this, bldgPt);
            if(locationScore.isValid() && locationScore.getValue() == 0)
                locationScore = 1;
        }

        if(!locationScore.isValid())
            return score.invalidate();

        score = Score(0);
        score += locationScore;

        MapPoint flagPt = bldg2flag(bldgPt);
        RoadRequest::Constraints constraints{flagPt};
        constraints.excludeFromBldgPt = true;
        Score connectionPenalty = getConnectionPenalty(constraints);
        score -= connectionPenalty;

        if(score.isValid() && score.getValue() == 0)
            score = 1;
        SAY("BUILDING SCORE %: % = % - %", bldgPt, score, locationScore, connectionPenalty);
    }
    return score;
}

MapPoint View::findBuildingPosition(const BuildingRequest::Constraints& constraints, Score& score)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    if(potentialBuildingSites.empty())
        return MapPoint();

    const BuildingInfo& buildingInfo = BuildingInfo::get(constraints.buildingType);

    auto Cmp = [&](MapPoint ptA, MapPoint ptB) {
        Score scoreA = getBuildingScore(ptA, buildingInfo);
        if(!scoreA.isValid())
            return false;
        Score scoreB = getBuildingScore(ptB, buildingInfo);
        if(!scoreB.isValid())
            return true;
        if(constraints.closeToPt.isValid())
        {
            int distanceA = getConnectionLength({ptA, constraints.closeToPt});
            if(distanceA < 0 || distanceA > constraints.closeToDistance)
                return false;
            int distanceB = getConnectionLength({ptB, constraints.closeToPt});
            if(distanceB < 0 || distanceB > constraints.closeToDistance)
                return true;
            scoreA -= Config::Score::Penalties::CLOSE_TO_POINT_DISTANCE * std::max(0, distanceA - distanceB);
            scoreB -= Config::Score::Penalties::CLOSE_TO_POINT_DISTANCE * std::max(0, distanceB - distanceA);
        }
        return scoreA > scoreB;
    };

    MapPoint pt;
    if(constraints.closeToPt.isValid() && constraints.closeToDistance != -1)
    {
        const auto& points = GetPointsInRadiusWithCenter(constraints.closeToPt, constraints.closeToDistance);
        auto VCmp = [&](MapPoint ptA, MapPoint ptB) {
            bool pBSA = potentialBuildingSites.count(ptA);
            bool pBSB = potentialBuildingSites.count(ptB);
            if(!pBSB)
                return true;
            if(!pBSA)
                return false;
            return Cmp(ptA, ptB);
        };
        auto it = std::min_element(points.begin(), points.end(), VCmp);
        pt = *it;
    } else
    {
        auto it = std::min_element(potentialBuildingSites.begin(), potentialBuildingSites.end(), Cmp);
        pt = *it;
    }

    if(constraints.closeToPt.isValid())
    {
        unsigned distance = CalcDistance(pt, constraints.closeToPt);
        if(distance > constraints.closeToDistance)
        {
            SAY("Minimal point % invalid because of closeTo distance % from %", pt, constraints.closeToDistance,
                constraints.closeToPt);
            return MapPoint();
        }
    }

    score = getBuildingScore(pt, buildingInfo);
    SAY("[%] Potential building site: % with score %", pt, (*this)[pt].getLocalResources(), score);

    if(score.isValid())
        return pt;

    auto ConsumptionCallback = [&](const BuildingEffect& consumption) {
        if(!consumption.isLocal())
            return true;
        SAY("Decrement % from the global resource table because % couldn't be build", consumption.getThing(),
            buildingInfo.getBuildingType());
        globalResources[consumption.getThing()] = globalResources[consumption.getThing()] / 2;
        return true;
    };
    buildingInfo.foreachConsumption(ConsumptionCallback);
    return MapPoint();
}

void View::averageSurfaceResources(MapPoint)
{
#if 0
    Location& location = (*this)[pt];

    ProximityInfo::Kind kind = ProximityInfo::NOTHING;
    if(location.localResources[ProximityInfo::TREE] > 0)
        kind = ProximityInfo::TREE;
    else if(location.localResources[ProximityInfo::STONE] > 0)
        kind = ProximityInfo::STONE;
    else
        return;

    unsigned numFound = 0, pointsInCircle = 0;
    for(MapPoint  circlePt : GetPointsInRadiusWithCenter(pt, 2))
    {
        Location& circleLocation = (*this)[circlePt];
        numFound += circleLocation.localResources[kind] > 0;
        ++pointsInCircle;
    }

    unsigned fraction = kind == ProximityInfo::STONE ? 3 : 7;
    if(numFound > ((fraction * pointsInCircle) / 10))
    {
        ++location.localResources[kind];
        SAY("[%][UPDATE] surface resource % due to average rule, new value %", kind, pt, location.localResources[kind]);
    }
#endif
}

unsigned View::getConnectedRoads(MapPoint pt)
{
    AIInterface& aii = aiPlayer.getAIInterface();
    unsigned numRoads = 0;
    for(unsigned u = 0; u < 6; ++u)
        numRoads += aii.IsRoad(GetNeighbour(pt, Direction::fromInt(u)), reverseDirection(Direction::fromInt(u)));
    return numRoads;
}

bool View::followRoadAndDestroyFlags(AIInterface& aii, MapPoint roadPt, unsigned allowedRoads, unsigned lastDir,
                                     bool destroyBuilding, bool rebuildingRoads)
{
    bool changed = false;
    for(unsigned u = 0; u < 6; ++u)
    {
        Direction dir = Direction::fromInt(u);
        if(reverseDirection(dir).native_value() == lastDir)
            continue;
        if(!aii.IsRoad(roadPt, dir))
            continue;

        MapPoint nextPt = GetNeighbour(roadPt, dir);
        if(aii.IsObjectTypeOnNode(nextPt, NOP_FLAG))
        {
            if(!destroyFlag(nextPt, allowedRoads, destroyBuilding, dir.native_value(), rebuildingRoads))
                changed |= aii.DestroyRoad(nextPt, reverseDirection(dir));
            else
                changed = true;
        } else
        {
            changed |= followRoadAndDestroyFlags(aii, nextPt, allowedRoads, dir.native_value(), destroyBuilding,
                                                 rebuildingRoads);
        }
    }
    return changed;
}

bool View::destroyFlag(MapPoint pt, unsigned allowedRoads, bool destroyBuilding, unsigned lastDir, bool rebuildingRoads)
{
    SAY("Destroy flag %, allowedRoads %, destroyBuilding %, lastDir %, rebuildingRoads %", pt, allowedRoads,
        destroyBuilding, lastDir, rebuildingRoads);
    if(!flagPts.count(pt))
        return false;
    if(allowedRoads < 6 && getConnectedRoads(pt) > (allowedRoads + destroyBuilding))
        return false;

    if(rebuildingRoads)
    {
        const noFlag* flag = aiPlayer.gwb.GetSpecObj<noFlag>(pt);
        if(!flag)
            return false;
        if(flag->GetNumWares() > 0)
            return false;
        for(unsigned u = 0; u < 6; ++u)
        {
            Direction dir = Direction::fromInt(u);
            RoadSegment* roadSegment = flag->GetRoute(dir);
            for(unsigned c = 0; roadSegment && c < 2; ++c)
            {
                if(nofCarrier* carrier = roadSegment->getCarrier(c))
                    if(carrier->HasWare())
                        return false;
            }
        }
    }

    AIInterface& aii = aiPlayer.getAIInterface();
    MapPoint bldgPt = flag2bldg(pt);
    bool hasBuilding = (aii.IsObjectTypeOnNode(bldgPt, NodalObjectType::NOP_BUILDING)
                        || aii.IsObjectTypeOnNode(bldgPt, NodalObjectType::NOP_BUILDINGSITE));
    if(!destroyBuilding && !rebuildingRoads && hasBuilding)
        return false;

    bool changed = false;
    if(!hasBuilding || destroyBuilding)
    {
        if(!aii.DestroyFlag(pt))
            return false;
        changed = true;
        flagPts.erase(pt);
    }

    allowedRoads += (lastDir == unsigned(-1));
    changed |= followRoadAndDestroyFlags(aii, pt, allowedRoads, lastDir, destroyBuilding, rebuildingRoads);

    return changed;
}

template<typename NodeT>
struct SearchNode;
struct PathNode;
struct RoadNode;

template<typename NodeT>
struct SearchNodeCompare
{
    bool operator()(const NodeT* lhs, const NodeT* rhs) const;
};

template<typename NodeT>
struct SearchState
{
    SearchState(View& view, const RoadRequest::Constraints& constraints, const GameWorldBase& gwb)
        : view(view), constraints(constraints), gwb(gwb)
    {
        if(constraints.excludedPts)
        {
            invalid.insert(constraints.excludedPts->begin(), constraints.excludedPts->end());
            SAY("Constraints had % exluded points, search state has % invalid points", constraints.excludedPts->size(),
                invalid.size());
        }
    }

    NodeT* getNode(MapPoint pt)
    {
        auto it = nodeMap.find(pt);
        if(it != nodeMap.end())
            return it->second;
        allocations.emplace_back(pt);
        nodeMap.insert({pt, &allocations.back()});
        return &allocations.back();
    }

    View& view;
    const RoadRequest::Constraints& constraints;
    const GameWorldBase& gwb;

    std::deque<NodeT> allocations;
    std::map<MapPoint, NodeT*> nodeMap;
    std::set<NodeT*, SearchNodeCompare<NodeT>> open, closed;
    std::set<MapPoint> invalid;

    static const bool DEBUG = true;
};

template<typename NodeT>
struct SearchNode
{
    SearchNode() {}
    SearchNode(MapPoint pt) : pt(pt) {}
    int64_t getScore() const { return h + g; }

    bool operator==(MapPoint pt) const { return this->pt == pt; }
    bool operator!=(MapPoint pt) const { return !((*this) == pt); }
    operator MapPoint() const { return pt; }

    virtual bool isTarget(SearchState<NodeT>& searchState)
    {
        if(searchState.invalid.count(this->pt))
            return false;
        if(searchState.constraints.requestedToPt.isValid())
            return (*this) == searchState.constraints.requestedToPt;
        if(searchState.constraints.type == RoadRequest::Constraints::TERRAIN)
            return (*this) == searchState.constraints.requestedToPt;
        return searchState.view.flagPts.count(this->pt) && !searchState.view.singletonFlagPts.count(this->pt);
    }

    virtual bool isValidNode(SearchState<NodeT>& searchState, int64_t cost, Direction)
    {
        if(searchState.invalid.count(this->pt))
        {
            if(SearchState<NodeT>::DEBUG)
                SAY("INVALID set: %", this->pt);
            return false;
        }
        if(cost > int64_t(searchState.constraints.maximalCost))
        {
            if(SearchState<NodeT>::DEBUG)
                SAY("INVALID cost: %, % > %", this->pt, cost, searchState.constraints.maximalCost);
            return false;
        }
        PathConditionReachable PCR(searchState.gwb);
        if(!PCR.IsNodeOk(this->pt))
        {
            if(SearchState<NodeT>::DEBUG)
                SAY("INVALID unusable node: %, % > %", this->pt, cost, searchState.constraints.maximalCost);
            return false;
        }
        if(searchState.constraints.type == RoadRequest::Constraints::TERRAIN)
            return true;
        if(!searchState.gwb.IsPlayerTerritory(this->pt))
        {
            if(SearchState<NodeT>::DEBUG)
                SAY("INVALID territory: %", this->pt);
            return false;
        }
        return true;
    }
    virtual int64_t getMovementCost(SearchState<NodeT>& searchState, NodeT* predecessor, Direction dir) = 0;
    virtual void updateNode(SearchState<NodeT>& searchState, int64_t cost, NodeT* predecessor, Direction dir) = 0;

    Direction dir;
    const MapPoint pt;
    int64_t h = 0, g = 0;
};

struct PathNode : public SearchNode<PathNode>
{
    PathNode() : SearchNode() {}
    PathNode(MapPoint pt) : SearchNode(pt) {}

    bool isValidNode(SearchState<PathNode>& searchState, int64_t cost, Direction dir) override
    {
        if(!SearchNode<PathNode>::isValidNode(searchState, cost, dir))
            return false;
        if(searchState.constraints.type == RoadRequest::Constraints::SHORTEST)
            return true;
        if(searchState.constraints.type == RoadRequest::Constraints::TERRAIN)
            return true;

        if(!isTarget(searchState)
           && (!searchState.gwb.IsRoadAvailable(false, this->pt) || searchState.gwb.IsOnRoad(this->pt)))
        {
            if(SearchState<PathNode>::DEBUG)
                SAY("INVALID road: %", this->pt);
            return false;
        }

        if(SearchState<PathNode>::DEBUG)
            SAY("IsValid %p? IsFlag: %, hasFlag: %", this->pt, searchState.view.flagPts.count(this->pt), hasFlag);

        if(searchState.constraints.type == RoadRequest::Constraints::SECONDARY
           && searchState.view.flagPts.count(this->pt) && !searchState.view.singletonFlagPts.count(this->pt))
        {
            assert(!searchState.constraints.requestedToPt.isValid());
            // if (cost < Config::Score::Penalties::ROUTE_SEGMENT * 8)
            // return false;;

            RoadRequest::Constraints roadConstraints{searchState.constraints.fromPt, this->pt};
            roadConstraints.maximalCost = cost * Config::Score::SECONDARY_ROAD_FACTOR;

            MapPoint bldgPt = searchState.view.flag2bldg(this->pt);
            for(BuildingType buildingType : {BLD_HEADQUARTERS, BLD_HARBORBUILDING, BLD_STOREHOUSE})
                if(searchState.view.buildings[buildingType].count(bldgPt))
                    roadConstraints.maximalCost /= 2;
            if(const noFlag* flag = searchState.gwb.GetSpecObj<noFlag>(this->pt))
                if(flag->GetFlagType() == FT_LARGE)
                    roadConstraints.maximalCost = roadConstraints.maximalCost * 2 / 3;

            Score score;
            std::vector<Direction> directions;
            if(searchState.view.findRoadConnection(roadConstraints, directions, score))
            {
                if(SearchState<PathNode>::DEBUG)
                    SAY("[%] INVALID road connection % > %", this->pt, cost, score);
                return false;
            }
            if(SearchState<PathNode>::DEBUG)
                SAY("[%] NEW PATH COSTS %, road is more expensive %", pt, cost, score);
            // assert(0);
        }
        return true;
    };

    int64_t getMovementCost(SearchState<PathNode>& searchState, PathNode* predecessor, Direction) override
    {
        if(searchState.constraints.type == RoadRequest::Constraints::SHORTEST)
            return 1;
        if(searchState.constraints.type == RoadRequest::Constraints::TERRAIN)
            return 1;

        bool assumeHasFlag = !predecessor->hasFlag && searchState.view.canBuildFlag(this->pt);

        int64_t baseCost = Config::Score::Penalties::ROUTE_SEGMENT;
        int64_t buildingQualityCost = 0;
        if(predecessor->hasFlag || !assumeHasFlag)
            buildingQualityCost = searchState.view.getCloseByPenalty(pt, BQ_NOTHING, predecessor->pt).getValue();
        else
            buildingQualityCost = searchState.view.getCloseByPenalty(pt, BQ_FLAG, predecessor->pt).getValue();

        int64_t missingFlagCost = 0;
        if(!assumeHasFlag && !predecessor->hasFlag)
            missingFlagCost = Config::Score::Penalties::ROUTE_MISSING_FLAG;

        return std::max(int64_t(1), baseCost + buildingQualityCost + missingFlagCost);
    }

    void updateNode(SearchState<PathNode>& searchState, int64_t cost, PathNode* predecessor, Direction dir) override
    {
        this->g = cost;
        if(searchState.constraints.requestedToPt.isValid())
            this->h = searchState.view.CalcDistance(this->pt, searchState.constraints.requestedToPt);
        else
            this->h = searchState.view.CalcDistance(this->pt, searchState.view.aiPlayer.player.GetHQPos());

        hasFlag = !predecessor->hasFlag && searchState.view.canBuildFlag(this->pt);
        this->dir = dir;
    };

    bool hasFlag = true;
};

struct RoadNode : public SearchNode<RoadNode>
{
    RoadNode() = default;
    RoadNode(MapPoint pt) : SearchNode(pt) {}

    bool isValidNode(SearchState<RoadNode>& searchState, int64_t cost, Direction dir) override
    {
        if(!SearchNode<RoadNode>::isValidNode(searchState, cost, dir))
            return false;

        const noBase* no = searchState.gwb.GetNO(pt);
        const NodalObjectType noType = no->GetType();
        bool bldg = noType == NOP_BUILDING || noType == NOP_BUILDINGSITE;
        // SAY("[%] %: % + %,   %", this->pt, dir.native_value(), unsigned(searchState.gwb.GetPointRoad(this->pt, dir)),
        // bldg, searchState.view.flagPts.count(this->pt));

        if(searchState.gwb.GetPointRoad(this->pt, dir) == PointRoad::None || bldg)
        {
            // SAY("[%] INVALID road node in direction %, %", this->pt, dir.native_value(), cost);
            return false;
        }
        return true;
    };
    int64_t getMovementCost(SearchState<RoadNode>&, RoadNode*, Direction) override
    {
        int64_t baseCost = Config::Score::Penalties::ROUTE_SEGMENT;
        return baseCost;
    }
    void updateNode(SearchState<RoadNode>& searchState, int64_t cost, RoadNode*, Direction dir) override
    {
        assert(searchState.constraints.requestedToPt.isValid());
        this->dir = dir;
        this->g = cost;
        this->h = searchState.view.CalcDistance(this->pt, searchState.constraints.requestedToPt);
    };
};

template<typename NodeT>
bool SearchNodeCompare<NodeT>::operator()(const NodeT* lhs, const NodeT* rhs) const
{
    if(lhs->getScore() == rhs->getScore())
        return std::less<MapPoint>{}(lhs->pt, rhs->pt);
    return lhs->getScore() < rhs->getScore();
}

template<typename NodeT>
static MapPoint searchWay(SearchState<NodeT>& searchState, View& view, const RoadRequest::Constraints& constraints,
                          std::vector<Direction>& directions, Score& score)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    SAY("--- Search way % - % [Max: %]", constraints.fromPt, constraints.requestedToPt, constraints.maximalCost);
    score.invalidate();

    NodeT* initialNode = searchState.getNode(constraints.fromPt);
    searchState.open.insert(initialNode);

    if(constraints.excludeFromPt)
        searchState.invalid.insert(constraints.fromPt);
    if(constraints.excludeFromBldgPt)
        searchState.invalid.insert(searchState.getNode(view.flag2bldg(constraints.fromPt))->pt);

    NodeT* current = nullptr;
    while(!searchState.open.empty())
    {
        auto currentOpenIt = searchState.open.begin();
        current = *currentOpenIt;
        searchState.open.erase(currentOpenIt);

        auto nextIt = searchState.open.begin();
        if(nextIt != searchState.open.end())
        {
            if(SearchState<NodeT>::DEBUG)
                SAY(" --  % [h:%|g:%] ::: % [h:%|g:%]", current->pt, current->h, current->g, (*nextIt)->pt,
                    (*nextIt)->h, (*nextIt)->g);
        } else
        {
            if(SearchState<NodeT>::DEBUG)
                SAY(" --  % [h:%|g:%]", current->pt, current->h, current->g);
        }

        if(!current->pt.isValid() || current->isTarget(searchState))
            break;

        auto dealWithNeighbour = [&](NodeT* neighbour, int64_t cost, Direction dir) {
            // if(SearchState<NodeT>::DEBUG)
            // SAY("open: % closed: % neighbour: % [valid:%] [target:%]", searchState.open.size(),
            // searchState.closed.size(), neighbour->pt, neighbour->isValidNode(searchState, cost),
            // neighbour->isTarget(searchState));

            auto neighbourOpenIt = searchState.open.find(neighbour);
            bool neighbourInOpen = neighbourOpenIt != searchState.open.end();
            auto neighbourClosedIt = searchState.closed.find(neighbour);
            bool neighbourInClosed = neighbourClosedIt != searchState.closed.end();
            if(neighbourInOpen && cost < neighbour->g)
            {
                searchState.open.erase(neighbour);
                neighbourInOpen = false;
            } else if(neighbourInClosed && cost < neighbour->g)
            {
                searchState.closed.erase(neighbourClosedIt);
                neighbourInClosed = false;
            }
            // if(SearchState<NodeT>::DEBUG)
            // SAY("in open %, in closed %", neighbourInOpen, neighbourInClosed);
            if(neighbourInOpen || neighbourInClosed)
                return;
            neighbour->updateNode(searchState, cost, current, dir);
            searchState.open.insert(neighbour);
        };

        searchState.closed.insert(current);

        if(constraints.type == RoadRequest::Constraints::TERRAIN)
        {
            auto it = view.connectionLengthCache.find({current->pt, constraints.requestedToPt});
            if(it != view.connectionLengthCache.end())
            {
                score = it->second + current->g;
                return current->pt;
            }
        }

        for(unsigned u = 0; u < 6; ++u)
        {
            Direction dir = Direction::fromInt(u);
            Direction rdir = view.reverseDirection(dir);
            if(current->g && current->dir == rdir)
                continue;
            PathConditionReachable PCR(searchState.gwb);
            if(!PCR.IsEdgeOk(current->pt, dir))
                continue;

            NodeT* neighbour = searchState.getNode(view.GetNeighbour(current->pt, dir));
            int64_t cost = current->g + neighbour->getMovementCost(searchState, current, dir);

            if(!neighbour->isValidNode(searchState, cost, rdir))
                continue;

            dealWithNeighbour(neighbour, cost, dir);
        }
    }

    if(SearchState<NodeT>::DEBUG)
        SAY("DONE [%]: open: % closed: % curent: %", current->isTarget(searchState), searchState.open.size(),
            searchState.closed.size(), current->pt);

    score = current->g;
    if(!current->isTarget(searchState))
        return MapPoint();
    if(constraints.type == RoadRequest::Constraints::TERRAIN)
        return current->pt;

    directions.clear();
    int64_t h = current->h;
    int64_t g = current->g;
    MapPoint finalPt = current->pt;
    if(SearchState<NodeT>::DEBUG)
        SAY("R %", current->pt);
    while((*current) != constraints.fromPt)
    {
        directions.push_back(current->dir);
        current = searchState.nodeMap[view.GetNeighbour(current->pt, view.reverseDirection(current->dir))];
        assert(current->pt.isValid());
        if(SearchState<NodeT>::DEBUG)
            SAY("R %", current->pt);
    };

    std::reverse(directions.begin(), directions.end());
    if(SearchState<NodeT>::DEBUG)
        SAY("SEARCH[%] % -> % [h:%|g:%]: %", constraints.maximalCost, constraints.fromPt, finalPt, h, g,
            construction::Route(directions));

    return finalPt;
}

MapPoint View::findConnectionPoint(const RoadRequest::Constraints& constraints, std::vector<Direction>& directions,
                                   Score& score)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    if(SearchState<PathNode>::DEBUG)
        SAY("\nfindConnectionPoint: % -> % [%]", constraints.fromPt, constraints.requestedToPt, constraints);

    if(constraints.type != RoadRequest::Constraints::SECONDARY)
    {
        SearchState<PathNode> searchState(*this, constraints, aiPlayer.gwb);
        assert(!constraints.excludedPts || searchState.invalid.size() >= constraints.excludedPts->size());
        return searchWay<PathNode>(searchState, *this, constraints, directions, score);
    }

    MapPoint fakeTargetPt = aiPlayer.player.GetHQPos();
    RoadRequest::Constraints roadConstraints{constraints.fromPt, fakeTargetPt};
    bool roadFound = findRoadConnection(roadConstraints, directions, score);
    assert(!roadFound);
    (void)roadFound;

    RoadRequest::Constraints pathConstraints = constraints;
    pathConstraints.maximalCost = score.getValue() / Config::Score::SECONDARY_ROAD_FACTOR;

    MapPoint bldgPt = flag2bldg(constraints.fromPt);
    for(BuildingType buildingType : {BLD_HEADQUARTERS, BLD_HARBORBUILDING, BLD_STOREHOUSE})
        if(buildings[buildingType].count(bldgPt))
            pathConstraints.maximalCost *= 2;
    if(const auto* flag = aiPlayer.gwb.GetSpecObj<noFlag>(constraints.fromPt))
        if(flag->GetFlagType() == FT_LARGE)
            pathConstraints.maximalCost = pathConstraints.maximalCost * 3 / 2;

    if(SearchState<PathNode>::DEBUG)
        SAY("modified request: % -> % [%]", pathConstraints.fromPt, pathConstraints.requestedToPt, pathConstraints);

    directions.clear();
    SearchState<PathNode> searchState(*this, pathConstraints, aiPlayer.gwb);
    return searchWay<PathNode>(searchState, *this, pathConstraints, directions, score);
}
bool View::findRoadConnection(const RoadRequest::Constraints& constraints, std::vector<Direction>& directions,
                              Score& score)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    assert(constraints.requestedToPt.isValid());
    if(SearchState<RoadNode>::DEBUG)
        SAY("\nfindRoadConnection: % -> %", constraints.fromPt, constraints.requestedToPt);
    SearchState<RoadNode> searchState(*this, constraints, aiPlayer.gwb);
    return searchWay<RoadNode>(searchState, *this, constraints, directions, score).isValid();
}
unsigned View::getConnectionLength(const RoadRequest::Constraints& constraints)
{
    if(constraints.fromPt == constraints.requestedToPt)
        return 0;

    RAIISayTimer RST(__PRETTY_FUNCTION__);
    auto it = connectionLengthCache.find({constraints.fromPt, constraints.requestedToPt});
    if(it != connectionLengthCache.end())
        return it->second;

    unsigned connectionLength = -1;
    std::vector<Direction> directions;
    Score score;
    RoadRequest::Constraints connectionConstraints = constraints;
    connectionConstraints.type = RoadRequest::Constraints::TERRAIN;

    SearchState<PathNode> searchState(*this, connectionConstraints, aiPlayer.gwb);
    if(!searchWay<PathNode>(searchState, *this, connectionConstraints, directions, score).isValid())
    {
        connectionLength = -1;
        //if (!constraints.excludedPts)
          //connectionLengthCache[{constraints.fromPt, constraints.requestedToPt}] = connectionLength;
    } else
    {
        connectionLength = score.getValue();

        int distance = connectionLength;

        MapPoint curPt = constraints.fromPt;
        for(unsigned u = 0, e = directions.size(); u < e; ++u)
        {
            connectionLengthCache[{curPt, constraints.requestedToPt}] = (distance--);
            curPt = GetNeighbour(curPt, directions[u]);
        }
    }
    assert(connectionLength != 0);
    return connectionLength;
}
void View::precomputeConnections(const MapPoint& pt)
{
    if(!pt.isValid())
        return;
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    std::vector<Direction> directions;
    Score score;
    RoadRequest::Constraints constraints{pt, MapPoint(0, -1)};
    constraints.type = RoadRequest::Constraints::TERRAIN;

    SearchState<PathNode> searchState(*this, constraints, aiPlayer.gwb);
    MapPoint resultPt = searchWay<PathNode>(searchState, *this, constraints, directions, score);
    SAY("Result %", resultPt);
    assert(!resultPt.isValid());

    for(PathNode& pathNode : searchState.allocations)
    {
        connectionLengthCache[{pt, pathNode}] = pathNode.g;
        connectionLengthCache[{pathNode, pt}] = pathNode.g;
    }
}

void View::getRoadSCCs(std::vector<std::vector<MapPoint>>& sccs, std::vector<MapPoint>& singletonFlags)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);

    std::set<MapPoint> flags = flagPts;

    AIInterface& aii = aiPlayer.getAIInterface();
    std::vector<Direction> directions;
    auto findSCC = [&](MapPoint pt, std::vector<MapPoint>& scc) {
        bool hasBuilding = (aii.IsObjectTypeOnNode(flag2bldg(pt), NodalObjectType::NOP_BUILDING)
                            || aii.IsObjectTypeOnNode(flag2bldg(pt), NodalObjectType::NOP_BUILDINGSITE));
        if(getConnectedRoads(pt) <= hasBuilding)
        {
            singletonFlags.push_back(pt);
            flags.erase(pt);
            return;
        }

        directions.clear();
        Score score;
        RoadRequest::Constraints constraints{pt, MapPoint(0, -1)};
        constraints.type = RoadRequest::Constraints::SHORTEST;

        SearchState<RoadNode> searchState(*this, constraints, aiPlayer.gwb);
        MapPoint resultPt = searchWay<RoadNode>(searchState, *this, constraints, directions, score);

        for(RoadNode& roadNode : searchState.allocations)
        {
            if(flags.erase(roadNode.pt))
                scc.push_back(roadNode.pt);
        }
    };

    sccs.reserve(16);
    while(!flags.empty())
    {
        SAY("Get road network SCCs, % flags left, % sccs found", flags.size(), sccs.size());
        sccs.push_back({});
        findSCC(*flags.begin(), sccs.back());
    }
}

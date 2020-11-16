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

#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <random>
#include <set>

#include "AIPlayerJD.h"
#include "FindWhConditions.h"

#include "Actions.h"
#include "GameCommands.h"
#include "Point.h"
#include "RoadSegment.h"
#include "ai/AIResource.h"
#include "ai/aijd/Buildings.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobHQ.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobUsual.h"
#include "figures/nofAttacker.h"
#include "figures/nofCarrier.h"
#include "figures/nofPassiveSoldier.h"
#include "network/GameMessages.h"
#include "notifications/BuildingNote.h"
#include "notifications/ExpeditionNote.h"
#include "notifications/NodeNote.h"
#include "notifications/ResourceNote.h"
#include "notifications/RoadNote.h"
#include "notifications/ShipNote.h"
#include "notifications/ToolNote.h"
#include "world/NodeMapBase.h"
#include "nodeObjs/noFlag.h"
#include "nodeObjs/noSign.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/BuildingType.h"
#include "gameTypes/GoodTypes.h"
#include "gameTypes/InventorySetting.h"
#include "gameTypes/JobTypes.h"
#include "gameTypes/MapCoordinates.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/JobConsts.h"
#include "gameData/MilitaryConsts.h"
#include "gameData/SettingTypeConv.h"

#define _TAG_ "[PLAYERJD]"

//#undef SAY
//#define SAY(msg, ...) (tprintf((_TAG_ " " msg), __VA_ARGS__), std::cout << std::endl);

#define COUNTER_GUARD(NAME, VAL)            \
    if(COUNTER % config.counters.NAME != 0) \
        return VAL;
#define COUNTER_GUARD_FACTOR(NAME, BASE, VAL)        \
    if(COUNTER % (BASE * config.counters.NAME) != 0) \
        return VAL;

using namespace AI;
using namespace jd;

namespace distribution {
static constexpr unsigned char FOOD_2_GRANITE_MINE = 0;
static constexpr unsigned char FOOD_2_COAL_MINE = 1;
static constexpr unsigned char FOOD_2_IRON_MINE = 2;
static constexpr unsigned char FOOD_2_GOLD_MINE = 3;
static constexpr unsigned char GRAIN_2_MILL = 4;
static constexpr unsigned char GRAIN_2_PIGFARM = 5;
static constexpr unsigned char GRAIN_2_DONKEYBREEDER = 6;
static constexpr unsigned char GRAIN_2_BREWERY = 7;
static constexpr unsigned char GRAIN_2_CHARBURNER = 8;
static constexpr unsigned char IRON_2_ARMORY = 9;
static constexpr unsigned char IRON_2_METALWORKS = 10;
static constexpr unsigned char COAL_2_ARMORY = 11;
static constexpr unsigned char COAL_2_IRONSMELTER = 12;
static constexpr unsigned char COAL_2_MINT = 13;
static constexpr unsigned char WOOD_2_SAWMILL = 14;
static constexpr unsigned char WOOD_2_CHARBURNER = 15;
static constexpr unsigned char BOARDS_2_CONSTRUCTION = 16;
static constexpr unsigned char BOARDS_2_METALWORK = 17;
static constexpr unsigned char BOARDS_2_SHIPYARD = 18;
static constexpr unsigned char WATER_2_BAKERY = 19;
static constexpr unsigned char WATER_2_BREWERY = 20;
static constexpr unsigned char WATER_2_PIGFARM = 21;
static constexpr unsigned char WATER_2_DONKEYBREEDER = 22;
} // namespace distribution

void PlayerJD::chat(const std::string& Msg)
{
    GAMECLIENT.GetMainPlayer().sendMsgAsync(new GameMessage_Chat(playerId, CD_ALL, Msg));
}
bool PlayerJD::isDefeated()
{
    if(aii.IsDefeated())
        return true;
    if(!aii.GetStorehouses().empty())
        return false;

    chat(_("I die."));
    aii.DestroyAll();
    return true;
}

void PlayerJD::init()
{
    chat(_("Hello! It's me, " + identity + "!"));
    chat(_("I'm very much experimental, so... "));
    const_cast<nobHQ*>(aii.GetHeadquarter())->SetIsTent(true);

    numTotalGeologists = lookupJobInInventory(JOB_GEOLOGIST);
    numGeologistsInStore = lookupJobInStorehouses(JOB_GEOLOGIST);

    for(unsigned u = 0; u < 23; ++u)
        settings.distribution[u] = 10;

    settings.distribution[distribution::BOARDS_2_METALWORK] = 10;
    settings.distribution[distribution::IRON_2_METALWORKS] = 10;
    settings.distribution[distribution::IRON_2_ARMORY] = 1;
    settings.distribution[distribution::BOARDS_2_SHIPYARD] = 2;
    settings.distribution[distribution::GRAIN_2_BREWERY] = 10;
    updateItemDistribution = true;

    resetMilitarySettings();

    view.storehouseResources.clear();

    auto* hq = const_cast<nobHQ*>(aii.GetHeadquarter());
    for(unsigned u = 0; u < 5; u++)
    {
        hq->DecreaseReserveVisual(u);
        hq->SetRealReserve(u, 0);
    }

    makeBarracks(*hq);

    for(unsigned u = 0; u < NUM_BUILDING_TYPES; ++u)
    {
        auto buildingType = BuildingType(u);
        const BuildingInfo& buildingInfo = BuildingInfo::get(buildingType);
        if(!buildingInfo.isValid())
            continue;
        auto ProductionCB = [&](const BuildingEffect& production) {
            if(production.isGlobal() && aii.CanBuildBuildingtype(buildingType) && buildingType != BLD_HEADQUARTERS)
                producerMap[production.getThing()].push_back(buildingType);
            return true;
        };
        buildingInfo.foreachProduction(ProductionCB);

        auto ConsumptionCB = [&](const BuildingEffect& consumption) {
            if(consumption.isGlobal() && aii.CanBuildBuildingtype(buildingType) && buildingType != BLD_HEADQUARTERS)
                consumerMap[consumption.getThing()].push_back(buildingType);
            return true;
        };
        buildingInfo.foreachConsumption(ConsumptionCB);
    }

    // Hookup event callbacks.
    NotificationManager& notifications = gwb.GetNotifications();
    notificationHandles.emplace_back(notifications.subscribe<BuildingNote>([this](const BuildingNote& note) {
        if(note.player != playerId)
            return;
        // SAY("Building note % for %", note.type, note.player);
        switch(note.type)
        {
            case BuildingNote::Constructed: registerFinishedBuilding(note.pos, note.bld); break;
            case BuildingNote::Destroyed: shouldReconnect = true; break;
            case BuildingNote::Captured:
            {
                MapPoint flagPt = view.bldg2flag(note.pos);
                unsigned numConnections = view.getConnectedRoads(flagPt);
                if(auto* militaryBuilding =
                     const_cast<nobMilitary*>(static_cast<const nobMilitary*>(gwb.GetNO(note.pos))))
                    if(numConnections == 1 && (militaryBuilding->GetMaxTroopsCt() <= 3 || !shouldExpand()))
                    {
                        aii.DestroyBuilding(militaryBuilding);
                        break;
                    }

                RoadRequest::Constraints constraints{flagPt};
                constraints.excludeFromPt = true;
                if(numConnections > 1)
                    constraints.type = RoadRequest::Constraints::SECONDARY;
                actionManager.constructRoad(nullptr, constraints);
                // printf("adding captured road construction to %lu pending actions.",
                // actionManager.getNumPendingActions());
                // createSecondaryRoad(flagPt);
                shouldReconnect = true;

                if(shouldExpand())
                    expand(/* force */ false);

                break;
            }
            case BuildingNote::Lost:
                if(barracks.erase(note.pos))
                {
                    for(auto* storehouse : aii.GetStorehouses())
                    {
                        makeBarracks(*storehouse);
                        break;
                    }
                }

                // withLookout.clear();
                // if(BuildingProperties::IsMilitary(note.bld))
                // enemiesToAttack.insert(gwb.GetNode(note.pos).owner - 1);
                shouldReconnect = true;
                break;
            case BuildingNote::NoRessources:
                registerOutOfResourceBuilding(note.pos, note.bld);
                // checkRoads = true;
                // shouldReconnect |=
                // view.destroyFlag(view.bldg2flag(note.pos), 6, [> DestroyBuilding */ false, /* lastDir */ -1, /*
                // rebuildRoads <] true);
                break;
            case BuildingNote::LuaOrder: break;
            case BuildingNote::LostLand: enemiesToAttack.insert(gwb.GetNode(note.pos).owner - 1); break;
        };
    }));
    notificationHandles.emplace_back(notifications.subscribe<ExpeditionNote>([this](const ExpeditionNote& note) {
        if(note.player != playerId)
            return;
    }));
    notificationHandles.emplace_back(notifications.subscribe<ResourceNote>([this](const ResourceNote& note) {
        if(note.player != playerId)
            return;

        switch(note.res.getType())
        {
            // case Resource::Iron: view.globalResources[ResourceInfo::IRONORE]++; break;
            // case Resource::Coal: view.globalResources[ResourceInfo::COAL]++; break;
            // case Resource::Granite: view.globalResources[ResourceInfo::GRANITE]++; break;
            // case Resource::Gold: view.globalResources[ResourceInfo::GOLD]++; break;
            default: break;
        };
        // SAY("Resource note % @ (%|%) for %" , note.res.getType(), note.pos.x, note.pos.y, note.player);
    }));
    notificationHandles.emplace_back(notifications.subscribe<RoadNote>([this](const RoadNote& note) {
        if(note.player != playerId)
            return;
    }));
    notificationHandles.emplace_back(notifications.subscribe<ShipNote>([this](const ShipNote& note) {
        if(note.player != playerId)
            return;
    }));
    notificationHandles.emplace_back(notifications.subscribe<NodeNote>([](const NodeNote&) {}));

    notificationHandles.emplace_back(notifications.subscribe<ToolNote>([this](const ToolNote& note) {
        if(note.player != playerId)
            return;
        if(note.type == ToolNote::OrderCompleted)
        {
            toolsRequested = std::max(0, toolsRequested - 1);
        } else if(note.type == ToolNote::OrderPlaced)
        {
            //++toolsRequested;
        }
        // for(unsigned u = 0; u < NUM_TOOLS; ++u)
        // toolSettings[u] = player.GetToolsOrdered(u);
    }));
}

void PlayerJD::makeBarracks(nobBaseWarehouse& storehouse)
{
    barracks.insert(storehouse.GetPos());
    setInventorySetting(storehouse, GD_BEER, (EInventorySetting::COLLECT));
    setInventorySetting(storehouse, GD_SWORD, (EInventorySetting::COLLECT));
    setInventorySetting(storehouse, GD_SHIELDAFRICANS, (EInventorySetting::COLLECT));
    setInventorySetting(storehouse, GD_SHIELDROMANS, (EInventorySetting::COLLECT));
    setInventorySetting(storehouse, GD_SHIELDJAPANESE, (EInventorySetting::COLLECT));
    setInventorySetting(storehouse, GD_SHIELDVIKINGS, (EInventorySetting::COLLECT));
}

bool PlayerJD::shouldBuildForester(MapPoint woodcutterPt)
{
    unsigned numForester = aii.GetBuildings(BLD_FORESTER).size();
    if(numForester == 0)
        return true;

    if(view.globalResources[CommodityInfo::WOOD] - (2 * view.getNumBuildings(BLD_SAWMILL)) > 2
       || view.globalResources[ResourceInfo::TREE] >= 40
       || lookupGoodInInventory(GD_BOARDS) + lookupGoodInInventory(GD_WOOD) >= (100 - 20 * numForester))
    {
        return false;
    }
    if(view.getNumBuildings(BLD_SAWMILL) <= view.getNumBuildings(BLD_FORESTER))
        return false;
    if(view.getNumBuildings(BLD_SAWMILL) * 2 <= aii.GetBuildings(BLD_WOODCUTTER).size())
        return false;

    unsigned numHuts = 0, numPlantspace = 0;
    ;
    for(const MapPoint& circlePt : view.GetPointsInRadiusWithCenter(woodcutterPt, 6))
    {
        BuildingQuality buildingQuality = view.getBuildingQuality(circlePt);
        numHuts += canUseBq(buildingQuality, BQ_HUT);
        numPlantspace += view[circlePt].getLocalResource(ResourceInfo::PLANTSPACE);
    }

    return (numHuts < 1 || numPlantspace < 4);
}

void PlayerJD::registerOutOfResourceBuilding(MapPoint bldgPt, BuildingType buildingType)
{
    SAY("registerOutOfResourceBuilding: % %", bldgPt, buildingType);

    bool consumesOnlyResource = true;
    auto ConsumptionCallback = [&](const BuildingEffect& consumption) {
        consumesOnlyResource &= ResourceInfo::isa(consumption.getThing());
        return true;
    };

    BuildingInfo buildingInfo = BuildingInfo::get(buildingType);
    buildingInfo.foreachConsumption(ConsumptionCallback);

    if(buildingType == BLD_WOODCUTTER)
    {
        auto it = view.buildings[BLD_WOODCUTTER].find(bldgPt);
        if(it != view.buildings[BLD_WOODCUTTER].end() && (*it).second.hasRequiredResources())
            return;
        if(view.buildings[BLD_FORESTER].empty() && view.buildings[BLD_WOODCUTTER].size() < 4)
            return;
        int numForester = 0, numWoodcutter = 0;
        for(auto& it : view.buildings[BLD_FORESTER])
            if(view.CalcDistance(it.first, bldgPt) < 6)
                ++numForester;
        for(auto& it : view.buildings[BLD_WOODCUTTER])
            if(view.CalcDistance(it.first, bldgPt) < 10)
                ++numWoodcutter;
        if(numForester * 2 - 1 > numWoodcutter)
            return;

        if(shouldBuildForester(bldgPt))
        {
            Action* groupAction = new GroupAction(GroupAction::ANY);
            actionManager.appendAction(groupAction);

            Action* woodcutterAction = new GroupAction(GroupAction::EXECUTE);
            groupAction->addSubAction(woodcutterAction);
            woodcutterAction->addSubAction(new BuildingRequest(actionManager, BLD_WOODCUTTER));
            woodcutterAction->addSubAction(new DestroyRequest(actionManager, {bldgPt}));

            if(view.getNumBuildings(BLD_SAWMILL) > view.getNumBuildings(BLD_FORESTER))
            {
                Action* foresterAction = new GroupAction(GroupAction::ANY);
                groupAction->addSubAction(foresterAction);
                foresterAction->addSubAction(new BuildingRequest(actionManager, BLD_FORESTER, bldgPt, 6));
                foresterAction->addSubAction(new DestroyRequest(actionManager, {bldgPt}));
            }

            actions.insert(groupAction);

            return;
        }
    }

    if(consumesOnlyResource)
    {
        aii.DestroyBuilding(bldgPt);
        return;
    }
}

void PlayerJD::registerFinishedBuilding(MapPoint pos, BuildingType buildingType)
{
    finishedBuildings[pos] = buildingType;
}
void PlayerJD::registerDestroyedBuilding(MapPoint pos, BuildingType buildingType)
{
    if(finishedBuildings.erase(pos))
    {
        // Job job = BLD_WORK_DESC[buildingType].job;
        // unreserveJob(job);
    }
    buildingsWithRequestedTools.erase(pos);
    auto it = buildingCallbacks.find(pos);
    if(it != buildingCallbacks.end())
    {
        it->second(pos, false);
        buildingCallbacks.erase(it);
    }
}
void PlayerJD::checkForCommissionings()
{
    auto TriggerBuildingCallbacks = [&](const MapPoint& pos, bool build) {
        auto it = buildingCallbacks.find(pos);
        if(it != buildingCallbacks.end())
        {
            it->second(pos, build);
            buildingCallbacks.erase(it);
        }
    };

    discard_if(finishedBuildings, [&](const auto& pair) {
        const noBase* no = gwb.GetNO(pair.first);
        const NodalObjectType noType = no->GetType();
        SAY("finshed building % %   % ", pair.first, pair.second, noType);
        if(BuildingProperties::IsMilitary(pair.second))
        {
            if(static_cast<const nobMilitary*>(no)->IsNewBuilt())
                return false;
            view.requiresFullUpdate = true;
            TriggerBuildingCallbacks(pair.first, true);
            return true;
        }

        if(noType != NOP_BUILDING)
        {
            TriggerBuildingCallbacks(pair.first, false);
            return true;
        }
        if(!static_cast<const nobUsual*>(no)->HasWorker())
        {
            if(auto job = BLD_WORK_DESC[pair.second].job)
            {
                reserveJob(*job);
            }
            return false;
        }
        if(pair.second == BLD_LOOKOUTTOWER)
            view.requiresFullUpdate = true;
        TriggerBuildingCallbacks(pair.first, true);
        return true;
    });
}

void PlayerJD::reserveForBuilding(BuildingType buildingType, unsigned deliveredBoards, unsigned deliveredStones)
{
    const auto& buildingCosts = BUILDING_COSTS[aii.GetNation()][buildingType];
    reserveGood(GD_BOARDS, buildingCosts.boards - deliveredBoards);
    reserveGood(GD_STONES, buildingCosts.stones - deliveredStones);

    if(auto job = BLD_WORK_DESC[buildingType].job)
    {
        reserveJob(job);
    }
}

void PlayerJD::reserveJob(boost::optional<Job> job)
{
    if(!job)
        return;
    reservedInventory.Add(*job);
}

void PlayerJD::updateReserve()
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    reservedInventory.clear();

    for(const noBuildingSite* buildingSite : aii.GetBuildingSites())
    {
        BuildingType buildingType = buildingSite->GetBuildingType();
        reserveForBuilding(buildingType, buildingSite->getBoards() + buildingSite->getUsedBoards(),
                           buildingSite->getStones() + buildingSite->getUsedStones());
    }

    SAY("\n[GLOBAL] % [%]", view.globalResources);
    SAY("\n[STORE] % [%]", view.storehouseResources);
    view.globalResources -= view.storehouseResources;
    SAY("\n[GLOBAL] % [%]", view.globalResources);
    view.storehouseResources = Tracker();
    for(unsigned u = 0; u < NUM_WARE_TYPES; ++u)
    {
        auto good = GoodType(u);
        unsigned available = lookupGoodInStorehouses(good);
        CommodityInfo::Kind kind = goodType2CommodityInfoKind(good);
        view.storehouseResources[kind] = available / Config::View::NUM_WARES_EQUAL_PRODUCTION;
        SAY("% % in storehouse (based on % %)", view.storehouseResources[kind], u, available, good);
    }
    view.globalResources += view.storehouseResources;
    SAY("\n[GLOBAL] % [%]", view.globalResources);

    checkForCommissionings();

    view.globalResources[ResourceInfo::WATER] = 1;
    view.globalResources[CommodityInfo::SWORD] = -1;
    view.globalResources[CommodityInfo::SHIELD] = -1;
    if(lookupGoodInStorehouses(GD_COINS) < 10)
        view.globalResources[CommodityInfo::COINS] = -1;

    for(unsigned u = 0; u < NUM_JOB_TYPES; ++u)
    {
        Job job = (Job)u;
        if(!lookupJobInStorehouses(job, /* includeTools */ true))
            if(boost::optional<GoodType> tool = job2tool(job))
                requestTool(*tool, std::max(1U, reservedInventory[job]));
    }
}

#if 0
void PlayerJD::issueSupplyChain(ProximityInfo::Kind kind, Action* parentAction)
{
    // if(view.getGlobalResource(-1, kind, 0) > 0)
    // return;

    bool isEnd;
    const auto& buildingTypeIt = getRandomElement(producerMap[kind], &isEnd);
    if(isEnd)
        return;
    actions.insert(actionManager.constructBuilding(parentAction, *buildingTypeIt));
    ProximityInfo proximityInfo(*buildingTypeIt);

    for(unsigned cIdx = 0; cIdx < proximityInfo.consuming.size(); ++cIdx)
        if(proximityInfo.consumingRadius[cIdx] == -1 && proximityInfo.consuming[cIdx] != ProximityInfo::NOTHING)
        {
            issueSupplyChain(proximityInfo.consuming[cIdx], nullptr);
        }
}
#endif

void PlayerJD::RunGF(unsigned gf, bool gfisnwf)
{
    if(!gfisnwf || gf < config.player.MINIMAL_GF)
        return;

    if(isDefeated())
        return;

    if(notificationHandles.empty())
        init();

    if(++NWF_COUNTER != config.player.SKIP_GF_CHANCE)
        return;
    NWF_COUNTER = 0;

    if(updateMilitarySettings)
    {
        aii.ChangeMilitary(settings.military_settings);
        // player.FillVisualSettings(settings);
        updateMilitarySettings = false;
    }

    if(updateItemDistribution)
    {
        aii.ChangeDistribution(settings.distribution);
        // player.FillVisualSettings(settings);
        updateItemDistribution = false;
    }

    ++COUNTER;
    view.update(COUNTER % 100 == 0);
    updateReserve();

    if(actionManager.getNumPendingActions() > 2000)
    {
        // This is purely a safeguard to avoid some endless loops we could get stuck in.
        chat("I have too many things going on right now, I'll start fresh in an effort to untangle my brain.");
        actionManager.clear();
    }

    SAY("\n-----\nNewGF %", gf);
    SAY("\n[GLOBAL] % [%]", view.globalResources, emergencyInfo);
    SAY("[Reserved| % boards | % stone | % miner | % founder]", reservedInventory[GD_BOARDS],
        reservedInventory[GD_STONES], reservedInventory[JOB_MINER], reservedInventory[JOB_MINTER]);
    SAY("[#Actions: %]", actions.size());
    SAY("[#Targets: %]", currentTargets.size());
    SAY("[#Geologists: %]", currentGeologistExpeditions.size());

    // Remove actions that are retired, thus done.
    discard_if(actions, [&](Action* action) {
        if(!action->isRetired())
            return false;
        delete action;
        return true;
    });

    adjustMines();
    adjustMilitaryBuildings(gf);

    auto storehouses = aii.GetStorehouses();
    unsigned numStorehouses = storehouses.size();
    if(numStorehouses > 1)
    {
        GoodType goods[2] = {GD_BOARDS, GD_STONES};
        unsigned averages[2] = {0, 0};
        for(nobBaseWarehouse* storehouse : storehouses)
        {
            for(unsigned i = 0; i < 2; ++i)
                averages[i] += storehouse->GetNumRealWares(goods[i]);
        }
        for(unsigned i = 0; i < 2; ++i)
            averages[i] /= numStorehouses;
        for(nobBaseWarehouse* storehouse : storehouses)
        {
            for(unsigned i = 0; i < 2; ++i)
            {
                if(storehouse->GetNumRealWares(goods[i]) > averages[i])
                    setInventorySetting(*storehouse, goods[i], EInventorySetting::STOP);
                else
                    setInventorySetting(*storehouse, goods[i], InventorySetting());
            }
        }
    }

    if(emergencyInfo.isEmergency(EmergencyInfo::WAR))
        if(handleWarEmergency(gf))
        {
            actionManager.execute();
            return;
        }

    if(emergencyInfo.any() && actionManager.getNumPendingActions())
    {
        SAY("Continue to work on % actions regarding %", actionManager.getNumPendingActions(), emergencyInfo);
        actionManager.execute();
        return;
    }

    identifyEmergencies();
    SAY("\n[GLOBAL] % [%]", view.globalResources, emergencyInfo);

    if(actionManager.getNumPendingActions())
    {
        actionManager.execute();
        return;
    }

    handleSpecialists(gf);
    if(handleRoads(gf))
    {
        actionManager.execute();
        return;
    }

    if(shouldExpand())
    {
        bool force =
          aii.GetMilitaryBuildings().empty()
          && (!emergencyInfo.isEmergency(EmergencyInfo::WAR) || emergencyInfo.isEmergency(EmergencyInfo::SPACE));
        expand(force);
    }

    if(emergencyInfo.any())
        if(handleEmergency())
        {
            actionManager.execute();
            return;
        }

    if(handleMilitaryBuildings(gf))
    {
        actionManager.execute();
        return;
    }

    handleMetalworks();
    handleDonkeyBreeding();

    handleLookouttowers();
    handleWoodcutter();
    handleFishery();
    buildMines();
    if(gf > 10000)
        handleArmories();
    handleFarms();

    if(gf > 5000 && !emergencyInfo.isEmergency(EmergencyInfo::SPACE))
        if(view.globalResources[CommodityInfo::WATER] < int(std::min(gf / 10000, 3u)))
            actions.insert(actionManager.constructBuilding(nullptr, BLD_WELL));

    if(gf > 6000)
    {
        std::set<unsigned> consumed;
        bool missedResources = false;
        auto CreateNewBuilding = [&](bool consume) {
            std::array<unsigned, ConceptInfo::LAST> indices;
            std::iota(indices.begin(), indices.end(), 0);
            std::shuffle(indices.begin(), indices.end(), rng);
            for(unsigned idx : indices)
            {
                int globalVal = view.globalResources[idx];
                SAY("Check what to do about % % (consume: %)", globalVal, idx, consume);
                if(globalVal == 0)
                    continue;
                if(consume && globalVal < 0)
                    continue;
                if(!consume && globalVal > 0)
                    continue;

                bool isEnd = false;
                auto it = getRandomElement(globalVal < 0 ? producerMap[idx] : consumerMap[idx], &isEnd);
                if(isEnd)
                {
                    SAY("No producer/consumer found for %", idx);
                    continue;
                }

                BuildingType buildingType = *it;
                // if(buildingType == BLD_CHARBURNER && view.globalResources[ResourceInfo::COAL]
                //&& view.globalResources[ResourceInfo::COAL] >= 16)
                // continue;
                const BuildingInfo& buildingInfo = BuildingInfo::get(buildingType);
                if(BuildingProperties::IsMine(buildingType))
                    continue;
                if(buildingType == BLD_FORESTER)
                    continue;

                if(buildingType == BLD_MINT)
                {
                    SAY("MINT MINT MINT % % %", lookupJobInStorehouses(JOB_MINTER), reservedInventory[JOB_MINTER],
                        player.GetToolsOrdered(GD_CRUCIBLE));
                    if(view.getNumBuildings(BLD_MINT) > view.getNumBuildings(BLD_IRONSMELTER) + 2)
                        continue;
                }
                // if(view.getNumBuildings(buildingInfo.getBuildingType()) && !ensureResources(buildingType))
                //{
                // SAY("Not enough resources for %", buildingType);
                // missedResources = true;
                // continue;
                //}
                unsigned numBuildings = 0;
                unsigned producitivity = 0;
                for(auto* building : aii.GetBuildings(buildingType))
                {
                    ++numBuildings;
                    producitivity += building->GetProductivity();
                }
                if(numBuildings && producitivity / numBuildings < 70)
                {
                    SAY("Existing producitivy of % is bad (%)", buildingType, producitivity / numBuildings);
                    continue;
                }
                if(numBuildings + 1 < view.getNumBuildings(buildingType))
                {
                    SAY("Waiting for % building sites of %", view.getNumBuildings(buildingType) - numBuildings,
                        buildingType);
                    continue;
                }

                // if(buildingType == BLD_WOODCUTTER || buildingType == BLD_SAWMILL)
                //{
                // if(!lookupJobInStorehouses(buildingInfo.getJob()))
                //{
                // SAY("No job for %", buildingType);
                // continue;
                //}
                //} else
                {
                    if(!ensureJob(buildingInfo.getJob()))
                    {
                        SAY("No job for %", buildingType);
                        continue;
                    }
                }

                int overproduction = 0;
                auto ProductionCB = [&](const BuildingEffect& production) {
                    if(production.isGlobal())
                    {
                        overproduction += view.globalResources[production.getThing()];
                        switch(production.getThing())
                        {
                            case CommodityInfo::FOOD: overproduction = 0; break;
                            case CommodityInfo::STONES: overproduction -= 4; break;
                            case CommodityInfo::BOARDS: overproduction -= 6; break;
                            case CommodityInfo::GRAIN:
                            case CommodityInfo::IRONORE:
                            case CommodityInfo::COAL:
                            case CommodityInfo::GOLD: --overproduction;
                            default: break;
                        };
                    }
                    return true;
                };
                buildingInfo.foreachProduction(ProductionCB);
                if(overproduction > 0)
                {
                    SAY("Do not issue % because of overproduction", buildingType);
                    continue;
                }

                unsigned shortage = 0;
                auto ConsumptionCB = [&](const BuildingEffect& consumption) {
                    if(!consumption.isGlobal())
                        return true;
                    // if(!view.getNumBuildings(buildingInfo.getBuildingType()))
                    // return true;
                    if(consumption.getThing() == CommodityInfo::BEER || consumption.getThing() == CommodityInfo::WATER)
                        return true;
                    if(consumption.getThing() == CommodityInfo::IRON && view.getNumBuildings(BLD_IRONSMELTER)
                       && view.globalResources[consumption.getThing()] >= -1)
                        return true;
                    if(consumption.getThing() == CommodityInfo::COAL && view.getNumBuildings(BLD_COALMINE)
                       && view.globalResources[consumption.getThing()] >= -1)
                        return true;
                    if(consumption.getThing() == CommodityInfo::FOOD
                       && view.globalResources[consumption.getThing()] >= -2)
                        return true;
                    auto producers = producerMap[consumption.getThing()];
                    if(view.globalResources[consumption.getThing()] == 0
                       && std::accumulate(producers.begin(), producers.end(), 0, [&](int s, BuildingType buildingType) {
                              return s + view.getNumBuildings(buildingType);
                          }) == 0)
                    {
                        ++shortage;
                        SAY("Do not issue % because of % shortage", buildingType, consumption.getThing());
                    }
                    if(view.globalResources[consumption.getThing()] < 0)
                    {
                        ++shortage;
                        SAY("Do not issue % because of % shortage", buildingType, consumption.getThing());
                    }
                    if(consumed.count(consumption.getThing()))
                    {
                        ++shortage;
                        SAY("Do not issue % because of % was already used by another building", buildingType,
                            consumption.getThing());
                    }
                    /*
                    switch(consumption.getThing())
                    {
                        case CommodityInfo::GOLD:
                        case CommodityInfo::IRON:
                            shortage = true;
                            SAY("Do not issue % because of % cannot be produced", buildingType, consumption.getThing());
                            break;
                        default: break;
                    };
                    */
                    return true;
                };
                // if (!consume)
                buildingInfo.foreachConsumption(ConsumptionCB);
                if(shortage)
                    continue;

                auto ConsumptionTrackerCB = [&](const BuildingEffect& consumption) {
                    if(consumption.isGlobal())
                        consumed.insert(consumption.getThing());
                    return true;
                };
                buildingInfo.foreachConsumption(ConsumptionTrackerCB);

                SAY("Issue % because of % %", buildingType, globalVal, idx);
                actions.insert(actionManager.constructBuilding(nullptr, buildingType));
                reserveForBuilding(buildingType, 0, 0);
            }
        };
        CreateNewBuilding(/* consume */ false);
        if(!missedResources || checkChance(5))
            CreateNewBuilding(/* consume */ true);
    }

    if(checkChance(33))
        createSecondaryRoads(gf);

    actionManager.execute();

    // actions.insert(actionManager.constructBuilding(nullptr, BLD_FARM));
    // actions.insert(actionManager.constructBuilding(nullptr, BLD_FARM));
    // actions.insert(actionManager.constructBuilding(nullptr, BLD_FORESTER));
}

bool PlayerJD::isToolRequested(GoodType tool)
{
    unsigned toolIdx = tool /* BEER */ - 1 + /* WATER */ (tool > GD_SCYTHE ? -2 : 0);
    if(toolSettings[toolIdx] > 0)
    {
        //--view.globalResources[ConceptInfo::METALWORKS];
        return true;
    }
    return false;
}

bool PlayerJD::requestTool(GoodType tool, unsigned char amount)
{
    if(tool == GD_NOTHING)
        return true;
    SAY("Request % of tool %, #metalworks: %", amount, tool, view.getNumBuildings(BLD_METALWORKS));
    unsigned toolIdx = tool /* BEER */ - 1 + /* WATER */ (tool > GD_SCYTHE ? -2 : 0);
    --view.globalResources[ConceptInfo::METALWORKS];
    if(isToolRequested(tool))
        return false;
    // printf("Request %i of tool %i, #metalworks: %i", amount, tool, view.getNumBuildings(BLD_METALWORKS));

    ToolSettings request{};
    request[toolIdx] = 1;
    player.ChangeToolOrderVisual(toolIdx, 1);
    aii.ChangeTools(ToolSettings{}, (int8_t*)request.data());
    ++toolSettings[toolIdx];
    ++toolsRequested;
    SAY("Requested % of % (%)", 1, tool, toolIdx);
    return true;
}

bool PlayerJD::ensureTool(boost::optional<GoodType> tool, unsigned char amount, bool inInventory)
{
    if(!tool || *tool == GD_RODANDLINE || *tool == GD_NOTHING)
        return true;
    unsigned available = inInventory ? lookupGoodInInventory(*tool) : lookupGoodInStorehouses(*tool);
    SAY("Ensure Tool %, got %, want %, looked in %", tool, available, amount,
        inInventory ? "inventory" : "storehouses");
    if(available >= amount)
        return true;
    //--view.globalResources[ConceptInfo::METALWORKS];
    // return aii.GetBuildings(BLD_METALWORKS).size();
    return requestTool(*tool, amount - available);
}
bool PlayerJD::ensureJob(boost::optional<Job> job, unsigned char amount, bool inInventory)
{
    if(!job)
        return true;
    unsigned available = inInventory ? lookupJobInInventory(*job, /* includeTools */ false) :
                                       lookupJobInStorehouses(*job, /* includeTools */ false);
    SAY("Ensure Job %, got %, want %, looked in %", job, available, amount, inInventory ? "inventory" : "storehouses");
    if(available >= amount)
        return true;
    return ensureTool(job2tool(*job), amount - available, inInventory);
}
bool PlayerJD::ensureResources(BuildingType buildingType)
{
    auto& buildingCosts = BUILDING_COSTS[aii.GetNation()][buildingType];
    if(lookupGoodInStorehouses(GD_BOARDS) < buildingCosts.boards)
        return false;
    if(lookupGoodInStorehouses(GD_STONES) < buildingCosts.stones)
        return false;
    return true;
}

static unsigned substractReserved(unsigned available, unsigned reserved)
{
    if(available <= reserved)
        return 0;
    return available - reserved;
}
unsigned PlayerJD::lookupGoodInInventory(GoodType good)
{
    unsigned available = aii.GetInventory()[good];
    unsigned reserved = reservedInventory[good];
    return substractReserved(available, reserved);
}
unsigned PlayerJD::lookupGoodInStorehouses(GoodType good)
{
    unsigned available = 0;
    for(auto* storehouse : aii.GetStorehouses())
        available += storehouse->GetNumRealWares(good);
    unsigned reserved = reservedInventory[good];
    return substractReserved(available, reserved);
}
unsigned PlayerJD::lookupJobInInventory(Job job, bool includeTools)
{
    auto tool = job2tool(job);
    unsigned available = aii.GetInventory()[job] + (includeTools && tool ? lookupGoodInInventory(*tool) : 0);
    unsigned reserved = reservedInventory[job];
    return substractReserved(available, reserved);
}
unsigned PlayerJD::lookupJobInStorehouses(Job job, bool includeTools)
{
    auto tool = job2tool(job);
    unsigned available = 0;
    for(auto* storehouse : aii.GetStorehouses())
    {
        assert(storehouse);
        if(storehouse)
        {
            available += storehouse->GetNumRealFigures(job);
            if(includeTools && tool)
                available += storehouse->GetNumRealWares(*tool);
        }
    }
    unsigned reserved = reservedInventory[job];
    return substractReserved(available, reserved);
}

bool PlayerJD::identifyEmergencies()
{
    bool wasWar = emergencyInfo.isEmergency(EmergencyInfo::WAR);

    emergencyInfo.resetEmergencies();
    if(getNumMilitaryBuildingSites() == 0)
    {
        if(view.potentialBuildingSites.size() < 5)
            emergencyInfo.addEmergency(EmergencyInfo::SPACE);
        // if(view.getNumBuildings(BLD_WOODCUTTER) + view.getNumBuildings(BLD_QUARRY) < 4)
        // emergencyInfo.addEmergency(EmergencyInfo::SPACE);
    }

    // if(view.globalResources[CommodityInfo::BOARDS] < 4)
    // emergencyInfo.addEmergency(EmergencyInfo::BOARD);
    unsigned numForester = aii.GetBuildings(BLD_FORESTER).size();
    if((lookupGoodInStorehouses(GD_WOOD) + lookupGoodInStorehouses(GD_BOARDS)) < (100 - (40 * numForester)))
    {
        if(aii.GetBuildings(BLD_SAWMILL).size() < 2)
            emergencyInfo.addEmergency(EmergencyInfo::BOARD);
        if(aii.GetBuildings(BLD_WOODCUTTER).size() < 4)
            emergencyInfo.addEmergency(EmergencyInfo::BOARD);
    }

    if(view.globalResources[CommodityInfo::STONES] < 3)
        emergencyInfo.addEmergency(EmergencyInfo::STONE);
    if(view.globalResources[CommodityInfo::STONES] < 5
       && lookupGoodInStorehouses(GD_STONES) < Config::Actions::EMERGENCY_MIN_STONES_IN_STORE)
        emergencyInfo.addEmergency(EmergencyInfo::STONE);
    if(view.getNumBuildings(BLD_QUARRY) + view.getNumBuildings(BLD_GRANITEMINE) < 2
       && (view.globalResources[ResourceInfo::GRANITE] || view.globalResources[ResourceInfo::STONE]))
        emergencyInfo.addEmergency(EmergencyInfo::STONE);

    // if(view.globalResources[ConceptInfo::ENEMY] > 0) {
    // emergencyInfo.addEmergency(EmergencyInfo::WAR);
    //} else
    {
        for(nobMilitary* militaryBuilding : aii.GetMilitaryBuildings())
        {
            switch(militaryBuilding->GetFrontierDistance())
            {
                case nobMilitary::DIST_FAR: break;
                case nobMilitary::DIST_MID: emergencyInfo.addEmergency(EmergencyInfo::WAR); break;
                case nobMilitary::DIST_HARBOR: break;
                case nobMilitary::DIST_NEAR: emergencyInfo.addEmergency(EmergencyInfo::WAR); break;
            };
            if(militaryBuilding->IsUnderAttack())
                emergencyInfo.addEmergency(EmergencyInfo::WAR);
        }
    }

    if(wasWar && !emergencyInfo.isEmergency(EmergencyInfo::WAR))
        resetMilitarySettings();
    if(!wasWar && emergencyInfo.isEmergency(EmergencyInfo::WAR))
    {
        settings.military_settings[4] = 0;
        updateMilitarySettings = true;
    }

    return emergencyInfo.any();
}

void PlayerJD::setInventorySetting(nobBaseWarehouse& storehouse, GoodType good, InventorySetting state)
{
    aii.SetInventorySetting(storehouse.GetPos(), good, state);
    // storehouse.SetInventorySetting(false, good, state);
    storehouse.SetInventorySettingVisual(false, good, state);
}
void PlayerJD::setInventorySetting(nobBaseWarehouse& storehouse, Job job, InventorySetting state)
{
    aii.SetInventorySetting(storehouse.GetPos(), job, state);
    // storehouse.SetInventorySetting(false, job, state);
    storehouse.SetInventorySettingVisual(true, job, state);
}

void PlayerJD::resetMilitarySettings()
{
    settings.military_settings = MILITARY_SETTINGS_SCALE;
    settings.military_settings[1] = 1;
    updateMilitarySettings = true;
}

void PlayerJD::adjustMilitaryBuildings(unsigned gf)
{
    COUNTER_GUARD(ADJUST_MILITARY_BUILDINGS, );
    RAIISayTimer RST(__PRETTY_FUNCTION__);

    bool atWar = emergencyInfo.isEmergency(EmergencyInfo::WAR);
    bool sendPrivatesBack = !atWar && !lookupJobInStorehouses(JOB_PRIVATE) && lookupJobInStorehouses(JOB_GENERAL);
    unsigned coinsInBldgs = 0;

    auto Cmp = [&](const nobMilitary* lhs, const nobMilitary* rhs) {
        auto LHSTroops = lhs->GetTroops();
        auto RHSTroops = rhs->GetTroops();
        unsigned ranksLHS[5] = {0, 0, 0, 0, 0}, ranksRHS[5] = {0, 0, 0, 0, 0};
        for(nofPassiveSoldier* lhsTroop : LHSTroops)
            ++ranksLHS[lhsTroop->GetRank()];
        for(nofPassiveSoldier* rhsTroop : RHSTroops)
            ++ranksRHS[rhsTroop->GetRank()];
        unsigned concurrentLHS = 0, concurrentRHS = 0, totalLHS = 0, totalRHS = 0;
        for(unsigned i = 0; i < 5; ++i)
        {
            concurrentLHS += ranksLHS[i] != 0;
            concurrentRHS += ranksRHS[i] != 0;
            if(i != 4)
            {
                totalLHS += ranksLHS[i];
                totalRHS += ranksRHS[i];
            }
        }
        if(concurrentLHS != concurrentRHS)
            return concurrentLHS > concurrentRHS;
        if(totalLHS != totalRHS)
            return totalLHS > totalRHS;
        int disDiff = lhs->GetFrontierDistance() - rhs->GetFrontierDistance();
        if(disDiff)
            return disDiff > 0;
        if(lhs->IsGoldDisabled() != rhs->IsGoldDisabled())
            return rhs->IsGoldDisabled();
        return lhs < rhs;
    };

    auto militaryBuildingList = aii.GetMilitaryBuildings();
    std::vector<nobMilitary*> militaryBuildings(militaryBuildingList.begin(), militaryBuildingList.end());
    std::sort(militaryBuildings.begin(), militaryBuildings.end(), Cmp);

    std::vector<nobMilitary*> frontBuildings;

    unsigned numMilitary = 0;
    bool newBuildings = false;
    bool nearEnemyBldgsFull = true;
    for(nobMilitary* militaryBuilding : militaryBuildings)
    {
        ++numMilitary;

        coinsInBldgs += militaryBuilding->GetNumCoins();

        bool isFront = false;
        auto frontierDistance = militaryBuilding->GetFrontierDistance();
        switch(frontierDistance)
        {
            case nobMilitary::DIST_FAR:
            case nobMilitary::DIST_HARBOR: break;
            case nobMilitary::DIST_MID:
            case nobMilitary::DIST_NEAR: isFront = true; break;
        };
        if(isFront)
            frontBuildings.push_back(militaryBuilding);

        if(frontierDistance == nobMilitary::DIST_NEAR)
        {
            nearEnemyBldgsFull &= militaryBuilding->GetNumTroops() == militaryBuilding->GetMaxTroopsCt();
        }
        newBuildings |= militaryBuilding->IsNewBuilt();

        // MapPoint pt = militaryBuilding->GetPos();

        unsigned numTroops = militaryBuilding->GetTroops().size();
        auto it = militaryBuilding->GetTroops().begin();
        auto rit = militaryBuilding->GetTroops().rbegin();
        if(checkChance(50) && numTroops && (*rit)->GetHitpoints() < 3 + (*rit)->GetRank())
        {
            militaryBuilding->SendSoldiersHome();
        }
        if(checkChance(50) && sendPrivatesBack && numTroops > 1 && (*(++it))->GetRank() == 0)
        {
            militaryBuilding->SendSoldiersHome();
            militaryBuilding->SendSoldiersHome();
            militaryBuilding->SendSoldiersHome();
            militaryBuilding->SendSoldiersHome();
        }
    }

    // auto oldCoinBldgs = coinBldgs;
    coinBldgs.clear();

    unsigned freeCoins = lookupGoodInInventory(GD_COINS) - coinsInBldgs;
    // (COUNTER % config.counters.COIN_ADJUSTMENT != 0) &&
    if(freeCoins)
    {
        auto HandleMilitaryBuilding = [&](nobMilitary* militaryBuilding) {
            int numTroops = militaryBuilding->GetTroops().size();
            auto frontierDistance = militaryBuilding->GetFrontierDistance();
            bool isFront = frontierDistance == nobMilitary::DIST_MID || frontierDistance == nobMilitary::DIST_NEAR;
            if(numTroops > (3 - isFront) && (*(++militaryBuilding->GetTroops().begin()))->GetRank() <= (2 + isFront))
            {
                bool enable =
                  coinBldgs.count(militaryBuilding) || lookupGoodInStorehouses(GD_COINS) >= (5 * coinBldgs.size());
                if(enable)
                    coinBldgs.insert(militaryBuilding);
            }
        };
        for(nobMilitary* militaryBuilding : militaryBuildings)
        {
            HandleMilitaryBuilding(militaryBuilding);
            if(coinBldgs.size() * 5 > freeCoins)
                break;
        }
    }

    // for(nobMilitary* militaryBuilding : oldCoinBldgs)
    //{
    // unsigned numTroops = militaryBuilding->GetTroops().size();
    // auto frontierDistance = militaryBuilding->GetFrontierDistance();
    // bool isFront = frontierDistance == nobMilitary::DIST_MID || frontierDistance == nobMilitary::DIST_NEAR;
    // if(numTroops > (3 - isFront) && (*(++militaryBuilding->GetTroops().begin()))->GetRank() <= 2)
    // coinBldgs.insert(militaryBuilding);
    //}

    for(nobMilitary* militaryBuilding : militaryBuildings)
    {
        bool enable = coinBldgs.count(militaryBuilding);
        militaryBuilding->SetCoinsAllowed(enable);
        if(militaryBuilding->IsGoldDisabled() != militaryBuilding->IsGoldDisabledVirtual())
            militaryBuilding->ToggleCoinsVirtual();
    }

    unsigned numStorehouses = 0;
    unsigned numPrivatesInStorehouses = 0;
    unsigned numSoldiersInStorehouses = 0;
    for(nobBaseWarehouse* storehouse : aii.GetStorehouses())
    {
        ++numStorehouses;
        numPrivatesInStorehouses += storehouse->GetNumRealFigures(JOB_PRIVATE);
        numSoldiersInStorehouses += storehouse->GetNumSoldiers();
        resetInventorySetting(*storehouse, JOB_PRIVATE);
        resetInventorySetting(*storehouse, JOB_PRIVATEFIRSTCLASS);
        resetInventorySetting(*storehouse, JOB_SERGEANT);
        resetInventorySetting(*storehouse, JOB_GENERAL);
    }

    if(numStorehouses > 1)
    {
        std::set<nobBaseWarehouse*> noGeneralStorehouses;
        for(nobMilitary* militaryBuilding : coinBldgs)
        {
            auto* storehouse = player.FindWarehouse(*militaryBuilding->GetFlag(), FW::NoCondition(), true, false);
            if(!storehouse)
                continue;
            // aii.SetInventorySetting(storehouse->GetPos(), JOB_GENERAL, EInventorySetting::STOP);
            noGeneralStorehouses.insert(storehouse);
            if(!militaryBuilding->HasMaxRankSoldier() || !storehouse->GetNumSoldiers()
               || militaryBuilding->GetFrontierDistance() == nobMilitary::DIST_NEAR)
                continue;
            setInventorySetting(*storehouse, JOB_PRIVATE, EInventorySetting::COLLECT);
            if(!storehouse->GetNumRealFigures(JOB_GENERAL))
                militaryBuilding->SendSoldiersHome();
            if(storehouse->GetNumRealFigures(JOB_GENERAL) < storehouse->GetNumSoldiers()
               && settings.military_settings[1] <= 1)
                militaryBuilding->SendSoldiersHome();
            // else if(militaryBuilding->GetFrontierDistance() != nobMilitary::DIST_NEAR)
            // militaryBuilding->SendSoldiersHome();
        }
        for(nobMilitary* militaryBuilding : frontBuildings)
        {
            auto* storehouse = player.FindWarehouse(*militaryBuilding->GetFlag(), FW::NoCondition(), true, false);
            if(!storehouse)
                continue;
            noGeneralStorehouses.erase(storehouse);
            setInventorySetting(*storehouse, GD_COINS, EInventorySetting::COLLECT);
            setInventorySetting(*storehouse, JOB_PRIVATEFIRSTCLASS, EInventorySetting::COLLECT);
            setInventorySetting(*storehouse, JOB_SERGEANT, EInventorySetting::COLLECT);
            setInventorySetting(*storehouse, JOB_OFFICER, EInventorySetting::COLLECT);
            setInventorySetting(*storehouse, JOB_GENERAL, EInventorySetting::COLLECT);
        }
        if(frontBuildings.empty())
        {
            for(nobBaseWarehouse* storehouse : aii.GetStorehouses())
                if(!noGeneralStorehouses.count(storehouse))
                    setInventorySetting(*storehouse, JOB_GENERAL, EInventorySetting::COLLECT);
        }
    }

    if(militarySettingsLastChangeGF + 500 > gf)
        return;

    if((numSoldiersInStorehouses < 6 && atWar && !nearEnemyBldgsFull) || (!numSoldiersInStorehouses && newBuildings)
       || (settings.military_settings[1] <= 1 && sendPrivatesBack && numPrivatesInStorehouses < 6))
    {
        if(settings.military_settings[4] > 0)
        {
            settings.military_settings[4] -= 1;
            militarySettingsLastChangeGF = gf;
            updateMilitarySettings = true;
        } else if(settings.military_settings[5] > 0)
        {
            settings.military_settings[5] -= 1;
            militarySettingsLastChangeGF = gf;
            updateMilitarySettings = true;
        } else if(settings.military_settings[5] == 0)
        {
            if(settings.military_settings[6] > 0)
            {
                settings.military_settings[6] -= 1;
                militarySettingsLastChangeGF = gf;
                updateMilitarySettings = true;
            }
            if(settings.military_settings[7] > 4)
            {
                settings.military_settings[7] -= 1;
                militarySettingsLastChangeGF = gf;
                updateMilitarySettings = true;
            }
        }
    }

    for(unsigned u = 7; numSoldiersInStorehouses && u >= 4; --u)
    {
        if(numSoldiersInStorehouses && settings.military_settings[u] < MILITARY_SETTINGS_SCALE[u])
        {
            if(u != 4 || (numSoldiersInStorehouses > numMilitary && !atWar))
            {
                settings.military_settings[u] += 1;
                militarySettingsLastChangeGF = gf;
                updateMilitarySettings = true;
                break;
            }
        }
    }
}

bool PlayerJD::handleMilitaryBuildings(unsigned gf)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);

    auto militaryBuildingList = aii.GetMilitaryBuildings();
    bool issuedCommands = false;
    for(nobMilitary* militaryBuilding : militaryBuildingList)
    {
        bool isSmall = militaryBuilding->GetMaxTroopsCt() <= 3;
        if(isSmall && militaryBuilding->IsUseless())
        {
            // const auto &militaryBuildingSites aii.GetBuildingSites():
            // if (std::all_of(militaryBuildingSites.begin(), militaryBuildingSites.end(), [&])
            aii.DestroyBuilding(militaryBuilding);
            issuedCommands = true;
            continue;
        }

        bool isFront = false;
        auto frontierDistance = militaryBuilding->GetFrontierDistance();
        switch(frontierDistance)
        {
            case nobMilitary::DIST_FAR: break;
            case nobMilitary::DIST_HARBOR: break;
            case nobMilitary::DIST_MID: isFront = true; break;
            case nobMilitary::DIST_NEAR: isFront = true; break;
        };

        MapPoint pt = militaryBuilding->GetPos();
        unsigned& lastIssueGF = catapultAndStorehouseIssued[pt];
        if(isFront && !militaryBuilding->IsNewBuilt() && lastIssueGF + 2000 < gf)
        {
            ensureJob(JOB_SCOUT);
            actionManager.constructBuilding(nullptr, BLD_LOOKOUTTOWER, militaryBuilding->GetPos(),
                                            militaryBuilding->GetMilitaryRadius());
            actionManager.constructBuilding(nullptr, BLD_STOREHOUSE, militaryBuilding->GetPos(),
                                            militaryBuilding->GetMilitaryRadius());
            issuedCommands = true;
            lastIssueGF = gf;
        }

        unsigned connectedRoads = view.getConnectedRoads(pt);
        if(connectedRoads < 2)
        {
            issuedCommands = createSecondaryRoad(pt);
        }
    }

    return issuedCommands;
}

void PlayerJD::handleMetalworks()
{
    toolsRequested = 0;
    for(unsigned u = 0; u < NUM_TOOLS; ++u)
        toolsRequested += player.GetToolsOrdered(u);
    // printf("toolsRequested %i\n", toolsRequested);
    for(nobUsual* building : aii.GetBuildings(BLD_METALWORKS))
    {
        building->SetProductionEnabled(toolsRequested > 0);
        if(building->IsProductionDisabled() != building->IsProductionDisabledVirtual())
            building->ToggleProductionVirtual();
    }

    if(!toolsRequested)
        return;

    if(!view.getNumBuildings(BLD_METALWORKS))
    {
        actions.insert(actionManager.constructBuilding(nullptr, BLD_METALWORKS));
        return;
    }

    if(toolsRequested > 8 * view.getNumBuildings(BLD_METALWORKS)
       && view.getNumBuildings(BLD_IRONSMELTER) + 1
            > view.getNumBuildings(BLD_ARMORY) + view.getNumBuildings(BLD_METALWORKS))
    {
        if(ensureJob(JOB_METALWORKER) && ensureJob(JOB_IRONFOUNDER))
        {
            Action* groupAction = new GroupAction(GroupAction::EXECUTE);
            actionManager.appendAction(groupAction);
            groupAction->addSubAction(new BuildingRequest(actionManager, BLD_IRONSMELTER));
            groupAction->addSubAction(new BuildingRequest(actionManager, BLD_METALWORKS));
            actions.insert(groupAction);
        }
    }
}
void PlayerJD::handleDonkeyBreeding()
{
    bool enable = lookupJobInStorehouses(JOB_PACKDONKEY) < 10;
    for(nobUsual* building : aii.GetBuildings(BLD_DONKEYBREEDER))
        building->SetProductionEnabled(enable);
}

void PlayerJD::handleArmories()
{
    bool enabled = lookupGoodInStorehouses(GD_COINS) < 15;
    for(auto* building : aii.GetBuildings(BLD_MINT))
        building->SetProductionEnabled(enabled);
    enabled = lookupGoodInStorehouses(GD_BEER) < 15;
    for(auto* building : aii.GetBuildings(BLD_BREWERY))
        building->SetProductionEnabled(enabled);

    if(checkChance(50))
        return;

    if(view.globalResources[CommodityInfo::GOLD] && !view.getNumBuildings(BLD_MINT))
    {
        if(ensureJob(JOB_MINTER))
            actions.insert(actionManager.constructBuilding(nullptr, BLD_MINT));
    }

    if(view.globalResources[CommodityInfo::COAL] <= 1 || view.globalResources[CommodityInfo::IRONORE] <= 0
       || view.globalResources[CommodityInfo::BEER] <= -1)
        return;

    if(!ensureJob(JOB_IRONFOUNDER) || !ensureJob(JOB_ARMORER))
        return;

    Action* groupAction = new GroupAction(GroupAction::EXECUTE);
    actionManager.appendAction(groupAction);
    groupAction->addSubAction(new BuildingRequest(actionManager, BLD_IRONSMELTER));
    groupAction->addSubAction(new BuildingRequest(actionManager, BLD_ARMORY));
    actions.insert(groupAction);

    // Pending = true;
    // addPostActionHook(groupAction, [&]() { Pending = false; });
}

void PlayerJD::handleLookouttowers()
{
    if(checkChance(50))
        return;
    if(!view.getNumBuildings(BLD_LOOKOUTTOWER) && !view.getNumBuildings(BLD_CATAPULT))
        return;
    RAIISayTimer RST(__PRETTY_FUNCTION__);

    std::set<MapPoint> enemeyBuildings;
    for(auto& enemyMilitaryBuildings : view.enemyMilitaryBuildings)
        for(auto* enemyMilitaryBuilding : enemyMilitaryBuildings)
            // if (!enemyMilitaryBuilding->IsNewBuilt())
            enemeyBuildings.insert(enemyMilitaryBuilding->GetPos());

    auto CheckAndDestroy = [&](const MapPoint& pt, unsigned distance) {
        bool buildingSite = aii.IsObjectTypeOnNode(pt, NodalObjectType::NOP_BUILDINGSITE);
        int owned = 0, unowned = 0, enemy = 0, enemyMil = 0, closestUnowned = 1000;
        for(const MapPoint& circlePt : view.GetPointsInRadiusWithCenter(pt, distance))
        {
            if(view[circlePt].getValue(ConceptInfo::OWNED))
            {
                ++owned;
            } else
            {
                ++unowned;
                closestUnowned = std::min(closestUnowned, int(distance));
            }
            enemy += view[circlePt].getValue(ConceptInfo::ENEMY);
            if(enemeyBuildings.count(circlePt))
            {
                auto IsOwned = [&](MapPoint pt, unsigned) { return view[pt].getValue(ConceptInfo::OWNED); };
                if(!view.CheckPointsInRadius(circlePt, 6, IsOwned, /* includePt */ false))
                    ++enemyMil;
            }
        }
        enemyMil += buildingSite;
        //(enemyMil == 0 || checkChance(50)) &&
        unowned += enemy * 10;
        unowned += enemyMil * 50;
        if((unowned == 0 || unowned * 100 / owned < 20 || (closestUnowned > 10 && !enemyMil)))
            aii.DestroyBuilding(pt);
    };
    for(auto& pair : view.buildings[BLD_LOOKOUTTOWER])
        CheckAndDestroy(pair.first, VISUALRANGE_LOOKOUTTOWER);
    for(auto& pair : view.buildings[BLD_CATAPULT])
        CheckAndDestroy(pair.first, 13);
}

void PlayerJD::handleFarms()
{
    if(view.globalResources[ResourceInfo::PLANTSPACE] <= 0 || view.globalResources[CommodityInfo::GRAIN] > 2)
        return;
    if(!ensureJob(JOB_FARMER) || !ensureResources(BLD_FARM))
        return;
    actions.insert(actionManager.constructBuilding(nullptr, BLD_FARM));
}

struct MineInfo
{
    const BuildingType mineBuildingType;
    const ResourceInfo::Kind resourceKind;
    const CommodityInfo::Kind resultKind;
    const GoodType resultGood;
    const BuildingType equilibriumBuildingType0 = BLD_NOTHING9, equilibriumBuildingType1 = BLD_NOTHING9;
    const BuildingType antiEquilibriumBuildingType = BLD_NOTHING9;
};
// BuildingType mineBuildingTypes[3] = {BLD_COALMINE, BLD_IRONMINE, BLD_GOLDMINE};

static MineInfo mineInfos[3] = {
  {BLD_GOLDMINE, ResourceInfo::GOLD, CommodityInfo::GOLD, GD_GOLD, BLD_COALMINE, BLD_NOTHING9, BLD_IRONMINE},
  {BLD_COALMINE, ResourceInfo::COAL, CommodityInfo::COAL, GD_COAL, BLD_IRONMINE, BLD_GOLDMINE},
  {BLD_IRONMINE, ResourceInfo::IRONORE, CommodityInfo::IRONORE, GD_IRONORE, BLD_COALMINE, BLD_NOTHING9, BLD_GOLDMINE}};

void PlayerJD::adjustMines()
{
    COUNTER_GUARD(ADJUST_MINE, );

    RAIISayTimer RST(__PRETTY_FUNCTION__);

    std::vector<std::pair<unsigned, unsigned>> waresAndDistribution;
    if(view.getNumBuildings(BLD_COALMINE))
        waresAndDistribution.push_back({lookupGoodInStorehouses(GD_COAL), distribution::FOOD_2_COAL_MINE});
    if(view.getNumBuildings(BLD_IRONMINE))
        waresAndDistribution.push_back({lookupGoodInStorehouses(GD_IRONORE), distribution::FOOD_2_IRON_MINE});
    if(view.getNumBuildings(BLD_GOLDMINE))
        waresAndDistribution.push_back({lookupGoodInStorehouses(GD_GOLD), distribution::FOOD_2_GOLD_MINE});
    if(view.getNumBuildings(BLD_GRANITEMINE))
        waresAndDistribution.push_back({lookupGoodInStorehouses(GD_STONES), distribution::FOOD_2_GRANITE_MINE});

    std::sort(waresAndDistribution.begin(), waresAndDistribution.end());
    int val = 10;
    bool anyZero = false, allZero = true;
    unsigned maxResources = 0;
    for(auto& pair : waresAndDistribution)
    {
        settings.distribution[pair.second] = val;
        // printf("DISTRIBUTION %i -> %i, because %i in storehouse\n", pair.second, val, pair.first);
        val = std::max(1, int(val - (14 / waresAndDistribution.size())) - 1);
        anyZero |= (pair.first == 0);
        allZero &= (pair.first == 0);
        maxResources = std::max(maxResources, pair.first);
    }
    updateItemDistribution = true;

    if(anyZero && !allZero)
    {
        for(MineInfo mineInfo : mineInfos)
        {
            unsigned stored = lookupGoodInStorehouses(mineInfo.resultGood);
            bool enable = (stored < 10) || (stored < maxResources / 4);
            nobUsual* last = nullptr;
            for(auto* mine : aii.GetBuildings(mineInfo.mineBuildingType))
            {
                mine->SetProductionEnabled(enable);
                last = mine;
            }
            if(last && !enable)
                last->SetProductionEnabled(true);
        }
    }

    if(view.getNumBuildings(BLD_QUARRY) > 2)
        for(auto* mine : aii.GetBuildings(BLD_GRANITEMINE))
            mine->SetProductionEnabled(false);
}

void PlayerJD::buildMines()
{
    if(checkChance(20))
        return;
    RAIISayTimer RST(__PRETTY_FUNCTION__);

    if(view.globalResources[CommodityInfo::FOOD] < -2)
        return;
    if(view.globalResources[ResourceInfo::COAL] <= 0 && view.globalResources[ResourceInfo::GOLD] <= 0
       && view.globalResources[ResourceInfo::IRONORE] <= 0)
        return;
    if(!ensureJob(JOB_MINER))
        return;

    for(MineInfo mineInfo : mineInfos)
    {
        if(view.globalResources[mineInfo.resourceKind] < BuildingInfo::get(mineInfo.mineBuildingType)
                                                           .getConsumption<ResourceInfo>(mineInfo.resourceKind)
                                                           .getAmount())
            continue;
        if(view.globalResources[mineInfo.resultKind] > 2)
            continue;
        if(mineInfo.antiEquilibriumBuildingType != BLD_NOTHING9
           && view.getNumBuildings(mineInfo.antiEquilibriumBuildingType) + 3
                <= view.getNumBuildings(mineInfo.mineBuildingType))
            continue;
        int numMines = view.buildings[mineInfo.mineBuildingType].size();
        int numEquilibriumMines = 0;
        if(mineInfo.equilibriumBuildingType0 != BLD_NOTHING9)
            numEquilibriumMines += view.buildings[mineInfo.equilibriumBuildingType0].size();
        if(mineInfo.equilibriumBuildingType1 != BLD_NOTHING9)
            numEquilibriumMines += view.buildings[mineInfo.equilibriumBuildingType1].size();
        if(mineInfo.antiEquilibriumBuildingType != BLD_NOTHING9)
            numEquilibriumMines -= view.buildings[mineInfo.antiEquilibriumBuildingType].size();
        if(numMines >= numEquilibriumMines + 2)
            continue;
        SAY("numMines %, numEquilibriumMines %, issue %", numMines, numEquilibriumMines, mineInfo.mineBuildingType);
        Action* action = actionManager.constructBuilding(nullptr, mineInfo.mineBuildingType);
        // addPostActionGlobalAdjustmentHook(action, mineInfo.resourceKind);
        actions.insert(action);
    }
}

void PlayerJD::handleFishery()
{
    if(checkChance(50))
        return;
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    struct Description
    {
        const BuildingType buildingType;
        const ResourceInfo::Kind resourceInfo;
        const Job job;
        const GoodType tool;
    };

    Description descriptions[2] = {{BLD_FISHERY, ResourceInfo::FISH, JOB_FISHER, GD_RODANDLINE},
                                   {BLD_HUNTER, ResourceInfo::ANIMAL, JOB_HUNTER, GD_BOW}};

    for(const Description& description : descriptions)
    {
        if(view.globalResources[description.resourceInfo] <= 0)
            continue;

        if(!ensureJob(description.job))
            continue;

        Action* action = actionManager.constructBuilding(nullptr, description.buildingType);
        // addPostActionGlobalAdjustmentHook(action, description.resourceInfo);
        actions.insert(action);
    }
}

static Action* woodCutterOrAlternativesAction(View& view, ActionManager& actionManager, EmergencyInfo& emergencyInfo)
{
    // Either build a woodcutter, if a suitable space is available, or build a forester and a woodcutter, or, if that
    // did not work, expand.
    Action* groupAction = new GroupAction(GroupAction::ANY);
    actionManager.appendAction(groupAction);

    groupAction->addSubAction(new BuildingRequest(actionManager, BLD_WOODCUTTER));

    auto ForesterAndWoodcutters = [&]() {
        if(emergencyInfo.isEmergency(EmergencyInfo::SPACE))
            return;
        Action* foresterAction = new GroupAction(GroupAction::EXECUTE);
        groupAction->addSubAction(foresterAction);
        foresterAction->addSubAction(new BuildingRequest(actionManager, BLD_FORESTER));
        foresterAction->addSubAction(new BuildingRequest(actionManager, BLD_WOODCUTTER));
        // foresterAction->addSubAction(new BuildingRequest(actionManager, BLD_WOODCUTTER));
    };

    // unsigned numWoodcutters = view.getNumBuildings(BLD_WOODCUTTER);
    // if(numWoodcutters < 3)
    if(view.getNumBuildings(BLD_SAWMILL) > view.getNumBuildings(BLD_FORESTER))
        ForesterAndWoodcutters();

    if(!emergencyInfo.isEmergency(EmergencyInfo::SPACE))
    {
        if(emergencyInfo.isEmergency(EmergencyInfo::STONE))
        {
            groupAction->addSubAction(new BuildingRequest(actionManager, BLD_BARRACKS));
        } else
        {
            groupAction->addSubAction(new BuildingRequest(actionManager, BLD_WATCHTOWER));
            groupAction->addSubAction(new BuildingRequest(actionManager, BLD_GUARDHOUSE));
        }
    }

    // if(numWoodcutters >= 3)
    // ForesterAndWoodcutters();

    return groupAction;
}

bool PlayerJD::handleRoads(unsigned gf)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    SAY("Handle roads, should reconnect: %", shouldReconnect);

#if 0
    if (!shouldReconnect && checkChance(1)) {
      bool isEnd;
      for (unsigned u = 0; !shouldReconnect && u < 30; ++u ) {
          auto it = getRandomElement(view.flagPts, &isEnd);
          if(!isEnd)
              shouldReconnect |= view.destroyFlag(*it, 6, /* DestroyBuilding */ false, /* lastDir */ -1, /* rebuildRoads */ true);
      }
      if(shouldReconnect)
        return true;
    }
#endif

    // if(!shouldReconnect && (!view.globalResources[ConceptInfo::BURNSITE] || checkChance(10))
    //&& (checkRoads || checkChance(10)))
    {
        // checkRoads = false;
        if(destroyUnusedRoads(gf))
            shouldReconnect = true;
    }
    if(reconnectFlags())
        return true;

    bool issuedCommand = false;
    COUNTER_GUARD_FACTOR(UPGRADE_ROAD_FACTOR, view.flagPts.size(), issuedCommand);

    auto EnsureDonkey = [&]() {
        if(lookupJobInStorehouses(JOB_PACKDONKEY) <= 0 && view.globalResources[ConceptInfo::DONKEY] <= 0)
            view.globalResources[ConceptInfo::DONKEY] = -1;
    };

    for(const MapPoint& pt : view.flagPts)
    {
        const noFlag* flag = gwb.GetSpecObj<noFlag>(pt);
        if(!flag)
            continue;

        if(flag->GetNumWares() < Config::Actions::GOODS_ON_FLAG_REQUIRE_DONKEY)
            continue;

        for(unsigned u = 0; u < Direction::COUNT; ++u)
            if(flag->GetNumWaresForRoad(Direction::fromInt(u)) > Config::Actions::GOODS_ON_FLAG_REQUIRE_DONKEY)
            {
                switch(flag->GetRoute(Direction::fromInt(u))->GetRoadType())
                {
                    case RoadType::Normal:
                        EnsureDonkey();
                        aii.UpgradeRoad(pt, Direction::fromInt(u));
                        break;
                    case RoadType::Donkey:
                        if(!issuedCommand)
                            createSecondaryRoad(pt);
                        break;
                    case RoadType::Water: break;
                }
            }
    }

    return issuedCommand;
}

void PlayerJD::handleWoodcutter()
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    unsigned numForester = aii.GetBuildings(BLD_FORESTER).size();
    if(numForester
       && (lookupGoodInStorehouses(GD_WOOD) + lookupGoodInStorehouses(GD_BOARDS)) > (100 - (10 * numForester)))
    {
        bool isEnd;
        auto it = getRandomElement(view.buildings[BLD_FORESTER], &isEnd);
        if(!isEnd)
        {
            // actions.insert(new DestroyRequest(actionManager, {it->first}));
            aii.DestroyBuilding(it->first);
            return;
        }
    }
    if(checkChance(20))
        return;
    // if(updateNo % 10 != 0)
    // return;

    for(auto& it : view.buildings[BLD_FORESTER])
    {
        MapPoint flagPt = view.bldg2flag(it.first);
        const noBase* no = gwb.GetNO(it.first);
        const NodalObjectType noType = no->GetType();
        if(noType != NOP_BUILDING)
            continue;
        if(!static_cast<const nobUsual*>(no)->HasWorker())
            continue;
        if(view.getConnectedRoads(flagPt) == 2)
            view.followRoadAndDestroyFlags(aii, flagPt, 2, Direction::SOUTHEAST);
    }

    if(lookupGoodInInventory(GD_WOOD) > 15 && checkChance(25))
        return;

    bool hasTrees = view.globalResources[ResourceInfo::TREE] > 16;
    if(lookupJobInInventory(JOB_WOODCUTTER, /* includeTools */ true)
       && (hasTrees || view.globalResources[CommodityInfo::WOOD] < 2))
    {
        if(lookupJobInStorehouses(JOB_WOODCUTTER))
        {
            Action* groupAction = new GroupAction(GroupAction::ANY);
            groupAction->addSubAction(new BuildingRequest(actionManager, BLD_WOODCUTTER));
            if((view.getNumBuildings(BLD_SAWMILL) > view.getNumBuildings(BLD_FORESTER))
               && (lookupGoodInStorehouses(GD_WOOD) + lookupGoodInStorehouses(GD_BOARDS)) < (100 - (40 * numForester)))
            {
                Action* foresterAction = new GroupAction(GroupAction::EXECUTE);
                groupAction->addSubAction(foresterAction);
                foresterAction->addSubAction(new BuildingRequest(actionManager, BLD_FORESTER));
                foresterAction->addSubAction(new BuildingRequest(actionManager, BLD_WOODCUTTER));
            }
            actionManager.appendAction(groupAction);
            actions.insert(groupAction);
        }
    }

    // unsigned numWoodcutters = aii.GetBuildings(BLD_WOODCUTTER).size();
    // unsigned numSawmills = aii.GetBuildings(BLD_SAWMILL).size();
    // if(emergencyInfo.isEmergency(EmergencyInfo::BOARD))
    // if(numWoodcutters < numSawmills * 2 + 2)
    // actions.insert(woodCutterOrAlternativesAction(view, actionManager, emergencyInfo));

    // static bool ForesterPending = false;
    // SAY("ForesterPending: % ", ForesterPending);
    // if (ForesterPending)
    // return;

    unsigned numGoodWoodcutters = 0;
    for(nobUsual* building : aii.GetBuildings(BLD_WOODCUTTER))
    {
        // if (building->GetProductivity() > 90) {
        //++numGoodWoodcutters;
        // continue;
        //}
        auto it = view.buildings[BLD_WOODCUTTER].find(building->GetPos());
        if(it != view.buildings[BLD_WOODCUTTER].end() && (*it).second.hasRequiredResources())
            ++numGoodWoodcutters;
    }
    SAY("Found % productive woodcutters and got % sawmills => %", numGoodWoodcutters,
        aii.GetBuildings(BLD_SAWMILL).size(), (numGoodWoodcutters + 1 >= aii.GetBuildings(BLD_SAWMILL).size() * 2));

    if(numGoodWoodcutters + 1 >= aii.GetBuildings(BLD_SAWMILL).size() * 2)
        return;

    // if (checkChance(25))
    // return;

    for(auto& it : view.buildings[BLD_WOODCUTTER])
    {
        if(it.second.hasRequiredResources())
            continue;
        if(!shouldBuildForester(it.first))
            continue;

        Action* groupAction = new GroupAction(GroupAction::ANY);
        actionManager.appendAction(groupAction);

        Action* woodcutterAction = new GroupAction(GroupAction::EXECUTE);
        groupAction->addSubAction(woodcutterAction);
        woodcutterAction->addSubAction(new BuildingRequest(actionManager, BLD_WOODCUTTER));
        // woodcutterAction->addSubAction(new DestroyRequest(actionManager, {it.first}));

        if(view.getNumBuildings(BLD_SAWMILL) > view.getNumBuildings(BLD_FORESTER))
        {
            Action* foresterAction = new GroupAction(GroupAction::ANY);
            groupAction->addSubAction(foresterAction);
            foresterAction->addSubAction(new BuildingRequest(actionManager, BLD_FORESTER, it.first, 6));
        }
        // foresterAction->addSubAction(new DestroyRequest(actionManager, {it.first}));

        actions.insert(groupAction);

        // ForesterPending = true;
        // addPostActionHook(groupAction, [&]() { ForesterPending = false; });
        // groupAction->deletionCallbacks.push_back(deletionCallback);
        return;
    }
}

bool PlayerJD::handleEmergency()
{
    // if(emergencyInfo.any() && !emergencyInfo.isOnlyEmergency(EmergencyInfo::WAR))
    // actionManager.clear();
    if(emergencyInfo.isEmergency(EmergencyInfo::SPACE))
        if(handleSpaceEmergency())
            return true;
    if(emergencyInfo.isEmergency(EmergencyInfo::BOARD))
        if(handleBoardEmergency())
            return true;
    if(emergencyInfo.isEmergency(EmergencyInfo::STONE))
        if(handleStoneEmergency())
            return true;
    return false;
}
bool PlayerJD::handleWarEmergency(unsigned gf)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    struct Target
    {
        MapPoint pt;
        unsigned enemyId;
        unsigned troopCount;
    };
    std::vector<Target> targets;
    bool isAttacked = false;
    bool issuedCommands = false;
    for(nobMilitary* militaryBuilding : aii.GetMilitaryBuildings())
    {
        auto frontierDistance = militaryBuilding->GetFrontierDistance();
        for(nofAttacker* attacker : militaryBuilding->GetAggressors())
        {
            if(!view[attacker->GetPos()].visible)
                continue;
            enemiesToAttack.insert(attacker->GetPlayer());
            nobBaseMilitary* attackerHome = attacker->GetHome();
            isAttacked = true;
            if(!attackerHome || !view[attackerHome->GetPos()].visible)
                continue;
            aii.Attack(attackerHome->GetPos(), 1, true);
        }

        if(isAttacked || (frontierDistance == nobMilitary::DIST_FAR || frontierDistance == nobMilitary::DIST_HARBOR))
            continue;

        bool giveUpBuilding = false;
        switch(militaryBuilding->GetBuildingType())
        {
            case BLD_BARRACKS:
            case BLD_GUARDHOUSE: giveUpBuilding = true; break;
            default: break;
        }
        unsigned NearbyBldgs = 0;
        auto CountNearbyBldgs = [&](MapPoint pt, unsigned) {
            for(unsigned u = 0; u < NUM_BUILDING_TYPES; ++u)
                NearbyBldgs += view.buildings[u].count(pt);
            return NearbyBldgs > 4;
        };
        giveUpBuilding &= !militaryBuilding->IsNewBuilt();
        if(giveUpBuilding && frontierDistance == nobMilitary::DIST_NEAR
           && !coveredMilitaryBuildings.count(militaryBuilding->GetPos()))
            if(!view.CheckPointsInRadius(militaryBuilding->GetPos(), 7, CountNearbyBldgs, /* includePt */ false))
                aii.DestroyBuilding(militaryBuilding);
        if(!shouldReconnect && coveredMilitaryBuildings.insert(militaryBuilding->GetPos()).second)
        {
            for(int i = 0; i < 2; ++i)
            {
                Action* groupAction = new GroupAction(GroupAction::ANY);
                actionManager.appendAction(groupAction);
                groupAction->addSubAction(
                  new BuildingRequest(actionManager, BLD_FORTRESS, militaryBuilding->GetPos(), 9));
                groupAction->addSubAction(
                  new BuildingRequest(actionManager, BLD_WATCHTOWER, militaryBuilding->GetPos(), 9));
                actions.insert(groupAction);
            }
            issuedCommands = true;
        }
    }

    settings.military_settings[1] = 10;
    updateMilitarySettings = true;

    auto oldEnemiesToAttack = enemiesToAttack;
    for(unsigned enemyId : oldEnemiesToAttack)
    {
        bool keep = false;
        for(auto& it : view.enemyMilitaryBuildings[enemyId])
        {
            nobMilitary* militaryBuilding = it;
            if(!view[militaryBuilding->GetPos()].visible)
                continue;
            keep = true;
            break;
        }
        if(!keep)
            enemiesToAttack.erase(enemyId);
    }

    for(unsigned enemyId : view.enemies)
    {
        SAY("Check % military buildings of %", view.enemyMilitaryBuildings[enemyId].size(), enemyId);
        const GamePlayer& otherPlayer = gwb.GetPlayer(enemyId);
        for(auto* buildingSite : otherPlayer.GetBuildingRegister().GetBuildingSites())
        {
            // if(!BuildingProperties::IsMilitary(buildingSite->GetBuildingType()))
            // continue;
            if(!view[buildingSite->GetPos()].visible)
                continue;
            // if (buildingSite->GetBuildProgress() < 30 * (unsigned(buildingSite->GetSize()) - 1) )
            //;
            // enemiesToAttack.insert(enemyId);
            unsigned& issueGF = catapultAndLookoutIssued[buildingSite->GetPos()];
            if(!shouldReconnect && issueGF + 5000 < gf)
            {
                Action* groupAction = new GroupAction(GroupAction::ANY);
                actionManager.appendAction(groupAction);
                MapPoint enemeyPt = buildingSite->GetPos();
                for(const MapPoint& circlePt : view.GetPointsInRadiusWithCenter(enemeyPt, 5))
                {
                    if(view.potentialBuildingSites.count(circlePt))
                        groupAction->addSubAction(
                          BuildingRequest::getBuildingRequestAt(actionManager, BLD_BARRACKS, circlePt));
                }
                if(BuildingProperties::IsMilitary(buildingSite->GetBuildingType()))
                    groupAction->addSubAction(
                      new BuildingRequest(actionManager, BLD_CATAPULT, buildingSite->GetPos(), 13));
                actions.insert(groupAction);
                issueGF = gf;
            }
        }
        for(auto& it : view.enemyMilitaryBuildings[enemyId])
        {
            nobMilitary* militaryBuilding = it;
            if(!view[militaryBuilding->GetPos()].visible)
                continue;
            if(militaryBuilding->IsNewBuilt())
                continue;
            // if(getNumMilitaryBuildingSites() > 0 && militaryBuilding->GetMaxTroopsCt() > 3 && !isAttacked
            //&& lookupGoodInInventory(GD_COINS) < 5)
            // continue;
            if(!enemiesToAttack.empty() && !enemiesToAttack.count(enemyId) && militaryBuilding->GetMaxTroopsCt() > 3)
                continue;
            unsigned& issueGF = catapultAndLookoutIssued[militaryBuilding->GetPos()];
            if(!shouldReconnect && issueGF + 1000 < gf)
            {
                actionManager.constructBuilding(nullptr, BLD_CATAPULT, militaryBuilding->GetPos(), 13);
                issueGF = gf;
            }
            targets.push_back({militaryBuilding->GetPos(), enemyId, militaryBuilding->GetMaxTroopsCt()});
        }
        MapPoint enemyHQPos = otherPlayer.GetHQPos();
        if(enemyHQPos.isValid() && aii.IsBuildingOnNode(enemyHQPos, BLD_HEADQUARTERS))
        {
            SAY("EnemyHQ: %", enemyHQPos);
            if(view[enemyHQPos].visible && !isAttacked)
                targets.push_back({enemyHQPos, enemyId, 20});
            enemiesToAttack.insert(enemyId);
        }
    }
    std::sort(targets.begin(), targets.end(),
              [](const Target& lhs, const Target& rhs) { return lhs.troopCount < rhs.troopCount; });

    for(auto& target : targets)
    {
        unsigned strength = 0, count = 0, numGens = 0;
        for(nobMilitary* militaryBuilding : aii.GetMilitaryBuildings())
        {
            // if(coinBldgs.count(militaryBuilding))
            // continue;
            unsigned c = 0;
            strength += militaryBuilding->GetSoldiersStrengthForAttack(target.pt, c);
            if(!c)
                continue;
            for(auto it = militaryBuilding->GetTroops().rbegin(), end = militaryBuilding->GetTroops().rend(); it != end;
                ++it)
            {
                if((*it)->GetRank() == 4 && (*it)->GetHitpoints() == 7)
                    ++numGens;
            }
            count += c;
        }
        unsigned factor = 6;
        // if(checkChance(90))
        //{
        if(numGens < target.troopCount && strength < target.troopCount * factor)
            continue;
        //}
        bool attacked = false;
        for(unsigned u = 0; u < (numGens ? numGens : count); ++u)
            attacked |= aii.Attack(target.pt, 1, true);
        if(!attacked)
            continue;
        if(enemiesToAttack.empty())
            enemiesToAttack.insert(target.enemyId);
    }
    return issuedCommands;
}

bool PlayerJD::handleSpaceEmergency()
{
    Action* groupAction = new GroupAction(GroupAction::ANY);
    actionManager.appendAction(groupAction);
    groupAction->addSubAction(new BuildingRequest(actionManager, BLD_WOODCUTTER));
    groupAction->addSubAction(new BuildingRequest(actionManager, BLD_QUARRY));
    if(emergencyInfo.isEmergency(EmergencyInfo::STONE))
    {
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_BARRACKS));
    } else
    {
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_GUARDHOUSE));
    }
    groupAction->addSubAction(new BuildingRequest(actionManager, BLD_WATCHTOWER));
    groupAction->addSubAction(new BuildingRequest(actionManager, BLD_FORTRESS));
    actions.insert(groupAction);

    return true;
}

bool PlayerJD::handleBoardEmergency()
{
    BuildingInfo sawmillInfo = BuildingInfo::get(BLD_SAWMILL);
    unsigned numWoodcutters = aii.GetBuildings(BLD_WOODCUTTER).size();
    unsigned numSawmills = aii.GetBuildings(BLD_SAWMILL).size();
    for(int i = view.getNumBuildings(BLD_SAWMILL); i < 4; ++i)
    {
        if(lookupJobInStorehouses(sawmillInfo.getJob(), 1))
            actions.insert(actionManager.constructBuilding(nullptr, BLD_SAWMILL));
    }

    for(int i = view.getNumBuildings(BLD_WOODCUTTER); i < 4; ++i)
    {
        if(lookupJobInStorehouses(JOB_WOODCUTTER, 1))
            actions.insert(woodCutterOrAlternativesAction(view, actionManager, emergencyInfo));
    }

    // if(!numSawmills || !numWoodcutters)
    // return false;

    if(view.globalResources[CommodityInfo::WOOD] > 1)
    {
        if(lookupJobInStorehouses(sawmillInfo.getJob()))
            actions.insert(actionManager.constructBuilding(nullptr, BLD_SAWMILL));
    } else
    {
        if(lookupJobInStorehouses(JOB_WOODCUTTER) && checkChance(50))
            actions.insert(woodCutterOrAlternativesAction(view, actionManager, emergencyInfo));
    }

    return false;
}
bool PlayerJD::handleStoneEmergency()
{
    auto BuildQuaryGranitMineOrExpand = [=]() {
        Action* groupAction = new GroupAction(GroupAction::ANY);
        actionManager.appendAction(groupAction);
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_QUARRY));
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_GRANITEMINE));
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_BARRACKS));
        actions.insert(groupAction);
    };
    if(view.getNumBuildings(BLD_QUARRY) + view.getNumBuildings(BLD_GRANITEMINE) == 0)
        view.globalResources[CommodityInfo::FOOD] = 2;
    BuildQuaryGranitMineOrExpand();
    BuildQuaryGranitMineOrExpand();
    return false;
}

unsigned PlayerJD::getNumMilitaryBuildingSites()
{
    unsigned numMilitaryBuildingSites = 0;
    // for (const noBuildingSite* buildingSite:  aii.GetBuildingSites()) {
    // BuildingType buildingType = buildingSite->GetBuildingType();
    // if(BuildingProperties::IsMilitary(buildingType))
    //++numMilitaryBuildingSites;
    //}
    const BuildingCount& buildingCount = player.GetBuildingRegister().GetBuildingNums();
    numMilitaryBuildingSites += buildingCount.buildingSites[BLD_BARRACKS];
    numMilitaryBuildingSites += buildingCount.buildingSites[BLD_GUARDHOUSE];
    numMilitaryBuildingSites += buildingCount.buildingSites[BLD_WATCHTOWER];
    numMilitaryBuildingSites += buildingCount.buildingSites[BLD_FORTRESS];

    for(const auto& pair : finishedBuildings)
    {
        if(!BuildingProperties::IsMilitary(pair.second))
            continue;
        const noBase* no = gwb.GetNO(pair.first);
        if(static_cast<const nobMilitary*>(no)->IsNewBuilt())
            ++numMilitaryBuildingSites;
    }
    return numMilitaryBuildingSites;
}

bool PlayerJD::shouldExpand()
{
    if(aii.GetMilitaryBuildings().size() < 2)
        return true;
    if(checkChance(33))
        return false;
    if(checkChance(3))
        return true;
    int maxBuildingSites = 2;
    if(view.globalResources[ResourceInfo::BORDERLAND] <= 0)
        return false;
    if(view.globalResources[ResourceInfo::COAL] < 32)
        maxBuildingSites += 1;
    if(view.globalResources[ResourceInfo::IRONORE] < 32)
        maxBuildingSites += 1;
    if(view.globalResources[ResourceInfo::GOLD] < 32)
        maxBuildingSites += 1;
    // if(view.globalResources[ResourceInfo::COAL] > 32 && view.globalResources[ResourceInfo::GOLD] > 32)
    // maxBuildingSites -= lookupJobInInventory(JOB_PRIVATE) / std::max(1U, lookupJobInInventory(JOB_GENERAL));
    // if(view.globalResources[ResourceInfo::COAL] > 32 && view.globalResources[ResourceInfo::IRONORE] > 32)
    // maxBuildingSites -= 1;
    // if(emergencyInfo.any() && !emergencyInfo.isOnlyEmergency(EmergencyInfo::SPACE))
    // maxBuildingSites -= 1;
    if(emergencyInfo.isEmergency(EmergencyInfo::WAR))
        maxBuildingSites -= 2;

    if((lookupJobInInventory(JOB_PRIVATE) * 2 < lookupJobInInventory(JOB_GENERAL))
       || (view.globalResources[ResourceInfo::COAL] < 32 && view.globalResources[ResourceInfo::GOLD] < 32
           && view.globalResources[ResourceInfo::IRONORE] < 32))
        maxBuildingSites += std::min(lookupGoodInStorehouses(GD_BOARDS) / 24, lookupGoodInStorehouses(GD_STONES) / 16);

    int numMilitaryBuildingSites = getNumMilitaryBuildingSites();
    SAY("ShouldExpand: #Military building sites: %, expand: %", numMilitaryBuildingSites,
        (numMilitaryBuildingSites < maxBuildingSites));
    return numMilitaryBuildingSites < maxBuildingSites;
}

void PlayerJD::expand(bool force)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);

    if(view.getNumBuildings(BLD_WATCHTOWER) + view.getNumBuildings(BLD_FORTRESS) == 0)
        actions.insert(actionManager.constructBuilding(nullptr, BLD_WATCHTOWER));

    MapPoint targetPt;
    if(!view.mountains.empty())
    {
        MapPoint hqPt = player.GetHQPos();
        auto pair = std::min_element(view.mountains.begin(), view.mountains.end(), [&](auto& lhs, auto& rhs) {
            if(view[lhs.first].getValue(ConceptInfo::OWNED))
                return false;
            if(view[rhs.first].getValue(ConceptInfo::OWNED))
                return false;
            if(currentTargets.count(lhs.first))
                return false;
            if(currentTargets.count(rhs.first))
                return true;
            unsigned minDistLHS = -1, minDistRHS = -1;
            for(const MapPoint& enemyHQ : view.enemyHQs)
            {
                RoadRequest::Constraints lhsConstraints{enemyHQ, lhs.first};
                RoadRequest::Constraints rhsConstraints{enemyHQ, rhs.first};
                minDistLHS = std::min(minDistLHS, view.getConnectionLength(lhsConstraints));
                minDistRHS = std::min(minDistRHS, view.getConnectionLength(rhsConstraints));
            }
            RoadRequest::Constraints lhsConstraints{hqPt, lhs.first};
            RoadRequest::Constraints rhsConstraints{hqPt, rhs.first};
            minDistLHS = (3 * minDistLHS) / 2;
            minDistRHS = (3 * minDistRHS) / 2;
            int lhsRaceDistance = (view.getConnectionLength(lhsConstraints) - minDistLHS);
            int rhsRaceDistance = (view.getConnectionLength(rhsConstraints) - minDistRHS);
            return lhsRaceDistance < rhsRaceDistance;
        });
        targetPt = pair->first;
    }

    if(currentTargets.count(targetPt))
        targetPt = MapPoint();

    SAY("Expansion towards % [% expansion ongoing]", targetPt, currentTargets.size());

    std::array<BuildingType, 4> militaryBuildings{BLD_BARRACKS, BLD_GUARDHOUSE, BLD_WATCHTOWER, BLD_FORTRESS};
    std::array<MapPoint, 4> points;
    std::array<Score, 4> scores;

    // for(unsigned u = 0; u < 4; ++u)
    // scores[u].invalidate();

    // unsigned u = getRandomNumber(0, 3);

    for(unsigned u = 0; u < 4; ++u)
    {
        points[u] = view.findBuildingPosition(BuildingRequest::Constraints{militaryBuildings[u], MapPoint(), targetPt},
                                              scores[u]);
        if(!points[u].isValid())
            scores[u].invalidate();
        else if(!ensureResources(militaryBuildings[u]))
            scores[u] -= 4096;
    }

    // if (!scores[u].isValid())
    // return;

    for(unsigned u = 0; u < 4; ++u)
        SAY("%: % @ % :: %", u, militaryBuildings[u], points[u], scores[u]);

    auto it = std::min_element(scores.begin(), scores.end(), [](auto& lhs, auto& rhs) { return lhs > rhs; });
    if(it->isValid())
    {
        unsigned idx = std::distance(scores.begin(), it);
        Action* action = new BuildingRequest(actionManager, {militaryBuildings[idx], points[idx]});
        actionManager.appendAction(action);
        actions.insert(action);

        currentTargets.insert(targetPt);
        buildingCallbacks[points[idx]] = [=](MapPoint pt, bool) { currentTargets.erase(targetPt); };

        addPostActionHook(action, [=]() {
            if(action->getStatus() == Action::SUCCESS)
                return;
            currentTargets.erase(targetPt);
            // checkRoads = true;
        });
        return;
    }

    if(force)
    {
        Action* groupAction = new GroupAction(GroupAction::ANY);
        actionManager.appendAction(groupAction);
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_FORTRESS));
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_WATCHTOWER));
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_GUARDHOUSE));
        groupAction->addSubAction(new BuildingRequest(actionManager, BLD_BARRACKS));
        // groupAction->addSubAction(new DestroyRequest(actionManager, {}));
        actions.insert(groupAction);
    }

    // checkRoads = true;
}

void PlayerJD::handleSpecialists(unsigned gf)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    // if(updateNo % 50 != 0)
    // return;
    // static unsigned Cooldown = 25;
    // if (Cooldown-- != 0)
    // return;
    // Cooldown = 30;

    handleScouts(gf);
    handleGeologists(gf);
}

void PlayerJD::handleScouts(unsigned)
{
    if(!emergencyInfo.isEmergency(EmergencyInfo::WAR) || checkChance(20))
        return;
    // bool atWar = emergencyInfo.isEmergency(EmergencyInfo::WAR);
    if(!lookupJobInStorehouses(JOB_SCOUT, /* includeTools */ false))
        return;

    bool isEnd;
    auto enemyIt = getRandomElement(view.enemyHQs, &isEnd);
    auto cmp = [&](const MapPoint& lhs, const MapPoint& rhs) {
        if(view.singletonFlagPts.count(lhs))
            return false;
        if(view.singletonFlagPts.count(rhs))
            return true;
        return view.getConnectionLength({*enemyIt, lhs}) < view.getConnectionLength({*enemyIt, rhs});
    };
    auto it = std::min_element(view.flagPts.begin(), view.flagPts.end(), cmp);
    if(it == view.flagPts.end())
        return;

    aii.CallSpecialist(*it, JOB_SCOUT);
}

void PlayerJD::handleGeologists(unsigned gf)
{
    if(gf < Config::Actions::GEOLOGIST_MIN_GF)
        return;

    if(WaitingForGeologistAction)
        return;

    int newNumTotalGeologists = lookupJobInInventory(JOB_GEOLOGIST);
    int newNumGeologistsInStore = lookupJobInStorehouses(JOB_GEOLOGIST);

    numTotalGeologists = std::min(6, newNumTotalGeologists);
    numGeologistsInStore = newNumGeologistsInStore;

    if(numGeologistsInStore == numTotalGeologists)
        currentGeologistExpeditions.clear();

    // We do not want to spam specialist actions so there is a chance we don't do anything.
    if(!checkChance(Config::Actions::GEOLOGIST_CHANCE))
        return;

    std::set<MapPoint> seen;
    std::vector<MapPoint> unknownMinePoints;
    for(const MapPoint& pt : view.potentialBuildingSites)
    {
        if(!view.wasVisitedByGeologist(pt) && view.getBuildingQuality(pt) == BQ_MINE)
        {
            for(const MapPoint& circlePt : view.GetPointsInRadiusWithCenter(pt, 1))
                if(seen.insert(circlePt).second)
                    unknownMinePoints.push_back(circlePt);
        }
    }

    SAY("Check if geologists should be send out to any of the % unknown mine locations", unknownMinePoints.size());

    if(unknownMinePoints.empty())
        return;

    // Test if we have enough geologists and request more if we don't.
    if(!ensureJob(JOB_GEOLOGIST, Config::Actions::GEOLOGIST_MIN_AMOUNT, /* inInventory */ true))
        return;
    // If we have geologists but not available, wait for them.
    unsigned availableGeologists = lookupJobInStorehouses(JOB_GEOLOGIST);
    if(availableGeologists < Config::Actions::GEOLOGIST_MIN_AMOUNT)
        return;

    SAY("% of % geologists are available", availableGeologists, numTotalGeologists);

    std::vector<std::pair<std::pair<int, int>, MapPoint>> unexploredNeighboursCount;
    for(const MapPoint& minePt : unknownMinePoints)
    {
        if(!view.canBuildFlag(minePt))
            continue;

        std::pair<std::pair<int, int>, MapPoint> posAndScore = {{0, 0}, minePt};
        for(const MapPoint& circlePt : view.GetPointsInRadiusWithCenter(minePt, 4))
        {
            posAndScore.first.first += !view.wasVisitedByGeologist(circlePt)
                                       && aii.GetBuildingQualityAnyOwner(circlePt) == BuildingQuality::BQ_MINE;
            posAndScore.first.second += aii.GetBuildingQualityAnyOwner(circlePt) == BuildingQuality::BQ_MINE;
            if(currentGeologistExpeditions.count(circlePt))
            {
                posAndScore.first.first -= 4;
                break;
            }
        }
        if(view.flagPts.count(minePt))
            posAndScore.first.first += 5;
        unexploredNeighboursCount.push_back(posAndScore);
    }

    if(unexploredNeighboursCount.empty())
        return;

    std::sort(unexploredNeighboursCount.begin(), unexploredNeighboursCount.end(),
              std::greater<std::pair<std::pair<int, int>, MapPoint>>());
    // if(unexploredNeighboursCount[0].first.first < unexploredNeighboursCount[0].first.second / 4)
    // return;
    if(unexploredNeighboursCount[0].first.first < 0)
        return;

    unsigned numGeologists = (availableGeologists + 1) / 2;

    Action* groupAction = new GroupAction(GroupAction::ANY);
    for(unsigned u = 0; u < std::min(size_t(5), unexploredNeighboursCount.size()); ++u)
    {
        // if(unexploredNeighboursCount[u].first.first < unexploredNeighboursCount[u].first.second / 4)
        // break;
        MapPoint flagPt = unexploredNeighboursCount[u].second;
        if(view.flagPts.count(flagPt) && view.singletonFlagPts.count(flagPt))
            continue;
        if(!view.flagPts.count(flagPt))
        {
            std::vector<Direction> directions;
            Score score;
            if(!view.findConnectionPoint({flagPt}, directions, score).isValid())
                continue;
        }
        SAY("Send % geologists to %", numGeologists, flagPt);
        Action* action = actionManager.requestGeologists(nullptr, flagPt, numGeologists);
        currentGeologistExpeditions[flagPt] += 30;
        groupAction->addSubAction(action);
    }
    if(!groupAction->hasChildren())
    {
        delete groupAction;
        return;
    }

    actionManager.appendAction(groupAction);
    actions.insert(groupAction);

    WaitingForGeologistAction = true;
    addPostActionHook(groupAction, [&]() { WaitingForGeologistAction = false; });
}

#if 0
bool View::shouldIssueBuilding(BuildingType buildingType)
{
    ProximityInfo proximityInfo(buildingType);

    int availableGloballyMin = 0;
    for(unsigned pIdx = 0; pIdx < proximityInfo.producing.size(); ++pIdx)
    {
        if(!proximityInfo.producing[pIdx].isGlobal())
            continue;
        int availableGlobally = getGlobalResource(proximityInfo.producing[pIdx].radius, proximityInfo.producing[pIdx].kind, 0);
        if(availableGlobally < 0)
        {
            SAY("[%] We need % %, issuing % is reasonable.", pIdx, -proximityInfo.producing[pIdx].kind, proximityInfo.producing[pIdx].kind,
                buildingType);
            return true;
        }
        availableGloballyMin = std::min(availableGloballyMin, availableGlobally);
    }

    if(availableGloballyMin > 0)
    {
        SAY("[X] Overproduction (>= %) of all globally produced resources, % is not reasonable", availableGloballyMin, buildingType);
        return false;
    }

    bool anyAvailable = false;
    for(unsigned cIdx = 0; cIdx < proximityInfo.consuming.size(); ++cIdx)
    {
        int resourceConsuming = getGlobalResource(proximityInfo.consumingRadius[cIdx], proximityInfo.consuming[cIdx], 0);
        if(resourceConsuming < 0)
        {
            SAY("[X] We are lacking % % already, do not issue %.", resourceConsuming, proximityInfo.consuming[cIdx], buildingType);
            anyAvailable = false;
            break;
        }
        anyAvailable |= resourceConsuming > 0;
        SAY("[X] We have % % left over, issuing % is reasonable.", resourceConsuming, proximityInfo.consuming[cIdx], buildingType);
    }
    if(anyAvailable)
    {
        SAY("[X] Issuing % seems reasonable", buildingType);
        return true;
    }

    return false;
}
#endif

void PlayerJD::addPostActionGlobalAdjustmentHook(Action* action, unsigned thing)
{
    Action::DeletionCallbackTy deletionCallback = [=]() {
        if(action->getStatus() == Action::SUCCESS)
            return;
        SAY("RESET % globally to 0 because of %", thing, *action);
        view.globalResources[thing] = 0;
    };
    action->deletionCallbacks.push_back(deletionCallback);
}

std::unique_ptr<AIPlayer> AI::jd::getPlayer(unsigned char playerId, const GameWorldBase& gwb, const AI::Level level)
{
    return std::make_unique<PlayerJD>(playerId, gwb, level);
}

unsigned PlayerJD::getRoadSegmentProductivity(unsigned gf, RoadSegment* roadSegment)
{
    unsigned productivity = 0;
    bool idle = true;
    if(nofCarrier* carrier = roadSegment->getCarrier(0))
    {
        unsigned GFsinEpoch = (gf - carrier->GetFirstGFInProductivityEpoch());
        productivity += carrier->GetWorkedGFInProducitivityEpoch() * 100 / GFsinEpoch;
        idle &= carrier->GetCarrierState() == CARRS_WAITFORWARE;
        // idle &= GFsinEpoch > 3000 || carrier->GetProductivity();
    }
    if(nofCarrier* carrier = roadSegment->getCarrier(1))
    {
        unsigned GFsinEpoch = (gf - carrier->GetFirstGFInProductivityEpoch());
        productivity += carrier->GetWorkedGFInProducitivityEpoch() * 100 / GFsinEpoch;
        idle &= carrier->GetCarrierState() == CARRS_WAITFORWARE;
        // idle &= GFsinEpoch > 3000 || carrier->GetProductivity();
    }
    assert(productivity <= 200);
    // printf("idle %i prod %i \n", idle, productivity );

    return idle ? productivity : 100;
}

bool PlayerJD::createSecondaryRoad(MapPoint pt)
{
    const noFlag* flag = gwb.GetSpecObj<noFlag>(pt);
    if(!flag || (flag->GetNumWares() == 0 && checkChance(50)))
        return false;
    RoadRequest::Constraints constraints{pt};
    constraints.excludeFromPt = true;
    constraints.type = RoadRequest::Constraints::SECONDARY;
    std::vector<Direction> directions;
    Score score;
    MapPoint connectionPt = view.findConnectionPoint(constraints, directions, score);
    if(!connectionPt.isValid())
        return false;

    actions.insert(actionManager.constructRoad(nullptr, constraints));
    actions.insert(actionManager.constructRoad(nullptr, constraints));
    return true;
}

void PlayerJD::createSecondaryRoads(unsigned gf)
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);

    for(auto& it : flagAgeMap)
        if(!view.flagPts.count(it.first))
            it.second = 0;

    for(const MapPoint& pt : view.flagPts)
    {
        if(view.singletonFlagPts.count(pt))
            continue;
        unsigned& age = flagAgeMap[pt];
        if(age + 2000 > gf)
            continue;
        age = gf;
        createSecondaryRoad(pt);
    }
}

bool PlayerJD::destroyUnusedRoads(unsigned gf)
{
    bool changed = false;
    COUNTER_GUARD(DESTROY_UNUSED_ROADS, changed);

    std::vector<RoadSegment*> roadSegments;
    for(const MapPoint& pt : view.flagPts)
    {
        const noFlag* flag = gwb.GetSpecObj<noFlag>(pt);
        if(!flag)
            continue;
        bool hasBuilding = (aii.IsObjectTypeOnNode(view.flag2bldg(pt), NodalObjectType::NOP_BUILDING)
                            || aii.IsObjectTypeOnNode(view.flag2bldg(pt), NodalObjectType::NOP_BUILDINGSITE));

        for(unsigned u = 0; u < 6; ++u)
        {
            Direction dir = Direction::fromInt(u);
            if(hasBuilding && dir == Direction::NORTHWEST)
                continue;
            if(RoadSegment* roadSegment = flag->GetRoute(dir))
            {
                SAY("[%] flag %, roadSegment %", pt, (void*)flag, (void*)roadSegment);

                bool specialRoad = view[roadSegment->GetF1()->GetPos()].getValue(ConceptInfo::FARM_SPACE)
                                   || view[roadSegment->GetF2()->GetPos()].getValue(ConceptInfo::FARM_SPACE)
                                   || view[roadSegment->GetF1()->GetPos()].getValue(ConceptInfo::NEAR_BORDER)
                                   || view[roadSegment->GetF2()->GetPos()].getValue(ConceptInfo::NEAR_BORDER);

                unsigned minProductivity = config.actions.MIN_ROAD_PRODUCTIVITY;
                unsigned productivity = getRoadSegmentProductivity(gf, roadSegment);

                if(productivity < minProductivity || (specialRoad && productivity < 100))
                    if(aii.DestroyRoad(pt, dir))
                    {
                        changed = true;
                    }
            }
        }
    }

    shouldReconnect |= changed;
    return changed;
}

bool PlayerJD::reconnectFlags()
{
    bool changed = false;
    if(!shouldReconnect)
        COUNTER_GUARD(RECONNECT_FLAGS, changed);
    shouldReconnect = true;

    RAIISayTimer RST(__PRETTY_FUNCTION__);
    using SCC = std::vector<MapPoint>;

    bool destroyedFlag = false;
    decltype(view.flagPts) flags = view.flagPts;
    for(const MapPoint& pt : flags)
    {
        const noFlag* flag = gwb.GetSpecObj<noFlag>(pt);
        if(!flag)
            continue;

        unsigned connectedRoads = view.getConnectedRoads(pt);
        if(connectedRoads >= 2 || --currentGeologistExpeditions[pt] > 0)
            continue;

        if(!flag->GetNumWares() && view.destroyFlag(pt, 1))
            destroyedFlag = true;
    }
    if(destroyedFlag)
    {
        return true;
    }

    view.singletonFlagPts.clear();
    std::vector<SCC> sccs;
    std::vector<MapPoint> singletonFlags;
    view.getRoadSCCs(sccs, singletonFlags);
    // printf("Road network has %li SCCs and %li singleton flag\n", sccs.size(), singletonFlags.size());

    for(const MapPoint& pt : singletonFlags)
    {
        MapPoint bldgPt = view.flag2bldg(pt);
        bool hasBuilding = (aii.IsObjectTypeOnNode(bldgPt, NodalObjectType::NOP_BUILDING)
                            || aii.IsObjectTypeOnNode(bldgPt, NodalObjectType::NOP_BUILDINGSITE));
        if(!hasBuilding)
            changed |= view.destroyFlag(pt, 0);
        else
            sccs.push_back({pt});
    }

    if(sccs.size() == 1)
    {
        for(nobBaseWarehouse* storehouse : aii.GetStorehouses())
        {
            RoadRequest::Constraints constraints{storehouse->GetFlagPos()};
            constraints.excludeFromPt = true;
            constraints.type = RoadRequest::Constraints::SECONDARY;
            actions.insert(actionManager.constructRoad(nullptr, constraints));
            actions.insert(actionManager.constructRoad(nullptr, constraints));
            actions.insert(actionManager.constructRoad(nullptr, constraints));
            actions.insert(actionManager.constructRoad(nullptr, constraints));
        }

        shouldReconnect = false;
        return changed;
    }

    if(changed)
        return true;

    std::map<MapPoint, SCC*> sccMapping;
    for(std::vector<MapPoint>& scc : sccs)
    {
        for(const MapPoint& pt : scc)
            sccMapping[pt] = &scc;
    }

    std::set<const SCC*> storehouseSCCs;
    for(nobBaseWarehouse* storehouse : aii.GetStorehouses())
        storehouseSCCs.insert(sccMapping[storehouse->GetFlagPos()]);

    std::sort(sccs.begin(), sccs.end(), [&](const auto& lhs, const auto& rhs) {
        bool lhsHasStorehouse = storehouseSCCs.count(&lhs);
        bool rhsHasStorehouse = storehouseSCCs.count(&rhs);
        if(lhsHasStorehouse != rhsHasStorehouse)
            return lhsHasStorehouse;
        return lhs.size() > rhs.size();
    });

    std::vector<Direction> directions;
    std::map<SCC*, SCC*> mergeMap;

    unsigned idx = 0;
    for(std::vector<MapPoint>& scc : sccs)
    {
        SAY("Create connection from SCC % with % flags\n", idx, scc.size());

        Score bestScore = Score::invalid();
        MapPoint bestPt;
        MapPoint bestConnectionPt;
        unsigned bestNumDirections = -1;
        for(const MapPoint& pt : scc)
        {
            RoadRequest::Constraints constraints{pt};
            constraints.excludeFromPt = true;
            constraints.excludedPts = &scc;
            constraints.lifetime = PERMANENT;
            directions.clear();
            Score score;
            MapPoint connectionPt = view.findConnectionPoint(constraints, directions, score);
            SAY("Conncection pt % with score % and % steps", connectionPt, score, directions.size());
            if(!connectionPt.isValid() || !score.isValid())
                continue;

            SAY("best score %, score %, % % ", bestScore, score, bestScore.isValid(), score < bestScore);
            if(bestScore.isValid() && score > bestScore)
                continue;

            bestPt = pt;
            bestScore = score;
            bestConnectionPt = connectionPt;
            bestNumDirections = directions.size();
        }

        // TODO: Handle this case
        if(!bestScore.isValid() || !bestConnectionPt.isValid())
        {
            printf("SCC %i cannot be connected :(\n", idx);
            ++idx;
            continue;
        }

        RoadRequest::Constraints roadConstraints{bestPt, bestConnectionPt};
        std::vector<Direction> roadDirections;
        Score roadScore;
        assert(!view.findRoadConnection(roadConstraints, roadDirections, roadScore));

        assert(sccMapping.count(bestConnectionPt));

        SAY("reconnect scc % via % -> % route", idx, bestPt, bestConnectionPt);
        actions.insert(actionManager.constructRoad(nullptr, RoadRequest::Constraints{bestPt, bestConnectionPt}));
        return true;
    }

    shouldReconnect = false;
    return changed;
}

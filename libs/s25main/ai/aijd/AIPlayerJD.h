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

#include "Actions.h"
#include "Config.h"
#include "Utils.h"
#include "View.h"
#include "ai/AIPlayer.h"
#include "buildings/nobBaseWarehouse.h"
#include "network/GameClient.h"
#include "gameTypes/InventorySetting.h"
#include "gameTypes/SettingsTypes.h"
#include "gameTypes/VisualSettings.h"

class GameWorldBase;
class GlobalGameSettings;

namespace AI { namespace jd {
    /// Return a new instance of the JD AI player.
    std::unique_ptr<AIPlayer> getPlayer(unsigned char playerId, const GameWorldBase& gwb, AI::Level level);

    struct EmergencyInfo
    {
        enum EmergencyKind
        {
            NONE = 0,
            STONE = 1 << 0,
            BOARD = 1 << 1,
            SPACE = 1 << 2,
            WAR = 1 << 3,
        } emergencies = NONE;

        void resetEmergencies() { emergencies = NONE; }
        void addEmergency(EmergencyKind kind) { emergencies = static_cast<EmergencyKind>(emergencies | kind); }
        bool isEmergency(EmergencyKind kind) const { return emergencies & kind; }
        bool isOnlyEmergency(EmergencyKind kind) const { return emergencies == kind; }
        bool any() const { return emergencies != NONE; }

        friend StreamTy& operator<<(StreamTy& out, EmergencyKind kind) noexcept
        {
            if(kind == NONE)
                return out << "[NONE]";
            if(kind & WAR)
                out << "[WAR]";
            if(kind & SPACE)
                out << "[SPACE]";
            if(kind & BOARD)
                out << "[BOARD]";
            if(kind & STONE)
                out << "[STONE]";
            return out;
        }
        friend StreamTy& operator<<(StreamTy& out, EmergencyInfo obj) noexcept
        {
            return out << "Emergencies: " << obj.emergencies;
        }
    };

    /// An experimental AI as an alternative to the JH one.
    class PlayerJD : public AIPlayer
    {
        /// Counter for NWF GFs.
        unsigned NWF_COUNTER = 0;

        unsigned COUNTER = 0;

        /// The current configuration used by the AI.
        Config config;

        VisualSettings& settings;

        /// The item distribution used.
        bool updateItemDistribution = true;

        /// Our view of the world.
        View view;

        ActionManager actionManager;

        /// Our internal random number generator (RNG).
        std::mt19937 rng;

        /// Identitiy is important, we don't want to be a generic AI.
        std::string identity, identity2;

        /// Both bounds are inclusive.
        unsigned getRandomNumber(unsigned lowerBound, unsigned upperBound)
        {
            std::uniform_int_distribution<typename decltype(rng)::result_type> dist(lowerBound, upperBound);
            return dist(rng);
        }

        /// [0,maximum)
        unsigned getRandomNumber(unsigned maximum)
        {
            assert(maximum > 0);
            return getRandomNumber(0, maximum - 1);
        }

        /// TODO
        bool checkChance(unsigned char chance) { return getRandomNumber(100) < chance; }

        template<typename ContainerT>
        typename ContainerT::iterator getRandomElement(ContainerT& container, bool* isEnd)
        {
            if(isEnd)
                *isEnd = true;
            if(container.empty())
                return container.end();
            if(isEnd)
                *isEnd = false;
            auto it = container.begin();
            std::advance(it, getRandomNumber(container.size()));
            return it;
        }

    public:
        PlayerJD(unsigned char playerId, const GameWorldBase& gwb, const AI::Level level)
            : AIPlayer(playerId, gwb, level), config(level), settings(GAMECLIENT.visual_settings), view(*this),
              actionManager(*this, view)
        {
            // Get some randomness going first.
            std::random_device rnd_dev;
            rng.seed(rnd_dev() + playerId);

            identity = getRandomName(rng);
            if(rng() % 2)
                identity2 = "<none>";
        }

        void RunGF(unsigned gf, bool gfisnwf) override;

        AIInterface& getAII() { return aii; }

    private:
        /// {{
        void registerNewConstruction(BuildingType buildingType);
        void registerFinishedBuilding(MapPoint pos, BuildingType buildingType);
        void reserveForBuilding(BuildingType buildingType, unsigned deliveredBoards, unsigned deliveredStones);
        void updateReserve();
        void registerDestroyedBuilding(MapPoint pos, BuildingType buildingType);
        void checkForCommissionings();
        /// }}

        void init();

        void setInventorySetting(nobBaseWarehouse& storehouse, GoodType good, InventorySetting state);
        void setInventorySetting(nobBaseWarehouse& storehouse, Job job, InventorySetting state);
        void resetInventorySetting(nobBaseWarehouse& storehouse, GoodType good)
        {
            setInventorySetting(storehouse, good, InventorySetting());
        }
        void resetInventorySetting(nobBaseWarehouse& storehouse, Job job)
        {
            setInventorySetting(storehouse, job, InventorySetting());
        }

        std::set<MapPoint> barracks;
        void makeBarracks(nobBaseWarehouse& storehouse);

        // void issueSupplyChain(ProximityInfo::Kind kind, Action* parentAction);

        void adjustMines();
        void buildMines();
        void handleDonkeyBreeding();
        void handleMetalworks();
        void handleArmories();
        void handleFarms();
        void handleLookouttowers();

        void handleFishery();

        bool shouldBuildForester(MapPoint woodcutterPt);
        void handleWoodcutter();

        void resetMilitarySettings();

        bool updateMilitarySettings = true;
        unsigned militarySettingsLastChangeGF = 0;
        MilitarySettings militarySettings;

        void adjustMilitaryBuildings(unsigned gf);
        bool handleMilitaryBuildings(unsigned gf);

        std::set<nobMilitary*> coinBldgs;
        std::set<unsigned> enemiesToAttack;
        std::set<MapPoint> coveredMilitaryBuildings;
        std::map<MapPoint, unsigned> catapultAndLookoutIssued;
        std::map<MapPoint, unsigned> catapultAndStorehouseIssued;
        // std::set<MapPoint> withLookout;

        /// {{

        bool identifyEmergencies();
        bool handleEmergency();

        bool handleSpaceEmergency();
        bool handleBoardEmergency();
        bool handleStoneEmergency();
        bool handleWarEmergency(unsigned gf);

        EmergencyInfo emergencyInfo;
        /// }}

        /// {{

        bool isToolRequested(GoodType tool);
        bool requestTool(GoodType tool, unsigned char amount = 1);
        bool ensureTool(boost::optional<GoodType> tool, unsigned char amount = 1, bool inInventory = false);
        bool ensureJob(boost::optional<Job> job, unsigned char amount = 1, bool inInventory = false);
        bool ensureResources(BuildingType buildingType);

        std::set<MapPoint> buildingsWithRequestedTools;
        int toolsRequested = 0;
        ToolSettings toolSettings{};

        /// }}

        void registerOutOfResourceBuilding(MapPoint pt, BuildingType buildingType);

        /// {{

        unsigned getRoadSegmentProductivity(unsigned gf, RoadSegment* roadSegment);

        bool createSecondaryRoad(MapPoint pt);
        void createSecondaryRoads(unsigned gf);
        bool destroyUnusedRoads(unsigned gf);
        bool reconnectFlags();

        std::map<MapPoint, unsigned> flagAgeMap;
        bool checkRoads = false;
        bool shouldReconnect = false;
        /// }}

        /// {{

        unsigned lookupGoodInInventory(GoodType good);
        unsigned lookupGoodInStorehouses(GoodType good);
        unsigned lookupJobInInventory(Job job, bool includeTools = false);
        unsigned lookupJobInStorehouses(Job job, bool includeTools = false);
        unsigned lookupJobInStorehouses(boost::optional<Job> job, bool includeTools = false)
        {
            if(!job)
                return 0;
            return lookupJobInStorehouses(*job, includeTools);
        }

        void reserveJob(boost::optional<Job> job);
        void unreserveJob(boost::optional<Job> job)
        {
            if(job)
                reservedInventory.Remove(*job);
        }
        void reserveGood(GoodType good, unsigned amount = 1)
        {
            if(good != GD_NOTHING)
                reservedInventory.Add(good, amount);
        }
        void unreserveGood(GoodType good, unsigned amount = 1)
        {
            if(good != GD_NOTHING)
                reservedInventory.Remove(good, amount);
        }

        Inventory reservedInventory;
        /// }}

        void chat(const std::string& Msg);

        bool isDefeated();

        /// {{

        unsigned getNumMilitaryBuildingSites();
        bool shouldExpand();
        void expand(bool force);

        std::set<MapPoint> currentTargets;
        /// }}

        bool handleRoads(unsigned gf);

        /// Specialist utilities {{

        /// TODO
        void handleSpecialists(unsigned gf);
        void handleGeologists(unsigned gf);
        void handleScouts(unsigned gf);

        bool WaitingForGeologistAction = false;
        unsigned numTotalGeologists = 0;
        unsigned numGeologistsInStore = 0;
        std::array<Action*, 3> currentGeologistExpeditionActions{};
        std::map<MapPoint, int> currentGeologistExpeditions;
        /// }}

        /// {{

        void addPostActionGlobalAdjustmentHook(Action* action, unsigned thing);

        template<typename HookTy>
        void addPostActionHook(Action* action, HookTy hook)
        {
            Action::DeletionCallbackTy deletionCallback = hook;
            action->deletionCallbacks.push_back(deletionCallback);
        }

        std::set<Action*> actions;
        /// }}

        std::map<MapPoint, std::function<void(MapPoint, bool)>> buildingCallbacks;

        std::map<MapPoint, BuildingType> finishedBuildings;

        std::map<unsigned, std::vector<BuildingType>> consumerMap;
        std::map<unsigned, std::vector<BuildingType>> producerMap;

        std::vector<Subscription> notificationHandles;

        unsigned updateNo = 0;
    };
}} // namespace AI::jd

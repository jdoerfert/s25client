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

#include "Actions.h"
#include "Buildings.h"
#include "Utils.h"

#include "ai/AIPlayer.h"
#include "notifications/Subscription.h"
#include "gameData/BuildingConsts.h"
#include "buildings/nobMilitary.h"
#include "gameTypes/BuildingType.h"

#include <set>
#include <unordered_set>
#include <vector>

namespace AI { namespace jd {

    // {{{ Map Utility Classes

    struct Tracker
    {
        Tracker()
        {
            clear();
            assert(!any());
        }

        friend StreamTy& operator<<(StreamTy& out, const Tracker& obj)
        {
            for(unsigned u = 0; u < size(); ++u)
            {
                if(obj.values[u] == 0)
                    continue;
                if(ResourceInfo::isa(u))
                    out << "[" << ResourceInfo::Kind(u) << ":" << obj.values[u] << "]";
                else if(CommodityInfo::isa(u))
                    out << "[" << CommodityInfo::Kind(u) << ":" << obj.values[u] << "]";
                else if(ConceptInfo::isa(u))
                    out << "[" << ConceptInfo::Kind(u) << ":" << obj.values[u] << "]";
                else
                    out << "[" << unsigned(ResourceInfo::FIRST) << "," << u << "," << unsigned(ResourceInfo::LAST) << " :" << obj.values[u]
                        << "]";
            }
            return out;
        }

        int& operator[](unsigned idx)
        {
            assert(idx < size());
            return values[idx];
        }

        Tracker& operator+=(const Tracker& other)
        {
            for(unsigned u = 0; u < size(); ++u)
                values[u] += other.values[u];
            return *this;
        }
        Tracker& operator-=(const Tracker& other)
        {
            for(unsigned u = 0; u < size(); ++u)
                values[u] -= other.values[u];
            return *this;
        }

        bool any() const
        {
            for(unsigned u = 0; u < size(); ++u)
                if(values[u])
                    return true;
            return false;
        }

        void clear()
        {
            for(unsigned u = 0; u < size(); ++u)
                values[u] = 0;
        }

        static std::size_t size() { return ConceptInfo::LAST; }

        std::array<int, ConceptInfo::LAST> values = {0};
    };

    struct Location : Base<Location>
    {
        Location() { localTracker[ResourceInfo::WATER] = 1; }

        bool visible = false;
        bool visitedByGeologist = false;

        int militaryInProximity = 0;

        MapPoint pt;

        int &getValue(unsigned idx) { return localTracker[idx]; }
        int getLocalResource(ResourceInfo::Kind kind) { return localTracker[kind]; }
        void setLocalResource(ResourceInfo::Kind kind, int value) { localTracker[kind] = value; }
        void setValue(unsigned idx, int value) { localTracker[idx] = value; }
        void addValue(unsigned idx, int value) { localTracker[idx] += value; }
        Tracker &getLocalResources() { return localTracker; }

        StreamTy& operator>>(StreamTy& out) const noexcept override
        {
#if 0
            switch(buildingQuality)
            {
                case BQ_NOTHING: out << "nothing"; break;
                case BQ_FLAG: out << "flag"; break;
                case BQ_HUT: out << "hut"; break;
                case BQ_HOUSE: out << "house"; break;
                case BQ_CASTLE: out << "castle"; break;
                case BQ_MINE: out << "mine"; break;
                case BQ_HARBOR: out << "harbor"; break;
            }
#endif
            return out << " @" << pt;
        }

        bool isEqual(const Location& other) const noexcept override { return pt == other.pt; }
        std::size_t hash() const noexcept override { return std::hash<MapPoint>{}(pt); }

      private:
        Tracker localTracker;
    };

    struct View : public NodeMapBase<Location>
    {
        View(AIPlayer& aiPlayer);

        void registerNotificationHandlers();
        void clearMapInfo(bool full);
        void clearCaches(bool full);
        void update(bool full);
        void updateLocation(MapPoint pt, bool globals);
        void updateResource(MapPoint pt, bool globals);

        void registerLostBuilding(MapPoint pt, BuildingType buildingType);
        void registerNewBuilding(MapPoint pt, BuildingType buildingType);
        void registerOutOfResourceBuilding(MapPoint pt, BuildingType buildingType);

        bool canBuildFlag(MapPoint  pt, bool orIsFlag = true);
        bool landIsUsed(MapPoint pt)
        {
          Location &location = (*this)[pt];
          return location.getValue(ConceptInfo::FARM_SPACE) + location.getValue(ConceptInfo::TREE_SPACE);
        }

        Score getCloseByPenalty(MapPoint pt, BuildingQuality buildingQuality, MapPoint prevPt= MapPoint());
        Score getConnectionPenalty(const RoadRequest::Constraints& constraints);
        Score getBuildingScore(MapPoint  pt, const BuildingInfo& buildingInfo);
        void averageSurfaceResources(MapPoint pt);

        BuildingQuality getBuildingQuality(MapPoint  pt);
        bool wasVisitedByGeologist(MapPoint  pt);
        bool isRoadOrFlag(MapPoint pt);

        unsigned getConnectedRoads(MapPoint pt);
        unsigned getNumBuildings(BuildingType buildingType) {
          return buildings[buildingType].size();
        }

        MapPoint findBuildingPosition(const BuildingRequest::Constraints& constraints, Score &score);

        MapPoint findConnectionPoint(const RoadRequest::Constraints& constraints, std::vector<Direction>& directions, Score &score);
        bool findRoadConnection(const RoadRequest::Constraints& constraints, std::vector<Direction>& directions, Score &score);

        std::map<std::pair<MapPoint, MapPoint>, unsigned> connectionLengthCache;
        unsigned getConnectionLength(const RoadRequest::Constraints& constraints);
        void precomputeConnections(const MapPoint &pt);

        void getRoadSCCs(std::vector<std::vector<MapPoint>> &sccs, std::vector<MapPoint> &singletonFlags);

        bool requiresFullUpdate = true;

        Tracker globalResources{};
        Tracker storehouseResources{};

        std::map<MapPoint, Score> cachedConnectionScores;
        std::map<MapPoint, Score> cachedLocationScores;
        std::unordered_map<MapPoint, std::array<Score, NUM_BUILDING_TYPES>> cachedBuildingScores;
        std::unordered_set<MapPoint> potentialBuildingSites;
        std::set<MapPoint> flagPts;
        std::set<MapPoint> singletonFlagPts;
        std::vector<Subscription> notificationHandles;
        MapExtent visibleframeMin;
        MapExtent visibleFrameMax{0, 0};
        AIPlayer& aiPlayer;

        std::array<std::map<MapPoint, Building>, NUM_BUILDING_TYPES> buildings;

        template<typename T>
        bool foreachBuilding(BuildingType buildingType, T CB) {
          for (auto &it : buildings[buildingType])
            if (!CB(it.first, it.second))
              return false;
          return true;
        }

        template<typename T>
        bool foreachStorehouse(T CB, bool IncludeHQ = true) {
          if (IncludeHQ)
            if (!foreachBuilding(BLD_HEADQUARTERS, CB))
              return false;
          return foreachBuilding(BLD_STOREHOUSE, CB);
        }

        bool followRoadAndDestroyFlags(AIInterface& aii, MapPoint roadPt, unsigned allowedRoads, unsigned lastDir = -1, bool destroyBuilding = false, bool rebuildingRoads = false);
        bool destroyFlag(MapPoint pt, unsigned allowedRoads, bool destroyBuilding = false, unsigned lastDir = -1, bool rebuildingRoads = false);

        MapPoint bldg2flag(MapPoint pt) const { return GetNeighbour(pt, Direction::SOUTHEAST); }
        MapPoint flag2bldg(MapPoint pt) const { return GetNeighbour(pt, Direction::NORTHWEST); }
        Direction reverseDirection(Direction dir) const { return dir + 3; }

        void setLocalTracker(MapPoint  pt, int radius, unsigned idx, int value);
        void addToLocalTracker(MapPoint  pt, int radius, unsigned idx, int difference);
        int sumLocalTrackerValues(MapPoint  pt, int radius, unsigned idx, bool requireReachable = false);
        int getGlobalCommodity(CommodityInfo::Kind kind);

        //void requestResourceGlobally(int radius, ProximityInfo::Kind kind, bool ifNotRequested)

        std::vector<unsigned> enemies;

        struct MilitaryBldgCmp {
            bool operator()(const nobMilitary* lhs, const nobMilitary* rhs) const { return lhs->GetMaxTroopsCt() < rhs->GetMaxTroopsCt(); }
        };
        std::vector<std::set<nobMilitary*, MilitaryBldgCmp>> enemyMilitaryBuildings;

        std::vector<std::pair<MapPoint, unsigned>> mountains;
        std::vector<MapPoint> enemyHQs;
        std::vector<MapPoint> enemyLand;
        std::vector<MapPoint> friendHQs;

        unsigned numFullUpdate = 0;
    };

    // }}}

}} // namespace AI::jd

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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <list>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Utils.h"
#include "ai/AIPlayer.h"
#include "notifications/RoadNote.h"

namespace AI { namespace jd {

    struct View;
    class ActionManager;
    class PlayerJD;

    enum Lifetime
    {
        PERMANENT,
        TEMPORARY,
    };

    class Action;
    using ActionPtr = Action*;

    class Action
    {
    public:
        enum Status
        {
            NEW,
            WAITING,
            UNDONE,
            RETRY,
            SUCCESS,
            FAILED,
            ABORTED,
        };

        const char* const ID;

    protected:
        Action* parentAction = nullptr;
        std::vector<Action*> subActions;

        unsigned outstandingSubActions = 0;
        Status status = NEW;

    public:
        Action(const char* ID) : ID(ID) {}

        using DeletionCallbackTy = std::function<void(void)>;
        std::vector<DeletionCallbackTy> deletionCallbacks;

        virtual ~Action()
        {
            for(auto& deletionCallback : deletionCallbacks)
                deletionCallback();
        }

        template<typename T>
        T* dyn_cast()
        {
            if(ID == &T::ID)
                return static_cast<T*>(this);
            return nullptr;
        }

        friend StreamTy& operator<<(StreamTy& out, const Action::Status& status)
        {
            ENUM_PRINT_BEGIN(status);
            ENUM_PRINT(out, NEW);
            ENUM_PRINT(out, WAITING);
            ENUM_PRINT(out, UNDONE);
            ENUM_PRINT(out, RETRY);
            ENUM_PRINT(out, SUCCESS);
            ENUM_PRINT(out, FAILED);
            ENUM_PRINT(out, ABORTED);
            ENUM_PRINT_END();
            return out;
        }

        bool hasChildren() const { return !subActions.empty(); }

        virtual void print(StreamTy& out) const noexcept;
        void printChildren(StreamTy& out) const noexcept;

        friend StreamTy& operator<<(StreamTy& out, const Action& obj) noexcept
        {
            obj.print(out);
            return out;
        }

        void addSubAction(ActionPtr t)
        {
            assert(t && !t->parentAction);
            t->parentAction = this;
            subActions.push_back(t);
            assert(t->status == NEW);
            ++outstandingSubActions;
        }

        void setStatus(Status newStatus);

        bool hasStarted() const { return status != NEW; }
        bool hasFinished() const
        {
            return status == SUCCESS || status == FAILED || status == UNDONE || status == ABORTED;
        }
        bool isRetired() const { return retired; }
        Status getStatus() const { return status; }
        bool isExplicitRequest() const { return parentAction; }
        Action* getParent() const { return parentAction; }

        virtual bool execute(ActionManager& actionManager) = 0;
        virtual bool undo(ActionManager& actionManager)
        {
            if(getStatus() == UNDONE)
                return false;
            setStatus(UNDONE);
            for(auto* subAction : subActions)
                subAction->undo(actionManager);
            return true;
        }
        virtual void finalize(ActionManager&) { retired = true; }

    protected:
        virtual void indicateSubActionFinished(Action& subAction, bool justFinished)
        {
            assert(outstandingSubActions > 0 || !justFinished);
            if(subAction.status == FAILED)
                setStatus(FAILED);
            if(justFinished)
                if(--outstandingSubActions == 0)
                    if(status == WAITING)
                        setStatus(SUCCESS);
        }

        bool retired = false;
        friend class ActionManager;
    };

    struct GroupAction : public Action
    {
        static char ID;
        enum Kind
        {
            EXECUTE,
            UNDO,
            ANY,
        };

        GroupAction(Kind kind) : Action(&ID), kind(kind) {}

        bool execute(ActionManager& actionManager) override;

        void finalize(ActionManager& actionManager) override
        {
            for(Action* subAction : subActions)
                if(!subAction->isRetired() && subAction->hasFinished())
                    subAction->finalize(actionManager);
            Action::finalize(actionManager);
        }

        virtual void print(StreamTy& out) const noexcept override;

        virtual void indicateSubActionFinished(Action& subAction, bool justFinished) override
        {
            if(kind == ANY && subAction.getStatus() == FAILED)
                subAction.setStatus(ABORTED);
            return Action::indicateSubActionFinished(subAction, justFinished);
        }

    private:
        unsigned idx = 0;
        const Kind kind;
    };

    struct ConstructAction : public Action
    {
        ConstructAction(const char* ID) : Action(ID) {}

        virtual void print(StreamTy& out) const noexcept override;
    };

    struct BuildingRequest : public ConstructAction
    {
        static char ID;

        struct Constraints
        {
            const BuildingType buildingType;
            const MapPoint requestedPt = MapPoint();
            const MapPoint closeToPt = MapPoint();
            unsigned closeToDistance = -1;

            friend StreamTy& operator<<(StreamTy& out, const BuildingRequest::Constraints& obj)
            {
                out << obj.buildingType;
                if(obj.requestedPt.isValid())
                    out << " @ " << obj.requestedPt;
                else if(obj.closeToPt.isValid())
                    out << " @ " << obj.closeToDistance << " radious around " << obj.closeToPt;
                return out;
            }
        };

        bool execute(ActionManager& actionManager) override;
        bool undo(ActionManager& actionManager) override;
        void finalize(ActionManager& actionManager) override;

        virtual void print(StreamTy& out) const noexcept override;

        BuildingRequest(ActionManager& actionManager, BuildingType buildingType, MapPoint closeToPt = MapPoint(),
                        unsigned closeToDistance = -1);
        BuildingRequest(ActionManager& actionManager, const Constraints& constraints);
        static BuildingRequest *getBuildingRequestAt(ActionManager& actionManager, BuildingType buildingType,
                                                    MapPoint requestedPt)
        {
            Constraints constraints{buildingType, requestedPt};
            return new BuildingRequest(actionManager, constraints);
        }

    private:
        MapPoint originPt;
        MapPoint flagPt;

        const Constraints constraints;
        MapPoint getFlagPt() const { return flagPt; }
        MapPoint getCloseToPt() const { return constraints.closeToPt; }
        BuildingType getBuildingType() const { return constraints.buildingType; }

        friend class ActionManager;
    };

    struct RoadRequest : public Action
    {
        static char ID;

        struct Constraints
        {
            const MapPoint fromPt;
            const MapPoint requestedToPt = MapPoint();
            bool excludeFromPt = false;
            bool excludeFromBldgPt = false;

            uint64_t maximalCost = std::numeric_limits<int64_t>::max() - 1;

            Lifetime lifetime = PERMANENT;

            const std::vector<MapPoint>* excludedPts = nullptr;

            enum
            {
                BEST,
                SHORTEST,
                TERRAIN,
                SECONDARY,
            } type = BEST;

            enum
            {
                NONE,
                SCOUT,
                GEOLOGIST,
            } specialistKind = NONE;
            unsigned numSpecialists = 1;

            friend StreamTy& operator<<(StreamTy& out, const RoadRequest::Constraints& obj)
            {
                out << obj.fromPt << " --> ";
                if(obj.requestedToPt.isValid())
                    out << obj.requestedToPt;
                else
                    out << "any flag";
                out << " [lifetime: " << obj.lifetime << "]";
                if(obj.maximalCost != std::numeric_limits<int64_t>::max() - 1)
                    out << " [cost< " << obj.maximalCost << "]";
                out << "[kind: " << unsigned(obj.type) << "]";
                return out;
            }
        };

        bool execute(ActionManager& actionManager) override;
        bool undo(ActionManager& actionManager) override;
        void finalize(ActionManager& actionManager) override;

        virtual void print(StreamTy& out) const noexcept override;

        MapPoint getRequestedToPt() const { return constraints.requestedToPt; }
        bool hasRequestedToPt() const { return constraints.requestedToPt.isValid(); }
        MapPoint getFromPt() const { return constraints.fromPt; }
        MapPoint getToPt() const { return toPt; }

    private:
        RoadRequest(ActionManager&, Constraints constraints)
            : Action(&ID), constraints(constraints), toPt(constraints.requestedToPt)
        {}

        const Constraints constraints;
        construction::Route route;
        MapPoint toPt;

        friend class ActionManager;
    };

    struct FlagRequest : public Action
    {
        static char ID;

        struct Constraints
        {
            const MapPoint flagPt;
            Lifetime lifetime = PERMANENT;

            friend StreamTy& operator<<(StreamTy& out, const FlagRequest::Constraints& obj)
            {
                return out << "  @ " << obj.flagPt << " [lifetime: " << obj.lifetime << "]";
            }
        };

        bool execute(ActionManager& actionManager) override;
        bool undo(ActionManager& actionManager) override;
        void finalize(ActionManager& actionManager) override;

        virtual void print(StreamTy& out) const noexcept override;
        MapPoint getFlagPt() const { return constraints.flagPt; }

    private:
        FlagRequest(ActionManager&, Constraints constraints) : Action(&ID), constraints(constraints) {}

        const Constraints constraints;
        friend class ActionManager;
    };

    struct SpecialistRequest : public Action
    {
        static char ID;

        struct Constraints
        {
            const MapPoint flagPt;
            const Job job;
            const unsigned amount = 3;

            friend StreamTy& operator<<(StreamTy& out, const SpecialistRequest::Constraints& obj)
            {
                return out << (obj.job == JOB_GEOLOGIST ? "geologist" : "scout") << " @ " << obj.flagPt;
            }
        };

        bool execute(ActionManager& actionManager) override;

        virtual void print(StreamTy& out) const noexcept override;

        MapPoint getFlagPt() const { return constraints.flagPt; }

    private:
        SpecialistRequest(ActionManager& actionManager, Constraints constraints);

        const Constraints constraints;
        friend class ActionManager;
    };

    struct DestroyRequest : public Action
    {
        static char ID;

        struct Constraints
        {
            const MapPoint pt;

            friend StreamTy& operator<<(StreamTy& out, const DestroyRequest::Constraints& obj)
            {
                return out << "Destroy building @ " << obj.pt;
            }
        };

        bool execute(ActionManager& actionManager) override;

        virtual void print(StreamTy& out) const noexcept override;

        DestroyRequest(ActionManager& actionManager, Constraints constraints);

    private:
        const Constraints constraints;
        friend class ActionManager;
    };

    class ActionManager
    {
        PlayerJD& playerJD;
        View& view;

        std::list<ActionPtr> actions;

        std::vector<Subscription> notificationHandles;

        std::vector<std::pair<unsigned, ActionPtr>> delayedActions;

        using TriggerTy = std::function<bool(ActionManager&)>;
        std::vector<std::pair<TriggerTy, ActionPtr>> triggeredActions;

    public:
        ActionManager(PlayerJD& playerJD, View& view);

        std::set<RoadRequest*> pendingRoads;

        void execute();
        void clear();

        std::size_t getNumPendingActions() const { return actions.size(); }

        View& getView() const { return view; }
        PlayerJD& getPlayer() const { return playerJD; }
        AIInterface& getAII() const;

        // void addAction(Action & actionPtr) { actions.push_back(actionPtr); }

        void appendAction(Action* action) { actions.push_back(action); }
        ActionPtr requestGeologists(Action* parentAction, MapPoint toPt, unsigned numGeologists);

        ActionPtr constructBuilding(Action* parentAction, BuildingType buildingType, MapPoint closeToPt = MapPoint(),
                                    unsigned closeToDistance = -1);
        ActionPtr constructFlag(Action* parentAction, const FlagRequest::Constraints& constraints);
        ActionPtr constructFlag(Action* parentAction, MapPoint flagPt)
        {
            return constructFlag(parentAction, FlagRequest::Constraints{flagPt});
        }

        ActionPtr constructRoad(Action* parentAction, const RoadRequest::Constraints& constraints);
        ActionPtr constructRoad(Action* parentAction, MapPoint fromPt)
        {
            return constructRoad(parentAction, RoadRequest::Constraints{fromPt});
        }
    };

}} // namespace AI::jd

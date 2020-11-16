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

#include "Actions.h"

#include "AIPlayerJD.h"
#include "Utils.h"
#include "View.h"
#include "notifications/BuildingNote.h"
#include "notifications/RoadNote.h"

#include "pathfinding/FreePathFinder.h"
#include "pathfinding/PathConditionRoad.h"
#include "pathfinding/RoadPathFinder.h"

#define _TAG_ "[ACTIONS]"

//#undef SAY
//#define SAY(msg, ...) (tprintf((_TAG_ " " msg), __VA_ARGS__), std::cout << std::endl);

using namespace AI;
using namespace jd;

BuildingRequest::BuildingRequest(ActionManager& actionManager, const Constraints& constraints)
    : ConstructAction(&ID), constraints(constraints)
{
    SAY("New %", *this);

    AIInterface& aii = actionManager.getAII();
    if(!aii.CanBuildBuildingtype(getBuildingType()))
    {
        setStatus(FAILED);
        return;
    }
}

BuildingRequest::BuildingRequest(ActionManager& actionManager, BuildingType buildingType, MapPoint closeToPt, unsigned closeToDistance)
    : BuildingRequest(actionManager, {buildingType, MapPoint(), closeToPt, closeToDistance})
{}

bool BuildingRequest::execute(ActionManager& actionManager)
{
    if(getStatus() != NEW && getStatus() != RETRY)
        return false;

    auto Fail = [&]() {
        if(getStatus() == NEW)
            setStatus(RETRY);
        else
            setStatus(FAILED);
        return true;
    };

    SAY("Execute %%", *this, isExplicitRequest() ? " explicit" : "");

    AIInterface& aii = actionManager.getAII();
    if(!aii.CanBuildBuildingtype(getBuildingType()))
    {
        SAY("Cannot build type %", getBuildingType());
        return Fail();
    }

    flagPt = MapPoint();

    bool available = true;
    View& view = actionManager.getView();
    auto ConsumptionCallback = [&](const BuildingEffect& consumption) {
        int required = consumption.getAmount();
        if(consumption.isGlobal() && view.globalResources[consumption.getThing()] < required)
        {
            SAY("3 Require % % but globally we have only %", required, consumption.getThing(),
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
                    if(view.globalResources[consumption.getThing()] + 2 >= required)
                    {
                        SAY("Ignored due ot importance", "");
                        return true;
                    }
                    break;
                default: //
                    break;
            };
            available = false;
            return false;
        }
        return true;
    };
    BuildingInfo::get(getBuildingType()).foreachConsumption(ConsumptionCallback);
    if(!available)
        return Fail();

    if (constraints.requestedPt.isValid()) {
      originPt =constraints.requestedPt;
    } else {
      Score score;
      originPt = view.findBuildingPosition(constraints, score);
      SAY("Building position % % [%]", originPt, constraints, score);
    }

    if(!originPt.isValid())
        return Fail();

    if(!aii.SetBuildingSite(originPt, constraints.buildingType))
    {
        SAY("PROBLEM: MIN ELEMENT NOT USABLE %", originPt);
        view.clearCaches(true);
        return Fail();
    }
    SAY("Building position %", originPt);

    flagPt = view.bldg2flag(originPt);

    if(aii.IsObjectTypeOnNode(getFlagPt(), NodalObjectType::NOP_FLAG))
    {
        setStatus(SUCCESS);
    } else
    {
        RoadRequest::Constraints constraints{getFlagPt()};
        constraints.excludeFromPt = true;
        constraints.lifetime = PERMANENT;
        if(getBuildingType() == BLD_FORESTER || getBuildingType() == BLD_BARRACKS)
            constraints.lifetime = TEMPORARY;
        actionManager.constructRoad(this, constraints);
        setStatus(WAITING);
    }

    return true;
}

bool BuildingRequest::undo(ActionManager& actionManager)
{
    if(!Action::undo(actionManager))
        return false;

    if(!originPt.isValid())
        return false;

    SAY("Undo %", *this);

    AIInterface& aii = actionManager.getAII();
    if(!aii.IsBuildingOnNode(originPt, getBuildingType()))
    {
        SAY("Expected building @ % not found! Abort undo!", originPt);
        return false;
    }
    aii.DestroyBuilding(originPt);

    View& view = actionManager.getView();
    if(!aii.IsObjectTypeOnNode(getFlagPt(), NodalObjectType::NOP_FLAG))
    {
        SAY("Expected flag @ % not found! Abort undo!", getFlagPt());
        return true;
    }
    view.destroyFlag(getFlagPt(), 1);
    return true;
}

void BuildingRequest::finalize(ActionManager& actionManager)
{
    SAY("FINALIZE %", *this);
    Action::finalize(actionManager);
    if(getStatus() != SUCCESS)
        return;
    // View& view = actionManager.getView();
    // view.registerNewBuilding(originPt, getBuildingType());

    // RoadRequest::Constraints constraints{getFlagPt()};
    // constraints.excludeFromPt = true;
    // constraints.type = RoadRequest::Constraints::SECONDARY;
    // actionManager.constructRoad(nullptr, constraints);

    View& view = actionManager.getView();
    view.flagPts.insert(getFlagPt());
}

bool RoadRequest::execute(ActionManager& actionManager)
{
    switch(getStatus())
    {
        case NEW: break;
        case RETRY: break;
        case UNDONE:
        case SUCCESS:
        case FAILED:
        case ABORTED: assert(0 && "Did not expect to execute finished action");
        case WAITING: setStatus(FAILED); return false;
    }

    SAY("Execute %", *this);

    auto Fail = [&]() {
        if(getStatus() == NEW)
            setStatus(RETRY);
        else
            setStatus(FAILED);
        return true;
    };

    View& view = actionManager.getView();
    AIInterface& aii = actionManager.getAII();
    if(!aii.IsObjectTypeOnNode(getFromPt(), NOP_FLAG))
    {
        SAY("From pt % has no flag!", getFromPt());
        return Fail();
    }

    Score score;
    toPt = view.findConnectionPoint(constraints, route.directions, score);
    SAY("Construct road from % to % via %", getFromPt(), toPt, route);
    if(!toPt.isValid())
        return Fail();
    if(toPt == getFromPt())
        return true;
    if(route.directions.size() < 2)
        return Fail();

    if(constraints.type == Constraints::BEST)
    {
        RoadRequest::Constraints roadConstraints{getFromPt(), toPt};
        std::vector<Direction> roadDirections;
        Score roadScore;
        if(view.findRoadConnection(roadConstraints, roadDirections, roadScore))
        {
            SAY("Existing road connection found % length and % score: %: %", roadDirections.size(), roadScore,
                construction::Route(roadDirections));
            return Fail();
        }
    }

    if(!aii.BuildRoad(getFromPt(), false, route.directions))
    {
        SAY("AII failed to build road\n", "");
        return Fail();
    }

    if(status == NEW)
        setStatus(WAITING);

    std::vector<MapPoint> flagPts;
    MapPoint curPt = getFromPt();
    for(auto& direction : route.directions)
    {
        curPt = view.GetNeighbour(curPt, direction);
        if(view.getBuildingQuality(curPt) > BuildingQuality::BQ_NOTHING)
            flagPts.push_back(curPt);
    }

    //Action* groupAction = new GroupAction(GroupAction::ANY);
    //addSubAction(groupAction);
    //actionManager.appendAction(groupAction);

    for(auto it = flagPts.rbegin(), end = flagPts.rend(); it != end; ++it)
    {
        FlagRequest::Constraints flagConstraints{*it};
        flagConstraints.lifetime = constraints.lifetime;
        actionManager.constructFlag(nullptr, flagConstraints);
    }

    actionManager.pendingRoads.insert(this);
    return true;
}

bool RoadRequest::undo(ActionManager& actionManager)
{
    actionManager.pendingRoads.erase(this);
    if(!Action::undo(actionManager))
        return false;
    if(!getFromPt().isValid() || route.directions.empty())
        return false;

    SAY("Undo %", *this);

    SAY("Destroy road %", route);
    View& view = actionManager.getView();
    MapPoint curPt = getFromPt();
    for(unsigned u = 0, e = route.directions.size(); u + 1 < e; ++u)
    {
        curPt = view.GetNeighbour(curPt, route.directions[u]);
        if(view.flagPts.count(curPt) && !view.destroyFlag(curPt, 2, /* destroyBuilding */ false, u))
            break;
    }

    route.directions.clear();
    return true;
}

void RoadRequest::finalize(ActionManager& actionManager)
{
    SAY("FINALIZE %", *this);
    Action::finalize(actionManager);
    actionManager.pendingRoads.erase(this);
    if(getStatus() != SUCCESS)
        return;

    AIInterface& aii = actionManager.getAII();
    switch(constraints.specialistKind)
    {
        case Constraints::NONE: break;
        case Constraints::SCOUT:
            for(unsigned u = 0; u < constraints.numSpecialists; ++u)
                aii.CallSpecialist(getFromPt(), JOB_SCOUT);
            break;
        case Constraints::GEOLOGIST:
            for(unsigned u = 0; u < constraints.numSpecialists; ++u)
                aii.CallSpecialist(getFromPt(), JOB_GEOLOGIST);
            assert(constraints.lifetime == TEMPORARY);
            break;
    };

    // View& view = actionManager.getView();
    // if(!route.directions.empty() && aii.IsBuildingOnNode(view.flag2bldg(getFromPt()), BLD_HEADQUARTERS))
    // aii.UpgradeRoad(getFromPt(), route.directions.front());
    // if(!route.directions.empty() && aii.IsBuildingOnNode(view.flag2bldg(getToPt()), BLD_HEADQUARTERS))
    // aii.UpgradeRoad(getToPt(), route.directions.back() + 3);

    // if(constraints.lifetime == PERMANENT)
    //{
    // RoadRequest::Constraints fromConstraints{getFromPt()};
    // fromConstraints.excludeFromPt = true;
    // fromConstraints.type = RoadRequest::Constraints::SECONDARY;
    // actionManager.constructRoad(nullptr, fromConstraints);
    // RoadRequest::Constraints toConstraints{getToPt()};
    // toConstraints.excludeFromPt = true;
    // toConstraints.type = RoadRequest::Constraints::SECONDARY;
    // actionManager.constructRoad(nullptr, toConstraints);
    //}
}

bool FlagRequest::execute(ActionManager& actionManager)
{
    AIInterface& aii = actionManager.getAII();
    if(aii.IsObjectTypeOnNode(getFlagPt(), NodalObjectType::NOP_FLAG))
    {
        setStatus(SUCCESS);
        return false;
    }
    if(!aii.SetFlag(getFlagPt()))
    {
        setStatus(FAILED);
        return false;
    }

    if(getStatus() == WAITING)
    {
        setStatus(FAILED);
        return false;
    }

    setStatus(WAITING);
    // return getParent() != nullptr;
    return true;
}

void FlagRequest::finalize(ActionManager& actionManager)
{
    SAY("FINALIZE %", *this);
    Action::finalize(actionManager);
    if(getStatus() != SUCCESS)
        return;

    AIInterface& aii = actionManager.getAII();
    if(!aii.IsObjectTypeOnNode(getFlagPt(), NodalObjectType::NOP_FLAG))
    {
        setStatus(FAILED);
        return;
    }

    // if(constraints.lifetime == PERMANENT)
    //{
    // RoadRequest::Constraints constraints{getFlagPt()};
    // constraints.excludeFromPt = true;
    // constraints.type = RoadRequest::Constraints::SECONDARY;
    // actionManager.constructRoad(nullptr, constraints);
    //}

    View& view = actionManager.getView();
    view.flagPts.insert(getFlagPt());
}

bool FlagRequest::undo(ActionManager& actionManager)
{
    if(getStatus() != SUCCESS)
        return false;
    if(!Action::undo(actionManager))
        return false;
    SAY("Undo %", *this);

    SAY("Destroy flag %", getFlagPt());
    View& view = actionManager.getView();
    return view.destroyFlag(getFlagPt(), 0);
}

SpecialistRequest::SpecialistRequest(ActionManager& actionManager, Constraints constraints) : Action(&ID), constraints(constraints)
{
    SAY("[NEW][SPECIALIST] %", *this);
}

bool SpecialistRequest::execute(ActionManager& actionManager)
{
    AIInterface& aii = actionManager.getAII();
    bool calledOne = false;
    for(unsigned u = 0; u < constraints.amount; ++u)
    {
        if(!aii.CallSpecialist(getFlagPt(), JOB_GEOLOGIST))
        {
            setStatus(calledOne ? SUCCESS : FAILED);
            return calledOne;
        }
        calledOne = true;
    }
    setStatus(SUCCESS);
    return calledOne;
}

DestroyRequest::DestroyRequest(ActionManager& actionManager, Constraints constraints) : Action(&ID), constraints(constraints)
{
    SAY("[NEW][Destroy] %", *this);
}

bool DestroyRequest::execute(ActionManager& actionManager)
{
    AIInterface& aii = actionManager.getAII();
    if(!aii.DestroyBuilding(constraints.pt))
    {
        setStatus(FAILED);
        return false;
    }
    setStatus(SUCCESS);
    return true;
}

bool GroupAction::execute(ActionManager& actionManager)
{
    setStatus(WAITING);
    SAY("Execute group action %", *this);
    if(subActions.size() > idx)
    {
        Action* subAction = subActions[idx];
        if(kind == EXECUTE)
        {
            if(!subAction->hasFinished())
                return subAction->execute(actionManager);
            ++idx;
            return execute(actionManager);
        }
        if(kind == UNDO)
        {
            if(undo(actionManager))
                return true;
            ++idx;
            return execute(actionManager);
        }
        if(kind == ANY)
        {
            if(subAction->hasFinished())
            {
                if(subAction->getStatus() == SUCCESS)
                {
                    for(; idx < subActions.size(); ++idx)
                        if(!subActions[idx]->hasFinished())
                            subActions[idx]->setStatus(ABORTED);
                    setStatus(SUCCESS);
                    return false;
                }
                ++idx;
                return execute(actionManager);
            }
            return subAction->execute(actionManager);
        }
    }

    if(kind == ANY)
    {
        setStatus(FAILED);
        return false;
    }

    setStatus(SUCCESS);
    return false;
}

ActionManager::ActionManager(PlayerJD& playerJD, View& view) : playerJD(playerJD), view(view)
{
    unsigned char playerId = playerJD.GetPlayerId();

    NotificationManager& notifications = playerJD.gwb.GetNotifications();
    notificationHandles.emplace_back(notifications.subscribe<BuildingNote>([=](const BuildingNote& note) {
        if(note.player != playerId)
            return;
        SAY("Building note %, type % @ %", note.bld, note.type, note.pos);
        switch(note.type)
        {
            case BuildingNote::Constructed: break;
            case BuildingNote::Destroyed: break;
            case BuildingNote::Captured: return;
            case BuildingNote::Lost: break;
            case BuildingNote::NoRessources: return;
            case BuildingNote::LuaOrder: return;
            case BuildingNote::LostLand: return;
        };
    }));
    notificationHandles.emplace_back(notifications.subscribe<RoadNote>([=](const RoadNote& note) {
        if(note.player != playerId)
            return;
        SAY("Road from % via %, note: % ", note.pos, construction::Route(note.route), note.type);
        for(Action* action : pendingRoads)
        {
            RoadRequest* roadRequest = action->dyn_cast<RoadRequest>();
            if(!roadRequest || roadRequest->getStatus() != Action::WAITING)
                continue;
            if(roadRequest->getFromPt() != note.pos)
                continue;
            if(roadRequest->route.directions != note.route)
                continue;
            if(note.type == RoadNote::Constructed)
                roadRequest->setStatus(Action::SUCCESS);
            else
                roadRequest->setStatus(roadRequest->getStatus() == Action::WAITING ? Action::RETRY : Action::FAILED);
            return;
        }
        SAY("RoadNote could not be associated :(", "");
    }));
}

ActionPtr ActionManager::constructBuilding(Action* parentAction, BuildingType buildingType, MapPoint closeToPt, unsigned closeToDistance)
{
    // if(!closeToPt.isValid())
    // closeToPt = aiPlayer.playerJD.GetHQPos();

    ActionPtr actionPtr(new BuildingRequest(*this, buildingType, closeToPt, closeToDistance));
    actions.push_back(actionPtr);
    if(parentAction)
        parentAction->addSubAction(actionPtr);
    return actionPtr;
}

ActionPtr ActionManager::constructRoad(Action* parentAction, const RoadRequest::Constraints& constraints)
{
    ActionPtr actionPtr(new RoadRequest(*this, constraints));
    actions.push_front(actionPtr);
    if(parentAction)
        parentAction->addSubAction(actionPtr);
    return actionPtr;
}

ActionPtr ActionManager::constructFlag(Action* parentAction, const FlagRequest::Constraints& constraints)
{
    ActionPtr actionPtr(new FlagRequest(*this, constraints));
    actions.push_front(actionPtr);
    if(parentAction)
        parentAction->addSubAction(actionPtr);
    return actionPtr;
}

static unsigned countVisitedNeighbours(ActionManager& actionManager, MapPoint pt)
{
    View& view = actionManager.getView();
    unsigned sum = 0;
    for(MapPoint circlePt : view.GetPointsInRadiusWithCenter(pt, 5))
        sum += view.wasVisitedByGeologist(circlePt);
    return sum;
}

ActionPtr ActionManager::requestGeologists(Action* parentAction, MapPoint toPt, unsigned numGeologists)
{
    if(view.flagPts.count(toPt) && !view.singletonFlagPts.count(toPt))
    {
        ActionPtr actionPtr(new SpecialistRequest(*this, {toPt, JOB_GEOLOGIST, numGeologists}));
        if(parentAction)
            parentAction->addSubAction(actionPtr);
        return actionPtr;
    }

    FlagRequest::Constraints flagConstraints{toPt};
    flagConstraints.lifetime = TEMPORARY;
    ActionPtr flagAction(new FlagRequest(*this, flagConstraints));

    RoadRequest::Constraints roadConstraints{toPt};
    roadConstraints.lifetime = TEMPORARY;
    // roadConstraints.type = RoadRequest::Constraints::BEST;
    roadConstraints.excludeFromPt = true;
    roadConstraints.specialistKind = RoadRequest::Constraints::GEOLOGIST;
    roadConstraints.numSpecialists = numGeologists;
    ActionPtr roadAction = new RoadRequest(*this, roadConstraints);

    Action* outer = new GroupAction(GroupAction::EXECUTE);
    outer->addSubAction(flagAction);
    outer->addSubAction(roadAction);
    if(parentAction)
        parentAction->addSubAction(outer);
    return outer;

#if 0
    auto* state = new std::pair<bool, int>;
    *state = {false, countVisitedNeighbours(*this, toPt)};

    TriggerTy trigger = [=](ActionManager& actionManager) {
        int val = countVisitedNeighbours(actionManager, toPt);
        SAY("\nTEST TRIGGER % <% %>", val, state->first, state->second);
        if(!state->first && val <= state->second + 5)
            return false;
        if(!state->first)
        {
            if(view.getConnectedRoads(toPt) == 1)
                roadAction->undo(actionManager);
            state->second -= 25 * std::max(1U, (9 / numGeologists));
            state->first = true;
            SAY("[2] TEST TRIGGER % <% %>", val, state->first, state->second);
            return false;
        }
        if(state->second++ >= val)
            return false;
        delete state;
        return true;
    };

    Action* groupUndo = new GroupAction(GroupAction::UNDO);
    outer->addSubAction(groupUndo);
    groupUndo->addSubAction(flagAction);
    triggeredActions.push_back({trigger, groupUndo});
    if(parentAction)
        parentAction->addSubAction(groupUndo);
    return groupUndo;
#endif
}

void ActionManager::execute()
{
    RAIISayTimer RST(__PRETTY_FUNCTION__);
    for(unsigned u = 0, e = delayedActions.size(); u < e; ++u)
    {
        if(--delayedActions[u].first)
            continue;
        actions.push_back(delayedActions[u].second);

        std::swap(delayedActions[u], delayedActions[e - 1]);
        --u;
        --e;
        delayedActions.pop_back();
    }
    for(unsigned u = 0, e = triggeredActions.size(); u < e; ++u)
    {
        if(!triggeredActions[u].first(*this))
            continue;
        actions.push_back(triggeredActions[u].second);

        std::swap(triggeredActions[u], triggeredActions[e - 1]);
        --u;
        --e;
        triggeredActions.pop_back();
    }

    SAY("Actionmanager execute, % actions waiting", actions.size());

    if(actions.empty())
        return;

    for(auto* action : actions)
        SAY("%", *action);

    auto it = actions.begin(), end = actions.end();

    while(it != end)
    {
        Action* action = *it;
        if(action->hasFinished())
        {
            bool performedUndo = false;
            if(action->getStatus() == Action::FAILED)
                performedUndo = action->undo(*this);
            action->finalize(*this);
            it = actions.erase(it);
            if(performedUndo)
                return;
        } else
        {
            it++;
        }
    }

    bool plantedFlags = false;
    it = actions.begin();
    while(it != end)
    {
        if(plantedFlags && !(*it)->dyn_cast<FlagRequest>())
            return;
        if(!(*it)->hasFinished() && (*it)->execute(*this))
        {
            if(!(*it)->dyn_cast<FlagRequest>())
                return;
            plantedFlags = true;
        }

        ++it;
    }
}

void ActionManager::clear()
{
    for(Action* action : actions)
    {
        // if (action->dyn_cast<FlagRequest>() && !action->getParent())
        // continue;
        if(!action->hasStarted() && !action->getParent())
            action->setStatus(Action::ABORTED);
        else
            action->setStatus(Action::FAILED);
    }
}

AIInterface& ActionManager::getAII() const
{
    return playerJD.getAIInterface();
}

void Action::setStatus(Status newStatus)
{
    SAY("Action::setStatus % for %", newStatus, *this);

    if(status == newStatus)
        return;
    if(newStatus == SUCCESS && outstandingSubActions)
        newStatus = WAITING;

    bool wasFinished = hasFinished();
    assert(newStatus != NEW);

    status = newStatus;
    assert(!wasFinished || hasFinished());

    if(status == FAILED)
        for(Action* subAction : subActions)
            subAction->setStatus(FAILED);
    if(parentAction && hasFinished())
        parentAction->indicateSubActionFinished(*this, !wasFinished);
}
void Action::print(StreamTy& out) const noexcept
{
    std::string indent = "";
    Action* tmp = parentAction;
    while(tmp)
    {
        indent += "\t";
        tmp = tmp->parentAction;
    }

    out << indent << ":: Action(" << uintptr_t(this) << ") [";
    out << status << "]";
    if(parentAction)
        out << " [Parent:" << uintptr_t(parentAction) << "]";
    if(!subActions.empty())
        out << " [#children:" << subActions.size() << "]";
}
void Action::printChildren(StreamTy& out) const noexcept
{
    for(Action* subAction : subActions)
        out << *subAction;
}
void GroupAction::print(StreamTy& out) const noexcept
{
    Action::print(out);
    out << "Group[" << kind << ":" << idx << "]";
    out << "\n";
    printChildren(out);
}
void ConstructAction::print(StreamTy& out) const noexcept
{
    Action::print(out);
    out << "[Construct]";
}
void BuildingRequest::print(StreamTy& out) const noexcept
{
    ConstructAction::print(out);
    out << "[Constraints] " << constraints;
    if(originPt.isValid())
        out << "[Origin " << originPt << "]";
    else
        out << "[SEARCHING]";
    out << "\n";
    printChildren(out);
}
void RoadRequest::print(StreamTy& out) const noexcept
{
    Action::print(out);
    out << "[Road]";
    out << "[Constraints] " << constraints;
    if(toPt.isValid())
        out << "[To " << toPt << ": " << route << "]";
    else
        out << "[SEARCHING]";
    out << "\n";
    printChildren(out);
}
void FlagRequest::print(StreamTy& out) const noexcept
{
    Action::print(out);
    out << "[Flag]";
    out << "[Constraints] " << constraints;
    out << "\n";
    printChildren(out);
}
void SpecialistRequest::print(StreamTy& out) const noexcept
{
    Action::print(out);
    out << "[Specialis]t";
    out << "[Constraints] " << constraints;
    out << "\n";
    printChildren(out);
}
void DestroyRequest::print(StreamTy& out) const noexcept
{
    Action::print(out);
    out << "[DESTROY]t";
    out << "[BUILDING] " << constraints;
    out << "\n";
    printChildren(out);
}

char GroupAction::ID;
char BuildingRequest::ID;
char RoadRequest::ID;
char FlagRequest::ID;
char SpecialistRequest::ID;
char DestroyRequest::ID;

// Copyright (c) 2005 - 2020 Settlers Freaks (sf-team at siedler25.org)
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

#include "JobFactory.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobBaseMilitary.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobUsual.h"
#include "nodeObjs/noRoadNode.h"

//////////////////////////////////////////////////////////////////////////
// Job includes, maybe use register function in nof* classes?
//////////////////////////////////////////////////////////////////////////
#include "figures/nofArmorer.h"
#include "figures/nofBaker.h"
#include "figures/nofBrewer.h"
#include "figures/nofBuilder.h"
#include "figures/nofButcher.h"
#include "figures/nofCarpenter.h"
#include "figures/nofCarrier.h"
#include "figures/nofCatapultMan.h"
#include "figures/nofCharburner.h"
#include "figures/nofDonkeybreeder.h"
#include "figures/nofFarmer.h"
#include "figures/nofFisher.h"
#include "figures/nofForester.h"
#include "figures/nofGeologist.h"
#include "figures/nofHunter.h"
#include "figures/nofIronfounder.h"
#include "figures/nofMetalworker.h"
#include "figures/nofMiller.h"
#include "figures/nofMiner.h"
#include "figures/nofMinter.h"
#include "figures/nofPassiveSoldier.h"
#include "figures/nofPassiveWorker.h"
#include "figures/nofPigbreeder.h"
#include "figures/nofPlaner.h"
#include "figures/nofScout_Free.h"
#include "figures/nofScout_LookoutTower.h"
#include "figures/nofShipWright.h"
#include "figures/nofStonemason.h"
#include "figures/nofWellguy.h"
#include "figures/nofWoodcutter.h"
#include "helpers/toString.h"
#include "nodeObjs/noFlag.h"
#include <stdexcept>

noFigure* JobFactory::CreateJob(const Job job_id, const MapPoint pt, const unsigned char player, noRoadNode* const goal)
{
    switch(job_id)
    {
        case Job::Builder:
            if(!goal)
                return new nofBuilder(pt, player, nullptr);
            else if(goal->GetGOT() != GOT_BUILDINGSITE)
                return new nofPassiveWorker(Job::Builder, pt, player, goal);
            else
                return new nofBuilder(pt, player, static_cast<noBuildingSite*>(goal));
        case Job::Planer:
            RTTR_Assert(dynamic_cast<noBuildingSite*>(goal));
            return new nofPlaner(pt, player, static_cast<noBuildingSite*>(goal));
        case Job::Carpenter:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofCarpenter(pt, player, static_cast<nobUsual*>(goal));
        case Job::Armorer:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofArmorer(pt, player, static_cast<nobUsual*>(goal));
        case Job::Stonemason:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofStonemason(pt, player, static_cast<nobUsual*>(goal));
        case Job::Brewer:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofBrewer(pt, player, static_cast<nobUsual*>(goal));
        case Job::Minter:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofMinter(pt, player, static_cast<nobUsual*>(goal));
        case Job::Butcher:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofButcher(pt, player, static_cast<nobUsual*>(goal));
        case Job::IronFounder:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofIronfounder(pt, player, static_cast<nobUsual*>(goal));
        case Job::Miller:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofMiller(pt, player, static_cast<nobUsual*>(goal));
        case Job::Metalworker:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofMetalworker(pt, player, static_cast<nobUsual*>(goal));
        case Job::Baker:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofBaker(pt, player, static_cast<nobUsual*>(goal));
        case Job::Helper:
            // Wenn goal = 0 oder Lagerhaus, dann Auslagern anscheinend und mann kann irgendeinen Typ nehmen
            if(!goal)
                return new nofWellguy(pt, player, static_cast<nobUsual*>(nullptr));
            else if(goal->GetGOT() == GOT_NOB_STOREHOUSE || goal->GetGOT() == GOT_NOB_HARBORBUILDING
                    || goal->GetGOT() == GOT_NOB_HQ)
                return new nofWellguy(pt, player, static_cast<nobBaseWarehouse*>(goal));
            else if(goal->GetGOT() == GOT_NOB_USUAL)
            {
                auto* goalBld = static_cast<nobUsual*>(goal);
                if(goalBld->GetBuildingType() == BLD_WELL)
                    return new nofWellguy(pt, player, goalBld);
                else if(goalBld->GetBuildingType() == BLD_CATAPULT)
                    return new nofCatapultMan(pt, player, goalBld);
            }
            throw std::runtime_error("Invalid goal type: " + helpers::toString(goal->GetGOT()) + " for job "
                                     + helpers::toString(job_id));
        case Job::Geologist:
            RTTR_Assert(dynamic_cast<noFlag*>(goal));
            return new nofGeologist(pt, player, static_cast<noFlag*>(goal));
        case Job::Scout:
            // Im Spähturm arbeitet ein anderer Späher-Typ
            // Wenn goal = 0 oder Lagerhaus, dann Auslagern anscheinend und mann kann irgendeinen Typ nehmen
            if(!goal)
                return new nofScout_LookoutTower(pt, player, static_cast<nobUsual*>(nullptr));
            else if(goal->GetGOT() == GOT_NOB_HARBORBUILDING || goal->GetGOT() == GOT_NOB_STOREHOUSE
                    || goal->GetGOT() == GOT_NOB_HQ)
                return new nofPassiveWorker(Job::Scout, pt, player, goal);
            else if(goal->GetGOT() == GOT_NOB_USUAL) // Spähturm / Lagerhaus?
            {
                RTTR_Assert(dynamic_cast<nobUsual*>(goal));
                return new nofScout_LookoutTower(pt, player, static_cast<nobUsual*>(goal));
            } else if(goal->GetGOT() == GOT_FLAG)
                return new nofScout_Free(pt, player, goal);
            throw std::runtime_error("Invalid goal type: " + helpers::toString(goal->GetGOT()) + " for job "
                                     + helpers::toString(job_id));
        case Job::Miner:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofMiner(pt, player, static_cast<nobUsual*>(goal));
        case Job::Farmer:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofFarmer(pt, player, static_cast<nobUsual*>(goal));
        case Job::Forester:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofForester(pt, player, static_cast<nobUsual*>(goal));
        case Job::Woodcutter:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofWoodcutter(pt, player, static_cast<nobUsual*>(goal));
        case Job::PigBreeder:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofPigbreeder(pt, player, static_cast<nobUsual*>(goal));
        case Job::DonkeyBreeder:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofDonkeybreeder(pt, player, static_cast<nobUsual*>(goal));
        case Job::Hunter:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofHunter(pt, player, static_cast<nobUsual*>(goal));
        case Job::Fisher:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofFisher(pt, player, static_cast<nobUsual*>(goal));
        case Job::Private:
        case Job::PrivateFirstClass:
        case Job::Sergeant:
        case Job::Officer:
        case Job::General:
            // TODO: Is this ever called? If yes, then why is the home here set to nullptr?
            RTTR_Assert(dynamic_cast<nobBaseMilitary*>(goal));
            return new nofPassiveSoldier(pt, player, static_cast<nobBaseMilitary*>(goal), nullptr,
                                         getSoldierRank(job_id));
        case Job::PackDonkey: return new nofCarrier(CarrierType::Donkey, pt, player, nullptr, goal);
        case Job::Shipwright:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofShipWright(pt, player, static_cast<nobUsual*>(goal));
        case Job::CharBurner:
            RTTR_Assert(dynamic_cast<nobUsual*>(goal));
            return new nofCharburner(pt, player, static_cast<nobUsual*>(goal));
        case Job::BoatCarrier:
            throw std::runtime_error("Cannot create a boat carrier job (try creating Job::Helper).");
            break;
    }
    throw std::runtime_error("Invalid job type " + helpers::toString(job_id));
}

#include "zerg_crush.h"

using namespace sc2;

void ZergCrush::OnStep() {
    const ObservationInterface *observation = Observation();
    const int framesToSkip = 4;
    const int supplyDepotRaiseFramesToSkip = 500;

    if (observation->GetGameLoop() % supplyDepotRaiseFramesToSkip == 0) {
        RaiseAllSupplyDepots();
    }

    if (observation->GetGameLoop() % framesToSkip != 0) {
        //do we want to call this everytime, or only if we detect enemies approaching?
        if(DetectRush(enemyRace)) {OnRushDetected(enemyRace);}

        
        //pass rush as a bool to ManageArmy()?
        ManageArmy();
        return;
    }
    
    ManageMacro();
    ManageUpgrades();
    if (TryBuildSCV()) return;
    TryCallDownMule();
    BuildArmy();
}

void ZergCrush::BuildArmy() {
    const ObservationInterface *observation = Observation();

    auto allSquadrons = armyComposition->getAllSquadrons();
    auto unitsToBuild = armyComposition->squadronsToBuild(allSquadrons, observation);
    for (const auto &unit: unitsToBuild) {
        // Grab army and building counts
        Units buildings = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit->getBuildingStructureType()));

        for (const auto &building: buildings) {
            auto addOn = observation->GetUnit(building->add_on_tag);
            uint32_t numOrders = 1; // Number of orders to queue at a time
            if (addOn && std::find(reactorTypes.begin(), reactorTypes.end(), addOn->unit_type) != reactorTypes.end()) {
                numOrders++; // If we have a reactor build two at a time
            }
            if (building->orders.size() < numOrders) {
                Actions()->UnitCommand(building, unit->getAbilityId());
            }
        }
    }
}

void ZergCrush::RayCastWithUnit(const Unit *unit, const ObservationInterface &observation) {
    Point2D current_pos = unit->pos;
    RayCastInstance cast(unit);

    if (cast.castWithUnit(unit, observation)) {
        std::cout << "NO WAY" << std::endl;
    }

}

bool ZergCrush::TryBuildWallPiece(sc2::UnitTypeID piece) {
    const ObservationInterface *observation = Observation();
    Positions pos;
    std::array<std::vector<sc2::Point2D>, 4> map_postions;


    //get the postions of the map we are on
    switch (pos.getMap(observation)) {
        case Maps::CACTUS:
            map_postions = pos.cactus_postions;
            break;
        case Maps::BELSHIR:
            map_postions = pos.belshir_postions;
            break;
        case Maps::PROXIMA:
            map_postions = pos.proxima_postions;
            break;
        default:
            break;
    }

    //figure out which ramp we are closest to
    int closest_ramp = 0;   
    int index = 0;

    sc2::Point2D startLocation = observation->GetStartLocation();
    float closest_distance = std::numeric_limits<float>::infinity();
    //the most scuffed for loop ever
    for (const auto& pos : map_postions[0]) {
        float test_distance = DistanceSquared2D(startLocation, map_postions[0][index]);
        if(test_distance < closest_distance) 
        {closest_ramp = index; closest_distance = test_distance;}
        //std::cout << index << std::endl;
        ++index;

    }
    //std::cout << closest_ramp << std::endl;
    //set the build location based on structure type/current wall progress
    //supply depot = map_postions[0]
    //barrack 1 = map_postions[1]
    //barrack 2 = map_postions[2]
    if (piece == UNIT_TYPEID::TERRAN_BARRACKS) {
        //check if its the first or second barrack
        if (!Query()->Placement(ABILITY_ID::BUILD_BARRACKS, map_postions[1][closest_ramp])) {
            //std::cout << "building barrack 2 at " << map_postions[2][closest_ramp].x << std::endl;
            return TryBuildStructure(ABILITY_ID::BUILD_BARRACKS, UNIT_TYPEID::TERRAN_SCV, map_postions[2][closest_ramp], false);
        }
        else {
            //std::cout << "building barrack 1 at " << map_postions[1][closest_ramp].x << std::endl;
            return TryBuildStructure(ABILITY_ID::BUILD_BARRACKS, UNIT_TYPEID::TERRAN_SCV, map_postions[1][closest_ramp], false);
        }
    }
    else if(piece == UNIT_TYPEID::TERRAN_MISSILETURRET) {
        //std::cout <<"building wall turret" <<std::endl;
        return TryBuildStructure(ABILITY_ID::BUILD_MISSILETURRET, UNIT_TYPEID::TERRAN_SCV, map_postions[3][closest_ramp], false);
    }
    //supply depot
    else {
        //std::cout << "building depot " << map_postions[0][closest_ramp].x << std::endl;
        return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT, UNIT_TYPEID::TERRAN_SCV, map_postions[0][closest_ramp], false);
        
        }

}

bool ZergCrush::DetectRush(Race enemyRace) {
    bool rush_detected = false;
    const ObservationInterface *observation = Observation();
    //rush_detected = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_MARINE)).size() > 10;
    int threshold = DetermineThreshold(*observation);
    Units enemy_units = observation->GetUnits(Unit::Enemy);
    int far_away = 0;
    for (const auto &unit : enemy_units) {
        if (DistanceSquared2D(startingLocation, unit->pos) > 50.0) {++far_away;} 
    }

    //different conditions for different matchups?
    
    switch(enemyRace) {
        case Terran: {
            rush_detected = (enemy_units.size() - far_away) > threshold;
            break;
        }
        case Zerg: {
            Units zerglings = observation->GetUnits(Unit::Enemy, IsUnit(UNIT_TYPEID::ZERG_ZERGLING));
            rush_detected = zerglings.size() > threshold;
            break;
        }
        case Protoss: {
            rush_detected =(enemy_units.size() - far_away) > threshold;
            break;
        }
        default:
            rush_detected = (enemy_units.size() - far_away) > threshold;
            break;

    }
    
   //rush_detected = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_MARINE)).size() > 10;
    Units supply_depots = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED));

    if (rush_detected) {
        for (const auto& supply_depot : supply_depots) {
            //std::cout << "raising depot" << std::endl;
            Actions()->UnitCommand(supply_depot, ABILITY_ID::MORPH_SUPPLYDEPOT_RAISE);
        }

    return rush_detected;

    }
}
int ZergCrush::DetermineThreshold(const ObservationInterface &observation) {
    //how to determine?
    /*
    -size of our army/goal size
    -time in game
    -supply cap
    */
   std::vector<ArmySquadron *> armies = armyComposition->getAllSquadrons();
   int largest_army = armies[0]->getSquadron().size();

   for (const auto &army : armies) {
        int army_size = army->getSquadron().size();
        if (army_size > largest_army) {largest_army = army_size;}
   }
   //std::cout << "threshhold :" << largest_army << std::endl;
   return std::max(10, largest_army);

}

void ZergCrush::OnRushDetected(Race enemyRace) {
    std::cout << "RUSH DETECTED" << std::endl;
    const ObservationInterface *observation = Observation();
    
    //priorities: repair the bases and the wall
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsStructure(observation));
    for(const auto &base : bases) {
        if(base->health < (base->health_max) * 0.75) {
            const Unit* random_SCV; 
            GetRandomUnit(random_SCV, observation, UNIT_TYPEID::TERRAN_SCV);
            std::cout << "repairing base" << std::endl;
            Actions()->UnitCommand(random_SCV, ABILITY_ID::EFFECT_REPAIR_SCV);
        }
    }

    //idk what we should do
    switch(enemyRace) {
        case Terran: {
            break;
        }
        case Zerg: {
            break;
        }
        case Protoss: {
            break;
        }
        default:
            break;

    }


}

void ZergCrush::ManageMacro() {
    auto observation = Observation();
    auto structures = buildOrder->structuresToBuild(observation);

    Units supply_depots = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOT));

    for (const auto &structure: structures) {
        switch ((UNIT_TYPEID) structure->getUnitTypeID()) {
            case UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
                
                if(structure->isChainBuildLeader()) {
                    //std::cout << "wall depot" << std::endl;
                    TryBuildWallPiece(UNIT_TYPEID::TERRAN_SUPPLYDEPOT);
                } else {
                    TryBuildSupplyDepot();
                }
                for (const auto& supply_depot : supply_depots) {
                    //std::cout << "lowering depot" << std::endl;
                    Actions()->UnitCommand(supply_depot, ABILITY_ID::MORPH_SUPPLYDEPOT_LOWER);
                    depots_raised = false;
                }
                break;
        
            case UNIT_TYPEID::TERRAN_REFINERY:
                BuildRefinery();
                break;
            case UNIT_TYPEID::TERRAN_COMMANDCENTER:
                TryExpand(ABILITY_ID::BUILD_COMMANDCENTER, UNIT_TYPEID::TERRAN_SCV);
                break;
            case UNIT_TYPEID::TERRAN_ORBITALCOMMAND: {
                const Unit *commandCenter = structure->getBaseStruct(observation);
                if (commandCenter) {
                    Actions()->UnitCommand(commandCenter, ABILITY_ID::MORPH_ORBITALCOMMAND);
                } else {
                    // All command centers have become orbital commands
                    structure->setBuiltAddOn(true);
                }
                break;
            }
            case UNIT_TYPEID::TERRAN_BUNKER: {
                //std::cout << "building bunker" << std::endl;
                //possible bunker locations:
                /*
                -near the starting base
                -on the bottom of the ramp
                */
               Units command_centers = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_COMMANDCENTER));
               int farthest_command_center = 0;
               int index = 0;
               //we build a bunker at the furthest expansion base location
               float farthest_distance = DistanceSquared3D(startingLocation, command_centers[0]->pos);
               for (const auto& center: command_centers) {
                    float new_distance = DistanceSquared3D(startingLocation, command_centers[index]->pos);
                    if (new_distance > farthest_distance) {
                        farthest_distance = new_distance;
                        farthest_command_center = index;
                    }
                    ++index;
               }
                
                Point2D proxy_center = command_centers[farthest_command_center]->pos;
                //try placing at 4 90 degree angles away to make sure we dont place on the minerals
                //North
                if(TryBuildStructure(ABILITY_ID::BUILD_BUNKER, UNIT_TYPEID::TERRAN_SCV, Point2D(proxy_center.x + 5.0, proxy_center.y + 5.0), false)) {continue;}
                //East
                else if(TryBuildStructure(ABILITY_ID::BUILD_BUNKER, UNIT_TYPEID::TERRAN_SCV, Point2D(proxy_center.x + 5.0, proxy_center.y - 5.0), false)) {continue;}
                //South
                else if(TryBuildStructure(ABILITY_ID::BUILD_BUNKER, UNIT_TYPEID::TERRAN_SCV, Point2D(proxy_center.x - 5.0, proxy_center.y - 5.0), false)) {continue;}
                //West
                else if(TryBuildStructure(ABILITY_ID::BUILD_BUNKER, UNIT_TYPEID::TERRAN_SCV, Point2D(proxy_center.x - 5.0, proxy_center.y + 5.0), false)) {continue;}
                else{
                    //hopefully we dont get here
                    //std::cout <<"why here" <<std::endl;
                    TryBuildStructure(ABILITY_ID::BUILD_BUNKER, UNIT_TYPEID::TERRAN_SCV, getRandomLocationBy(command_centers[farthest_command_center]->pos, 5.0), false);
                    continue;
                }

                Units marines = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_MARINE));
                int marine_index = 0;
                for(const auto& marine : marines) {
                    if(index >= 4) {break;}
                    std::cout << "loading bunker" <<std::endl;
                    Actions()->UnitCommand(marine, ABILITY_ID::LOAD_BUNKER);
                    ++marine_index;
                }
                
                
                
            }
            
            default:
                if (structure->isAddOn()) {
                    const Unit *baseStruct = structure->getBaseStruct(observation);
                    if (baseStruct) {
                        structure->setBuiltAddOn(TryBuildFrom(structure->getAbilityId(), baseStruct->tag));
                    }
                    break;
                }
                if(structure->getUnitTypeID() == UNIT_TYPEID::TERRAN_BARRACKS && structure->getChainBuild()) {
                    //std::cout << "wall barrack 2" << std::endl;
                    TryBuildWallPiece(UNIT_TYPEID::TERRAN_BARRACKS);
                    break;
                }
                if(structure->getUnitTypeID() == UNIT_TYPEID::TERRAN_MISSILETURRET && structure->getChainBuild()) {
                    //std::cout << "turret" << std::endl;
                    TryBuildWallPiece(UNIT_TYPEID::TERRAN_MISSILETURRET);
                    break;
                }
                

                sc2::Tag tag;
                tag = structure->getBuiltBy();
                if (tag) {

                    TryBuildStructureRandomWithUnit(structure->getAbilityId(), Observation()->GetUnit(tag));
                } else {

                    TryBuildStructureRandom(structure->getAbilityId(), UNIT_TYPEID::TERRAN_SCV);
                }
        }
    }
}

void ZergCrush::ManageUpgrades() {
    const ObservationInterface *observation = Observation();
    auto upgrades = observation->GetUpgrades();

    TryBuildUnit(ABILITY_ID::RESEARCH_STIMPACK, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB);
    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONS, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYARMOR, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
    TryBuildUnit(ABILITY_ID::RESEARCH_COMBATSHIELD, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB);
    TryBuildUnit(ABILITY_ID::RESEARCH_CONCUSSIVESHELLS, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB);
    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONS, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYARMOR, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONS, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYARMOR, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
}



void ZergCrush::ManageArmy() {
    // TODO: it seems like some army units are being left idle - not sure why...
    const ObservationInterface *observation = Observation();

    Units army = observation->GetUnits(Unit::Alliance::Self, IsArmy(observation));

    uint32_t waitUntilSupply = 12; // TODO: Think of better plan


    auto allSquadrons = armyComposition->getAllSquadrons();

    std::vector<ArmySquadron *> scouts = armyComposition->getSquadronsByType(allSquadrons, SCOUT);
    for (auto &scoutSquadron: scouts) {
        // Only scout with completed squadrons
        if (scoutSquadron->needMore() || scoutSquadron->getSquadron().empty()) continue;
        ScoutWithUnits(observation, scoutSquadron->getSquadron());
    }

    std::vector<ArmySquadron *> main = armyComposition->getSquadronsByType(allSquadrons, MAIN);
    Units enemiesNearBase = observation->GetUnits(Unit::Alliance::Enemy, CombinedFilter({
            IsVisible(), WithinDistanceOf(baseRallyPoint, 30.0f)})); // enemies at base

    if (waitUntilSupply >= observation->GetArmyCount()) {
        for (auto &mainSquadron: main) {
            if (!enemiesNearBase.empty()) {
                for (const auto &unit: mainSquadron->getSquadron()) {
                    attackMicro->microUnit(observation, unit);
                }
                continue;
            }

            // If our units are in multiple clusters move them together to a point by our base rally point
            auto clusters = search::Cluster(mainSquadron->getSquadron(), SQUADRON_CLUSTER_DISTANCE);
            if (clusters.size() > 1 || clusters.size() == 1 && Distance2D(clusters.front().first, baseRallyPoint) >
                                                               SQUADRON_CLUSTER_DISTANCE) {
                Actions()->UnitCommand(mainSquadron->getSquadron(),
                                       ABILITY_ID::SMART, getRandomLocationBy(baseRallyPoint, 3.0f));
            };
        }
    } else {
        Units mainArmyUnits;
        for (auto &mainSquadron: main) {
            auto squadron = mainSquadron->getSquadron();
            mainArmyUnits.insert(mainArmyUnits.end(), squadron.begin(), squadron.end());
        }
        ScoutWithUnits(observation, mainArmyUnits, SQUADRON_CLUSTER_DISTANCE);
    }
}

void ZergCrush::ScoutWithUnits(const sc2::ObservationInterface *observation, const sc2::Units &units,
                               float clusterDistance) {
    if (units.empty()) return;

    sc2::Units scoutingUnits = Units(units);
    auto clusters = getClusters(units, clusterDistance);

    for (const auto &cluster: clusters) {
        auto clusterUnits = cluster.second;
        const Unit *clusterLeader = &(clusterUnits.front());

        sc2::Units attackableEnemies = observation->GetUnits(sc2::Unit::Alliance::Enemy,
                CombinedFilter({
                        TargetableBy(observation, clusterLeader),
                        WithinDistanceOf(clusterLeader, std::max(MicroInformation(observation, clusterLeader).range * 2, 15.0f))}));


        if (!attackableEnemies.empty()) {
            // We can set an assumed starting location here, since we are being attacked
            setAssumedEnemyStartingLocation(cluster.first);

            // Remove the units in this from the main scouting group
            scoutingUnits.erase(std::remove_if(scoutingUnits.begin(), scoutingUnits.end(), [&](const auto& smallClusterUnit) {
                return std::any_of(cluster.second.begin(), cluster.second.end(), [&](const auto& unit) {
                    return unit.tag == smallClusterUnit->tag;
                });
            }), scoutingUnits.end());

            // And attack with these units
            for (const auto &unit: cluster.second) {
                attackMicro->microUnit(observation, &unit);
            }
        }
    }

    // Set the largest cluster size to send smaller clusters to
    int minClusterSize = (int) units.size() / 4;
    size_t largestClusterSize = minClusterSize;
    Point3D largestClusterPosition = baseRallyPoint;
    for (const auto &cluster: clusters) {
        if (cluster.second.size() > largestClusterSize) {
            largestClusterSize = cluster.second.size();
            largestClusterPosition = cluster.first;
        }
    }

    // Now, outside the clusters that are being attacked, remove small clusters and send them towards the main army
    auto smallClusters = getClusters(scoutingUnits, clusterDistance, 1, minClusterSize);
    for (const auto &cluster: smallClusters) {

        // Remove these small clusters from the main scouting group
        scoutingUnits.erase(std::remove_if(scoutingUnits.begin(), scoutingUnits.end(), [&](const auto& smallClusterUnit) {
            return std::any_of(cluster.second.begin(), cluster.second.end(), [&](const auto& unit) {
                return unit.tag == smallClusterUnit->tag;
            });
        }), scoutingUnits.end());

        // Send small clusters towards main army
        for (const auto &unit: cluster.second) {
            Actions()->UnitCommand(&unit, ABILITY_ID::SMART, largestClusterPosition);
        }
    }

    // Ignore small clusters, they are heading towards the main cluster
    auto scoutingUnitClusters = getClusters(scoutingUnits, clusterDistance, minClusterSize - 1);
    if (scoutingUnitClusters.size() > 1) {
        clusterUnits(scoutingUnits, clusterDistance);
        return;
    }

    if (scoutingUnits.empty()) return;

    // Now we can do some scouting with the remaining units that are not in battle and are clustered
    auto unitLocation = scoutingUnitClusters.front().first;
    markOffScoutedLocations(unitLocation);

    LowerSupplyDepotsNear(unitLocation, SQUADRON_CLUSTER_DISTANCE*2); // Make sure our army can get through

    if (assumedEnemyStartingLocation != nullptr) {
        if (expansionMap.count(*assumedEnemyStartingLocation) > 0) { // We haven't discounted this location yet
            Actions()->UnitCommand(scoutingUnits, ABILITY_ID::MOVE_MOVEPATROL, expansionMap[*assumedEnemyStartingLocation].front());
        }
        return;
    }

    for (auto &location: enemyStartingLocations) {
        if (expansionMap.count(location) > 0) { // We haven't discounted this location yet
            Actions()->UnitCommand(scoutingUnits, ABILITY_ID::MOVE_MOVEPATROL, expansionMap[location].front());
        }
    }
}

void ZergCrush::setAssumedEnemyStartingLocation(const Point3D &scoutLocation) {
    if (assumedEnemyStartingLocation == nullptr) {
        for (auto &location: enemyStartingLocations) {
            if (Distance2D(scoutLocation, location) < 30.0f && expansionMap.count(location) > 0) {
                assumedEnemyStartingLocation = &location;
            }
        }
    }
}

void ZergCrush::RaiseAllSupplyDepots() {
    for (auto& supplyDepot: Observation()->GetUnits(IsUnit(sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED))) {
        Actions()->UnitCommand(supplyDepot, ABILITY_ID::MORPH_SUPPLYDEPOT_RAISE);
    }
}

void ZergCrush::LowerSupplyDepotsNear(const Point3D &location, float distance) {
    for (auto &supplyDepot: Observation()->GetUnits(IsUnit(sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOT))) {
        if (Distance2D(location, location) < distance) {
            Actions()->UnitCommand(supplyDepot, ABILITY_ID::MORPH_SUPPLYDEPOT_LOWER);
        }
    }
}

void ZergCrush::markOffScoutedLocations(const Point3D &scoutLocation) {
    for (auto &location: enemyStartingLocations) {
        // These units are definitely not in battle - so no enemies at this location
        if (Distance2D(scoutLocation, location) < 5.0f && expansionMap.count(location) > 0) {
            expansionMap.erase(location);// Do not scout this expansion anymore ...
            continue;
        }

        // Otherwise lets check if we are by any of its expansions and remove any that we are near
        if (expansionMap.count(location) > 0) {
            auto& expansions = expansionMap[location];
            expansions.erase(std::remove_if(expansions.begin(), expansions.end(), [scoutLocation](const auto &expansion) {
                return Distance2D(scoutLocation, expansion) < 5.0f;
            }), expansions.end());
        }
    }
}


std::vector<std::pair<Point3D, std::vector<Unit>>> ZergCrush::getClusters(const Units &units,
                                                                          float clusterDistance,
                                                                          size_t clusterMinSize,
                                                                          size_t clusterMaxSize) {
    auto allClusters = search::Cluster(units, clusterDistance);

    // Remove small and large clusters
    auto filteredClusters = std::remove_if(allClusters.begin(), allClusters.end(),
                                           [clusterMinSize, clusterMaxSize](const auto &cluster) {
                                               size_t clusterSize = cluster.second.size();
                                               return clusterSize < clusterMinSize || clusterSize > clusterMaxSize;
                                           });

    // Erase the removed clusters
    allClusters.erase(filteredClusters, allClusters.end());
    return allClusters;
}


void ZergCrush::clusterUnits(const Units &units, float clusterDistance) {
    auto clusters = search::Cluster(units, clusterDistance); // Ensure the group is together
    auto totalUnitCount = units.size(); // TODO: Based on supply
    if (clusters.size() > 1) {
        Point3D clustersMassCenter; // The center point for all clusters -> where the units should meet up
        for (const auto &cluster: clusters) {
            clustersMassCenter += cluster.first;
        }
        clustersMassCenter /= (float) clusters.size();
        Point3D shiftAmount = {0.0f, 0.0f, 0.0f}; // We shift the mass center towards clusters with the most units
        for (const auto &cluster: clusters) {
            float clusterUnitRatio = (float) cluster.second.size() / (float) totalUnitCount;
            Point3D toMassCenter = cluster.first - clustersMassCenter;
            shiftAmount += toMassCenter * clusterUnitRatio;
        }
        clustersMassCenter += shiftAmount;

        Actions()->UnitCommand(units, ABILITY_ID::SMART, clustersMassCenter);
    }
}

void ZergCrush::HandleIdleWorker(const Unit *worker) {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
    Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(sc2::UNIT_TYPEID::TERRAN_REFINERY));

    if (bases.empty()) return;

    // First start game
    if (observation->GetGameLoop() < 300) {
        auto mineralPatch = FindNearestMineralPatch(worker->pos);
        Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, mineralPatch);
        return;
    }

    // Search for a geyser that is missing workers to assign this worker to
    for (const auto &geyser: geysers) {
        if (geyser->assigned_harvesters < geyser->ideal_harvesters) {
            Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, geyser);
            return;
        }
    }

    // Search for a base that is missing workers to assign this worker (prioritize the base that built the SCV by sorting)
    std::sort(bases.begin(), bases.end(), [worker](const auto &baseA, const auto &baseB) {
        return Distance2D(baseA->pos, worker->pos) < Distance2D(baseB->pos, worker->pos);
    });
    for (const auto &base: bases) {
        if (base->ideal_harvesters == 0 || base->build_progress != 1) { continue; }
        if (base->assigned_harvesters < base->ideal_harvesters) {
            Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, FindNearestMineralPatch(base->pos));
            return;
        }
    }

    // If all workers are spots are filled just go to any base
    const Unit *randomBase = GetRandomEntry(bases);
    Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, FindNearestMineralPatch(randomBase->pos));
}

const Unit *ZergCrush::FindNearestMineralPatch(const Point2D &start) {
    Units mineralFields = Observation()->GetUnits(IsUnit(UNIT_TYPEID::NEUTRAL_MINERALFIELD));
    return *std::min_element(mineralFields.begin(), mineralFields.end(),
                             [start](const auto &mineralFieldA, const auto &mineralFieldB) {
                                 return Distance2D(start, mineralFieldA->pos) < Distance2D(start, mineralFieldB->pos);
                             });
}

//From MultiplayerBot, gets a random unit of a certain type
bool ZergCrush::GetRandomUnit(const Unit*& unit_out, const ObservationInterface* observation, UnitTypeID unit_type) {
    Units my_units = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
    if (!my_units.empty()) {
        unit_out = GetRandomEntry(my_units);
        return true;
    }
    return false;
}


// TODO: Copied from MultiplayerBot An estimate of how many workers we should have based on what buildings we have
int ZergCrush::GetExpectedWorkers() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
    Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(sc2::UNIT_TYPEID::TERRAN_REFINERY));
    int expected_workers = 0;

    for (const auto &base: bases) {
        if (base->build_progress != 1) {
            continue;
        }
        expected_workers += base->ideal_harvesters;
    }

    for (const auto &geyser: geysers) {
        if (geyser->vespene_contents > 0) {
            if (geyser->build_progress != 1) {
                continue;
            }
            expected_workers += geyser->ideal_harvesters;
        }
    }

    return expected_workers;
}

bool ZergCrush::TryBuildUnit(AbilityID abilityTypeForUnit, UnitTypeID buildingUnitType) {
    const ObservationInterface *observation = Observation();

    auto buildingUnits = observation->GetUnits(IsUnit(buildingUnitType));
    if (buildingUnits.empty()) return false;
    auto buildingUnit = GetRandomEntry(buildingUnits);

    return TryBuildFrom(abilityTypeForUnit, buildingUnit->tag);
}

bool ZergCrush::TryBuildSCV() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

    if (observation->GetFoodWorkers() >= MAX_WORKER_COUNT) return false;
    if (observation->GetFoodUsed() >= observation->GetFoodCap()) return false;
    if (observation->GetFoodWorkers() > GetExpectedWorkers()) return false;

    for (const auto &base: bases) {
        if (base->assigned_harvesters < base->ideal_harvesters && base->build_progress == 1) {
            if (observation->GetMinerals() >= 50) {
                return TryBuildUnit(ABILITY_ID::TRAIN_SCV, base->unit_type);
            }
        }
    }
    return false;
}

void ZergCrush::TryCallDownMule() {
    auto bases = Observation()->GetUnits(IsTownHall());
    for (const auto &base: bases) {
        if (base->unit_type == UNIT_TYPEID::TERRAN_ORBITALCOMMAND && base->energy > 50) {
            Actions()->UnitCommand(base, ABILITY_ID::EFFECT_CALLDOWNMULE, FindNearestMineralPatch(base->pos));
        }
    }
}

// TODO: Copied from MultiplayerBot Try build structure given a location. This is used most of the time
bool ZergCrush::TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Point2D location,
                                  bool isExpansion) {
    const ObservationInterface *observation = Observation();
    Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));

    //if we have no workers Don't build
    if (workers.empty()) return false;

    // Check to see if there is already a worker heading out to build it
    for (const auto &worker: workers) {
        for (const auto &order: worker->orders) {
            if (order.ability_id == ability_type_for_structure) {
                return false;
            }
        }
    }

    // If no worker is already building one, get a random worker to build one
    const Unit *unit = GetRandomEntry(workers);

    // Check to see if unit can make it there
    if (Query()->PathingDistance(unit, location) < 0.1f) {
        return false;
    }
    if (!isExpansion) {
        for (const auto &expansion: expansionLocations) {
            if (Distance2D(location, Point2D(expansion.x, expansion.y)) < 7) {
                return false;
            }
        }
    }
    // Check to see if unit can build there
    if (Query()->Placement(ability_type_for_structure, location)) {
        Actions()->UnitCommand(unit, ability_type_for_structure, location);
        return true;
    }
    return false;

}

// TODO: Copied from MultiplayerBot Try to build a structure based on tag, Used mostly for Vespene, since the pathing check will fail even though the geyser is "Pathable"
bool ZergCrush::TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Tag location_tag) {
    const ObservationInterface *observation = Observation();
    Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
    const Unit *target = observation->GetUnit(location_tag);

    if (workers.empty()) {
        return false;
    }

    // Check to see if there is already a worker heading out to build it
    for (const auto &worker: workers) {
        for (const auto &order: worker->orders) {
            if (order.ability_id == ability_type_for_structure) {
                return false;
            }
        }
    }

    // If no worker is already building one, get a random worker to build one
    const Unit *unit = GetRandomEntry(workers);

    // Check to see if unit can build there
    if (Query()->Placement(ability_type_for_structure, target->pos)) {
        Actions()->UnitCommand(unit, ability_type_for_structure, target);
        return true;
    }
    return false;

}

bool ZergCrush::TryBuildStructureUnit(AbilityID ability_type_for_structure, const Unit *unit, Point2D location,
                                      bool isExpansion = false) {
    // Check to see if unit can make it there
    if (Query()->PathingDistance(unit, location) < 0.1f) {
        return false;
    }
    if (!isExpansion) {
        for (const auto &expansion: expansionLocations) {
            if (Distance2D(location, Point2D(expansion.x, expansion.y)) < 7) {
                return false;
            }
        }
    }

    // Check to see if unit can build there
    if (Query()->Placement(ability_type_for_structure, location)) {
        Actions()->UnitCommand(unit, ability_type_for_structure, location);
        return true;
    }
    return false;
}

// TODO: Copied from MultiplayerBot Expands to nearest location and updates the start location to be between the new location and old bases.
bool ZergCrush::TryExpand(AbilityID buildAbility, UnitTypeID workerType) {
    const ObservationInterface *observation = Observation();
    float minimumDistance = std::numeric_limits<float>::max();
    Point3D closestExpansion;
    for (const auto &expansion: expansionMap[startingLocation]) {
        float currentDistance = Distance2D(startingLocation, expansion);
        if (currentDistance < .01f) continue; // is starting location

        if (currentDistance < minimumDistance) {
            if (Query()->Placement(buildAbility, expansion)) {
                closestExpansion = expansion;
                minimumDistance = currentDistance;
            }
        }
    }

    LowerSupplyDepotsNear(); // Lower supply depots while we go build the command center
    if (TryBuildStructure(buildAbility, workerType, closestExpansion, true) &&
        observation->GetUnits(Unit::Self, IsTownHall()).size() < 4) {
        // TODO: Update this rally point
        baseRallyPoint = Point3D(((baseRallyPoint.x + closestExpansion.x) / 2),
                                 ((baseRallyPoint.y + closestExpansion.y) / 2),
                                 ((baseRallyPoint.z + closestExpansion.z) / 2));
        return true;
    }
    return false;

}

bool ZergCrush::TryBuildFrom(AbilityID abilityId, Tag baseStructure) {
    const Unit *unit = Observation()->GetUnit(baseStructure);
    if (unit == nullptr) return false;
    if (unit->build_progress != 1) return false;
    if (unit->orders.empty()) {
        Actions()->UnitCommand(unit, abilityId);
        return true;
    }
    return false;
}

// Original function with random build location
// TODO: Copied from MultiplayerBot
bool ZergCrush::TryBuildStructureRandom(AbilityID abilityTypeForStructure, UnitTypeID unitType) {
    Point2D buildLocation = getRandomLocationBy(startingLocation, 15.0f);

    Units units = Observation()->GetUnits(Unit::Self, IsStructure(Observation()));

    if (IsTooCloseToStructures(buildLocation, units, 6.0f)) {
        return false;
    }

    return TryBuildStructure(abilityTypeForStructure, unitType, buildLocation);
}

Point2D ZergCrush::getRandomLocationBy(Point2D location, float farRadius, float closeRadius) {
    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    Point2D point = Point2D(location.x + rx * (farRadius - closeRadius) + closeRadius,
                            location.y + ry * (farRadius - closeRadius) + closeRadius);
    return point;
}

bool ZergCrush::TryBuildStructureRandomWithUnit(AbilityID abilityTypeForStructure, const Unit *unit) {
    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    if (unit == nullptr) return false; // died
    Point2D buildLocation = Point2D(unit->pos.x + rx * 6, unit->pos.y + ry * 6);

    Units units = Observation()->GetUnits(Unit::Self, IsStructure(Observation()));

    if (IsTooCloseToStructures(buildLocation, units, 6.0f)) {
        return false;
    }

    return TryBuildStructureUnit(abilityTypeForStructure, unit, buildLocation, false);
}

//TODO: Copied from MultiplayerBot Tries to build a geyser for a base
bool ZergCrush::TryBuildGas(AbilityID build_ability, UnitTypeID worker_type, Point2D base_location) {
    const ObservationInterface *observation = Observation();
    Units geysers = observation->GetUnits(Unit::Alliance::Neutral, IsGeyser());

    //only search within this radius
    float minimum_distance = 15.0f;
    Tag closestGeyser = 0;
    for (const auto &geyser: geysers) {
        float current_distance = Distance2D(base_location, geyser->pos);
        if (current_distance < minimum_distance) {
            if (Query()->Placement(build_ability, geyser->pos)) {
                minimum_distance = current_distance;
                closestGeyser = geyser->tag;
            }
        }
    }

    // In the case where there are no more available geysers nearby
    if (closestGeyser == 0) {
        return false;
    }
    return TryBuildStructure(build_ability, worker_type, closestGeyser);

}

// TODO: Copied from MultiplayerBot
bool ZergCrush::TryBuildSupplyDepot() {
    const ObservationInterface *observation = Observation();

    if (observation->GetMinerals() < 100) return false;

    // check to see if there is already one building
    Units units = observation->GetUnits(Unit::Alliance::Self, IsUnits(supplyDepotTypes));
    if (observation->GetFoodUsed() < 40) { // TODO IAN: Same comment as above, may only want to check optionally
        //  not the responsibility of this function
        for (const auto &unit: units) {
            if (unit->build_progress != 1) return false;
        }
    }

    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    Point2D build_location = Point2D(startingLocation.x + rx * 15, startingLocation.y + ry * 15);
    return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT, UNIT_TYPEID::TERRAN_SCV, build_location);
}

// TODO: Copied from MultiplayerBot
bool ZergCrush::BuildRefinery() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

    if (observation->GetUnits(sc2::Unit::Self, IsUnit(sc2::UNIT_TYPEID::TERRAN_REFINERY)).size() >= bases.size() * 2) {
        return false;
    }

    for (const auto &base: bases) {
        TryBuildGas(ABILITY_ID::BUILD_REFINERY, UNIT_TYPEID::TERRAN_SCV, base->pos);
    }
    return true;
}

bool ZergCrush::IsTooCloseToStructures(const Point2D &buildLocation, const Units &structures, float minDistance) {
    return std::any_of(structures.begin(), structures.end(), [&](const auto &structure) {
        return (structure->unit_type != UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED) &&
               (Distance2D(structure->pos, buildLocation) < minDistance);
    });
}

void ZergCrush::setEnemyRace(const ObservationInterface *observation) {
    auto playerId = observation->GetPlayerID();
    auto gameInfo = observation->GetGameInfo();
    enemyRace = std::find_if(gameInfo.player_info.begin(), gameInfo.player_info.end(), [playerId](auto playerInfo) {
        return playerInfo.player_id != playerId && playerInfo.player_type == Participant ||
               playerInfo.player_type == Computer;
    })->race_requested;
}

void ZergCrush::OnUnitIdle(const Unit *unit) {
    switch (unit->unit_type.ToType()) {
        case UNIT_TYPEID::TERRAN_SCV: {
            HandleIdleWorker(unit);
            break;
        }
        default:
            break;
    }
}

void ZergCrush::OnGameEnd() {
    std::cout << "Game Ended for: " << std::to_string(Control()->Proto().GetAssignedPort()) << std::endl;
}

void ZergCrush::OnGameStart() {
    enemyStartingLocations = Observation()->GetGameInfo().enemy_start_locations;
    expansionLocations = search::CalculateExpansionLocations(Observation(), Query());

    startingLocation = Observation()->GetStartLocation();
    baseRallyPoint = startingLocation;

    setEnemyExpansionLocations();

    auto observation = Observation();
    setEnemyRace(observation);

    attackMicro = new ZergCrushMicro(Actions());

    std::vector<BuildOrderStructure> tvtStructures = {
            BuildOrderStructure(observation, 13, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, true),
            BuildOrderStructure(observation, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 18, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 23, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 25, UNIT_TYPEID::TERRAN_MISSILETURRET, true),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 28, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 28, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 32, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 32, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 33, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 33, UNIT_TYPEID::TERRAN_STARPORTTECHLAB, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 34, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 43, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 52, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
    };

    std::vector<ArmySquadron *> tvtArmyComposition = {
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_REAPER, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKS), 1, 2},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_REAPER), 1, 10},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_HELLION, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORY), 1, 2},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SIEGETANK, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORYTECHLAB), 1, 3},
            }),
    };

    std::vector<BuildOrderStructure> tvzStructures = {
            BuildOrderStructure(observation, 13, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, true),
            BuildOrderStructure(observation, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 21, UNIT_TYPEID::TERRAN_BUNKER),
            BuildOrderStructure(observation, 23, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 25, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 27, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 28, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 31, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB),
            BuildOrderStructure(observation, 35, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 35, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 44, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 44, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 45, UNIT_TYPEID::TERRAN_BUNKER),
            BuildOrderStructure(observation, 47, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 47, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 53, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 57, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 25, UNIT_TYPEID::TERRAN_MISSILETURRET, true),
            BuildOrderStructure(observation, 59, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
            BuildOrderStructure(observation, 60, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 64, UNIT_TYPEID::TERRAN_FACTORYTECHLAB),
            BuildOrderStructure(observation, 73, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 78, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 79, UNIT_TYPEID::TERRAN_STARPORTREACTOR, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 82, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 82, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 95, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 100, UNIT_TYPEID::TERRAN_ARMORY),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 108, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 125, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 126, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 126, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 139, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 144, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 144, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 144, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 150, UNIT_TYPEID::TERRAN_FACTORYREACTOR, UNIT_TYPEID::TERRAN_FACTORY),

    };

    std::vector<ArmySquadron *> tvzArmyComposition = {
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SCV, UNIT_TYPEID::TERRAN_COMMANDCENTER, {
                    {IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOT), 1, 1},
            }, SCOUT, false), // Scouting SCV
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKS), 1,        4},
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSREACTOR), 1, 20},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARAUDER, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB), 1, 12},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SIEGETANK, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORYTECHLAB), 1, 5},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MEDIVAC, UNIT_TYPEID::TERRAN_STARPORT, {
                    {IsUnit(UNIT_TYPEID::TERRAN_STARPORT), 1, 4},
            }),
    };
    std::vector<BuildOrderStructure> tvpStructures = {
            BuildOrderStructure(observation, 13, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, true),
            BuildOrderStructure(observation, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 21, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 22, UNIT_TYPEID::TERRAN_BUNKER),
            BuildOrderStructure(observation, 23, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 24, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 24, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 30, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 37, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 40, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
            BuildOrderStructure(observation, 43, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 43, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 45, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 47, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 49, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 50, UNIT_TYPEID::TERRAN_FACTORYREACTOR, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 57, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 57, UNIT_TYPEID::TERRAN_MISSILETURRET, true),
            BuildOrderStructure(observation, 59, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
            BuildOrderStructure(observation, 60, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 64, UNIT_TYPEID::TERRAN_FACTORYTECHLAB),
            BuildOrderStructure(observation, 70, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 70, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 73, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 78, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 79, UNIT_TYPEID::TERRAN_STARPORTREACTOR, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 95, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 100, UNIT_TYPEID::TERRAN_ARMORY),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 108, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 125, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 126, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 126, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 139, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 144, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 144, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 144, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 150, UNIT_TYPEID::TERRAN_FACTORYREACTOR, UNIT_TYPEID::TERRAN_FACTORY),
    };
        
    std::vector<ArmySquadron *> tvpArmyComposition = {
        new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_BARRACKS, {
                {IsUnit(UNIT_TYPEID::TERRAN_BARRACKS),        1, 4},
                {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSREACTOR), 1, 20},
        }),
        new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARAUDER, UNIT_TYPEID::TERRAN_BARRACKS, {
                {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB), 1, 10},
        }),
        new ArmySquadron(observation, UNIT_TYPEID::TERRAN_REAPER, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKS), 1, 3},
        }),
        new ArmySquadron(observation, UNIT_TYPEID::TERRAN_HELLION, UNIT_TYPEID::TERRAN_FACTORY, {
                {IsUnit(UNIT_TYPEID::TERRAN_FACTORYTECHLAB), 1, 5},
        }),
        new ArmySquadron(observation, UNIT_TYPEID::TERRAN_VIKINGASSAULT, UNIT_TYPEID::TERRAN_STARPORT, {
                {IsUnit(UNIT_TYPEID::TERRAN_STARPORT), 1, 4},
        }),
        
    };

    switch (enemyRace) {
        case Random:
        case Terran: {
            buildOrder = new BuildOrder(tvtStructures);
            armyComposition = new ArmyComposition(tvtArmyComposition);
            break;
        }
        case Zerg: {
            buildOrder = new BuildOrder(tvzStructures);
            armyComposition = new ArmyComposition(tvzArmyComposition);
            break;
        }
        case Protoss:
            buildOrder = new BuildOrder(tvpStructures);
            armyComposition = new ArmyComposition(tvpArmyComposition);
            break;
    }
}

void ZergCrush::setEnemyExpansionLocations() {
    for (const auto &expansionLocation: expansionLocations) {
        float distanceToAllyStartingLocation = Distance2D(startingLocation, expansionLocation);
        float minDistanceToEnemyStartingLocation = std::numeric_limits<float>::max();
        Point2D closestEnemyStartingLocation;

        // Iterate through each enemy starting location to find the closest one
        for (const auto &enemyStartingLocation: enemyStartingLocations) {
            float distanceToEnemyStartingLocation = Distance2D(enemyStartingLocation, expansionLocation);

            // Update the closest enemy starting location and its distance
            if (distanceToEnemyStartingLocation < minDistanceToEnemyStartingLocation) {
                minDistanceToEnemyStartingLocation = distanceToEnemyStartingLocation;
                closestEnemyStartingLocation = enemyStartingLocation;
            }
        }

        if (minDistanceToEnemyStartingLocation < distanceToAllyStartingLocation) {
            expansionMap[closestEnemyStartingLocation].push_back(expansionLocation);
        } else {
            expansionMap[startingLocation].push_back(expansionLocation);
        }
    }
    sortEnemyExpansionLocations();
}

void ZergCrush::sortEnemyExpansionLocations() {
    for (auto& expansionsByStartingLocation : expansionMap) {
        auto& expansionLocationsByLocation = expansionsByStartingLocation.second;

        // Sort the expansions based on the distance to the starting location
        std::sort(expansionLocationsByLocation.begin(), expansionLocationsByLocation.end(), [this](const auto& a, const auto& b) {
            return Distance2D(startingLocation, a) < Distance2D(startingLocation, b);
        });
    }
}

void ZergCrush::OnUnitEnterVision(const sc2::Unit *) {
    armyComposition->setDesiredUnitCounts(Observation());
}

void ZergCrush::OnUnitCreated(const sc2::Unit *unit) {
    buildOrder->OnUnitCreated(Observation(), unit);
    armyComposition->setDesiredUnitCounts(Observation());
    armyComposition->assignUnitToSquadron(unit);
}

void ZergCrush::OnBuildingConstructionComplete(const Unit *unit) {
    buildOrder->OnBuildingFinished(Observation(), unit);
}

void ZergCrush::OnUnitDestroyed(const sc2::Unit *unit) {
    buildOrder->OnUnitDestroyed(unit);
}

void ZergCrush::OnUpgradeCompleted(sc2::UpgradeID upgradeId) {
    attackMicro->onUpgradeComplete(upgradeId);
}

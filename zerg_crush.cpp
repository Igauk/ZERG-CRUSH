#include "zerg_crush.h"

using namespace sc2;

void ZergCrush::OnStep() {
    const ObservationInterface *observation = Observation();
    const int framesToSkip = 4;

    if (observation->GetGameLoop() % framesToSkip == 0) return; // Help with lag

    ManageArmy();
    ManageMacro();
    ManageUpgrades();
    TryBuildSCV();
    TryCallDownMule();
    BuildArmy();
}

void ZergCrush::BuildArmy() {
    const ObservationInterface *observation = Observation();

    auto allSquadrons = armyComposition->getAllSquadrons();
    auto unitsToBuild = armyComposition->squadronsToBuild(allSquadrons, observation);
    std::random_shuffle(unitsToBuild.begin(), unitsToBuild.end()); // Shuffle units to randomize build order
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
            return TryBuildStructure(ABILITY_ID::BUILD_BARRACKS, map_postions[2][closest_ramp], false);
        }
        else {
            //std::cout << "building barrack 1 at " << map_postions[1][closest_ramp].x << std::endl;
            return TryBuildStructure(ABILITY_ID::BUILD_BARRACKS, map_postions[1][closest_ramp], false);
        }
    }
    else if(piece == UNIT_TYPEID::TERRAN_MISSILETURRET) {
        //std::cout <<"building wall turret" <<std::endl;
        return TryBuildStructure(ABILITY_ID::BUILD_MISSILETURRET, map_postions[3][closest_ramp], false);
    }
        //supply depot
    else {
        //std::cout << "building depot " << map_postions[0][closest_ramp].x << std::endl;
        return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT, map_postions[0][closest_ramp], false);

        }

}

void ZergCrush::ManageMacro() {
    auto observation = Observation();
    auto structures = buildOrder->structuresToBuild(observation);

    for (const auto &structure: structures) {
        switch ((UNIT_TYPEID) structure->getUnitTypeID()) {
            case UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
                if(structure->isChainBuildLeader()) {
                    TryBuildWallPiece(UNIT_TYPEID::TERRAN_SUPPLYDEPOT);
                } else {
                    TryBuildSupplyDepot();
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
               Units command_centers = observation->GetUnits(Unit::Self, IsTownHall());
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
                if(TryBuildStructure(ABILITY_ID::BUILD_BUNKER, Point2D(proxy_center.x + 5.0, proxy_center.y + 5.0),
                                     false)) {continue;}
                //East
                else if(TryBuildStructure(ABILITY_ID::BUILD_BUNKER, Point2D(proxy_center.x + 5.0, proxy_center.y - 5.0),
                                          false)) {continue;}
                //South
                else if(TryBuildStructure(ABILITY_ID::BUILD_BUNKER, Point2D(proxy_center.x - 5.0, proxy_center.y - 5.0),
                                          false)) {continue;}
                //West
                else if(TryBuildStructure(ABILITY_ID::BUILD_BUNKER, Point2D(proxy_center.x - 5.0, proxy_center.y + 5.0),
                                          false)) {continue;}
                else{
                    //hopefully we dont get here
                    //std::cout <<"why here" <<std::endl;
                    TryBuildStructure(ABILITY_ID::BUILD_BUNKER,
                                      getRandomLocationBy(command_centers[farthest_command_center]->pos, 5.0), false);
                    continue;
                }
                break;
            }
            default:
                if (structure->isAddOn()) {
                    const Unit *baseStruct = structure->getBaseStruct(observation);
                    if (baseStruct) {
                        structure->setBuiltAddOn(TryBuildFrom(structure->getAbilityId(), baseStruct->tag, true));
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
    for (auto & upgrade: upgradeOrder->upgradesToBuild(observation)) {
        TryBuildUnit(upgrade->getAbilityId(), upgrade->getUpgradeStructure());
    }
}

void ZergCrush::ManageArmy() {
    const ObservationInterface *observation = Observation();

    Units army = observation->GetUnits(Unit::Alliance::Self, IsArmy(observation));
    uint32_t waitUntilSupply = 30;

    auto allSquadrons = armyComposition->getAllSquadrons();

    std::vector<ArmySquadron *> scouts = armyComposition->getSquadronsByType(allSquadrons, SCOUT);
    for (auto &scoutSquadron: scouts) {
        // Only scout with completed squadrons
        if (scoutSquadron->needMore() || scoutSquadron->getSquadron().empty()) continue;
        ScoutWithUnits(observation, scoutSquadron->getSquadron());
    }

    std::vector<ArmySquadron *> main = armyComposition->getSquadronsByType(allSquadrons, MAIN);
    // Once we are ready to attack
    if (waitUntilSupply >= observation->GetArmyCount()) {
        for (auto &mainSquadron: main) {
            auto squadron = mainSquadron->getSquadron();
            if (squadron.empty()) continue;

            // If there are units nearby use micro strategies (i.e. we are already on the offensive, or being attacked)
            sc2::Units nearby = observation->GetUnits(sc2::Unit::Enemy, WithinDistanceOf(squadron.front()->pos, 20.0f));
            if (!nearby.empty()) {
                for (const auto &unit: squadron) {
                    attackMicro->microUnit(observation, unit);
                }
                continue;
            }

            // If there are units by the base, and we are not by the base return home
            sc2::Units byBase = observation->GetUnits(sc2::Unit::Enemy, CombinedFilter({IsVisible(), WithinDistanceOf(startingLocation, 50.0f)}));
            if (!byBase.empty() && Distance2D(squadron.front()->pos, startingLocation) > SQUADRON_CLUSTER_DISTANCE) {
                Actions()->UnitCommand(squadron, ABILITY_ID::MOVE_MOVE, byBase.front()->pos);
                continue;
            }

            // Otherwise send units near the rally point if they aren't there
            if (Distance2D(squadron.front()->pos, baseRallyPoint) > SQUADRON_CLUSTER_DISTANCE) {
                Actions()->UnitCommand(squadron, ABILITY_ID::MOVE_MOVE, getRandomLocationBy(baseRallyPoint, 5.0f));
            } else {
                // And group them together once there
                clusterUnits(squadron, SQUADRON_CLUSTER_DISTANCE);
            }
        }
    } else { // We are ready to attack
        // Group our units into one large squadron
        Units mainArmyUnits;
        for (auto &mainSquadron: main) {
            auto squadron = mainSquadron->getSquadron();
            mainArmyUnits.insert(mainArmyUnits.end(), squadron.begin(), squadron.end());
        }
        // Scout collectively with this main army
        ScoutWithUnits(observation, mainArmyUnits, SQUADRON_CLUSTER_DISTANCE * (float) (mainArmyUnits.size() / 20));
    }
}

void ZergCrush::ScoutWithUnits(const sc2::ObservationInterface *observation, const sc2::Units &units,
                               float clusterDistance) {
    if (units.empty()) return; // Nothing to do

    sc2::Units scoutingUnits = Units(units);
    auto clusters = getClusters(units, clusterDistance);

    // First we handle units that are being attacked while scouting
    for (const auto &cluster: clusters) {
        auto clusterUnits = cluster.second;
        const Unit *clusterLeader = &(clusterUnits.front());

        sc2::Units nearbyEnemies = observation->GetUnits(sc2::Unit::Enemy, WithinDistanceOf(clusterLeader, 15.0f));
        sc2::Units byBase = observation->GetUnits(sc2::Unit::Enemy, CombinedFilter({IsVisible(), WithinDistanceOf(baseRallyPoint, 40.0f)}));
        sc2::Units invisibleNearby = observation->GetUnits(sc2::Unit::Enemy, CombinedFilter({IsInvisible(), WithinDistanceOf(clusterLeader, 15.0f)}));
        
        // If there are multiple enemies at this location this is likely where the enemy base is
        if (nearbyEnemies.size() > 1) setAssumedEnemyStartingLocation(cluster.first);
        
        // We are being attacked, or our base is being attacked
        if (!nearbyEnemies.empty() || !byBase.empty()) {
            
            // Scan if we are being attacked by invisible units
            if (!invisibleNearby.empty()) {
                TryScannerSweep(observation, clusterLeader->pos);
            }
            
            // Remove the units in this from the main scouting group
            scoutingUnits.erase(std::remove_if(scoutingUnits.begin(), scoutingUnits.end(), [&](const auto& smallClusterUnit) {
                return std::any_of(cluster.second.begin(), cluster.second.end(), [&](const auto& unit) {
                    return unit.tag == smallClusterUnit->tag;
                });
            }), scoutingUnits.end());

            // And attack with these units
            for (const auto &unit: cluster.second) attackMicro->microUnit(observation, &unit);
        }
    }

    // Set the largest cluster size to send smaller clusters to
    int minClusterSize = (int) units.size() / 5;
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
            Actions()->UnitCommand(&unit, ABILITY_ID::MOVE_MOVE, largestClusterPosition);
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

    if (assumedEnemyStartingLocation != nullptr) {
        if (scoutingUnits.empty()) return;
        auto scoutLeader = scoutingUnits.front();
        if (sc2::Distance2D(scoutLeader->pos, *assumedEnemyStartingLocation) < SQUADRON_CLUSTER_DISTANCE) {
            sc2::Units enemiesByLocation = observation->GetUnits(sc2::Unit::Alliance::Enemy, CombinedFilter({IsVisible(), WithinDistanceOf(*assumedEnemyStartingLocation, 15.0f)}));

            if (enemiesByLocation.empty()) {
                assumedEnemyStartingLocation = nullptr;
                return;
            }
        }


        Actions()->UnitCommand(scoutingUnits, ABILITY_ID::MOVE_MOVEPATROL, *assumedEnemyStartingLocation);
        return;
    }

    for (auto &location: getExpansionsToCheck()) {
        if (expansionMap.count(location) > 0) { // We haven't discounted this location yet
            Actions()->UnitCommand(scoutingUnits, ABILITY_ID::MOVE_MOVEPATROL, expansionMap[location].front());
        }
    }
}

void ZergCrush::TryScannerSweep(const ObservationInterface *observation, Point2D scanLocation) {
    auto bases = observation->GetUnits(Unit::Self, IsTownHall());
    for (auto &base: bases) {
        if (base->energy > 50) {
            Actions()->UnitCommand(base, ABILITY_ID::EFFECT_SCAN, scanLocation);
            break;
        }
    }
}

void ZergCrush::setAssumedEnemyStartingLocation(const Point3D &scoutLocation) {
    if (assumedEnemyStartingLocation == nullptr) {
        for (auto &location: getExpansionsToCheck()) {
            if (Distance2D(scoutLocation, location) < 30.0f && expansionMap.count(location) > 0) {
                assumedEnemyStartingLocation = new Point2D(location);
            }
        }
    }
}

void ZergCrush::RaiseAllSupplyDepots() {
    for (auto& supplyDepot: Observation()->GetUnits(IsUnit(sc2::UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED))) {
        Actions()->UnitCommand(supplyDepot, ABILITY_ID::MORPH_SUPPLYDEPOT_RAISE);
    }
}

void ZergCrush::markOffScoutedLocations(const Point3D &scoutLocation) {
    if (assumedEnemyStartingLocation != nullptr) return; // If we know where the enemy is we can leave this
    const float markoffDistance = 7.5f;
    for (auto &location: getExpansionsToCheck()) {
        // These units are definitely not in battle - so no enemies at this location
        if (Distance2D(scoutLocation, location) < markoffDistance && expansionMap.count(location) > 0) {
            expansionMap.erase(location); // Do not scout this expansion anymore ...
            continue;
        }

        if (expansionMap.size() == 2) {
            // enemy must be at last location
            for (auto &kv : expansionMap) {
                if (kv.first != Point2D(startingLocation)) {
                    assumedEnemyStartingLocation = &kv.first;
                }
            }
        }

        if (expansionMap.size() == 1) {
            refreshExpansionLocations(Observation(), scoutLocation);
            checkOurExpansions = true;
        }

        // Otherwise lets check if we are by any of its expansions and remove any that we are near
        if (expansionMap.count(location) > 0) {
            auto& expansions = expansionMap[location];
            expansions.erase(std::remove_if(expansions.begin(), expansions.end(), [markoffDistance, scoutLocation](const auto &expansion) {
                return Distance2D(scoutLocation, expansion) < markoffDistance;
            }), expansions.end());
        }
    }
}

std::vector<Point2D> ZergCrush::getExpansionsToCheck() const {
    std::vector<Point2D> toCheck = enemyStartingLocations;
    if (checkOurExpansions) {
        toCheck.emplace_back(startingLocation);
    }
    return toCheck;
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

    if (clusters.size() <= 1) {
        return;  // No need to cluster if there's only one or zero clusters
    }

    // Special Case: Clusters at different heights, group at the largest one
    float heightDeltaThreshold = 1.0f;
    if (std::adjacent_find(clusters.begin(), clusters.end(), [&](const auto &cluster1, const auto &cluster2) {
        return std::abs(cluster1.first.z - cluster2.first.z) > heightDeltaThreshold;
    }) != clusters.end()) {
        // There are clusters at different heights, move units to the center of the largest cluster
        auto largestCluster = std::max_element(clusters.begin(), clusters.end(),
                                               [](const auto &clusterA, const auto &clusterB) {
                                                   return clusterA.second.size() < clusterB.second.size();
                                               });

        Actions()->UnitCommand(units, ABILITY_ID::SMART, largestCluster->first);
        return;
    }

    // General case: Calculate the clustersMassCenter and shiftAmount
    Point3D clustersMassCenter;
    for (const auto &cluster : clusters) {
        clustersMassCenter += cluster.first;
    }
    clustersMassCenter /= static_cast<float>(clusters.size());

    Point3D shiftAmount = {0.0f, 0.0f, 0.0f};
    for (const auto &cluster : clusters) {
        float clusterUnitRatio = static_cast<float>(cluster.second.size()) / static_cast<float>(totalUnitCount);
        Point3D toMassCenter = cluster.first - clustersMassCenter;
        shiftAmount += toMassCenter * clusterUnitRatio;
    }
    clustersMassCenter += shiftAmount;

    // Move units to the clustersMassCenter
    Actions()->UnitCommand(units, ABILITY_ID::SMART, clustersMassCenter);
}


void ZergCrush::HandleIdleWorker(const Unit *worker) {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
    Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(sc2::UNIT_TYPEID::TERRAN_REFINERY));

    if (bases.empty()) return;

    const Unit* potentialGeyser = nullptr;
    const Unit* potentialBase = nullptr;

    // Search for a geyser that is missing workers to assign this worker to
    for (const auto &geyser: geysers) {
        if (geyser->assigned_harvesters < geyser->ideal_harvesters) {
            potentialGeyser = geyser;
            break;
        }
    }

    // Search for a base that is missing workers to assign this worker (prioritize the base that built the SCV by sorting)
    std::sort(bases.begin(), bases.end(), [worker](const auto &baseA, const auto &baseB) {
        return Distance2D(baseA->pos, worker->pos) < Distance2D(baseB->pos, worker->pos);
    });
    for (const auto &base: bases) {
        if (base->ideal_harvesters == 0 || base->build_progress != 1) { continue; }
        if (base->assigned_harvesters < base->ideal_harvesters) {
            potentialBase = base;
            break;
        }
    }

    if (!potentialGeyser && !potentialBase) {
        // If all workers are spots are filled just go to any base
        const Unit *randomBase = GetRandomEntry(bases);
        Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, FindNearestMineralPatch(randomBase->pos));
    } else if (potentialBase && !potentialGeyser) {
        Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, FindNearestMineralPatch(potentialBase->pos));
    } else if (potentialGeyser && !potentialBase) {
        Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, potentialGeyser);
    } else {
        // 50/50 assign to one or the other
        if (rand() % 2 == 0) Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, FindNearestMineralPatch(potentialBase->pos));
        else Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER_SCV, potentialGeyser);
    }


}

const Unit *ZergCrush::FindNearestMineralPatch(const Point2D &start) {
    Units mineralFields = Observation()->GetUnits(IsUnit(UNIT_TYPEID::NEUTRAL_MINERALFIELD));
    return *std::min_element(mineralFields.begin(), mineralFields.end(),
                             [start](const auto &mineralFieldA, const auto &mineralFieldB) {
                                 return Distance2D(start, mineralFieldA->pos) < Distance2D(start, mineralFieldB->pos);
                             });
}


// Copied from MultiplayerBot An estimate of how many workers we should have based on what buildings we have
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

    return TryBuildFrom(abilityTypeForUnit, buildingUnit->tag, false);
}

bool ZergCrush::TryBuildSCV() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

    if (observation->GetFoodWorkers() >= MAX_WORKER_COUNT) return false;
    if (observation->GetFoodUsed() >= observation->GetFoodCap()) return false;
    if (observation->GetFoodWorkers() > GetExpectedWorkers()) return false;

    const Unit* baseToBePopulated = nullptr;
    const Unit* geyserToBePopulated = nullptr;
    int numWorkersNeeded = 0;

    // Find a base that is missing workers, we will try to supply it
    for (const auto &base: bases) {
        if (base->assigned_harvesters < base->ideal_harvesters && base->build_progress == 1) {
            baseToBePopulated = base;
            numWorkersNeeded = base->ideal_harvesters - base->assigned_harvesters;
            break;
        }
    }
    if (baseToBePopulated == nullptr) return false; // No base needs SCVs

    std::sort(bases.begin(), bases.end(), [baseToBePopulated](const auto& baseA, const auto& baseB) {
        return Distance2D(baseToBePopulated->pos, baseA->pos) < Distance2D(baseToBePopulated->pos, baseB->pos);
    });

    for (const auto &base: bases) {
        if (base->orders.empty() && numWorkersNeeded != 0 && TryBuildUnit(ABILITY_ID::TRAIN_SCV, base->unit_type)) {
            Actions()->UnitCommand(base, ABILITY_ID::RALLY_COMMANDCENTER, baseToBePopulated->pos);
            numWorkersNeeded--;
        }
    }
    return true;
}

void ZergCrush::TryCallDownMule() {
    auto bases = Observation()->GetUnits(IsTownHall());
    for (const auto &base: bases) {
        if (base->unit_type == UNIT_TYPEID::TERRAN_ORBITALCOMMAND && base->energy > 50) {
            Actions()->UnitCommand(base, ABILITY_ID::EFFECT_CALLDOWNMULE, FindNearestMineralPatch(base->pos));
        }
    }
}

// Modified from MultiplayerBot Try build structure given a location. This is used most of the time
bool ZergCrush::TryBuildStructure(AbilityID abilityIdForStruct, Point2D location, bool isExpansion) {
    const ObservationInterface *observation = Observation();
    Units workers = observation->GetUnits(Unit::Alliance::Self, CombinedFilter({
        IsUnit(sc2::UNIT_TYPEID::TERRAN_SCV),
        WithinDistanceOf(location, isExpansion ? 50.0f : 15.0f)
    }));

    // If we have no workers Don't build
    if (workers.empty()) return false;

    // Check to see if there is already a worker heading out to build it
    for (const auto &worker: workers) {
        for (const auto &order: worker->orders) {
            if (order.ability_id == abilityIdForStruct) {
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
    if (Query()->Placement(abilityIdForStruct, location)) {
        Actions()->UnitCommand(unit, abilityIdForStruct, location);
        return true;
    }
    return false;

}

// Modified from MultiplayerBot - Try to build a structure based on tag, Used mostly for Vespene, since the pathing check will fail even though the geyser is "Pathable"
bool ZergCrush::TryBuildStructure(AbilityID abilityTypeForStructure, Tag structureTag) {
    const ObservationInterface *observation = Observation();
    const Unit *target = observation->GetUnit(structureTag);
    Units workers = observation->GetUnits(Unit::Alliance::Self, CombinedFilter({
        IsUnit(sc2::UNIT_TYPEID::TERRAN_SCV),
        WithinDistanceOf(target->pos, 10.0f)
    }));

    if (workers.empty()) return false;

    // Check to see if there is already a worker heading out to build it
    for (const auto &worker: workers) {
        for (const auto &order: worker->orders) {
            if (order.ability_id == abilityTypeForStructure) {
                return false;
            }
        }
    }

    // If no worker is already building one, get a random worker to build one
    const Unit *unit = GetRandomEntry(workers);

    // Check to see if unit can build there
    if (Query()->Placement(abilityTypeForStructure, target->pos)) {
        Actions()->UnitCommand(unit, abilityTypeForStructure, target);
        return true;
    }
    return false;

}

bool ZergCrush::TryBuildStructureUnit(AbilityID abilityTypeForStructure, const Unit *unit, Point2D location,
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
    if (Query()->Placement(abilityTypeForStructure, location, unit)) {
        Actions()->UnitCommand(unit, abilityTypeForStructure, location);
        return true;
    }
    return false;
}

// Modified from MultiplayerBot Expands to nearest location and updates the start location to be between the new location and old bases.
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

    if (TryBuildStructure(buildAbility, closestExpansion, true) &&
        observation->GetUnits(Unit::Self, IsTownHall()).size() < 4) {
        auto rallyPoint = getRandomLocationBy(closestExpansion, 15.0f);
        baseRallyPoint = {rallyPoint.x, rallyPoint.y, 0.0};
        return true;
    }
    return false;

}

bool ZergCrush::TryBuildFrom(AbilityID abilityId, Tag baseStructure, bool checkPlacement) {
    const Unit *unit = Observation()->GetUnit(baseStructure);
    if (unit == nullptr) return false;
    if (unit->build_progress != 1) return false;
    if (unit->orders.empty()) {
        if (checkPlacement && !Query()->Placement(abilityId, unit->pos, unit)) {
            return TryBuildStructureRandomWithUnit(abilityId, unit); // Lift off if need be
        } else {
            Actions()->UnitCommand(unit, abilityId);
            return true;
        }
    }
    return false;
}

// Modified from MultiplayerBot Original function with random build location
bool ZergCrush::TryBuildStructureRandom(AbilityID abilityTypeForStructure, UnitTypeID unitType) {
    Point2D buildLocation = getRandomLocationBy(startingLocation, 15.0f);

    Units units = Observation()->GetUnits(Unit::Self, IsStructure(Observation()));

    return TryBuildStructure(abilityTypeForStructure, buildLocation, false);
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
    if (unit == nullptr) return false; // destroyed
    Point2D buildLocation = Point2D(unit->pos.x + rx * 10.0f, unit->pos.y + ry * 10.0f);

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
    return TryBuildStructure(build_ability, closestGeyser);

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
    return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT, build_location, false);
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
            RaiseAllSupplyDepots(); // Might be stuck behind supply depot
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


// TODO: Build order is still bad -> more units at certain points, use conditions
void ZergCrush::OnGameStart() {
    auto observation = Observation();
    startingLocation = observation->GetStartLocation();
    baseRallyPoint = startingLocation;

    refreshExpansionLocations(observation, startingLocation);

    setEnemyRace(observation);

    attackMicro = new ZergCrushMicro(Actions());

    std::vector<Upgrade> tvt_upgrades {
            Upgrade(observation, 20, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, ABILITY_ID::RESEARCH_STIMPACK, UPGRADE_ID::STIMPACK),
            Upgrade(observation, 40, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, ABILITY_ID::RESEARCH_COMBATSHIELD, UPGRADE_ID::COMBATSHIELD),
            Upgrade(observation, 40, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, ABILITY_ID::RESEARCH_INFERNALPREIGNITER, UPGRADE_ID::INFERNALPREIGNITERS),
            Upgrade(observation, 80, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYARMORLEVEL1, UPGRADE_ID::TERRANINFANTRYARMORSLEVEL1),
            Upgrade(observation, 80, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONSLEVEL1, UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL1),
            Upgrade(observation, 101, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYARMORLEVEL2, UPGRADE_ID::TERRANINFANTRYARMORSLEVEL2),
            Upgrade(observation, 101, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONSLEVEL2, UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL2),
    };

    std::vector<BuildOrderStructure> tvtStructures = {
            BuildOrderStructure(observation, 13, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 15, UNIT_TYPEID::TERRAN_REFINERY, {{
                IsUnit(sc2::UNIT_TYPEID::TERRAN_BARRACKS), 1, false, Unit::Alliance::Self
            }}),
            BuildOrderStructure(observation, 15, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_FACTORYREACTOR, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 21, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 22, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 22, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, {
                    {IsTownHall(), 3, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 22, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 27, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 31, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 33, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 31, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 41, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 41, UNIT_TYPEID::TERRAN_STARPORTTECHLAB, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 48, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 48, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 52, UNIT_TYPEID::TERRAN_REFINERY, {
                {IsTownHall(), 3, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 53, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 53, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 60, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 68, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 74, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 74, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_STARPORTREACTOR),
            BuildOrderStructure(observation, 83, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 84, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 85, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 100, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 100, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 103, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 103, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 104, UNIT_TYPEID::TERRAN_REFINERY)
    };

    std::vector<ArmySquadron *> tvtArmyComposition = {
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SCV, UNIT_TYPEID::TERRAN_COMMANDCENTER, {
                    {IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOT), 1, 1},
            }, SCOUT, false), // Scouting SCV
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSREACTOR), 1, 16, true},
                    {IsUnit(sc2::UNIT_TYPEID::TERRAN_BARRACKS), 1, 8, true},
                    {IsUnit(UNIT_TYPEID::TERRAN_MARINE), 1, 1, Unit::Alliance::Enemy, true, false},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARAUDER, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB), 1, 8, true},
                    {IsUnit(sc2::UNIT_TYPEID::TERRAN_MARINE), 2, 1, true},
                    {IsUnit(UNIT_TYPEID::TERRAN_MARAUDER), 1, 1, Unit::Alliance::Enemy, true, false},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_HELLION, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit{UNIT_TYPEID::TERRAN_MARINE}, 4, 1, true},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SIEGETANK, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB), 1, 1},
                    {IsUnit{UNIT_TYPEID::TERRAN_MARINE}, 8, 1, true}
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MEDIVAC, UNIT_TYPEID::TERRAN_STARPORT, {
                    {IsUnit(UNIT_TYPEID::TERRAN_STARPORT), 1, 3},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_BANSHEE, UNIT_TYPEID::TERRAN_STARPORT, {
                    {IsUnit(UNIT_TYPEID::TERRAN_STARPORTTECHLAB), 1, 10},
            }),
    };


    std::vector<BuildOrderStructure> tvzStructures = {
            BuildOrderStructure(observation, 0, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY,{
                    {IsUnits({UNIT_TYPEID::ZERG_ROACHWARREN, UNIT_TYPEID::ZERG_ROACH}), 1}}),
            BuildOrderStructure(observation, 0, UNIT_TYPEID::TERRAN_FACTORYREACTOR, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnits({UNIT_TYPEID::ZERG_ROACHWARREN, UNIT_TYPEID::ZERG_ROACH}), 1, true},
                    {IsUnits({UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::ZERG_ROACH}), 1, true, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnits({UNIT_TYPEID::ZERG_ROACHWARREN, UNIT_TYPEID::ZERG_ROACH}), 1}}),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::ZERG_ROACHWARREN), 1, true},
                    {IsUnit(UNIT_TYPEID::ZERG_ROACH), 10, true}
            }),

            BuildOrderStructure(observation, 13, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY, {
                {IsUnit(sc2::UNIT_TYPEID::TERRAN_BARRACKS), 1, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, {
                    {IsTownHall(), 2, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 27, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 28, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 31, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 35, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 44, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 45, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 45, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, {
                    {IsTownHall(), 3, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 45, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, {
                    {IsTownHall(), 3, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 45, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 53, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 57, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 59, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
            BuildOrderStructure(observation, 60, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 61, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 64, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 73, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 78, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 79, UNIT_TYPEID::TERRAN_STARPORTREACTOR, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 82, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 82, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 87, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 95, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 100, UNIT_TYPEID::TERRAN_ARMORY),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 101, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 108, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 112, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 120, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 125, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 125, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 126, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 130, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 134, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 138, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 144, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 144, UNIT_TYPEID::TERRAN_FACTORYREACTOR, UNIT_TYPEID::TERRAN_FACTORY),
    };

    std::vector<BuildOrderStructure> tvpStructures = {
            BuildOrderStructure(observation, 0, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY,{
                    {IsUnit(UNIT_TYPEID::PROTOSS_STALKER), 3}}),
            BuildOrderStructure(observation, 0, UNIT_TYPEID::TERRAN_FACTORYREACTOR, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::PROTOSS_STALKER), 3, true},
            }),
            // Build a factory techlab if we don't have one yet at this point
            BuildOrderStructure(observation, 35, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY,{
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORYTECHLAB), 1, true, Unit::Alliance::Self}
            }),

            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 30, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 70, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 110, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),

            BuildOrderStructure(observation, 86, UNIT_TYPEID::TERRAN_STARPORTREACTOR, UNIT_TYPEID::TERRAN_STARPORT),

            BuildOrderStructure(observation, 13, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 15, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY, {
                    {IsUnit(sc2::UNIT_TYPEID::TERRAN_BARRACKS), 1, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, {
                    {IsTownHall(), 2, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 22, UNIT_TYPEID::TERRAN_BARRACKS,{
                    {IsUnit(sc2::UNIT_TYPEID::TERRAN_FACTORY), 1, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 25, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 25, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 42, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 45, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 46, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 51, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 60, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 62, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
            BuildOrderStructure(observation, 67, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 65, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 70, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 70, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 78, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 81, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 90, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 90, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, {
                    {IsTownHall(), 3, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 90, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 90, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 91, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 93, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 96, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 100, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 100, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 108, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 114, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB),
            BuildOrderStructure(observation, 121, UNIT_TYPEID::TERRAN_ARMORY),
            BuildOrderStructure(observation, 125, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 134, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 134, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, {
                    {IsTownHall(), 4, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 134, UNIT_TYPEID::TERRAN_SUPPLYDEPOT, {
                    {IsTownHall(), 4, false, Unit::Alliance::Self}
            }),
            BuildOrderStructure(observation, 150, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 160, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 171, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 171, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 172, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 178, UNIT_TYPEID::TERRAN_STARPORTREACTOR),
            BuildOrderStructure(observation, 178, UNIT_TYPEID::TERRAN_FACTORYTECHLAB),
    };
    std::vector<Upgrade> tvz_upgrades {
            Upgrade(observation, 20, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, ABILITY_ID::RESEARCH_STIMPACK, UPGRADE_ID::STIMPACK),
            Upgrade(observation, 40, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, ABILITY_ID::RESEARCH_COMBATSHIELD, UPGRADE_ID::COMBATSHIELD),
            Upgrade(observation, 50, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONSLEVEL1, UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL1),
            Upgrade(observation, 80, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYARMORLEVEL1, UPGRADE_ID::TERRANINFANTRYARMORSLEVEL1),
            Upgrade(observation, 90, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONSLEVEL2, UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL2),
            Upgrade(observation, 100, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYARMORLEVEL2, UPGRADE_ID::TERRANINFANTRYARMORSLEVEL2),
            Upgrade(observation, 120, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONSLEVEL3, UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL3),
            Upgrade(observation, 120, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYARMORLEVEL3, UPGRADE_ID::TERRANINFANTRYARMORSLEVEL3),
    };

    std::vector<ArmySquadron *> tvzArmyComposition = {
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SCV, UNIT_TYPEID::TERRAN_COMMANDCENTER, {
                    {IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOT), 1, 1},
            }, SCOUT, false), // Scouting SCV
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSREACTOR), 1, 16, true},
                    {IsUnit(sc2::UNIT_TYPEID::TERRAN_BARRACKS), 1, 8, true},
                    {IsUnit(UNIT_TYPEID::ZERG_ZERGLING), 2, 1, Unit::Alliance::Enemy, true, false},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARAUDER, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB), 1, 5},
                    {IsUnit(UNIT_TYPEID::TERRAN_MARINE), 8, 4, true},
                    {IsUnit(UNIT_TYPEID::ZERG_ROACH), 1, 1, Unit::Alliance::Enemy, true, false},
                    {IsUnit(UNIT_TYPEID::ZERG_BANELING), 4, 1, Unit::Alliance::Enemy, true, false},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_CYCLONE, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORYTECHLAB), 1, 5, true},
                    {IsUnit(UNIT_TYPEID::TERRAN_MARINE), 5, 1, true},
                    {IsUnit(UNIT_TYPEID::ZERG_ROACH), 3, 1, Unit::Alliance::Enemy, true, false},
                    {IsUnit(UNIT_TYPEID::ZERG_ULTRALISK), 1, 3, Unit::Alliance::Enemy, true, false}
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_HELLION, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORY), 1, 3},
                    {IsUnit(UNIT_TYPEID::ZERG_ZERGLING), 5, 1, Unit::Alliance::Enemy, true, false},
                    {IsUnit(UNIT_TYPEID::ZERG_ROACH), 3, 0, Unit::Alliance::Enemy, false, true}
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MEDIVAC, UNIT_TYPEID::TERRAN_STARPORT, {
                    {IsUnit(UNIT_TYPEID::TERRAN_STARPORT), 1, 3},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_LIBERATOR, UNIT_TYPEID::TERRAN_STARPORT, {
                    {IsUnit(UNIT_TYPEID::TERRAN_STARPORTTECHLAB), 1, 5, true},
                    {IsUnits({UNIT_TYPEID::ZERG_MUTALISK}), 1, 1, Unit::Alliance::Enemy, true, false}
            }),
    };

    std::vector<Upgrade> tvp_upgrades {
            Upgrade(observation, 20, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, ABILITY_ID::RESEARCH_STIMPACK, UPGRADE_ID::STIMPACK),
            Upgrade(observation, 40, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, ABILITY_ID::RESEARCH_COMBATSHIELD, UPGRADE_ID::COMBATSHIELD),
            Upgrade(observation, 75, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYARMORLEVEL1, UPGRADE_ID::TERRANINFANTRYARMORSLEVEL1),
            Upgrade(observation, 75, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONSLEVEL1, UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL1),
            Upgrade(observation, 101, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYARMORLEVEL2, UPGRADE_ID::TERRANINFANTRYARMORSLEVEL2),
            Upgrade(observation, 101, UNIT_TYPEID::TERRAN_ENGINEERINGBAY, ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONSLEVEL2, UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL2),
    };


    std::vector<ArmySquadron *> tvpArmyComposition = {
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SCV, UNIT_TYPEID::TERRAN_COMMANDCENTER, {
                    {IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOT), 1, 1},
            }, SCOUT, false), // Scouting SCV
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKS), 1, 4, true},
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSREACTOR), 1, 16, true},
                    {IsUnit(UNIT_TYPEID::PROTOSS_IMMORTAL), 1, 5, Unit::Alliance::Enemy, false, false},
                    // Stop building if we see 2+ collosi or 16+ stalker
                    {IsUnit(UNIT_TYPEID::PROTOSS_STALKER), 10, 16, Unit::Alliance::Enemy, false, true},
                    {IsUnit(UNIT_TYPEID::PROTOSS_COLOSSUS), 2, 0, Unit::Alliance::Enemy, false, true},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARAUDER, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB), 1, 5, true},
                    {IsUnit(UNIT_TYPEID::PROTOSS_STALKER), 3, 5, Unit::Alliance::Enemy, true, false},
                    {IsUnit(UNIT_TYPEID::PROTOSS_COLOSSUS), 1, 5, Unit::Alliance::Enemy, true, false}
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MEDIVAC, UNIT_TYPEID::TERRAN_STARPORT, {
                    {IsUnit(UNIT_TYPEID::TERRAN_STARPORT), 1, 3},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_VIKINGFIGHTER, UNIT_TYPEID::TERRAN_STARPORT, {
                    {IsUnit(UNIT_TYPEID::TERRAN_STARPORT), 1, 2, true},
                    // Very strong against collosi
                    {IsUnit(UNIT_TYPEID::PROTOSS_COLOSSUS), 1, 5, Unit::Alliance::Enemy, true, false},
                    {IsUnit(UNIT_TYPEID::PROTOSS_VOIDRAY), 1, 1, Unit::Alliance::Enemy, true, false},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_CYCLONE, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_MARINE), 5, 1, true},
                    {IsUnit(UNIT_TYPEID::TERRAN_MARAUDER), 3, 1, true},
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORY), 2, 16, true},
                    {IsUnit(UNIT_TYPEID::PROTOSS_IMMORTAL), 4, 0, Unit::Alliance::Enemy, false, true},
                    {IsUnits({UNIT_TYPEID::PROTOSS_ADEPT, UNIT_TYPEID::PROTOSS_ADEPTPHASESHIFT}), 2, 1, Unit::Alliance::Enemy, true, false},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SIEGETANK, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORYTECHLAB), 1, 4, true},
                    {IsUnit(UNIT_TYPEID::TERRAN_MARAUDER), 8, 2, true},
            }),

    };

    switch (enemyRace) {
        case Random:
        case Terran: {
            buildOrder = new BuildOrder(tvtStructures);
            armyComposition = new ArmyComposition(tvtArmyComposition);
            upgradeOrder = new UpgradeOrder(tvt_upgrades);
            break;
        }
        case Zerg: {
            buildOrder = new BuildOrder(tvzStructures);
            armyComposition = new ArmyComposition(tvzArmyComposition);
            upgradeOrder = new UpgradeOrder(tvt_upgrades);
            break;
        }
        case Protoss:
            buildOrder = new BuildOrder(tvpStructures);
            armyComposition = new ArmyComposition(tvpArmyComposition);
            upgradeOrder = new UpgradeOrder(tvp_upgrades);
            break;
    }
}

/**
 * Refresh expansionLocations and expansions map and sort based on a reference location
 */
void ZergCrush::refreshExpansionLocations(const ObservationInterface *observation, Point2D referenceLocation) {
    enemyStartingLocations = observation->GetGameInfo().enemy_start_locations;
    expansionLocations = search::CalculateExpansionLocations(observation, Query());

    // Weird clusters at bottom corner
    expansionLocations.erase(std::remove_if(expansionLocations.begin(), expansionLocations.end(), [](auto &location) {
        return location.x == 0.0f && location.y == 0.0f;
    }), expansionLocations.end());

    setEnemyExpansionLocations(referenceLocation);
}

void ZergCrush::setEnemyExpansionLocations(Point2D referenceLocation) {
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
    sortEnemyExpansionLocations(referenceLocation);
}

void ZergCrush::sortEnemyExpansionLocations(Point2D referenceLocation) {
    for (auto& expansionsByStartingLocation : expansionMap) {
        auto& expansionLocationsByLocation = expansionsByStartingLocation.second;

        // Sort the expansions based on the distance to the reference location
        std::sort(expansionLocationsByLocation.begin(), expansionLocationsByLocation.end(), [referenceLocation](const auto& a, const auto& b) {
            return Distance2D(referenceLocation, a) < Distance2D(referenceLocation, b);
        });
    }

    // And enemy base locations too
    std::sort(enemyStartingLocations.begin(), enemyStartingLocations.end(), [referenceLocation](const auto& a, const auto& b) {
        return Distance2D(referenceLocation, a) < Distance2D(referenceLocation, b);
    });
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

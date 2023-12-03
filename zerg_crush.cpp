#include "zerg_crush.h"

using namespace sc2;

void ZergCrush::OnStep() {
    const ObservationInterface *observation = Observation();
    const int framesToSkip = 4;

    if (observation->GetGameLoop() % framesToSkip != 0) {
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

void ZergCrush::ManageMacro() {
    auto observation = Observation();
    auto structures = buildOrder->structuresToBuild(observation);
    for (const auto &structure: structures) {
        switch ((UNIT_TYPEID) structure->getUnitTypeID()) {
            case UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
                TryBuildSupplyDepot();
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
            default:
                if (structure->isAddOn()) {
                    const Unit *baseStruct = structure->getBaseStruct(observation);
                    if (baseStruct) {
                        structure->setBuiltAddOn(TryBuildFrom(structure->getAbilityId(), baseStruct->tag));
                    }
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
        if (scoutSquadron->needMore() || scoutSquadron->getSquadron().empty())
            continue; // Only scout with completed squadrons
        for (const auto &unit: scoutSquadron->getSquadron()) ScoutWithUnit(observation, unit);
    }

    std::vector<ArmySquadron *> main = armyComposition->getSquadronsByType(allSquadrons, MAIN);
    Units enemiesNearBase = observation->GetUnits(Unit::Alliance::Enemy, CombinedFilter<IsVisible, WithinDistanceOf>(
            IsVisible(), WithinDistanceOf(baseRallyPoint, 30.0f))); // enemies at base

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
        ScoutWithUnits(observation, mainArmyUnits, ARMY_CLUSTER_DISTANCE);
    }
}

void ZergCrush::ScoutWithUnits(const sc2::ObservationInterface *observation, const sc2::Units &units,
                               float clusterDistance) {
    if (units.empty()) return;
    sc2::Units scoutingUnits = Units(units);
    int minClusterSize = 6;

    auto clusters = getClusters(units, clusterDistance); // unit clusters
    size_t largestClusterSize = minClusterSize;
    Point3D largestClusterPosition = baseRallyPoint;
    for (const auto& cluster : clusters) {
        if (cluster.second.size() > largestClusterSize) {
            largestClusterSize = cluster.second.size();
            largestClusterPosition = cluster.first;
        }
        auto clusterUnits = cluster.second;
        const Unit *clusterLeader = &(clusterUnits.front());

        sc2::Units attackableEnemies = observation->GetUnits(
                sc2::Unit::Alliance::Enemy,
                CombinedFilter<TargetableBy, WithinDistanceOf>(
                        TargetableBy(observation, clusterLeader),
                        WithinDistanceOf(clusterLeader, MicroInformation(observation, clusterLeader).range * 2))
        );
        if (!attackableEnemies.empty()) {
            for (const auto &unit: clusterUnits) {
                attackMicro->microUnit(observation, &unit);
                auto unitIter = std::find_if(scoutingUnits.begin(), scoutingUnits.end(), [unit](const auto& notInBattle) {
                    return unit.tag == notInBattle->tag;
                });
                if (unitIter != scoutingUnits.end()) scoutingUnits.erase(unitIter);
            }
        }
    }

    // Send small clusters towards main army...
    auto smallClusters = getClusters(scoutingUnits, clusterDistance, 1, minClusterSize);
    for (const auto& cluster : smallClusters) {
        for (const auto& unit: cluster.second) {
            Actions()->UnitCommand(&unit, ABILITY_ID::SMART, largestClusterPosition);
            auto unitIter = std::find_if(scoutingUnits.begin(), scoutingUnits.end(), [unit](const auto& smallClusterUnit) {
                return unit.tag == smallClusterUnit->tag;
            });
            if (unitIter != scoutingUnits.end()) scoutingUnits.erase(unitIter);
        }
    }

    auto scoutingUnitClusters = getClusters(scoutingUnits, clusterDistance, minClusterSize - 1); // Ignore small clusters
    if (scoutingUnitClusters.size() > 1) {
        clusterUnits(scoutingUnits, clusterDistance);
        return;
    }

    Actions()->UnitCommand(scoutingUnits, ABILITY_ID::SMART, enemyStartingLocation); // TODO: we need to find other bases
}


std::vector<std::pair<Point3D, std::vector<Unit>>> ZergCrush::getClusters(const Units& units,
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
    auto totalUnitCount = units.size();
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

void ZergCrush::ScoutWithUnit(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
    sc2::Units attackableEnemies = observation->GetUnits(sc2::Unit::Alliance::Enemy, TargetableBy(observation, unit));
    if (!unit->orders.empty()) return;

    if (!attackableEnemies.empty()) {
        attackMicro->microUnit(observation, unit);
        return;
    }

    Actions()->UnitCommand(unit, ABILITY_ID::SMART, enemyStartingLocation);
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
                                  bool isExpansion = false) {
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
bool ZergCrush::TryExpand(AbilityID build_ability, UnitTypeID worker_type) {
    const ObservationInterface *observation = Observation();
    float minimum_distance = std::numeric_limits<float>::max();
    Point3D closest_expansion;
    for (const auto &expansion: expansionLocations) {
        float current_distance = Distance2D(startingLocation, expansion);
        if (current_distance < .01f) {
            continue;
        }

        if (current_distance < minimum_distance) {
            if (Query()->Placement(build_ability, expansion)) {
                closest_expansion = expansion;
                minimum_distance = current_distance;
            }
        }
    }
    //only update staging location up till 3 bases.
    if (TryBuildStructure(build_ability, worker_type, closest_expansion, true) &&
        observation->GetUnits(Unit::Self, IsTownHall()).size() < 4) {
        // TODO: Update this rally point
        baseRallyPoint = Point3D(((baseRallyPoint.x + closest_expansion.x) / 2),
                                 ((baseRallyPoint.y + closest_expansion.y) / 2),
                                 ((baseRallyPoint.z + closest_expansion.z) / 2));
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
    enemyStartingLocation = Observation()->GetGameInfo().enemy_start_locations.front();
    expansionLocations = search::CalculateExpansionLocations(Observation(), Query());

    //Temporary, we can replace this with observation->GetStartLocation() once implemented
    startingLocation = Observation()->GetStartLocation();
    baseRallyPoint = startingLocation;

    auto observation = Observation();
    setEnemyRace(observation);

    attackMicro = new ZergCrushMicro(Actions());

    std::vector<BuildOrderStructure> tvtStructures = {
            BuildOrderStructure(observation, 14, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 23, UNIT_TYPEID::TERRAN_COMMANDCENTER),
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
            BuildOrderStructure(observation, 1, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 23, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 27, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 28, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 29, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB),
            BuildOrderStructure(observation, 35, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 35, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 44, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 44, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 47, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 47, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 53, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 57, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 59, UNIT_TYPEID::TERRAN_ENGINEERINGBAY),
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

    // TODO: BATALLION - combination of squadrons?
    std::vector<ArmySquadron *> tvzArmyComposition = {
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKS),        1, 4},
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
            break;
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
    buildOrder->onBuildingFinished(Observation(), unit);
}

void ZergCrush::OnUnitDestroyed(const sc2::Unit *unit) {
    buildOrder->OnUnitDestroyed(unit);
}

void ZergCrush::OnUpgradeCompleted(sc2::UpgradeID upgradeId) {
    attackMicro->onUpgradeComplete(upgradeId);
}

#include "zerg_crush.h"

using namespace sc2;

bool ZergCrush::TryBuildUnit(AbilityID abilityTypeForUnit, UnitTypeID buildingUnitType) {
    const ObservationInterface* observation = Observation();

    auto buildingUnits = observation->GetUnits(IsUnit(buildingUnitType));
    if (buildingUnits.empty()) return false;

    auto buildingUnit = GetRandomEntry(buildingUnits);

    if (!buildingUnit->orders.empty()) return false;
    if (buildingUnit->build_progress != 1) return false;

    Actions()->UnitCommand(buildingUnit, abilityTypeForUnit);
    return true;
}

// TODO: Copied from Multiplayer Bot
const Unit* ZergCrush::FindNearestMineralPatch(const Point2D& start) {
    Units units = Observation()->GetUnits(Unit::Alliance::Neutral);
    float distance = std::numeric_limits<float>::max();
    const Unit* target = nullptr;
    for (const auto& u : units) {
        if (u->unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD) {
            float d = DistanceSquared2D(u->pos, start);
            if (d < distance) {
                distance = d;
                target = u;
            }
        }
    }
    //If we never found one return false;
    if (distance == std::numeric_limits<float>::max()) {
        return target;
    }
    return target;
}

// TODO: Copied from MultiplayerBot Mine the nearest mineral to Town hall.
// If we don't do this, probes may mine from other patches if they stray too far from the base after building.
void ZergCrush::MineIdleWorkers(const Unit* worker, AbilityID worker_gather_command, UnitTypeID vespene_building_type) {
    const ObservationInterface* observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
    Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(vespene_building_type));

    const Unit* valid_mineral_patch = nullptr;

    if (bases.empty()) {
        return;
    }

    for (const auto& geyser : geysers) {
        if (geyser->assigned_harvesters < geyser->ideal_harvesters) {
            Actions()->UnitCommand(worker, worker_gather_command, geyser);
            return;
        }
    }
    //Search for a base that is missing workers.
    for (const auto& base : bases) {
        //If we have already mined out here skip the base.
        if (base->ideal_harvesters == 0 || base->build_progress != 1) {
            continue;
        }
        if (base->assigned_harvesters < base->ideal_harvesters) {
            valid_mineral_patch = FindNearestMineralPatch(base->pos);
            Actions()->UnitCommand(worker, worker_gather_command, valid_mineral_patch);
            return;
        }
    }

    if (!worker->orders.empty()) {
        return;
    }

    //If all workers are spots are filled just go to any base.
    const Unit* random_base = GetRandomEntry(bases);
    valid_mineral_patch = FindNearestMineralPatch(random_base->pos);
    Actions()->UnitCommand(worker, worker_gather_command, valid_mineral_patch);
}

// TODO: Copied from MultiplayerBot An estimate of how many workers we should have based on what buildings we have
int ZergCrush::GetExpectedWorkers(UNIT_TYPEID vespene_building_type) {
    const ObservationInterface* observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
    Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(vespene_building_type));
    int expected_workers = 0;
    for (const auto& base : bases) {
        if (base->build_progress != 1) {
            continue;
        }
        expected_workers += base->ideal_harvesters;
    }

    for (const auto& geyser : geysers) {
        if (geyser->vespene_contents > 0) {
            if (geyser->build_progress != 1) {
                continue;
            }
            expected_workers += geyser->ideal_harvesters;
        }
    }

    return expected_workers;
}

// TODO: Copied from MultiplayerBot To ensure that we do not over or under saturate any base.
void ZergCrush::ManageWorkers(UNIT_TYPEID worker_type, AbilityID worker_gather_command, UNIT_TYPEID vespene_building_type) {
    const ObservationInterface* observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
    Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(vespene_building_type));

    if (bases.empty()) {
        return;
    }

    for (const auto& base : bases) {
        //If we have already mined out or still building here skip the base.
        if (base->ideal_harvesters == 0 || base->build_progress != 1) {
            continue;
        }
        //if base is
        if (base->assigned_harvesters > base->ideal_harvesters) {
            Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(worker_type));

            for (const auto& worker : workers) {
                if (!worker->orders.empty()) {
                    if (worker->orders.front().target_unit_tag == base->tag) {
                        //This should allow them to be picked up by mineidleworkers()
                        MineIdleWorkers(worker, worker_gather_command,vespene_building_type);
                        return;
                    }
                }
            }
        }
    }
    Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(worker_type));
    for (const auto& geyser : geysers) {
        if (geyser->ideal_harvesters == 0 || geyser->build_progress != 1) {
            continue;
        }
        if (geyser->assigned_harvesters > geyser->ideal_harvesters) {
            for (const auto& worker : workers) {
                if (!worker->orders.empty()) {
                    if (worker->orders.front().target_unit_tag == geyser->tag) {
                        //This should allow them to be picked up by mineidleworkers()
                        MineIdleWorkers(worker, worker_gather_command, vespene_building_type);
                        return;
                    }
                }
            }
        }
        else if (geyser->assigned_harvesters < geyser->ideal_harvesters) {
            for (const auto& worker : workers) {
                if (!worker->orders.empty()) {
                    //This should move a worker that isn't mining gas to gas
                    const Unit* target = observation->GetUnit(worker->orders.front().target_unit_tag);
                    if (target == nullptr) {
                        continue;
                    }
                    if (target->unit_type != vespene_building_type) {
                        //This should allow them to be picked up by mineidleworkers()
                        MineIdleWorkers(worker, worker_gather_command, vespene_building_type);
                        return;
                    }
                }
            }
        }
    }
}

//TODO: Copied from MultiplayerBot Tries to build a geyser for a base
bool ZergCrush::TryBuildGas(AbilityID build_ability, UnitTypeID worker_type, Point2D base_location) {
    const ObservationInterface* observation = Observation();
    Units geysers = observation->GetUnits(Unit::Alliance::Neutral, IsGeyser());

    //only search within this radius
    float minimum_distance = 15.0f;
    Tag closestGeyser = 0;
    for (const auto& geyser : geysers) {
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

// TODO: Copied from MultiplayerBot Try build structure given a location. This is used most of the time
bool ZergCrush::TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Point2D location, bool isExpansion = false) {
    const ObservationInterface* observation = Observation();
    Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));

    //if we have no workers Don't build
    if (workers.empty()) return false;

    // Check to see if there is already a worker heading out to build it
    for (const auto& worker : workers) {
        for (const auto& order : worker->orders) {
            if (order.ability_id == ability_type_for_structure) {
                return false;
            }
        }
    }

    // If no worker is already building one, get a random worker to build one
    const Unit* unit = GetRandomEntry(workers);

    // Check to see if unit can make it there
    if (Query()->PathingDistance(unit, location) < 0.1f) {
        return false;
    }
    if (!isExpansion) {
        for (const auto& expansion : expansionLocations) {
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
    const ObservationInterface* observation = Observation();
    Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
    const Unit* target = observation->GetUnit(location_tag);

    if (workers.empty()) {
        return false;
    }

    // Check to see if there is already a worker heading out to build it
    for (const auto& worker : workers) {
        for (const auto& order : worker->orders) {
            if (order.ability_id == ability_type_for_structure) {
                return false;
            }
        }
    }

    // If no worker is already building one, get a random worker to build one
    const Unit* unit = GetRandomEntry(workers);

    // Check to see if unit can build there
    if (Query()->Placement(ability_type_for_structure, target->pos)) {
        Actions()->UnitCommand(unit, ability_type_for_structure, target);
        return true;
    }
    return false;

}

// TODO: Copied from MultiplayerBot Expands to nearest location and updates the start location to be between the new location and old bases.
bool ZergCrush::TryExpand(AbilityID build_ability, UnitTypeID worker_type) {
    const ObservationInterface* observation = Observation();
    float minimum_distance = std::numeric_limits<float>::max();
    Point3D closest_expansion;
    for (const auto& expansion : expansionLocations) {
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
    if (TryBuildStructure(build_ability, worker_type, closest_expansion, true) && observation->GetUnits(Unit::Self, IsTownHall()).size() < 4) {
        baseRallyPoint = Point3D(((baseRallyPoint.x + closest_expansion.x) / 2), ((baseRallyPoint.y + closest_expansion.y) / 2),
                                 ((baseRallyPoint.z + closest_expansion.z) / 2));
        return true;
    }
    return false;

}

void ZergCrush::ScoutWithUnit(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
    sc2::Units attackableEnemies = observation->GetUnits(sc2::Unit::Alliance::Enemy, TargetableBy(observation, unit));
    if (!unit->orders.empty()) return;
    Point2D targetPosition;

    if (!attackableEnemies.empty()) {
        attackMicro->microUnit(observation, unit);
        return;
    }

    Actions()->UnitCommand(unit, ABILITY_ID::SMART, enemyStartingLocation);
}

bool ZergCrush::TryBuildSCV() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

    // TODO IAN: Extract mule drop, not responsibility of this function
    //  ALSO, We can call down mules to another base, nearest to this given base may not be most efficient
    for (const auto &base: bases) {
        if (base->unit_type == UNIT_TYPEID::TERRAN_ORBITALCOMMAND && base->energy > 50) {
            if (FindNearestMineralPatch(base->pos)) Actions()->UnitCommand(base, ABILITY_ID::EFFECT_CALLDOWNMULE);
        }
    }

    // TODO IAN: If the game lasts this long
    if (observation->GetFoodWorkers() >= MAX_WORKER_COUNT) {
        return false;
    }

    if (observation->GetFoodUsed() >= observation->GetFoodCap()) {
        return false;
    }

    // TODO IAN: I don't like GetExpectedWorkers() - we may need more for scouting or not necessarily want full
    //  capacity on all refineries, this should maybe be based on build order more so, and then we don't have
    //  to spend time looping through structures... Also next check makes this irrelevant
    if (observation->GetFoodWorkers() > GetExpectedWorkers(UNIT_TYPEID::TERRAN_REFINERY)) {
        return false;
    }

    // TODO IAN: We can build SCVs from one base and assign them to a base with less than full
    for (const auto &base: bases) {
        //if there is a base with less than ideal workers
        if (base->assigned_harvesters < base->ideal_harvesters && base->build_progress == 1) {
            if (observation->GetMinerals() >= 50) {
                return TryBuildUnit(ABILITY_ID::TRAIN_SCV, base->unit_type);
            }
        }
    }
    return false;
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

void ZergCrush::RayCastWithUnit(const Unit* unit, const ObservationInterface &observation) {
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
    switch(pos.getMap(observation)) {
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
    int index = 1;

    sc2::Point2D startLocation = observation->GetStartLocation();
    float closest_distance = DistanceSquared2D(startLocation, map_postions[0][0]);
    //the most scuffed for loop ever
    for (const auto& pos : map_postions) {
        float test_distance = DistanceSquared2D(startLocation, pos[index]);
        if(test_distance < closest_distance) 
        {closest_ramp = index; closest_distance = test_distance;}
        ++index;

    }

    //set the build location based on structure type/current wall progress
    //supply depot = map_postions[0]
    //barrack 1 = map_postions[1]
    //barrack 2 = map_postions[2]
    if (piece == UNIT_TYPEID::TERRAN_BARRACKS) {
        //check if its the first or second barrack
        if (Query()->Placement(ABILITY_ID::BUILD_BARRACKS, map_postions[2][index])) {
            std::cout << "building barrack 2 at " << map_postions[2][index].x << std::endl;
            return TryBuildStructure(ABILITY_ID::BUILD_BARRACKS, UNIT_TYPEID::TERRAN_SCV, map_postions[2][closest_ramp]);
        }
        else {
            std::cout << "building barrack 1 at " << map_postions[1][index].x << std::endl;
            return TryBuildStructure(ABILITY_ID::BUILD_BARRACKS, UNIT_TYPEID::TERRAN_SCV, map_postions[1][closest_ramp]);
        }
    }
    //supply depot
    else {
        std::cout << "building depot " << map_postions[1][index].x << std::endl;
        return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT, UNIT_TYPEID::TERRAN_SCV, map_postions[0][closest_ramp]);
        }

}


void ZergCrush::ManageMacro() {
    auto observation = Observation();
    auto structures = buildOrder->structuresToBuild(observation);
    static int num = 1;
    for (const auto &structure: structures) {
        switch ((UNIT_TYPEID) structure->getUnitTypeID()) {
            case UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
                if(structure->isChainBuildLeader()) {
                    std::cout << "wall depot" << std::endl;
                    TryBuildWallPiece(UNIT_TYPEID::TERRAN_SUPPLYDEPOT);
                }
                else {
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
                const Unit* commandCenter = structure->getBaseStruct(observation);
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
                    const Unit* baseStruct = structure->getBaseStruct(observation);
                    if (baseStruct) {
                        structure->setBuiltAddOn(TryBuildAddOn(structure->getAbilityId(), baseStruct->tag));
                    }
                    break;
                }
                if(structure->getUnitTypeID() == UNIT_TYPEID::TERRAN_BARRACKS && structure->getChainBuild()) {
                    std::cout << "wall barrack 2" << std::endl;
                    TryBuildWallPiece(UNIT_TYPEID::TERRAN_BARRACKS);
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

// TODO: Copied from MultiplayerBot
bool ZergCrush::TryBuildAddOn(AbilityID ability_type_for_structure, Tag base_structure) {
    const Unit *unit = Observation()->GetUnit(base_structure);
    if (unit == nullptr) {
        return false;
    }
    if (unit->build_progress != 1) {
        return false;
    }
    if (unit->orders.empty()) {
        Actions()->UnitCommand(unit, ability_type_for_structure);
        return true;
    }
    return false;
}

bool ZergCrush::IsTooCloseToStructures(const Point2D& buildLocation, const Units& structures, float minDistance) {
    return std::any_of(structures.begin(), structures.end(), [&](const auto& structure) {
        return (structure->unit_type != UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED) &&
               (Distance2D(structure->pos, buildLocation) < minDistance);
    });
}

// Original function with random build location
// TODO: Copied from MultiplayerBot
bool ZergCrush::TryBuildStructureRandom(AbilityID abilityTypeForStructure, UnitTypeID unitType) {
    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    Point2D buildLocation = Point2D(startingLocation.x + rx * 15, startingLocation.y + ry * 15);

    Units units = Observation()->GetUnits(Unit::Self, IsStructure(Observation()));

    if (IsTooCloseToStructures(buildLocation, units, 6.0f)) {
        return false;
    }

    return TryBuildStructure(abilityTypeForStructure, unitType, buildLocation);
}

// New function taking a const Unit*
bool ZergCrush::TryBuildStructureRandomWithUnit(AbilityID abilityTypeForStructure, const Unit* unit) {
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


bool ZergCrush::TryBuildStructureUnit(AbilityID ability_type_for_structure, const Unit* unit, Point2D location, bool isExpansion = false) {
    // Check to see if unit can make it there
    if (Query()->PathingDistance(unit, location) < 0.1f) {
        return false;
    }
    if (!isExpansion) {
        for (const auto& expansion : expansionLocations) {
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
    const ObservationInterface *observation = Observation();

    Units army = observation->GetUnits(Unit::Alliance::Self, IsArmy(observation));
    uint32_t waitUntilSupply = 20; // TODO: Think of better plan

    auto allSquadrons = armyComposition->getAllSquadrons();

    std::vector<ArmySquadron *> scouts = armyComposition->getSquadronsByType(allSquadrons, SCOUT);
    for (auto &scoutSquadron: scouts) {
        if (scoutSquadron->needMore() || scoutSquadron->getSquadron().empty())
            continue; // Only scout with completed squadrons
        for (const auto &unit: scoutSquadron->getSquadron()) ScoutWithUnit(observation, unit);
    }

    std::vector<ArmySquadron *> main = armyComposition->getSquadronsByType(allSquadrons, MAIN);
    Units enemyUnits = observation->GetUnits(Unit::Alliance::Enemy);
    for (auto &mainSquadron: main) {
        for (const auto &unit: mainSquadron->getSquadron()) {
            if (!enemyUnits.empty()) {
                attackMicro->microUnit(observation, unit);
                continue;
            }

            if (waitUntilSupply >= observation->GetArmyCount()) {
                Actions()->UnitCommand(unit, ABILITY_ID::SMART, baseRallyPoint);
            } else {
                ScoutWithUnit(observation, unit);
            }
        }
    }
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

void ZergCrush::OnStep() {
    const ObservationInterface *observation = Observation();
    Units units = observation->GetUnits(Unit::Self, IsArmy(observation));
    Units nukes = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_NUKE));

    //Throttle some behavior that can wait to avoid duplicate orders.
    int frames_to_skip = observation->GetFoodUsed() >= observation->GetFoodCap() ? 6 : 4;

    // TODO IAN: This seems like it might hinder us in some micro situations... check if we can make
    //  this smarter
    if (observation->GetGameLoop() % frames_to_skip != 0) {
        return; // Only act every 4th frame if we are not capped and every 6th frame otherwise
    }

    ManageArmy();

    ManageMacro();

    ManageWorkers(UNIT_TYPEID::TERRAN_SCV, ABILITY_ID::HARVEST_GATHER, UNIT_TYPEID::TERRAN_REFINERY);

    ManageUpgrades();

    if (TryBuildSCV()) return;

    BuildArmy();

}

void ZergCrush::OnUnitIdle(const Unit *unit) {
    switch (unit->unit_type.ToType()) {
        case UNIT_TYPEID::TERRAN_SCV: {
            MineIdleWorkers(unit, ABILITY_ID::HARVEST_GATHER, UNIT_TYPEID::TERRAN_REFINERY);
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
            BuildOrderStructure(observation, UNIT_TYPEID::TERRAN_BARRACKS, true),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_BARRACKS, true),
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
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKS), 1, 4},
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSREACTOR), 1, 10},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MARAUDER, UNIT_TYPEID::TERRAN_BARRACKS, {
                    {IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB), 1, 8},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_HELLION, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORY), 1, 2},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_SIEGETANK, UNIT_TYPEID::TERRAN_FACTORY, {
                    {IsUnit(UNIT_TYPEID::TERRAN_FACTORYTECHLAB), 1, 5},
            }),
            new ArmySquadron(observation, UNIT_TYPEID::TERRAN_MEDIVAC, UNIT_TYPEID::TERRAN_STARPORT, {
                    {IsUnit(UNIT_TYPEID::TERRAN_STARPORT),        1, 2},
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

void ZergCrush::setEnemyRace(const ObservationInterface *observation) {
    auto playerId = observation->GetPlayerID();
    auto gameInfo = observation->GetGameInfo();
    enemyRace = std::find_if(gameInfo.player_info.begin(), gameInfo.player_info.end(), [playerId](auto playerInfo) {
        return playerInfo.player_id != playerId && playerInfo.player_type == Participant ||
               playerInfo.player_type == Computer;
    })->race_requested;
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

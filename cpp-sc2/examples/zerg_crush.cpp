#include "common/bot_examples.cc"

#include <algorithm>
#include <iterator>

#include "sc2api/sc2_unit_filters.h"
#include "sc2lib/sc2_lib.h"
#include "common/bot_examples.h"

#include <string>
#include <utility>


using namespace sc2;

void ZergCrush::ScoutWithUnit(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
    sc2::Units attackableEnemies = observation->GetUnits(sc2::Unit::Alliance::Enemy, TargetableBy(observation, unit));
    if (!unit->orders.empty()) return;
    Point2D targetPosition;

    if (!attackableEnemies.empty()) {
        attackMicro->microUnit(observation, unit);
        return;
    }

    if (FindEnemyPosition(targetPosition)) Actions()->UnitCommand(unit, ABILITY_ID::SMART, targetPosition);
    else if (TryFindRandomPathableLocation(unit, targetPosition))
        Actions()->UnitCommand(unit, ABILITY_ID::SMART, targetPosition);
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
    if (observation->GetFoodWorkers() >= max_worker_count_) {
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

bool ZergCrush::TryBuildSupplyDepot() {
    const ObservationInterface *observation = Observation();

    if (observation->GetMinerals() < 100) return false;

    // check to see if there is already one building
    Units units = observation->GetUnits(Unit::Alliance::Self, IsUnits(supplyDepotTypes));
    if (observation->GetFoodUsed() < 40) { // TODO IAN: Same comment as above, may only want to check optionally
        //    not the responsibility of this function
        for (const auto &unit: units) {
            if (unit->build_progress != 1) return false;
        }
    }

    // Try and build a supply depot. Find a random SCV and give it the order.
    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    Point2D build_location = Point2D(startLocation_.x + rx * 15, startLocation_.y + ry * 15);
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
bool ZergCrush::TryBuildStructureRandom(AbilityID abilityTypeForStructure, UnitTypeID unitType) {
    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    Point2D buildLocation = Point2D(startLocation_.x + rx * 15, startLocation_.y + ry * 15);

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
        for (const auto& expansion : expansions_) {
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
                Actions()->UnitCommand(unit, ABILITY_ID::SMART, staging_location_);
            } else {
                ScoutWithUnit(observation, unit);
            }
        }
    }
}

bool ZergCrush::BuildRefinery() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

    if (CountUnitType(observation, UNIT_TYPEID::TERRAN_REFINERY) >= bases.size() * 2) {
        return false;
    }

    for (const auto &base: bases) {
        TryBuildGas(ABILITY_ID::BUILD_REFINERY, UNIT_TYPEID::TERRAN_SCV, base->pos);
    }
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
    MultiplayerBot::OnGameStart();

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
    enemyRace = std::find_if(game_info_.player_info.begin(), game_info_.player_info.end(), [playerId](auto playerInfo) {
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

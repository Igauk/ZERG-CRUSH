#include "common/bot_examples.cc"

#include <algorithm>
#include <iterator>

#include "sc2api/sc2_unit_filters.h"
#include "sc2lib/sc2_lib.h"
#include "common/bot_examples.h"

#include <string>
#include <optional>
#include <utility>


using namespace sc2;

bool ZergCrush::TryBuildSCV() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

    // TODO IAN: Extract mule drop, not responsibility of this function
    //  ALSO, We can call down mules to another base, nearest to this given base may not be most efficient
    for (const auto &base: bases) {
        if (base->unit_type == UNIT_TYPEID::TERRAN_ORBITALCOMMAND && base->energy > 50) {
            if (FindNearestMineralPatch(base->pos)) {
                Actions()->UnitCommand(base, ABILITY_ID::EFFECT_CALLDOWNMULE);
            }
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

bool ZergCrush::UnitTryBuildStructure(AbilityID ability_type_for_structure, const Unit *unit, Point2D location,
                                      bool isExpansion = false) {
    if (!isExpansion) {
        for (const auto &expansion: expansions_) {
            if (Distance2D(location, Point2D(expansion.x, expansion.y)) < 7) {
                // Cannot build where we might expand TODO IAN: some of these expansion locations could be
                //                                      for the enemy... we may not want to make this check
                return false;
            }
        }
    }

    // Check to see if unit can build there
    if (Query()->Placement(ability_type_for_structure, location)) {
        Actions()->UnitCommand(unit, ability_type_for_structure, location); // Queue build
        return true;
    }
    return false;
}

bool ZergCrush::TryBuildSupplyDepot() {
    // TODO IAN: replace this with a more specific function that builds at a given location
    const ObservationInterface *observation = Observation();

    // If we are not supply capped, don't build a supply depot. TODO IAN: We should check this outside of this method
    //                                                              or only optionally, since we may want to build anyways
    if (observation->GetFoodUsed() < observation->GetFoodCap() - 6) {
        return false;
    }

    if (observation->GetMinerals() < 100) {
        return false;
    }

    //check to see if there is already one building
    Units units = observation->GetUnits(Unit::Alliance::Self, IsUnits(supply_depot_types));
    if (observation->GetFoodUsed() < 40) { // TODO IAN: Same comment as above, may only want to check optionally
        //    not the responsibility of this function
        for (const auto &unit: units) {
            if (unit->build_progress != 1) {
                return false;
            }
        }
    }

    // Try and build a supply depot. Find a random SCV and give it the order.
    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    Point2D build_location = Point2D(staging_location_.x + rx * 15, staging_location_.y + ry * 15);
    return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT, UNIT_TYPEID::TERRAN_SCV, build_location);
}

void ZergCrush::BuildArmy() {
    const ObservationInterface *observation = Observation();
    // Grab army and building counts
    Units barracks = observation->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_BARRACKS));
    Units factorys = observation->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_FACTORY));
    Units starports = observation->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_STARPORT));

    size_t widowmine_count = CountUnitTypeTotal(observation, widow_mine_types, UNIT_TYPEID::TERRAN_FACTORY,
                                                ABILITY_ID::TRAIN_WIDOWMINE);
    size_t siege_tank_count = CountUnitTypeTotal(observation, siege_tank_types, UNIT_TYPEID::TERRAN_FACTORY,
                                                 ABILITY_ID::TRAIN_SIEGETANK);
    size_t marine_count = CountUnitTypeTotal(observation, UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_BARRACKS,
                                             ABILITY_ID::TRAIN_MARINE);
    size_t marauder_count = CountUnitTypeTotal(observation, UNIT_TYPEID::TERRAN_MARAUDER,
                                               UNIT_TYPEID::TERRAN_BARRACKS, ABILITY_ID::TRAIN_MARAUDER);
    size_t reaper_count = CountUnitTypeTotal(observation, UNIT_TYPEID::TERRAN_REAPER, UNIT_TYPEID::TERRAN_BARRACKS,
                                             ABILITY_ID::TRAIN_REAPER);
    size_t ghost_count = CountUnitTypeTotal(observation, UNIT_TYPEID::TERRAN_GHOST, UNIT_TYPEID::TERRAN_BARRACKS,
                                            ABILITY_ID::TRAIN_GHOST);
    size_t medivac_count = CountUnitTypeTotal(observation, UNIT_TYPEID::TERRAN_MEDIVAC,
                                              UNIT_TYPEID::TERRAN_STARPORT, ABILITY_ID::TRAIN_MEDIVAC);
    size_t raven_count = CountUnitTypeTotal(observation, UNIT_TYPEID::TERRAN_RAVEN, UNIT_TYPEID::TERRAN_STARPORT,
                                            ABILITY_ID::TRAIN_RAVEN);
    size_t battlecruiser_count = CountUnitTypeTotal(observation, UNIT_TYPEID::TERRAN_MEDIVAC,
                                                    UNIT_TYPEID::TERRAN_STARPORT, ABILITY_ID::TRAIN_BATTLECRUISER);
    size_t banshee_count = CountUnitTypeTotal(observation, UNIT_TYPEID::TERRAN_MEDIVAC,
                                              UNIT_TYPEID::TERRAN_STARPORT, ABILITY_ID::TRAIN_BANSHEE);

    if (CountUnitType(observation, UNIT_TYPEID::TERRAN_GHOSTACADEMY) +
        CountUnitType(observation, UNIT_TYPEID::TERRAN_FACTORY) > 0) {
        if (!nukeBuilt) {
            Units ghosts = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_GHOST));
            if (observation->GetMinerals() > 100 && observation->GetVespene() > 100) {
                TryBuildUnit(ABILITY_ID::BUILD_NUKE, UNIT_TYPEID::TERRAN_GHOSTACADEMY);
            }
            if (!ghosts.empty()) {
                AvailableAbilities abilities = Query()->GetAbilitiesForUnit(ghosts.front());
                for (const auto &ability: abilities.abilities) {
                    if (ability.ability_id == ABILITY_ID::EFFECT_NUKECALLDOWN) {
                        nukeBuilt = true;
                    }
                }
            }

        }
    }


    if (!starports.empty()) {
        for (const auto &starport: starports) {
            if (observation->GetUnit(starport->add_on_tag) == nullptr) {
                if (starport->orders.empty() && medivac_count < 5) {
                    Actions()->UnitCommand(starport, ABILITY_ID::TRAIN_MEDIVAC);
                }
                continue;
            } else {
                if (starport->orders.empty() && raven_count < 2) {
                    Actions()->UnitCommand(starport, ABILITY_ID::TRAIN_RAVEN);
                }
                if (CountUnitType(observation, UNIT_TYPEID::TERRAN_FUSIONCORE) > 0) {
                    if (starport->orders.empty() && battlecruiser_count < 2) {
                        Actions()->UnitCommand(starport, ABILITY_ID::TRAIN_BATTLECRUISER);
                        if (battlecruiser_count < 1) {
                            return;
                        }
                    }
                }
                if (starport->orders.empty() && banshee_count < 2) {
                    Actions()->UnitCommand(starport, ABILITY_ID::TRAIN_BANSHEE);
                }
            }
        }
    }

    if (!barracks.empty()) {
        for (const auto &barrack: barracks) {
            if (observation->GetUnit(barrack->add_on_tag) == nullptr) {
                continue;
            }
            if (observation->GetUnit(barrack->add_on_tag)->unit_type == UNIT_TYPEID::TERRAN_BARRACKSREACTOR) {
                if (barrack->orders.size() < 2 && marine_count < 20) {
                    Actions()->UnitCommand(barrack, ABILITY_ID::TRAIN_MARINE);
                } else if (observation->GetMinerals() > 1000 && observation->GetVespene() < 300) {
                    Actions()->UnitCommand(barrack, ABILITY_ID::TRAIN_MARINE);
                }
            } else {
                if (barrack->orders.empty()) {
                    if (reaper_count < 2) {
                        Actions()->UnitCommand(barrack, ABILITY_ID::TRAIN_REAPER);
                    } else if (ghost_count < 4) {
                        Actions()->UnitCommand(barrack, ABILITY_ID::TRAIN_GHOST);
                    } else if (marauder_count < 10) {
                        Actions()->UnitCommand(barrack, ABILITY_ID::TRAIN_MARAUDER);
                    } else {
                        Actions()->UnitCommand(barrack, ABILITY_ID::TRAIN_MARINE);
                    }
                }
            }
        }
    }

    if (!factorys.empty()) {
        for (const auto &factory: factorys) {
            if (observation->GetUnit(factory->add_on_tag) == nullptr) {
                continue;
            }
            if (observation->GetUnit(factory->add_on_tag)->unit_type == UNIT_TYPEID::TERRAN_FACTORYREACTOR) {
                if (factory->orders.size() < 2 && widowmine_count < 4) {
                    Actions()->UnitCommand(factory, ABILITY_ID::TRAIN_WIDOWMINE);
                }
            } else {
                if (factory->orders.empty() && siege_tank_count < 7) {
                    Actions()->UnitCommand(factory, ABILITY_ID::TRAIN_SIEGETANK);
                }
            }
        }
    }
}

void ZergCrush::BuildOrderTest() {
    auto observation = Observation();
    auto nextStruct = tvtBuildOrder.nextStructureToBuild(observation);
    if (nextStruct.has_value()) {
        auto structure = nextStruct.value();
        switch ((UNIT_TYPEID) structure->getUnitTypeID()) {
            case UNIT_TYPEID::TERRAN_SUPPLYDEPOT:
                TryBuildSupplyDepot();
                break;
            case UNIT_TYPEID::TERRAN_REFINERY:
                BuildRefinery();
                break;
            case UNIT_TYPEID::TERRAN_COMMANDCENTER:
                TryBuildExpansionCom();
                break;
            case UNIT_TYPEID::TERRAN_ORBITALCOMMAND:
                Actions()->UnitCommand(observation->GetUnit(structure->getBaseStruct(observation).value()), ABILITY_ID::MORPH_ORBITALCOMMAND);
                structure->built = true;
                break;
            default:
                if (structure->isAddOn()) {
                    std::optional<Tag> baseStruct = structure->getBaseStruct(observation);
                    if (baseStruct.has_value()) {
                        // TODO: cannot track building of addons directly it seems...
                        structure->built = TryBuildAddOn(structure->getAbilityId(), baseStruct.value());
                    }
                    break;
                }
                TryBuildStructureRandom(structure->getAbilityId(), UNIT_TYPEID::TERRAN_SCV);
        }
    }
}

void ZergCrush::ManageMacro() {
    // Until the first SCV has been built do nothing
    if (!firstAdditionalSCV) { return; }

    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Self, IsTownHall());
    Units barracks = observation->GetUnits(Unit::Self, IsUnits(barrack_types));
    Units factories = observation->GetUnits(Unit::Self, IsUnits(factory_types));
    Units starports = observation->GetUnits(Unit::Self, IsUnits(starport_types));
    Units barracks_tech = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB));
    Units factoriesTech = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_FACTORYTECHLAB));
    Units starports_tech = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_STARPORTTECHLAB));

    Units supply_depots = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOT));

    // If we have less than three bases and we have a fusion core (which can build battlecruisers) then TRY to build another command center
    if (bases.size() < 3 && CountUnitType(observation, UNIT_TYPEID::TERRAN_FUSIONCORE)) {
        TryBuildExpansionCom(); // TODO IAN: look into this
        return;
    }

    // Lower all supply depots (don't block movement)
    // TODO IAN: Look into using supply depots for walling - kinda OP against rushes
    for (const auto &supply_depot: supply_depots) {
        Actions()->UnitCommand(supply_depot, ABILITY_ID::MORPH_SUPPLYDEPOT_LOWER);
    }

    if (!barracks.empty()) {
        for (const auto &base: bases) {
            if (base->unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTER && observation->GetMinerals() > 150) {
                Actions()->UnitCommand(base, ABILITY_ID::MORPH_ORBITALCOMMAND);
            }
        }
    }

    for (const auto &barrack: barracks) {
        if (!barrack->orders.empty() || barrack->build_progress != 1) {
            continue;
        }
        if (observation->GetUnit(barrack->add_on_tag) == nullptr) {
            if (barracks_tech.size() < barracks.size() / 2 || barracks_tech.empty()) {
                TryBuildAddOn(ABILITY_ID::BUILD_TECHLAB_BARRACKS, barrack->tag);
            } else {
                TryBuildAddOn(ABILITY_ID::BUILD_REACTOR_BARRACKS, barrack->tag);
            }
        }
    }

    for (const auto &factory: factories) {
        if (!factory->orders.empty()) {
            continue;
        }

        if (observation->GetUnit(factory->add_on_tag) == nullptr) {
            if (CountUnitType(observation, UNIT_TYPEID::TERRAN_BARRACKSREACTOR) < 1) {
                TryBuildAddOn(ABILITY_ID::BUILD_REACTOR_FACTORY, factory->tag);
            } else {
                TryBuildAddOn(ABILITY_ID::BUILD_TECHLAB_FACTORY, factory->tag);
            }
        }
    }

    for (const auto &starport: starports) {
        if (!starport->orders.empty()) {
            continue;
        }
        if (observation->GetUnit(starport->add_on_tag) == nullptr) {
            if (CountUnitType(observation, UNIT_TYPEID::TERRAN_STARPORTREACTOR) < 1) {
                TryBuildAddOn(ABILITY_ID::BUILD_REACTOR_STARPORT, starport->tag);
            } else {
                TryBuildAddOn(ABILITY_ID::BUILD_TECHLAB_STARPORT, starport->tag);
            }
        }
    }

    size_t barracks_count_target = std::min<size_t>(3 * bases.size(),
                                                    8); // We want 3 barracks per base up to a max of 8
    size_t armory_count_target = 1;
    size_t starportCountTarget = std::min<size_t>(1 * bases.size(), 4);
    size_t factoriesCountTarget = std::min<size_t>(2 * bases.size(), 7);

    // Then starports
    if (!factories.empty() && starports.size() < starportCountTarget) {
        if (observation->GetMinerals() > 150 && observation->GetVespene() > 100) {
            TryBuildStructureRandom(ABILITY_ID::BUILD_STARPORT, UNIT_TYPEID::TERRAN_SCV);
        }
    }

    // Then factories
    if (!barracks.empty() && factories.size() < factoriesCountTarget) {
        if (observation->GetMinerals() > 150 && observation->GetVespene() > 100) {
            TryBuildStructureRandom(ABILITY_ID::BUILD_FACTORY, UNIT_TYPEID::TERRAN_SCV);
        }
    }


    // Barracks first
    if (barracks.size() < barracks_count_target) {
        if (observation->GetFoodWorkers() >= target_worker_count_) {
            TryBuildStructureRandom(ABILITY_ID::BUILD_BARRACKS, UNIT_TYPEID::TERRAN_SCV);
        }
    }

    if (CountUnitType(observation, UNIT_TYPEID::TERRAN_ENGINEERINGBAY) < 2) {
        if (observation->GetMinerals() > 150 &&
            observation->GetVespene() > 100) { // TODO IAN: don't need vespene to build this - why this limit
            //    is this to "set a time" when we build?
            TryBuildStructureRandom(ABILITY_ID::BUILD_ENGINEERINGBAY, UNIT_TYPEID::TERRAN_SCV);
        }
    }
    if (!barracks.empty() && CountUnitType(observation, UNIT_TYPEID::TERRAN_GHOSTACADEMY) < 1) {
        if (observation->GetMinerals() > 150 && observation->GetVespene() > 50) {
            TryBuildStructureRandom(ABILITY_ID::BUILD_GHOSTACADEMY, UNIT_TYPEID::TERRAN_SCV);
        }
    }
    if (!factories.empty() && CountUnitType(observation, UNIT_TYPEID::TERRAN_FUSIONCORE) < 1) {
        if (observation->GetMinerals() > 150 && observation->GetVespene() > 150) {
            TryBuildStructureRandom(ABILITY_ID::BUILD_FUSIONCORE, UNIT_TYPEID::TERRAN_SCV);
        }
    }

    if (!barracks.empty() && CountUnitType(observation, UNIT_TYPEID::TERRAN_ARMORY) < armory_count_target) {
        if (observation->GetMinerals() > 150 && observation->GetVespene() > 100) {
            TryBuildStructureRandom(ABILITY_ID::BUILD_ARMORY, UNIT_TYPEID::TERRAN_SCV);
        }
    }
}

bool ZergCrush::TryBuildAddOn(AbilityID ability_type_for_structure, Tag base_structure) {
    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    const Unit *unit = Observation()->GetUnit(base_structure);

    if (unit->build_progress != 1) {
        return false;
    }

    Point2D build_location = Point2D(unit->pos.x + rx * 15, unit->pos.y + ry * 15);

    Units units = Observation()->GetUnits(Unit::Self, IsStructure(Observation()));

    if (Query()->Placement(ability_type_for_structure, unit->pos, unit)) {
        Actions()->UnitCommand(unit, ability_type_for_structure);
        return true;
    }

    float distance = std::numeric_limits<float>::max();
    for (const auto &u: units) {
        float d = Distance2D(u->pos, build_location);
        if (d < distance) {
            distance = d;
        }
    }
    if (distance < 6) {
        return false;
    }

    if (Query()->Placement(ability_type_for_structure, build_location, unit)) {
        Actions()->UnitCommand(unit, ability_type_for_structure, build_location);
        return true;
    }
    return false;

}

bool ZergCrush::TryBuildStructureRandom(AbilityID ability_type_for_structure, UnitTypeID unit_type) {
    float rx = GetRandomScalar();
    float ry = GetRandomScalar();
    Point2D build_location = Point2D(staging_location_.x + rx * 15, staging_location_.y + ry * 15);

    Units units = Observation()->GetUnits(Unit::Self, IsStructure(Observation()));
    float distance = std::numeric_limits<float>::max();
    for (const auto &u: units) {
        if (u->unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED) {
            continue;
        }
        float d = Distance2D(u->pos, build_location);
        if (d < distance) {
            distance = d;
        }
    }
    if (distance < 6) {
        return false;
    }
    return TryBuildStructure(ability_type_for_structure, unit_type, build_location);
}

void ZergCrush::ManageUpgrades() {
    const ObservationInterface *observation = Observation();
    auto upgrades = observation->GetUpgrades();
    size_t base_count = observation->GetUnits(Unit::Alliance::Self, IsTownHall()).size();


    if (upgrades.empty()) {
        TryBuildUnit(ABILITY_ID::RESEARCH_STIMPACK, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB);
    } else {
        for (const auto &upgrade: upgrades) {
            if (CountUnitType(observation, UNIT_TYPEID::TERRAN_ARMORY) > 0) {
                if (upgrade == UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL1 && base_count > 2) {
                    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONS,
                                 UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
                } else if (upgrade == UPGRADE_ID::TERRANINFANTRYARMORSLEVEL1 && base_count > 2) {
                    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYARMOR, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
                }
                if (upgrade == UPGRADE_ID::TERRANINFANTRYWEAPONSLEVEL2 && base_count > 4) {
                    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONS,
                                 UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
                } else if (upgrade == UPGRADE_ID::TERRANINFANTRYARMORSLEVEL2 && base_count > 4) {
                    TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYARMOR, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
                }
            }
            // TODO: Reorder this in terms of what we want to do
            TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYWEAPONS, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
            TryBuildUnit(ABILITY_ID::RESEARCH_TERRANINFANTRYARMOR, UNIT_TYPEID::TERRAN_ENGINEERINGBAY);
            TryBuildUnit(ABILITY_ID::RESEARCH_COMBATSHIELD, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB);
            TryBuildUnit(ABILITY_ID::RESEARCH_CONCUSSIVESHELLS, UNIT_TYPEID::TERRAN_BARRACKSTECHLAB);
            TryBuildUnit(ABILITY_ID::RESEARCH_PERSONALCLOAKING, UNIT_TYPEID::TERRAN_GHOSTACADEMY);
            TryBuildUnit(ABILITY_ID::RESEARCH_BANSHEECLOAKINGFIELD, UNIT_TYPEID::TERRAN_STARPORTTECHLAB);
        }
    }
}

void ZergCrush::ManageArmy() {
    const ObservationInterface *observation = Observation();

    Units enemy_units = observation->GetUnits(Unit::Alliance::Enemy);
    Units army = observation->GetUnits(Unit::Alliance::Self, IsArmy(observation));
    Units nuke = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_NUKE));

    int waitUntilSupply = 100;

    for (const auto &unit: army) {
        if (enemy_units.empty() && observation->GetFoodArmy() < waitUntilSupply) {
            switch (unit->unit_type.ToType()) {
                case UNIT_TYPEID::TERRAN_SIEGETANKSIEGED: {
                    Actions()->UnitCommand(unit, ABILITY_ID::MORPH_UNSIEGE);
                    break;
                }
                default:
                    RetreatWithUnit(unit, staging_location_);
                    break;
            }
        } else if (!enemy_units.empty()) {
            switch (unit->unit_type.ToType()) {
                case UNIT_TYPEID::TERRAN_WIDOWMINE: {
                    float distance = GetClosestEnemyUnitDistance(enemy_units, unit);
                    if (distance < 6) {
                        Actions()->UnitCommand(unit, ABILITY_ID::BURROWDOWN);
                    }
                    break;
                }
                case UNIT_TYPEID::TERRAN_MARINE: {
                    if (stimResearched && !unit->orders.empty()) {
                        if (unit->orders.front().ability_id == ABILITY_ID::ATTACK) {
                            float distance = GetClosestEnemyUnitDistance(enemy_units, unit);
                            bool has_stimmed = false;
                            for (const auto &buff: unit->buffs) {
                                if (buff == BUFF_ID::STIMPACK) {
                                    has_stimmed = true;
                                }
                            }
                            if (distance < 6 && !has_stimmed) {
                                Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_STIM);
                                break;
                            }
                        }

                    }
                    AttackWithUnit(unit, observation);
                    break;
                }
                case UNIT_TYPEID::TERRAN_MARAUDER: {
                    if (stimResearched && !unit->orders.empty()) {
                        if (unit->orders.front().ability_id == ABILITY_ID::ATTACK) {
                            float distance = GetClosestEnemyUnitDistance(enemy_units, unit);
                            bool has_stimmed = false;
                            for (const auto &buff: unit->buffs) {
                                if (buff == BUFF_ID::STIMPACK) {
                                    has_stimmed = true;
                                }
                            }
                            if (distance < 7 && !has_stimmed) {
                                Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_STIM);
                                break;
                            }
                        }
                    }
                    AttackWithUnit(unit, observation);
                    break;
                }
                case UNIT_TYPEID::TERRAN_GHOST: {
                    float distance = std::numeric_limits<float>::max();
                    const Unit *closest_unit;
                    for (const auto &u: enemy_units) {
                        float d = Distance2D(u->pos, unit->pos);
                        if (d < distance) {
                            distance = d;
                            closest_unit = u;
                        }
                    }
                    if (ghostCloakResearched) {
                        if (distance < 7 && unit->energy > 50) {
                            Actions()->UnitCommand(unit, ABILITY_ID::BEHAVIOR_CLOAKON);
                            break;
                        }
                    }
                    if (nukeBuilt) {
                        Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_NUKECALLDOWN, closest_unit->pos);
                    } else if (unit->energy > 50 && !unit->orders.empty()) {
                        if (unit->orders.front().ability_id == ABILITY_ID::ATTACK)
                            Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_GHOSTSNIPE, unit);
                        break;
                    }
                    AttackWithUnit(unit, observation);
                    break;
                }
                case UNIT_TYPEID::TERRAN_SIEGETANK: {
                    float distance = GetClosestEnemyUnitDistance(enemy_units, unit);
                    if (distance < 11) {
                        Actions()->UnitCommand(unit, ABILITY_ID::MORPH_SIEGEMODE);
                    } else {
                        AttackWithUnit(unit, observation);
                    }
                    break;
                }
                case UNIT_TYPEID::TERRAN_SIEGETANKSIEGED: {
                    float distance = GetClosestEnemyUnitDistance(enemy_units, unit);
                    if (distance > 13) {
                        Actions()->UnitCommand(unit, ABILITY_ID::MORPH_UNSIEGE);
                    } else {
                        AttackWithUnit(unit, observation);
                    }
                    break;
                }
                case UNIT_TYPEID::TERRAN_MEDIVAC: {
                    Units bio_units = observation->GetUnits(Unit::Self, IsUnits(bio_types));
                    if (unit->orders.empty()) {
                        for (const auto &bio_unit: bio_units) {
                            if (bio_unit->health < bio_unit->health_max) {
                                Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_HEAL, bio_unit);
                                break;
                            }
                        }
                        if (!bio_units.empty()) {
                            Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, bio_units.front());
                        }
                    }
                    break;
                }
                case UNIT_TYPEID::TERRAN_VIKINGFIGHTER: {
                    Units flying_units = observation->GetUnits(Unit::Enemy, IsFlying());
                    if (flying_units.empty()) {
                        Actions()->UnitCommand(unit, ABILITY_ID::MORPH_VIKINGASSAULTMODE);
                    } else {
                        Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, flying_units.front());
                    }
                    break;
                }
                case UNIT_TYPEID::TERRAN_VIKINGASSAULT: {
                    Units flying_units = observation->GetUnits(Unit::Enemy, IsFlying());
                    if (!flying_units.empty()) {
                        Actions()->UnitCommand(unit, ABILITY_ID::MORPH_VIKINGFIGHTERMODE);
                    } else {
                        AttackWithUnit(unit, observation);
                    }
                    break;
                }
                case UNIT_TYPEID::TERRAN_CYCLONE: {
                    Units flying_units = observation->GetUnits(Unit::Enemy, IsFlying());
                    if (!flying_units.empty() && unit->orders.empty()) {
                        Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_LOCKON, flying_units.front());
                    } else if (!flying_units.empty() && !unit->orders.empty()) {
                        if (unit->orders.front().ability_id != ABILITY_ID::EFFECT_LOCKON) {
                            Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_LOCKON, flying_units.front());
                        }
                    } else {
                        AttackWithUnit(unit, observation);
                    }
                    break;
                }
                case UNIT_TYPEID::TERRAN_HELLION: {
                    if (CountUnitType(observation, UNIT_TYPEID::TERRAN_ARMORY) > 0) {
                        Actions()->UnitCommand(unit, ABILITY_ID::MORPH_HELLBAT);
                    }
                    AttackWithUnit(unit, observation);
                    break;
                }
                case UNIT_TYPEID::TERRAN_BANSHEE: {
                    if (bansheeCloakResearched) {
                        float distance = GetClosestEnemyUnitDistance(enemy_units, unit);
                        if (distance < 7 && unit->energy > 50) {
                            Actions()->UnitCommand(unit, ABILITY_ID::BEHAVIOR_CLOAKON);
                        }
                    }
                    AttackWithUnit(unit, observation);
                    break;
                }
                case UNIT_TYPEID::TERRAN_RAVEN: {
                    if (unit->energy > 125) {
                        Actions()->UnitCommand(unit, ABILITY_ID::EFFECT_HUNTERSEEKERMISSILE, enemy_units.front());
                        break;
                    }
                    if (unit->orders.empty()) {
                        Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, army.front()->pos);
                    }
                    break;
                }
                default: {
                    AttackWithUnit(unit, observation);
                }
            }
        } else { // There may or may not be enemies, but we are at less than `waitUntilSupply`
            switch (unit->unit_type.ToType()) {
                case UNIT_TYPEID::TERRAN_SIEGETANKSIEGED: {
                    Actions()->UnitCommand(unit, ABILITY_ID::MORPH_UNSIEGE);
                    break;
                }
                case UNIT_TYPEID::TERRAN_MEDIVAC: {
                    Units bio_units = observation->GetUnits(Unit::Self, IsUnits(bio_types));
                    if (unit->orders.empty()) {
                        Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, bio_units.front()->pos);
                    }
                    break;
                }
                default:
                    ScoutWithUnit(unit, observation);
                    break;
            }
        }
    }
}

float ZergCrush::GetClosestEnemyUnitDistance(Units &enemyUnit, const Unit *const &unit) const {
    float distance = std::numeric_limits<float>::max();
    for (const auto &u: enemyUnit) {
        float d = Distance2D(u->pos, unit->pos);
        if (d < distance) {
            distance = d;
        }
    }
    return distance;
}

bool ZergCrush::TryBuildExpansionCom() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
    //Don't have more active bases than we can provide workers for
    if (GetExpectedWorkers(UNIT_TYPEID::TERRAN_REFINERY) > max_worker_count_) {
        return false;
    }
    // If we have extra workers around, try and build another command center
    if (GetExpectedWorkers(UNIT_TYPEID::TERRAN_REFINERY) < observation->GetFoodWorkers() - 10) {
        return TryExpand(ABILITY_ID::BUILD_COMMANDCENTER, UNIT_TYPEID::TERRAN_SCV);
    }
    //Only build another Hatch if we are floating extra minerals
    if (observation->GetMinerals() > std::min<size_t>(bases.size() * 400, 1200)) {
        return TryExpand(ABILITY_ID::BUILD_COMMANDCENTER, UNIT_TYPEID::TERRAN_SCV);
    }
    return false;
}

bool ZergCrush::BuildRefinery() {
    const ObservationInterface *observation = Observation();
    Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

    if (CountUnitType(observation, UNIT_TYPEID::TERRAN_REFINERY) >=
        observation->GetUnits(Unit::Alliance::Self, IsTownHall()).size() * 2) {
        return false;
    }

    for (const auto &base: bases) {
        if (base->assigned_harvesters >= base->ideal_harvesters) {
            if (base->build_progress == 1) {
                if (TryBuildGas(ABILITY_ID::BUILD_REFINERY, UNIT_TYPEID::TERRAN_SCV, base->pos)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void ZergCrush::OnStep() {
    const ObservationInterface *observation = Observation();
    Units units = observation->GetUnits(Unit::Self, IsArmy(observation));
    Units nukes = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::TERRAN_NUKE));
    updateBuildOrder(observation);

    //Throttle some behavior that can wait to avoid duplicate orders.
    int frames_to_skip = observation->GetFoodUsed() >= observation->GetFoodCap() ? 6 : 4;

    // TODO IAN: This seems like it might hinder us in some micro situations... check if we can make
    //  this smarter
    if (observation->GetGameLoop() % frames_to_skip != 0) {
        return; // Only act every 4th frame if we are not capped and every 6th frame otherwise
    }

    if (!nuke_detected && nukes.empty()) {
        ManageArmy(); // If we have not been nuked and we don't have nukes ourselves
    } else {
        if (nuke_detected_frame + 400 < observation->GetGameLoop()) {
            nuke_detected = false; // We assume at this point that we fully retreated away from the nuke
        }
        for (const auto &unit: units) {
            // TODO IAN: If we are nuked, can we tell the nuke direction? Is there cases where retreating to
            //  our start location could be dumb?
            RetreatWithUnit(unit, startLocation_);
        }
    }

//    ManageMacro();
    BuildOrderTest();

    ManageWorkers(UNIT_TYPEID::TERRAN_SCV, ABILITY_ID::HARVEST_GATHER, UNIT_TYPEID::TERRAN_REFINERY);

    ManageUpgrades();

    if (TryBuildSCV()) return;

//    if (TryBuildSupplyDepot()) return;

    BuildArmy();

//    if (BuildRefinery()) return;

//    if (TryBuildExpansionCom()) return;
}

void ZergCrush::updateBuildOrder(const ObservationInterface *observation) {
    Units structures = observation->GetUnits(Unit::Self, IsStructure(observation));
    for (auto structure: structures) {
        if (std::find_if(buildOrderTracking.begin(), buildOrderTracking.end(), [structure](auto built) {
            return structure->tag == built.tag;
        }) != buildOrderTracking.end()) {
            continue; // This building has already been accounted for
        }
        if (structure->build_progress < 1.0f && structure->build_progress > 0.0f) {
            buildOrderTracking.emplace_back(BuiltStructure(structure->tag, observation->GetGameLoop(), structure->unit_type));
        }
    }
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

void ZergCrush::OnUpgradeCompleted(UpgradeID upgrade) {
    switch (upgrade.ToType()) {
        case UPGRADE_ID::STIMPACK: {
            stimResearched = true;
        }
        case UPGRADE_ID::PERSONALCLOAKING: {
            ghostCloakResearched = true;
        }
        case UPGRADE_ID::BANSHEECLOAK: {
            bansheeCloakResearched = true;
        }
        default:
            break;
    }
}

void ZergCrush::OnGameEnd() {
    auto ids = buildOrderIds();
    std::cout << "Game Ended for: " << std::to_string(Control()->Proto().GetAssignedPort()) << std::endl;
}

void ZergCrush::OnGameStart() {
    MultiplayerBot::OnGameStart();

    auto observation = Observation();
    setEnemyRace(observation);

    /**
     * Sets build order for TvT match-ups
     */
    std::vector<BuildOrderStructure> tvtStructures = {
            BuildOrderStructure(observation, 14, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 15, UNIT_TYPEID::TERRAN_BARRACKS),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 16, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, std::optional(UNIT_TYPEID::TERRAN_COMMANDCENTER)),
            BuildOrderStructure(observation, 19, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 20, UNIT_TYPEID::TERRAN_FACTORY),
            BuildOrderStructure(observation, 23, UNIT_TYPEID::TERRAN_COMMANDCENTER),
            BuildOrderStructure(observation, 26, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 28, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 28, UNIT_TYPEID::TERRAN_STARPORT),
            BuildOrderStructure(observation, 32, UNIT_TYPEID::TERRAN_BARRACKSREACTOR, std::optional(UNIT_TYPEID::TERRAN_BARRACKS)),
            BuildOrderStructure(observation, 32, UNIT_TYPEID::TERRAN_REFINERY),
            BuildOrderStructure(observation, 33, UNIT_TYPEID::TERRAN_FACTORYTECHLAB, std::optional(UNIT_TYPEID::TERRAN_FACTORY)),
            BuildOrderStructure(observation, 33, UNIT_TYPEID::TERRAN_STARPORTTECHLAB, std::optional(UNIT_TYPEID::TERRAN_STARPORT)),
            BuildOrderStructure(observation, 34, UNIT_TYPEID::TERRAN_ORBITALCOMMAND, std::optional(UNIT_TYPEID::TERRAN_COMMANDCENTER)),
            BuildOrderStructure(observation, 43, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
            BuildOrderStructure(observation, 52, UNIT_TYPEID::TERRAN_SUPPLYDEPOT),
    };
    tvtBuildOrder = BuildOrder(tvtStructures);
}

void ZergCrush::setEnemyRace(const ObservationInterface *observation) {
    auto playerId = observation->GetPlayerID();
    enemyRace = std::find_if(game_info_.player_info.begin(), game_info_.player_info.end(), [playerId](auto playerInfo) {
        return playerInfo.player_id != playerId && playerInfo.player_type == Participant;
    })->race_actual;
}

void ZergCrush::OnUnitCreated(const sc2::Unit *unit) {
    auto observation = Observation();
    if (unit->build_progress < 1.0 &&
        std::find_if(buildOrderTracking.begin(), buildOrderTracking.end(), [unit](auto built) {
            return unit->tag == built.tag;
        }) == buildOrderTracking.end()) {
        buildOrderTracking.emplace_back(BuiltStructure(unit->tag, observation->GetGameLoop(), unit->unit_type));
        tvtBuildOrder.updateBuiltStructures(unit->unit_type);
    }
}

#ifndef ARMY_COMP_H
#define ARMY_COMP_H

#include "sc2api/sc2_unit_filters.h"
#include <map>
#include <tuple>
#include <utility>

// TODO: could maybe have a option such that this is taken as a TRUMP condition, need something to stop production
struct SquadronBuildCondition {
    SquadronBuildCondition(const sc2::Filter &unitFilter,
                           uint32_t requiredAmountToTrigger,
                           uint32_t unitResponse,
                           bool isRatio = false,
                           bool antiCondition = false)
            : unitFilter(unitFilter),
              requiredAmountToTrigger(requiredAmountToTrigger),
              unitResponse(unitResponse),
              isRatio(isRatio),
              trumpCondition(antiCondition) {}

    SquadronBuildCondition(const sc2::Filter& unitFilter,
                           uint32_t requiredAmountToTrigger,
                           uint32_t unitResponse,
                           sc2::Unit::Alliance alliance) :
            SquadronBuildCondition(unitFilter, requiredAmountToTrigger, unitResponse) {
        this->alliance = alliance;
    }

    SquadronBuildCondition(const sc2::Filter& unitFilter,
                           uint32_t requiredAmountToTrigger,
                           uint32_t unitResponse,
                           sc2::Unit::Alliance alliance,
                           bool isRatio,
                           bool antiCondition) : SquadronBuildCondition(unitFilter, requiredAmountToTrigger, unitResponse, isRatio, antiCondition) {
        this->alliance = alliance;
    }

    /**
     * Which side triggers this condition
     */
    sc2::Unit::Alliance alliance = sc2::Unit::Alliance::Self;

    /**
     * What units trigger this condition
     */
    sc2::Filter unitFilter;

    /**
     * How many of these units
     */
    uint32_t requiredAmountToTrigger;

    /**
     * The response in terms of the units we build
     */
    uint32_t unitResponse;

    /**
     * Response is a ratio from the required amount to trigger
     */
    bool isRatio = false;

    /**
     * Trump condition to stop building this unit
     */
    bool trumpCondition;
};


enum ArmyType {
    SCOUT = 0,
    DEFENSIVE,
    MAIN,
    Count
};


class ArmySquadron {
public:
    ArmySquadron(const sc2::ObservationInterface *observationInterface,
                 sc2::UnitTypeID unitType,
                 sc2::UnitTypeID buildingStructureType,
                 std::vector<SquadronBuildCondition> conditions,
                 ArmyType armyType = MAIN,
                 bool restockSquadron = true)
            : conditions(std::move(conditions)),
              unitType(unitType),
              buildingStructureType(buildingStructureType),
              armyType(armyType),
              restockSquadron(restockSquadron) {
        sc2::UnitTypeData data = observationInterface->GetUnitTypeData().at(this->unitType);
        mineralCost = data.mineral_cost;
        vespeneCost = data.vespene_cost;
        abilityId = data.ability_id;
    }

    /**
     * Sets the number of units that we want at any given moment
     */
    void setGoalCount(const sc2::ObservationInterface *observation) {
        for (const auto &condition: conditions) {
            if (condition.trumpCondition) {
                int numTriggerUnits = (int) observation->GetUnits(condition.alliance, condition.unitFilter).size();
                if (numTriggerUnits >= condition.requiredAmountToTrigger) {
                    unitGoalCount = condition.unitResponse;
                    break;
                }
            }
            if (condition.isRatio) {
                int numTriggerUnits = (int) observation->GetUnits(condition.alliance, condition.unitFilter).size();
                if (numTriggerUnits >= condition.requiredAmountToTrigger) {
                    unitGoalCount = std::max(unitGoalCount, condition.unitResponse * numTriggerUnits);
                }
            }
            if (observation->GetUnits(condition.alliance, condition.unitFilter).size() >=
                condition.requiredAmountToTrigger) {
                // Build more units if the condition is stronger
                unitGoalCount = std::max(unitGoalCount, condition.unitResponse);
            };
        }
    }

    bool shouldBuild(const sc2::ObservationInterface *observation) {
        checkSquadron(observation);
        bool haveEnoughResources = observation->GetMinerals() >= mineralCost && observation->GetVespene() >= vespeneCost;
        return needMore() && haveEnoughResources;
    }

    void checkSquadron(const sc2::ObservationInterface *observation) {
        for (auto &unit: units) {
            auto updatedUnit = observation->GetUnit(unit->tag);
            if (updatedUnit == nullptr) {
                removeFallenSquadronMember(unit);
            } else {
                unit = updatedUnit;
            }
        }
    }

    bool needMore() const {
        if (!restockSquadron) {
            return unitGoalCount > units.size() + numFallenUnits; // Have we built enough in total?
        }
        return unitGoalCount > units.size();
    }

    sc2::Units getSquadron() {
        return units;
    }

    void assignSquadronMember(const sc2::Unit* unit) {
        units.push_back(unit);
    }

    /**
     * In the case that a squadron member belongs to this squadron and died, remove them from this squadron
     * @return true in the case that the squadron member belonged to this squadron
     */
    bool removeFallenSquadronMember(const sc2::Unit* unit) {
        auto fallenSquadronMemberIter = std::find(units.begin(), units.end(), unit);
        if (fallenSquadronMemberIter != units.end()) {
            units.erase(fallenSquadronMemberIter);
            numFallenUnits++;
            return true;
        };
        return false;
    }

    sc2::UnitTypeID getUnitTypeID() const { return unitType; };

    /**
     * Returns what ability creates this unit
     */
    sc2::AbilityID getAbilityId() const { return abilityId; }

    ArmyType getArmyType() const { return armyType; }

    sc2::UnitTypeID getBuildingStructureType() { return buildingStructureType; };

private:
    /**
     * Conditions determining the number of units to build
     */
    std::vector<SquadronBuildCondition> conditions;

    /**
     * Unit type ID for the structure to build
     */
    sc2::UnitTypeID unitType;

    /**
     * The goal number of units that we want to have in this squadron
     */
    uint32_t unitGoalCount = 0;

    /**
     * Amount of minerals required to build a unit of this squadron type
     */
    unsigned int mineralCost;

    /**
     * Amount of vespene gas required to build a unit of this squadron type
     */
    unsigned int vespeneCost;

    /**
     * Building required to make this unit
     */
    sc2::UnitTypeID buildingStructureType;

    /**
     * Ability used to create this unit
     */
    sc2::AbilityID abilityId;

    /**
     * What role do these units play
     */
    ArmyType armyType = MAIN;

    /**
     * References to the units in this squadron
     */
    sc2::Units units;

    /**
     * Is this a squadron that we want to keep stocked up on units?
     */
    bool restockSquadron = true;

    /**
     * How many comrades have fallen
     */
    uint32_t numFallenUnits = 0;
};


class ArmyComposition {
public:
    explicit ArmyComposition(std::vector<ArmySquadron *> &squadrons) : squadrons(squadrons) {}

    template<typename Condition>
    std::vector<ArmySquadron *> FilterSquadrons(std::vector<ArmySquadron *> &armySquadrons, Condition condition) {
        std::vector<ArmySquadron *> result;
        auto i = armySquadrons.begin();
        auto end = armySquadrons.end();

        while (i != end) {
            i = std::find_if(i, armySquadrons.end(), condition);
            if (i != end) {
                result.push_back(*i);
                i++;
            }
        }

        return result;
    }

    std::vector<ArmySquadron *> getAllSquadrons() {
        return squadrons;
    }

    std::vector<ArmySquadron *> squadronsToBuild(std::vector<ArmySquadron *> &armySquadrons,
                                                 const sc2::ObservationInterface *observation) {
        return FilterSquadrons(armySquadrons, [observation](ArmySquadron *&squadron) {
            return squadron->shouldBuild(observation);
        });
    }

    std::vector<ArmySquadron *> getSquadronsNeedMoreUnits(std::vector<ArmySquadron *> &armySquadrons) {
        return FilterSquadrons(armySquadrons, [](ArmySquadron *&squadron) {
            return squadron->needMore();
        });
    }


    std::vector<ArmySquadron *> getSquadronsByType(std::vector<ArmySquadron *> &armySquadrons, ArmyType armyType) {
        return FilterSquadrons(armySquadrons, [armyType](ArmySquadron *&squadron) {
            return squadron->getArmyType() == armyType;
        });
    };

    std::vector<ArmySquadron *> getSquadronsByUnitType(std::vector<ArmySquadron *> &armySquadrons,
                                                       sc2::UnitTypeID unitType) {
        return FilterSquadrons(armySquadrons, [unitType](ArmySquadron *&squadron) {
            return squadron->getUnitTypeID() == unitType;
        });
    };


    /**
     * Update the goal counts of all squadrons
     */
    void setDesiredUnitCounts(const sc2::ObservationInterface *observation) {
        for (auto &unit: squadrons) {
            unit->setGoalCount(observation);
        }
    }

    /**
     * Whenever we create a unit, it should be assigned to a squadron
     */
    void assignUnitToSquadron(const sc2::Unit *unit) {
        for (int i = 0; i < static_cast<int>(ArmyType::Count); ++i) { // Assigned in enum order
            auto currentArmyType = static_cast<ArmyType>(i);

            auto squadronsForUnit = getSquadronsByUnitType(squadrons, unit->unit_type);
            auto armySquadrons = getSquadronsByType(squadronsForUnit, currentArmyType);
            auto squadronsWithRoom = getSquadronsNeedMoreUnits(armySquadrons);

            if (!squadronsWithRoom.empty()) {
                sc2::GetRandomEntry(squadronsWithRoom)->assignSquadronMember(unit);
                return;
            }
        }
    }

private:
    std::vector<ArmySquadron *> squadrons = {};
};

#endif //ARMY_COMP_H

#ifndef ARMY_COMP_H
#define ARMY_COMP_H

#include "sc2api/sc2_unit_filters.h"
#include <map>
#include <tuple>
#include <utility>

struct ArmyUnitCondition {
    ArmyUnitCondition(sc2::Filter unitFilter,
                      uint32_t requiredAmountToTrigger,
                      uint32_t unitResponse,
                      sc2::Unit::Alliance alliance = sc2::Unit::Alliance::Self) :
            alliance(alliance),
            unitFilter(std::move(unitFilter)),
            requiredAmountToTrigger(requiredAmountToTrigger),
            unitResponse(unitResponse) {}

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
};

class ArmyUnit {
public:
    ArmyUnit(const sc2::ObservationInterface *observationInterface,
             sc2::UnitTypeID unitType,
             sc2::UnitTypeID buildingStructureType,
             std::vector<ArmyUnitCondition> conditions)
            : conditions(std::move(conditions)),
              unitType(unitType),
              buildingStructureType(buildingStructureType) {
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
            // TODO: could maybe have a option such that this is taken as a TRUMP condition, need something to stop production
            if (observation->GetUnits(condition.alliance, condition.unitFilter).size() >=
                condition.requiredAmountToTrigger) {
                // Build more units if the condition is stronger
                unitGoalCount = std::max(unitGoalCount, condition.unitResponse);
            };
        }
    }

    bool shouldBuild(const sc2::ObservationInterface *observation) {
        bool needMore = unitGoalCount > observation->GetUnits(sc2::Unit::Alliance::Self, sc2::IsUnit(unitType)).size();
        bool haveEnoughResources =
                observation->GetMinerals() >= mineralCost && observation->GetVespene() >= vespeneCost;
        return needMore && haveEnoughResources;
    }

    sc2::UnitTypeID getUnitTypeID() const { return unitType; };

    /**
     * Returns what ability creates this unit
     */
    sc2::AbilityID getAbilityId() const { return abilityId; }

    sc2::UnitTypeID getBuildingStructureType() { return buildingStructureType; };

private:
    /**
     * Conditions determining the number of units to build
     */
    std::vector<ArmyUnitCondition> conditions;

    /**
     * Unit type ID for the structure to build
     */
    sc2::UnitTypeID unitType;

    /**
     * The goal number of units that we want to have
     */
    uint32_t unitGoalCount = 0;

    /**
     * Amount of minerals required to build this unit
     */
    unsigned int mineralCost;

    /**
     * Amount of vespene gas required to build this unit
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
};


class ArmyComposition {
public:
    explicit ArmyComposition(std::vector<ArmyUnit> units) : units(std::move(units)) {}

    std::vector<ArmyUnit *> unitsToBuild(const sc2::ObservationInterface *observation) {
        std::vector<ArmyUnit *> unitsToBuild;
        auto i = units.begin();
        auto end = units.end();
        while (i != end) {
            i = std::find_if(i, units.end(), [observation](auto &unit) {
                return unit.shouldBuild(observation);
            });
            if (i != end) {
                unitsToBuild.push_back(&(*i));
                i++;
            }
        }
        return unitsToBuild;
    }

    /**
     * Update the goal counts of all units
     */
    void setDesiredUnitCounts(const sc2::ObservationInterface *observation) {
        for (auto &unit: units) {
            unit.setGoalCount(observation);
        }
    }

private:
    std::vector<ArmyUnit> units;

};

#endif //ARMY_COMP_H

#ifndef BUILD_ORDER_H
#define BUILD_ORDER_H

#include <utility>

#include "sc2api/sc2_unit_filters.h"


class BuildOrderStructure {
public:
    BuildOrderStructure(const sc2::ObservationInterface *observationInterface,
                        unsigned int supplyRequirement,
                        sc2::UnitTypeID structureType,
                        std::optional<sc2::UnitTypeID> baseStructureType = {})
            : supplyRequirement(supplyRequirement), structureType(structureType), baseStructureType(baseStructureType) {
        sc2::UnitTypeData data = observationInterface->GetUnitTypeData().at(this->structureType);
        mineralCost = data.mineral_cost;
        vespeneCost = data.vespene_cost;
        techRequirement = data.tech_requirement;
        abilityId = data.ability_id;
        if (sc2::UNIT_TYPEID::TERRAN_ORBITALCOMMAND ==this->structureType) { // Orbital command takes into account cost of command center
            sc2::UnitTypeData baseStructureData = observationInterface->GetUnitTypeData().at(this->baseStructureType.value());
            mineralCost -= baseStructureData.mineral_cost;
            vespeneCost -= baseStructureData.vespene_cost;
        }
    }

    /**
     * Returns true if we have enough resources and the prerequisites for building this structure
     */
    bool canBuild(const sc2::ObservationInterface *observation) {
        bool haveEnoughResources = observation->GetMinerals() >= mineralCost &&
                                   observation->GetVespene() >= vespeneCost;
        bool atSupply = observation->GetFoodUsed() >= supplyRequirement;
        bool haveRequiredTech = true;
        if (techRequirement) {
            haveRequiredTech = !observation->GetUnits(sc2::Unit::Alliance::Self,
                                                      sc2::IsUnit(techRequirement)).empty();
        }
        return haveEnoughResources && atSupply && haveRequiredTech && !built;
    }

    /**
     * Toggle representing whether or not this structure has been built
     */
    bool built = false;

    sc2::UnitTypeID getUnitTypeID() const { return structureType; };

    /**
     * Returns what ability creates this structure
     */
    sc2::AbilityID getAbilityId() const { return abilityId; }

    bool isAddOn() const { return baseStructureType.has_value(); };

    /**
     * Finds the first compatible base structure if any and returns its tag
     */
    std::optional<sc2::Unit> getBaseStruct(const sc2::ObservationInterface *observationInterface) const {
        if (isAddOn()) {
            auto possibleBaseStructs = observationInterface->GetUnits(sc2::Unit::Alliance::Self,
                                                                      sc2::IsUnit(baseStructureType.value()));
            // No compatible structures built
            if (possibleBaseStructs.empty()) {
                return {};
            }

            // Find first structure of type that has no add-ons
            auto baseStructIter = std::find_if(possibleBaseStructs.begin(), possibleBaseStructs.end(),
                                               [&observationInterface](const auto &structure) {
                                                   return observationInterface->GetUnit(structure->add_on_tag) ==
                                                          nullptr;
                                               });

            // If there is no such structure then we can return none
            if (baseStructIter == possibleBaseStructs.end()) {
                return {};
            }

            return {**baseStructIter};
        }
        return {}; // This structure is not an add-on
    };

private:
    /**
     * Supply needed before building
     */
    unsigned int supplyRequirement;

    /**
     * Unit type ID for the structure to build
     */
    sc2::UnitTypeID structureType;

    /**
     * Amount of minerals required to build this structure
     */
    unsigned int mineralCost;

    /**
     * Amount of vespene gas required to build this structure
     */
    unsigned int vespeneCost;

    /**
     * Ability used to create this structure
     */
    sc2::AbilityID abilityId;

    /**
     * Tech requirement for building this structure
     */
    sc2::UnitTypeID techRequirement;

    /**
     * Used in the case of an add-on structure
     */
    std::optional<sc2::UnitTypeID> baseStructureType;
};

class BuildOrder {
public:
    explicit BuildOrder(std::vector<BuildOrderStructure> structures) : structures(std::move(structures)) {}

    void updateBuiltStructures(const sc2::UnitTypeID unitTypeId) {
        auto structureIter = std::find_if(structures.begin(), structures.end(),
                                          [unitTypeId](auto &structure) {
                                              return !structure.built && structure.getUnitTypeID() == unitTypeId;
                                          });
        if (structureIter == structures.end()) return;
        structureIter->built = true;
    }

    std::vector<BuildOrderStructure *> structuresToBuild(const sc2::ObservationInterface* observation) {
        std::vector<BuildOrderStructure *> structuresToBuild;
        auto i = structures.begin();
        auto end = structures.end();
        while (i != end) {
            i = std::find_if(i, structures.end(), [observation](auto &structure) {
                return structure.canBuild(observation);
            });
            if (i != end) {
                structuresToBuild.push_back(&(*i));
                i++;
            }
        }
        return structuresToBuild;
    }

    void OnUnitCreated(const sc2::Unit *unit) {
        if (unit->build_progress < 1.0 && unit->tag &&
            std::find_if(builtStructures.begin(), builtStructures.end(), [unit](auto built) {
                return unit->tag == built;
            }) == builtStructures.end()) {
            builtStructures.push_back(unit->tag);
            updateBuiltStructures(unit->unit_type);
        }
    }

private:
    std::vector<BuildOrderStructure> structures;
    std::vector<sc2::Tag> builtStructures;
};

#endif //BUILD_ORDER_H

#ifndef BUILD_ORDER_H
#define BUILD_ORDER_H

#include <utility>

#include "sc2api/sc2_unit_filters.h"

class BuildOrderStructure {
public:
    BuildOrderStructure(const sc2::ObservationInterface *observationInterface,
                        unsigned int supplyRequirement,
                        sc2::UnitTypeID structureType,
                        bool chainBuild = false)
            : supplyRequirement(supplyRequirement), structureType(structureType), chainBuild(chainBuild) {
        sc2::UnitTypeData data = observationInterface->GetUnitTypeData().at(this->structureType);
        mineralCost = data.mineral_cost;
        vespeneCost = data.vespene_cost;
        techRequirement = data.tech_requirement;
        abilityId = data.ability_id;
    }

    BuildOrderStructure(const sc2::ObservationInterface *observationInterface,
                        sc2::UnitTypeID structureType,
                        bool chainBuild=false)
            : BuildOrderStructure(observationInterface, 0, structureType, chainBuild) {
    }

    BuildOrderStructure(const sc2::ObservationInterface *observationInterface,
                        unsigned int supplyRequirement,
                        sc2::UnitTypeID structureType,
                        sc2::UnitTypeID baseStructureType)
            : BuildOrderStructure(observationInterface, supplyRequirement, structureType) {
        this->baseStructureType = new sc2::UnitTypeID(baseStructureType);
        // Orbital command takes into account cost of command center
        if (sc2::UNIT_TYPEID::TERRAN_ORBITALCOMMAND == this->structureType) {
            sc2::UnitTypeData baseStructureData = observationInterface->GetUnitTypeData().at(
                    *(this->baseStructureType));
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
        if (isChainBuild()) {
            return getBuiltBy() && haveEnoughResources && !built();
        }
        return haveEnoughResources && atSupply && haveRequiredTech && !built();
    }

    /**
     * Finds the first compatible base structure if any and returns its tag
     */
    const sc2::Unit *getBaseStruct(const sc2::ObservationInterface *observationInterface) const {
        if (isAddOn()) {
            auto possibleBaseStructs = observationInterface->GetUnits(sc2::Unit::Alliance::Self,
                                                                      sc2::IsUnit(*baseStructureType));
            // No compatible structures built
            if (possibleBaseStructs.empty()) {
                return nullptr;
            }

            // Find first structure of type that has no add-ons
            auto baseStructIter = std::find_if(possibleBaseStructs.begin(), possibleBaseStructs.end(),
                                               [&observationInterface](const auto &structure) {
                                                   return observationInterface->GetUnit(structure->add_on_tag) ==
                                                          nullptr;
                                               });

            // If there is no such structure then we can return none
            if (baseStructIter == possibleBaseStructs.end()) {
                return nullptr;
            }

            return *baseStructIter;
        }
        return nullptr; // This structure is not an add-on
    };

    bool built() const { return getTag() != 0 || builtAddOn; }

    void setBuiltAddOn(bool builtAddOn) {
        BuildOrderStructure::builtAddOn = builtAddOn;
    }

    sc2::UnitTypeID getUnitTypeID() const { return structureType; };

    /**
    * Returns what ability creates this structure
    */
    sc2::AbilityID getAbilityId() const { return abilityId; }

    bool isAddOn() const { return baseStructureType != nullptr; };


    sc2::Tag getTag() const {
        return tag;
    }

    void setTag(sc2::Tag tag) {
        BuildOrderStructure::tag = tag;
    }

    sc2::Tag getBuiltBy() const {
        return builtBy;
    }

    void setBuiltBy(sc2::Tag builtBy) {
        BuildOrderStructure::builtBy = builtBy;
    }
    
    bool getChainBuild() const {return chainBuild;}

    bool isChainBuild() const {
        return chainBuild && !supplyRequirement;
    }

    bool isChainBuildLeader() const {
        return chainBuild && supplyRequirement;
    }

    void setSupplyRequirement(unsigned int supplyRequirement) {
        BuildOrderStructure::supplyRequirement = supplyRequirement;
    }

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
    sc2::UnitTypeID *baseStructureType = nullptr;

    sc2::Tag tag = 0;

    sc2::Tag builtBy = 0;

    /**
     * To be built by unit that built previous building
     */
    bool chainBuild = false;

    bool builtAddOn = false;
};

class BuildOrder {
public:
    explicit BuildOrder(std::vector<BuildOrderStructure> structures) : structures(std::move(structures)) {}

    void chainBuild(sc2::Tag chainBuilder) {
        auto buildingIter = std::find_if(structures.begin(), structures.end(), [](const auto& building) {
            return building.isChainBuild() && !building.built();
        });
        if (buildingIter != structures.end()) {
            buildingIter->setBuiltBy(chainBuilder);
        }
    }

    std::vector<BuildOrderStructure *> structuresToBuild(const sc2::ObservationInterface *observation) {
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

    void updateBuiltStructures(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        auto unitTypeId = unit->unit_type;
        auto structureIter = std::find_if(structures.begin(), structures.end(),
                                          [unitTypeId](auto &structure) {
                                              return !structure.built() && structure.getUnitTypeID() == unitTypeId;
                                          });
        if (structureIter == structures.end()) return;

        sc2::AbilityID abilityId = structureIter->getAbilityId();
        sc2::Tag tag = unit->tag;
        auto workers = observation->GetUnits(sc2::Unit::Self, sc2::IsUnit(sc2::UNIT_TYPEID::TERRAN_SCV));
        auto assignedWorkerIter = std::find_if(workers.begin(), workers.end(), [abilityId, tag](const auto &worker) {
            return std::find_if(worker->orders.begin(), worker->orders.end(), [&](const auto &order) {
                return order.ability_id == abilityId;
            }) != worker->orders.end();
        });
        if (assignedWorkerIter != workers.end()) {
            structureIter->setBuiltBy((*assignedWorkerIter)->tag);
        }
        structureIter->setTag(tag);
    }

    void OnUnitCreated(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        if (unit->build_progress < 1.0 && unit->tag &&
            std::find_if(structures.begin(), structures.end(), [unit](auto building) {
                return unit->tag == building.getTag();
            }) == structures.end()) {
            updateBuiltStructures(observation, unit);
        }
    }

    void OnUnitDestroyed(const sc2::Unit *unit) {
        sc2::Tag destroyedTag = unit->tag;
        auto buildingIter = std::find_if(structures.begin(), structures.end(), [destroyedTag](auto const &building) {
            return building.getTag() == destroyedTag;
        });
        if (buildingIter != structures.end()) {
            buildingIter->setTag(0);
        }
    }

    void OnBuildingFinished(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        sc2::Tag builtTag = unit->tag;
        auto buildingIter = std::find_if(structures.begin(), structures.end(), [builtTag](auto const &building) {
            return building.getTag() == builtTag;
        });
        if (buildingIter != structures.end() && buildingIter->isChainBuildLeader()) {
            auto builtByTag = buildingIter->getBuiltBy();
            if (observation->GetUnit(builtByTag) != nullptr) { // May have died
                chainBuild(builtByTag);
            }
        }
    }

private:
    std::vector<BuildOrderStructure> structures;
};

#endif //BUILD_ORDER_H

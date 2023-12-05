#ifndef UPGRADE_ORDER_H
#define UPGRADE_ORDER_H

class Upgrade {
public:
    Upgrade(const sc2::ObservationInterface *observationInterface,
            unsigned int supplyRequirement,
            sc2::UnitTypeID upgradeStructure,
            sc2::AbilityID abilityId,
            sc2::UpgradeID upgradeId)
            : supplyRequirement(supplyRequirement),
              upgradeStructure(upgradeStructure),
              abilityId(abilityId),
              upgradeId(upgradeId) {
        sc2::UpgradeData data = observationInterface->GetUpgradeData().at(this->upgradeId);
        mineralCost = data.mineral_cost;
        vespeneCost = data.vespene_cost;
    }
    
    bool canUpgrade(const sc2::ObservationInterface *observation) {
        bool haveResources = observation->GetMinerals() > mineralCost && observation->GetVespene() > vespeneCost;
        bool haveBuildingStructure = !observation->GetUnits(sc2::Unit::Alliance::Self, sc2::IsUnit(upgradeStructure)).empty();
        auto upgrades = observation->GetUpgrades();
        bool alreadyResearched = std::find(upgrades.begin(), upgrades.end(), upgradeId) != upgrades.end();
        return haveResources && haveBuildingStructure && !alreadyResearched;
    }

    const sc2::UnitTypeID &getUpgradeStructure() const {
        return upgradeStructure;
    }

    const sc2::AbilityID &getAbilityId() const {
        return abilityId;
    }

private:
    /**
     * Supply needed before building
     */
    unsigned int supplyRequirement;
    /**
     * Unit type ID for the structure to build
     */
    sc2::UnitTypeID upgradeStructure;

    /**
     * Amount of minerals required to build this upgrade
     */
    unsigned int mineralCost;

    /**
     * Amount of vespene gas required to build this upgrade
     */
    unsigned int vespeneCost;

    /**
     * Ability used to create this upgrade
     */
    sc2::AbilityID abilityId;
    
    /**
     * Upgrade ID
     */
    sc2::UpgradeID upgradeId;
};


class UpgradeOrder {
public:
    explicit UpgradeOrder(std::vector<Upgrade> upgrades) : upgrades(std::move(upgrades)) {}

    std::vector<Upgrade *> upgradesToBuild(const sc2::ObservationInterface *observation) {
        std::vector<Upgrade *> upgradesToBuild;
        auto i = upgrades.begin();
        auto end = upgrades.end();
        while (i != end) {
            i = std::find_if(i, upgrades.end(), [observation](auto &upgrade) {
                return upgrade.canUpgrade(observation);
            });
            if (i != end) {
                upgradesToBuild.push_back(&(*i));
                i++;
            }
        }
        return upgradesToBuild;
    }
    
private:
    std::vector<Upgrade> upgrades = {};
};



#endif //UPGRADE_ORDER_H

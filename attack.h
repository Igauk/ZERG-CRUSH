#ifndef ATTACK_H
#define ATTACK_H


#include "filters.h"

struct MicroInformation {
    MicroInformation(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        auto data = observation->GetUnitTypeData().at(unit->unit_type);
        if (data.weapons.empty()) {
            range = 0;
            strongAgainst = {};
            return;
        }
        range = data.weapons.front().range;
        for (const auto &bonus: data.weapons.front().damage_bonus) {
            strongAgainst.push_back(bonus.attribute);
        }
    }

    float range;
    std::vector<sc2::Attribute> strongAgainst;
};

class ZergCrushMicro {
public:
    explicit ZergCrushMicro(sc2::ActionInterface *actionInterface) : actionInterface(actionInterface) {}

    void microUnit(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        switch (unit->unit_type.ToType()) {
            case sc2::UNIT_TYPEID::TERRAN_MARINE:
                handleMarineMicro(observation, unit);
                break;
            case sc2::UNIT_TYPEID::TERRAN_MARAUDER:
                handleMarauderMicro(observation, unit);
                break;
            case sc2::UNIT_TYPEID::TERRAN_SIEGETANKSIEGED:
            case sc2::UNIT_TYPEID::TERRAN_SIEGETANK:
                handleSiegeTankMicro(observation, unit);
                break;
            default:
                attackWeakest(observation, unit);
        }
    }

    void onUpgradeComplete(sc2::UpgradeID upgradeId) {
        switch (upgradeId.ToType()) {
            case sc2::UPGRADE_ID::STIMPACK:
                stimResearched = true;
            default:
                return;

        }

    }

private:

    void handleMarineMicro(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        if (stimResearched && !unit->orders.empty()) {
            if (unit->orders.front().ability_id == sc2::ABILITY_ID::ATTACK) stimInRange(observation, unit);
        } else {
            attackWeakest(observation, unit);
        }
    }

    void handleMarauderMicro(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        if (stimResearched && !unit->orders.empty()) {
            if (unit->orders.front().ability_id == sc2::ABILITY_ID::ATTACK) stimInRange(observation, unit);
        } else {
            attackWeakest(observation, unit);
        }
    }

    void stimInRange(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        bool hasStimmed = false;
        if (unit->health < unit->health_max / 2) return; // don't stim at low hp
        auto distance = getClosestDistanceTo(observation->GetUnits(sc2::Unit::Enemy), unit);
        for (const auto& buff : unit->buffs) {
            if (buff == sc2::BUFF_ID::STIMPACK) {
                hasStimmed = true;
            }
        }
        if (distance < MicroInformation(observation, unit).range + 1 && !hasStimmed) {
            actionInterface->UnitCommand(unit, sc2::ABILITY_ID::EFFECT_STIM);
        }
    }

    void handleSiegeTankMicro(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        float distance = getClosestDistanceTo(observation->GetUnits(sc2::Unit::Enemy, sc2::IsVisible()), unit);
        if (distance > 13 && sc2::UNIT_TYPEID::TERRAN_SIEGETANKSIEGED == unit->unit_type) {
            actionInterface->UnitCommand(unit, sc2::ABILITY_ID::MORPH_UNSIEGE);
        } else if (distance < 11 && sc2::UNIT_TYPEID::TERRAN_SIEGETANK == unit->unit_type) {
            actionInterface->UnitCommand(unit, sc2::ABILITY_ID::MORPH_SIEGEMODE);
        }
        else {
            attackWeakest(observation, unit);
        }
    }

    void attackWeakest(const sc2::ObservationInterface *observation, const sc2::Unit *unit) {
        auto microInfo = MicroInformation(observation, unit);
        const TargetableBy &targetableByUnit = TargetableBy(observation, unit);
        float attackRange = std::max(microInfo.range * 2, 5.0f);
        const auto inRange = WithinDistanceOf(unit, attackRange);
        const NotUnits &notToTarget = NotUnits({sc2::UNIT_TYPEID::ZERG_EGG, sc2::UNIT_TYPEID::ZERG_LARVA});
        auto weakEnemies = observation->GetUnits(sc2::Unit::Enemy, CombinedFilter({
                inRange,
                targetableByUnit,
                HasAttribute(observation, microInfo.strongAgainst),
                IsDangerous(observation, 5.0f),
                notToTarget
                }));

        if (getClosestDistanceTo(weakEnemies, unit) <= attackRange) {
            std::sort(weakEnemies.begin(), weakEnemies.end(), [](auto& enemyA, auto& enemyB) {
                return enemyA->health < enemyB->health;
            });
            actionInterface->UnitCommand(unit, sc2::ABILITY_ID::ATTACK, weakEnemies.front());
            return;
        }

        auto allEnemiesInRange = observation->GetUnits(sc2::Unit::Enemy, CombinedFilter({inRange, targetableByUnit, notToTarget}));
        if (!allEnemiesInRange.empty()) {
            std::sort(allEnemiesInRange.begin(), allEnemiesInRange.end(), [](auto& enemyA, auto& enemyB) {
                return enemyA->health < enemyB->health;
            });
            actionInterface->UnitCommand(unit, sc2::ABILITY_ID::ATTACK, allEnemiesInRange.front());
        } else {
            auto allEnemies = observation->GetUnits(sc2::Unit::Enemy, CombinedFilter({sc2::IsVisible(), targetableByUnit, notToTarget}));
            if (!allEnemies.empty()) {
                actionInterface->UnitCommand(unit, sc2::ABILITY_ID::ATTACK, allEnemies.front());
            }
        }
    }

    float getClosestDistanceTo(const sc2::Units &units, const sc2::Unit *const &unit) {
        float minimumEnemyUnitDistance = std::numeric_limits<float>::max();
        for (const auto &enemyUnit: units) {
            float unitDistance = Distance2D(enemyUnit->pos, unit->pos);
            minimumEnemyUnitDistance = std::min(unitDistance, minimumEnemyUnitDistance);
        }
        return minimumEnemyUnitDistance;
    }

    sc2::ActionInterface *actionInterface;
    bool stimResearched = false;
};

#endif //ATTACK_H

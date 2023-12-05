#include <utility>

//
// Created by Ian Gauk on 2023-11-30.
//

#ifndef FILTERS_H
#define FILTERS_H

struct CombinedFilter {
    std::vector<sc2::Filter> filters;

    explicit CombinedFilter(std::vector<sc2::Filter> filters) : filters(std::move(filters)) {}

    bool operator()(const sc2::Unit &unit) {
        return std::all_of(filters.begin(), filters.end(), [unit](auto& filter) {return filter(unit); });
    }
};


struct IsFlying {
    bool operator()(const sc2::Unit &unit) {
        return unit.is_flying;
    }
};

struct IsDangerous {
    explicit IsDangerous(const sc2::ObservationInterface *observation, float damageThreshold = 0.0f)
            : observation_(observation),
              damageThreshold_(damageThreshold) {};

    bool operator()(const sc2::Unit &unit) {
        auto weapons = observation_->GetUnitTypeData().at(unit.unit_type).weapons;
        float maxDamage = 0.0f;
        for (auto &weapon : weapons) {
            maxDamage = std::max(maxDamage, weapon.damage_);
        }
        return maxDamage > damageThreshold_;
    }

    float damageThreshold_;
    const sc2::ObservationInterface *observation_;

};

struct WithinDistanceOf {
    explicit WithinDistanceOf(const sc2::Unit* unit, float distance): distance_(distance) {
        position_ = unit->pos;
    };

    explicit WithinDistanceOf(const sc2::Point2D position, float distance): distance_(distance), position_(position) {};

    bool operator()(const sc2::Unit &unit) {
        return sc2::Distance2D(unit.pos, position_) <= distance_;
    }

    sc2::Point2D position_;
    const float distance_;
};

struct HasAttribute {
    explicit HasAttribute(const sc2::ObservationInterface *observation, std::vector<sc2::Attribute> attributes)
            : observation_(observation),
              attributes_(std::move(attributes)) {}

    bool operator()(const sc2::Unit &unit) {
        if (attributes_.empty()) return true; // Base case
        auto attributes = observation_->GetUnitTypeData().at(unit.unit_type).attributes;
        std::vector<sc2::Attribute> attributeIntersection;
        std::set_intersection(attributes.begin(), attributes.end(), attributes_.begin(), attributes_.end(),
                              std::back_inserter(attributeIntersection));
        return !attributeIntersection.empty();
    }

    std::vector<sc2::Attribute> attributes_;
    const sc2::ObservationInterface *observation_;
};

struct TargetableBy {
    TargetableBy(const sc2::ObservationInterface *obs, const sc2::Unit *attackingUnit) {
        auto weapons = obs->GetUnitTypeData().at(attackingUnit->unit_type).weapons;
        for (const auto &weapon: weapons) {
            switch (weapon.type) {
                case sc2::Weapon::TargetType::Ground:
                    switch (targetType_) {
                        case sc2::Weapon::TargetType::Air:
                        case sc2::Weapon::TargetType::Any:
                            targetType_ = sc2::Weapon::TargetType::Any;
                        case sc2::Weapon::TargetType::Invalid:
                        case sc2::Weapon::TargetType::Ground:
                            targetType_ = sc2::Weapon::TargetType::Ground;
                            break;
                    };
                    break;
                case sc2::Weapon::TargetType::Air:
                    switch (targetType_) {
                        case sc2::Weapon::TargetType::Ground:
                        case sc2::Weapon::TargetType::Any:
                            targetType_ = sc2::Weapon::TargetType::Any;
                        case sc2::Weapon::TargetType::Invalid:
                        case sc2::Weapon::TargetType::Air:
                            targetType_ = sc2::Weapon::TargetType::Air;
                            break;
                    };
                    break;
                case sc2::Weapon::TargetType::Any:
                    targetType_ = sc2::Weapon::TargetType::Any;
                    break;
                case sc2::Weapon::TargetType::Invalid:
                    break;
            }
        }
    }

    bool operator()(const sc2::Unit &unit) const {
        switch (targetType_) {
            case sc2::Weapon::TargetType::Ground:
                return !unit.is_flying;
            case sc2::Weapon::TargetType::Air:
                return unit.is_flying;
            case sc2::Weapon::TargetType::Any:
                return true;
            case sc2::Weapon::TargetType::Invalid:
                return false;
        }
    }

    sc2::Weapon::TargetType targetType_ = sc2::Weapon::TargetType::Invalid;
};


// Ignores Overlords, workers, and structures
struct IsArmy {
    explicit IsArmy(const sc2::ObservationInterface *obs) : observation_(obs) {}

    bool operator()(const sc2::Unit &unit) const {
        auto attributes = observation_->GetUnitTypeData().at(unit.unit_type).attributes;
        for (const auto &attribute: attributes) {
            if (attribute == sc2::Attribute::Structure) return false;
        }
        switch (unit.unit_type.ToType()) {
            case sc2::UNIT_TYPEID::ZERG_OVERLORD:
            case sc2::UNIT_TYPEID::PROTOSS_PROBE:
            case sc2::UNIT_TYPEID::ZERG_DRONE:
            case sc2::UNIT_TYPEID::TERRAN_SCV:
            case sc2::UNIT_TYPEID::ZERG_QUEEN:
            case sc2::UNIT_TYPEID::ZERG_LARVA:
            case sc2::UNIT_TYPEID::ZERG_EGG:
            case sc2::UNIT_TYPEID::TERRAN_MULE:
            case sc2::UNIT_TYPEID::TERRAN_NUKE:
                return false;
            default:
                return true;
        }
    }

    const sc2::ObservationInterface *observation_;
};

struct IsStructure {
    explicit IsStructure(const sc2::ObservationInterface *obs) : observation_(obs) {};

    bool operator()(const sc2::Unit &unit) const {
        auto &attributes = observation_->GetUnitTypeData().at(unit.unit_type).attributes;
        return std::find_if(attributes.begin(), attributes.end(), [](const auto &attribute) {
            return attribute == sc2::Attribute::Structure;
        }) != attributes.end();
    }

    const sc2::ObservationInterface *observation_;
};

#endif //FILTERS_H

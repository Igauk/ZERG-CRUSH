#include <utility>

//
// Created by Ian Gauk on 2023-11-30.
//

#ifndef FILTERS_H
#define FILTERS_H

template <typename Filter1, typename Filter2>
struct CombinedFilter {
    Filter1 filter1;
    Filter2 filter2;

    CombinedFilter(Filter1 f1, Filter2 f2) : filter1(f1), filter2(f2) {}

    bool operator()(const sc2::Unit &unit) {
        return filter1(unit) && filter2(unit);
    }
};


struct IsFlying {
    bool operator()(const sc2::Unit &unit) {
        return unit.is_flying;
    }
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
                    switch (weapon.type) {
                        case sc2::Weapon::TargetType::Air:
                        case sc2::Weapon::TargetType::Any:
                            targetType_ = sc2::Weapon::TargetType::Any;
                        case sc2::Weapon::TargetType::Invalid:
                        case sc2::Weapon::TargetType::Ground:
                            break;
                    };
                case sc2::Weapon::TargetType::Air:
                    switch (weapon.type) {
                        case sc2::Weapon::TargetType::Ground:
                        case sc2::Weapon::TargetType::Any:
                            targetType_ = sc2::Weapon::TargetType::Any;
                        case sc2::Weapon::TargetType::Invalid:
                        case sc2::Weapon::TargetType::Air:
                            break;
                    };
                case sc2::Weapon::TargetType::Any:
                    targetType_ = sc2::Weapon::TargetType::Any;
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

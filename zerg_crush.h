#ifndef ZERG_CRUSH_H
#define ZERG_CRUSH_H

#include "sc2api/sc2_api.h"
#include "sc2api/sc2_args.h"
#include "sc2lib/sc2_lib.h"
#include "sc2utils/sc2_manage_process.h"
#include "sc2utils/sc2_arg_parser.h"

#include "build_order.h"
#include "army_comp.h"
#include "attack.h"
#include "filters.h"
#include "positions.h"
#include "ray_cast.h"

struct Point2DComparator {
    bool operator()(const sc2::Point2D& lhs, const sc2::Point2D& rhs) const {
        if (lhs.x < rhs.x) return true;
        if (lhs.x > rhs.x) return false;
        return lhs.y < rhs.y;
    }
};

static const float SQUADRON_CLUSTER_DISTANCE = 5.0f;
static const float ARMY_CLUSTER_DISTANCE = 2 * SQUADRON_CLUSTER_DISTANCE;
using namespace sc2;

class ZergCrush : public Agent {
public:
    bool TryBuildSCV();

    bool TryBuildSupplyDepot();

    /**
     * Builds unit from a specific base structure
     */
    bool TryBuildFrom(AbilityID abilityId, Tag baseStructure);

    bool TryBuildStructureRandom(AbilityID abilityTypeForStructure, UnitTypeID unitType);

    bool TryBuildWallPiece(sc2::UnitTypeID piece);

    void RayCastWithUnit(const Unit* unit, const ObservationInterface &observation);

    void BuildArmy();

    void ManageMacro();

    void ManageUpgrades();

    void ManageArmy();

    bool BuildRefinery();

    void OnStep() final;

    void OnUnitIdle(const Unit *unit) override;

    void OnGameEnd() final;

    void OnGameStart() final;

    void OnUnitCreated(const sc2::Unit *unit) final;

    void OnUnitDestroyed(const Unit *) final;

    void OnUnitEnterVision(const Unit *) final;

    void OnBuildingConstructionComplete(const Unit *unit) final;

    void OnUpgradeCompleted(UpgradeID upgradeId) final;

private:
    std::vector<UNIT_TYPEID> supplyDepotTypes = {UNIT_TYPEID::TERRAN_SUPPLYDEPOT,
                                                 UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED};
    std::vector<UNIT_TYPEID> bioUnitTypes = {UNIT_TYPEID::TERRAN_MARINE, UNIT_TYPEID::TERRAN_MARAUDER,
                                             UNIT_TYPEID::TERRAN_GHOST, UNIT_TYPEID::TERRAN_REAPER};
    std::vector<UNIT_TYPEID> reactorTypes = {UNIT_TYPEID::TERRAN_BARRACKSREACTOR, UNIT_TYPEID::TERRAN_STARPORTREACTOR,
                                             UNIT_TYPEID::TERRAN_FACTORYREACTOR};

    const uint32_t MAX_WORKER_COUNT = 70;

    /**
     * Queried at the start of the game, represents potential expansion locations for each starting location
     */
    std::map<const Point2D, std::vector<Point3D>, Point2DComparator> expansionMap = {};

    /**
     * Queried at the start of the game, represents potential expansion locations
     */
    std::vector<Point3D> expansionLocations = {};


    /**
     * Queried at the start of the game, represents the starting location of the enemy
     */
    std::vector<Point2D> enemyStartingLocations;

    /**
     * Where we think the enemy is, prioritize going here when scouting
     */
    Point2D* assumedEnemyStartingLocation = nullptr;

    /**
     * Queried at the start of the game, represents our starting location on the map
     */
    Point3D startingLocation;

    /**
     * Center point for the base
     */
    Point3D baseRallyPoint;

    /**
     * Queried at the beginning of the game, represents the race that is played by the opposing player
     */
    Race enemyRace;

    BuildOrder *buildOrder;
    ArmyComposition *armyComposition;
    ZergCrushMicro *attackMicro;

    void setEnemyRace(const ObservationInterface *observation);

    bool
    TryBuildStructureUnit(AbilityID ability_type_for_structure, const Unit *unit, Point2D location, bool isExpansion);

    static bool IsTooCloseToStructures(const Point2D &buildLocation, const Units &structures, float minDistance);

    bool TryBuildStructureRandomWithUnit(AbilityID abilityTypeForStructure, const Unit *unit);

    bool TryBuildUnit(AbilityID abilityTypeForUnit, UnitTypeID buildingUnitType);

    bool TryBuildGas(AbilityID build_ability, UnitTypeID worker_type, Point2D base_location);

    bool TryExpand(AbilityID build_ability, UnitTypeID worker_type);

    bool
    TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Point2D location, bool isExpansion = false);

    bool TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Tag location_tag);

    const Unit *FindNearestMineralPatch(const Point2D &start);

    /**
     * Finds a job for an idle SCV to do, finding a geyser or base requiring more SCVs
     */
    void HandleIdleWorker(const Unit *worker);

    void TryCallDownMule();

    int GetExpectedWorkers();

    /**
     * Finds a point within the close and far radius to a given location
     * @param location Base location
     * @param closeRadius Point will at least be this radius from the base location
     * @param farRadius Point will be within this radius of the base location
     * @return
     */
    static Point2D getRandomLocationBy(Point2D location, float farRadius, float closeRadius = 0.0f);

    void ScoutWithUnits(const sc2::ObservationInterface *observation, const sc2::Units &units,
                        float clusterDistance = SQUADRON_CLUSTER_DISTANCE);

    void clusterUnits(const Units &units, float clusterDistance = SQUADRON_CLUSTER_DISTANCE);

    static std::vector<std::pair<Point3D, std::vector<Unit>>> getClusters(const Units &units,
                                                                          float clusterDistance,
                                                                          size_t clusterMinSize = 1,
                                                                          size_t clusterMaxSize = std::numeric_limits<size_t>::max());

    void setEnemyExpansionLocations();

    void sortEnemyExpansionLocations();

    void markOffScoutedLocations(const Point3D &scoutLocation);

    void setAssumedEnemyStartingLocation(const Point3D &clusterPosition);
};

#endif
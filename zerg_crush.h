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

using namespace sc2;

class ZergCrush : public Agent {
public:
    bool TryBuildSCV();

    bool TryBuildSupplyDepot();

    bool TryBuildAddOn(AbilityID ability_type_for_structure, uint64_t base_structure);

    bool TryBuildStructureRandom(AbilityID ability_type_for_structure, UnitTypeID unit_type);

    void BuildArmy();

    void ManageMacro();

    void ManageUpgrades();

    void ManageArmy();

    bool BuildRefinery();

    void OnStep() final;

    void OnUnitIdle(const Unit* unit) override;

    void OnGameEnd() final;

    void OnGameStart() final;

    void OnUnitCreated(const sc2::Unit* unit) final;

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
     * Queried at the start of the game, represents potential expansion locations on the map
     */
    std::vector<Point3D> expansionLocations = {};

    /**
     * Queried at the start of the game, represents the starting location of the enemy
     */
    Point2D enemyStartingLocation;

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

    void ScoutWithUnit(const sc2::ObservationInterface *observation, const sc2::Unit *unit);

    bool
    TryBuildStructureUnit(AbilityID ability_type_for_structure, const Unit *unit, Point2D location, bool isExpansion);

    static bool IsTooCloseToStructures(const Point2D &buildLocation, const Units &structures, float minDistance);

    bool TryBuildStructureRandomWithUnit(AbilityID abilityTypeForStructure, const Unit *unit);

    bool TryBuildUnit(AbilityID abilityTypeForUnit, UnitTypeID buildingUnitType);

    bool TryBuildGas(AbilityID build_ability, UnitTypeID worker_type, Point2D base_location);

    bool TryExpand(AbilityID build_ability, UnitTypeID worker_type);

    bool
    TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Point2D location, bool isExpansion);

    bool TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Tag location_tag);

    const Unit *FindNearestMineralPatch(const Point2D &start);

    /**
     * Finds a job for an idle SCV to do, finding a geyser or base requiring more SCVs
     */
    void HandleIdleWorker(const Unit *worker);

    void TryCallDownMule();

    int GetExpectedWorkers();
};

#endif
#include "sc2api/sc2_unit_filters.h"

#include <array>
#include <optional>


enum Maps {CACTUS, BELSHIR, PROXIMA};
// Note: Map origin at bottom left.
// Each respective element of the following vectors relates to the same ramp spawn location.
// i.e. Use the same index for all of the vectors to get matching build locations.
// Leftmost barracks position.

struct Positions
{   
    Maps getMap(const sc2::ObservationInterface* &observation) {
        sc2::GameInfo info = observation->GetGameInfo();
        //std::cout << info.map_name << std::endl;
        if (info.map_name.find("Cactus") != std::string::npos) {
            return CACTUS;
        }
        else if (info.map_name.find("Proxima") != std::string::npos) {return PROXIMA;}
        else {return BELSHIR;}

    }
    const std::vector<sc2::Point2D> BARRACKS_POSITIONS1_CACTUS = {
        sc2::Point2D(26, 52),
        sc2::Point2D(48, 160),
        sc2::Point2D(138, 28),
        sc2::Point2D(158, 142),
    };
    // Rightmost barracks position.
    const std::vector<sc2::Point2D> BARRACKS_POSITIONS2_CACTUS = {
        sc2::Point2D(32, 49),
        sc2::Point2D(51, 165),
        sc2::Point2D(142, 32),
        sc2::Point2D(163, 139),
    };
    // Supply depot in center of wall.
    const std::vector<sc2::Point2D> SUPPLY_DEPOT_POSITIONS_CACTUS = {
        sc2::Point2D(30, 50),
        sc2::Point2D(50, 163),
        sc2::Point2D(141, 30),
        sc2::Point2D(163, 142),
    };
    // Turrent behind wall.
    const std::vector<sc2::Point2D> TURRET_POSITIONS_CACTUS = {
        sc2::Point2D(28, 48),
        sc2::Point2D(48, 165),
        sc2::Point2D(144, 28),
        sc2::Point2D(165, 144),
    };

    // Leftmost barracks position.
    const std::vector<sc2::Point2D> BARRACKS_POSITIONS1_BELSHIR = {
        sc2::Point2D(40, 130),
        sc2::Point2D(98, 25),
    };
    // Rightmost barracks position.
    const std::vector<sc2::Point2D> BARRACKS_POSITIONS2_BELSHIR = {
        sc2::Point2D(44, 134),
        sc2::Point2D(101, 29),
    };
    // Supply depot in center of wall.
    const std::vector<sc2::Point2D> SUPPLY_DEPOT_POSITIONS_BELSHIR = {
        sc2::Point2D(44, 132),
        sc2::Point2D(102, 27),
    };
    // Turrent behind wall.
    const std::vector<sc2::Point2D> TURRET_POSITIONS_BELSHIR = {
        sc2::Point2D(41, 134),
        sc2::Point2D(105, 26),
    };

    // Leftmost barracks position.
    const std::vector<sc2::Point2D> BARRACKS_POSITIONS1_PROXIMA = {
        sc2::Point2D(48, 50),
        sc2::Point2D(147, 121),
    };
    // Rightmost barracks position.
    const std::vector<sc2::Point2D> BARRACKS_POSITIONS2_PROXIMA = {
        sc2::Point2D(50, 47),
        sc2::Point2D(151, 118),
    };
    // Supply depot in center of wall.
    const std::vector<sc2::Point2D> SUPPLY_DEPOT_POSITIONS_PROXIMA = {
        sc2::Point2D(54, 49),
        sc2::Point2D(145, 120),
    };
    // Turrent behind wall.
    const std::vector<sc2::Point2D> TURRET_POSITIONS_PROXIMA = {
        sc2::Point2D(56, 46),
        sc2::Point2D(146, 124),
    };

    std::array<std::vector<sc2::Point2D>, 4> cactus_postions = {
    SUPPLY_DEPOT_POSITIONS_CACTUS,
    BARRACKS_POSITIONS1_CACTUS,
    BARRACKS_POSITIONS2_CACTUS,

    };
    std::array<std::vector<sc2::Point2D>, 4> belshir_postions = {
        SUPPLY_DEPOT_POSITIONS_BELSHIR,
        BARRACKS_POSITIONS1_BELSHIR,
        BARRACKS_POSITIONS2_BELSHIR,
        TURRET_POSITIONS_BELSHIR
    };
    std::array<std::vector<sc2::Point2D>, 4> proxima_postions = {
        SUPPLY_DEPOT_POSITIONS_PROXIMA,
        BARRACKS_POSITIONS1_PROXIMA,
        BARRACKS_POSITIONS2_PROXIMA,
        TURRET_POSITIONS_PROXIMA
    };
};


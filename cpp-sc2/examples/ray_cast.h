#include "sc2api/sc2_unit_filters.h"
#include "sc2lib/sc2_lib.h"

#include <array>
#include <iostream>
#include <cmath>

//stores the distance between a Unit and the 4 walls on its sides
//by using ray casting to determine the distance

//N, E, S, W
typedef std::array<float, 4> Walls;
typedef std::array<float, 4> Scores;

class RayCastInstance {
public:
    Walls wall_distances;
    Scores scores;
    const sc2::Unit* unit;

    RayCastInstance(const sc2::Unit* unit) : unit(unit) {
        for(int i = 0; i < 4; ++i) {
            wall_distances[i] = -1.0;
        }
    }

    //performs a 16-angle ray cast, with 4 sets of perpendicular rays
    bool castWithUnit(const sc2::Unit* unit, const sc2::ObservationInterface &observation) {
        sc2::GameInfo info = observation.GetGameInfo();


        for (int i = 0; i < 4; ++i) {
            Walls current_ray_walls;
            for (int j = 0; j < 4; ++j) {
                current_ray_walls[j] = rayCast(unit->pos, angles[(i*4) + j], info, current_ray_walls);
            }
            scores[i] = rayScore(current_ray_walls);
        }

        return isChokepoint(scores);
    }

    bool isChokepoint(const Scores &scores) {
        //some threshold value 

    }

private:
    //Uses DDA (Digital Differential Analysis) to find distance between current position
    //and wall in a certain direction

    //https://www.geeksforgeeks.org/dda-line-generation-algorithm-computer-graphics/
    //https://lodev.org/cgtutor/raycasting.html
    //https://www.youtube.com/watch?v=UeQZTDOVCDI

    const std::array<float, 16> angles = {
       0.0, 22.5, 45.0, 67.5, 90.0, 112.5, 135.0, 157.5, 180.0, 202.5, 225.0, 247.5, 270.0, 292.5, 315.0, 337.5
    };

    //calculates the distance to the wall from a single raycast at a specific angle
    float rayCast(sc2::Point2D original_pos, float angleDegrees, const sc2::GameInfo &info, Walls &walls) {
        sc2::PathingGrid pathing_grid = sc2::PathingGrid(info);
        // Convert angle from degrees to radians
        float angleRadians = angleDegrees * (M_PI / 180.0);
        
        //the line will travel across the whole map until it hits a wall
        float dx = 100 * std::cos(angleRadians);
        float dy = 100 * std::sin(angleRadians);

        // If dx > dy we will take step as dx 
        // else we will take step as dy to draw the complete 
        // line 
        int steps = static_cast<int>(std::max(std::abs(dx), std::abs(dy)));

        // Calculate the increments in x and y
        float xIncrement = dx / steps;
        float yIncrement = dy / steps;

        // Initialize the current position
        float x = original_pos.x;
        float y = original_pos.y;

        for (int i = 0; i <= steps; ++i) {
            // Round the current position to the nearest integer
            int roundedX = static_cast<int>(x + 0.5);
            int roundedY = static_cast<int>(y + 0.5);

            // Output the current position
            std::cout << "(" << roundedX << ", " << roundedY << ")" << std::endl;

            // Update the current position
            x += xIncrement;
            y += yIncrement;
            if(!pathing_grid.IsPathable(sc2::Point2DI(x, y))) {
                return sc2::Distance2D(sc2::Point2D(original_pos.x, original_pos.y), sc2::Point2D(x, y));
            }
            
        }
        //false flag value
        return -1.0;
    }
    float rayScore(Walls walls) {
        //float score;

        float ray1 = walls[0] + walls[2];
        float ray2 = walls[1] + walls[3];

        float long_ray = std::max(ray1, ray2);
        float short_ray = std::min(ray1, ray2);


        //SOME FACTORS FOR POTENTIAL CHOKEPOINT
        /*
        -high ratio of longer ray to shorter ray
        -both sides of the long ray are similar length
        
        NOT DONE YET
        
        */
       float length_ratio = long_ray / short_ray;
        
        return length_ratio;

    }
   
};

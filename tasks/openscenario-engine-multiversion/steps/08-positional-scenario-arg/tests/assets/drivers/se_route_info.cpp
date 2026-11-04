/*
 * se_route_info - SE_GetRoadInfoAlongRoute, contrasted with the already
 * existing SE_GetRoadInfoAtDistance at the same lookahead distance.
 *
 * usage: se_route_info <xosc> <lookahead_distance>
 *
 * The scenario fixtures park one entity on the approach road to a junction and
 * give it an inline route. Looking ahead past the junction, the two entry
 * points answer differently: the plain lookahead follows the road manager's
 * own straight-ahead choice, the route lookahead follows the assigned route.
 * The driver prints both, so a case can assert the divergence rather than a
 * single number.
 *
 * Only the fields present in SE_RoadInfo at the starting release are read;
 * the struct grew two members at the target release and neither is part of
 * what this step is about.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: %s <xosc> <lookahead_distance>\n", argv[0]);
        return 2;
    }
    float d = static_cast<float>(std::atof(argv[2]));

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    /* Init actions -- including the route assignment -- are applied on the
     * first steps. Fixed steps keep the whole thing deterministic. */
    for (int i = 0; i < 3; i++)
    {
        SE_StepDT(0.05f);
    }

    SE_RoadInfo straight;
    SE_RoadInfo route;
    /* Zero the structs so a member the implementation forgot to fill reads as
     * 0 rather than as stack garbage. */
    for (unsigned int i = 0; i < sizeof(straight); i++)
    {
        reinterpret_cast<char *>(&straight)[i] = 0;
        reinterpret_cast<char *>(&route)[i]    = 0;
    }

    int rc_straight = SE_GetRoadInfoAtDistance(0, d, &straight, 0, true);
    int rc_route    = SE_GetRoadInfoAlongRoute(0, d, &route, 0, true);

    std::printf("rc_straight=%d\n", rc_straight);
    std::printf("road_straight=%u\n", static_cast<unsigned int>(straight.roadId));
    std::printf("lane_straight=%d\n", straight.laneId);
    std::printf("s_straight=%.2f\n", static_cast<double>(straight.s));
    std::printf("x_straight=%.2f\n", static_cast<double>(straight.global_pos_x));
    std::printf("y_straight=%.2f\n", static_cast<double>(straight.global_pos_y));

    std::printf("rc_route=%d\n", rc_route);
    std::printf("rc_route_sign=%d\n", rc_route < 0 ? -1 : 1);
    std::printf("road_route=%u\n", static_cast<unsigned int>(route.roadId));
    std::printf("lane_route=%d\n", route.laneId);
    std::printf("s_route=%.2f\n", static_cast<double>(route.s));
    std::printf("x_route=%.2f\n", static_cast<double>(route.global_pos_x));
    std::printf("y_route=%.2f\n", static_cast<double>(route.global_pos_y));
    std::printf("same_road=%d\n", straight.roadId == route.roadId ? 1 : 0);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

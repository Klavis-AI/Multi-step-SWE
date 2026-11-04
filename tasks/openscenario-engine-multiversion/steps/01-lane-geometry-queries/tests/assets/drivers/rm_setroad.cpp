/*
 * rm_setroad - RM_SetRoadId: change which road a position belongs to while
 * keeping its world x,y.
 *
 * usage: rm_setroad <odr> <x> <y> <target_road_id>
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <x> <y> <target_road_id>\n", argv[0]);
        return 2;
    }
    int rc = RM_Init(argv[1]);
    std::printf("init=%d\n", rc);
    if (rc != 0)
        return 1;

    int   h      = RM_CreatePosition();
    float x      = static_cast<float>(std::atof(argv[2]));
    float y      = static_cast<float>(std::atof(argv[3]));
    int   target = std::atoi(argv[4]);

    RM_SetWorldXYHPosition(h, x, y, 0.0f);

    RM_PositionData before;
    RM_GetPositionData(h, &before);
    std::printf("road_before=%d\n", before.roadId);
    std::printf("x_before=%.3f\n", static_cast<double>(before.x));
    std::printf("y_before=%.3f\n", static_cast<double>(before.y));

    std::printf("set_rc=%d\n", RM_SetRoadId(h, target));

    RM_PositionData after;
    RM_GetPositionData(h, &after);
    std::printf("road_after=%d\n", after.roadId);
    std::printf("x_after=%.3f\n", static_cast<double>(after.x));
    std::printf("y_after=%.3f\n", static_cast<double>(after.y));

    RM_Close();
    return 0;
}

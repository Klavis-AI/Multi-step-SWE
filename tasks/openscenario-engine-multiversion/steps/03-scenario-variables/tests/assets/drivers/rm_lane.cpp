/*
 * rm_lane - the road-manager lane-geometry queries.
 *
 * usage: rm_lane <odr> <road_id> <lane_id> <s>
 *
 * Every query is asked twice: once by explicit (road, lane, s) and once
 * through a position handle placed at the same spot, because those are two
 * separate entry points that must agree.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s>\n", argv[0]);
        return 2;
    }
    const char *odr     = argv[1];
    int         road_id = std::atoi(argv[2]);
    int         lane_id = std::atoi(argv[3]);
    float       s       = static_cast<float>(std::atof(argv[4]));

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    if (rc != 0)
        return 1;

    std::printf("nroads=%d\n", RM_GetNumberOfRoads());
    std::printf("nlanes=%d\n", RM_GetRoadNumberOfLanes(road_id, s));

    /* by explicit road id */
    std::printf("width_by_road=%.4f\n", static_cast<double>(RM_GetLaneWidthByRoadId(road_id, lane_id, s)));
    std::printf("type_by_road=%d\n", RM_GetLaneTypeByRoadId(road_id, lane_id, s));

    /* through a position handle placed at the same (road, lane, s) */
    int h = RM_CreatePosition();
    std::printf("handle=%d\n", h);
    std::printf("set_lane=%d\n", RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true));

    RM_PositionData d;
    RM_GetPositionData(h, &d);
    std::printf("pos_road=%d\n", d.roadId);
    std::printf("pos_lane=%d\n", d.laneId);
    std::printf("pos_s=%.3f\n", static_cast<double>(d.s));

    std::printf("width_handle=%.4f\n", static_cast<double>(RM_GetLaneWidth(h, lane_id)));
    std::printf("type_handle=%d\n", RM_GetLaneType(h, lane_id));

    RM_Close();
    return 0;
}

/*
 * rm_core_inherited - the road-manager queries that step 01 already
 * established and that must still pass here.
 *
 * usage: rm_core_inherited <odr> <road_id> <lane_id> <s>
 *
 * This driver is compiled and run against BOTH the starting tag and the target
 * tag. It exercises only entry points whose signatures did not change at this
 * step, so a tree at v2.46.4 and a tree at v2.51.1 both compile it and both
 * pass it.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s>\n", argv[0]);
        return 2;
    }
    const char *odr     = argv[1];
    id_t        road_id = static_cast<id_t>(std::atoi(argv[2]));
    int         lane_id = std::atoi(argv[3]);
    float       s       = static_cast<float>(std::atof(argv[4]));

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int n = RM_GetNumberOfRoads();
    std::printf("nroads=%d\n", n);

    int ok_ids = 1;
    for (int i = 0; i < n; i++)
    {
        id_t id = RM_GetIdOfRoadFromIndex(static_cast<unsigned int>(i));
        if (id == static_cast<id_t>(-1) || RM_GetRoadLength(id) <= 0.0f)
        {
            ok_ids = 0;
        }
        if (i == 0)
        {
            std::printf("first_road_id=%d\n", static_cast<int>(id));
        }
    }
    std::printf("all_roads_have_length=%d\n", ok_ids);
    std::printf("road_length=%.3f\n", static_cast<double>(RM_GetRoadLength(road_id)));

    int h = RM_CreatePosition();
    std::printf("handle=%d\n", h);

    int set_rc = RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true);
    std::printf("set_lane_ok=%d\n", set_rc >= 0 ? 1 : 0);

    RM_PositionData d;
    if (RM_GetPositionData(h, &d) != 0)
    {
        std::printf("get_position=0\n");
        return 1;
    }
    std::printf("get_position=1\n");
    std::printf("pos_road=%d\n", static_cast<int>(d.roadId));
    std::printf("pos_lane=%d\n", d.laneId);
    std::printf("pos_s=%.3f\n", static_cast<double>(d.s));

    /* The type query did not change signature. */
    std::printf("lane_type=%d\n", RM_GetLaneType(h, lane_id));
    std::printf("lane_type_by_road=%d\n", RM_GetLaneTypeByRoadId(road_id, lane_id, s));

    /* Round-trip through world coordinates. */
    int h2 = RM_CreatePosition();
    std::printf("xyh_ok=%d\n", RM_SetWorldXYHPosition(h2, d.x, d.y, d.h) >= 0 ? 1 : 0);
    RM_PositionData d2;
    RM_GetPositionData(h2, &d2);
    std::printf("roundtrip_road=%d\n", static_cast<int>(d2.roadId));
    std::printf("roundtrip_lane=%d\n", d2.laneId);

    /* Overlap queries. */
    std::printf("n_overlap=%d\n", RM_GetNumberOfRoadsOverlapping(h));
    std::printf("overlap_0=%d\n", static_cast<int>(RM_GetOverlappingRoadId(h, 0)));

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

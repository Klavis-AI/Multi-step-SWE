/* rm_core.cpp - road-manager entry points that already existed at the step's
 * starting release. This is the inherited-regression driver: nothing it calls
 * is new at the target release, so a tree that still behaves like its
 * predecessor passes it and a tree that broke the road manager while adding
 * the new queries does not.
 *
 * usage: rm_core <odr> <road_id> <lane_id> <s>
 */
#include <cstdio>
#include <cstdlib>
#include "esminiRMLib.hpp"

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s>\n", argv[0]);
        return 2;
    }
    const char* odr     = argv[1];
    int         road_id = std::atoi(argv[2]);
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

    /* Every index in range must map to a road id, and that id must have a
     * positive length. The ids themselves are network data, not behaviour, so
     * only the first one is printed for an exact assertion. */
    int ok_ids = 1;
    for (int i = 0; i < n; i++)
    {
        int id = RM_GetIdOfRoadFromIndex(i);
        if (id < 0 || RM_GetRoadLength(id) <= 0.0f)
        {
            ok_ids = 0;
        }
        if (i == 0)
        {
            std::printf("first_road_id=%d\n", id);
        }
    }
    std::printf("all_roads_have_length=%d\n", ok_ids);
    std::printf("road_length=%.3f\n", static_cast<double>(RM_GetRoadLength(road_id)));
    std::printf("nlanes=%d\n", RM_GetRoadNumberOfLanes(road_id, s));

    int h = RM_CreatePosition();
    std::printf("handle=%d\n", h);

    /* The road-manager return code is widened at the target release: 0 still
     * means OK, but 1 and 2 report "moved into a new road segment" and "moved
     * into a junction and made a choice". Anything >= 0 is success from here
     * on, and a check written as `== 0` would be a latent failure. */
    int set_rc = RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true);
    std::printf("set_lane_ok=%d\n", set_rc >= 0 ? 1 : 0);

    RM_PositionData d;
    if (RM_GetPositionData(h, &d) != 0)
    {
        std::printf("get_position=0\n");
        return 1;
    }
    std::printf("get_position=1\n");
    std::printf("pos_road=%d\n", d.roadId);
    std::printf("pos_lane=%d\n", d.laneId);
    std::printf("pos_s=%.3f\n", static_cast<double>(d.s));

    /* Round-tripping the world coordinates back through the road manager must
     * land on the same road and lane. */
    int h2 = RM_CreatePosition();
    std::printf("xyh_ok=%d\n", RM_SetWorldXYHPosition(h2, d.x, d.y, d.h) >= 0 ? 1 : 0);
    RM_PositionData d2;
    RM_GetPositionData(h2, &d2);
    std::printf("roundtrip_road=%d\n", d2.roadId);
    std::printf("roundtrip_lane=%d\n", d2.laneId);

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

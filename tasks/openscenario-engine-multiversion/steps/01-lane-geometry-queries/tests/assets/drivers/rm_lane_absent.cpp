/*
 * rm_lane_absent - the answers the two lane-geometry queries give when there
 * is nothing to answer about.
 *
 * usage: rm_lane_absent <odr> <road_id> <lane_id> <s> <absent_lane> <absent_road>
 *
 * instruction.md sections 1 and 2 state these: a lane id the road does not
 * have, a road id the network does not contain and a handle that was created
 * but never placed all report width 0.0 and a negative lane type. That is the
 * negative half of the same contract the positive cases check, and it is what
 * separates "returns the right number when asked a sensible question" from
 * "has a defined domain".
 *
 * This is a separate program from rm_lane on purpose. rm_lane reports the road's
 * lane count for whatever road id it is given, and at this release
 * RM_GetRoadNumberOfLanes and RM_SetLanePosition both fault on a road id the
 * network does not contain -- inherited behaviour this step neither changes
 * nor scores. So the absent-road probe has to be made through the ByRoadId
 * entry points alone, with the handle kept on a road that exists, which is
 * exactly what this driver does.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s> <absent_lane> <absent_road>\n", argv[0]);
        return 2;
    }
    const char *odr          = argv[1];
    int         road_id      = std::atoi(argv[2]);
    int         lane_id      = std::atoi(argv[3]);
    float       s            = static_cast<float>(std::atof(argv[4]));
    int         absent_lane  = std::atoi(argv[5]);
    int         absent_road  = std::atoi(argv[6]);

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    if (rc != 0)
        return 1;

    std::printf("nroads=%d\n", RM_GetNumberOfRoads());

    /* A handle that exists but has never been placed. Asked about any lane at
       all, it has no road and no s to answer from. */
    int hu = RM_CreatePosition();
    std::printf("unplaced_handle=%d\n", hu);
    std::printf("unplaced_width=%.4f\n", static_cast<double>(RM_GetLaneWidth(hu, lane_id)));
    std::printf("unplaced_type=%d\n", RM_GetLaneType(hu, lane_id));

    /* From here on the handle is on a real lane of a real road, so anything
       that goes wrong below is the query and not the placement. */
    int h = RM_CreatePosition();
    std::printf("set_lane=%d\n", RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true));
    RM_PositionData d;
    RM_GetPositionData(h, &d);
    std::printf("pos_road=%d\n", d.roadId);
    std::printf("pos_lane=%d\n", d.laneId);

    /* The same handle and the same road, asked about a lane the road has not
       got. */
    std::printf("absent_lane_width_handle=%.4f\n", static_cast<double>(RM_GetLaneWidth(h, absent_lane)));
    std::printf("absent_lane_type_handle=%d\n", RM_GetLaneType(h, absent_lane));
    std::printf("absent_lane_width_by_road=%.4f\n",
                static_cast<double>(RM_GetLaneWidthByRoadId(road_id, absent_lane, s)));
    std::printf("absent_lane_type_by_road=%d\n", RM_GetLaneTypeByRoadId(road_id, absent_lane, s));

    /* A road id the network does not contain, through the ByRoadId forms only. */
    std::printf("absent_road_width=%.4f\n", static_cast<double>(RM_GetLaneWidthByRoadId(absent_road, lane_id, s)));
    std::printf("absent_road_type=%d\n", RM_GetLaneTypeByRoadId(absent_road, lane_id, s));

    /* And the real lane still answers, so a build that simply returned the
       failure answer to everything does not pass by accident. */
    std::printf("present_width=%.4f\n", static_cast<double>(RM_GetLaneWidthByRoadId(road_id, lane_id, s)));
    std::printf("present_type=%d\n", RM_GetLaneTypeByRoadId(road_id, lane_id, s));

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

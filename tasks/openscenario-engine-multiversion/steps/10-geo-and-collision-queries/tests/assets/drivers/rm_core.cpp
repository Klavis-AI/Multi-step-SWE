/* rm_core.cpp - RM_ road-manager core queries, present since well before this
 * step's start tag. Regression material only: every value asserted here must
 * be measured identical at both ends of the step.
 *
 * usage: rm_core <road.xodr> <roadId> <laneId> <s>
 */
#include <cstdio>
#include <cstdlib>
#include "esminiRMLib.hpp"

int main(int argc, char** argv)
{
    if (argc < 5) { std::fprintf(stderr, "usage: %s <road.xodr> <roadId> <laneId> <s>\n", argv[0]); return 2; }

    int roadId = std::atoi(argv[2]);
    int laneId = std::atoi(argv[3]);
    float s = static_cast<float>(std::atof(argv[4]));

    int rc = RM_Init(argv[1]);
    std::printf("init=%d\n", rc);
    if (rc != 0) return 1;

    int nroads = RM_GetNumberOfRoads();
    std::printf("nroads=%d\n", nroads);

    int firstId = RM_GetIdOfRoadFromIndex(0);
    std::printf("first_road_id=%d\n", firstId);

    float len = RM_GetRoadLength(firstId);
    std::printf("road_length=%.3f\n", len);

    int nlanes = RM_GetRoadNumberOfLanes(roadId, s, -1);
    std::printf("nlanes=%d\n", nlanes);

    int handle = RM_CreatePosition();
    std::printf("handle=%d\n", handle);

    int setrc = RM_SetLanePosition(handle, roadId, laneId, 0, s, true);
    std::printf("set_lane_ok=%d\n", setrc >= 0);

    RM_PositionData pd;
    int getrc = RM_GetPositionData(handle, &pd);
    std::printf("get_position=%d\n", getrc >= 0);
    std::printf("pos_road=%d pos_lane=%d pos_s=%.3f\n", pd.roadId, pd.laneId, pd.s);

    double width = 0.0;
    int wrc = RM_GetLaneWidth(handle, laneId, &width);
    std::printf("width_rc=%d width=%.4f\n", wrc >= 0, width);

    RM_DeletePosition(handle);
    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

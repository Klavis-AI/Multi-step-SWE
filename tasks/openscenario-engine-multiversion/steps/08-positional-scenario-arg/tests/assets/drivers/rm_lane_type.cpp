/*
 * rm_lane_type - the lane-type field this step adds to the road-manager
 * lane-info struct.
 *
 * usage: rm_lane_type <odr> <road_id> <lane_id> <s>
 *
 * RM_RoadLaneInfo.lane_type is a struct member, so it is invisible to the
 * dynamic symbol table and a tree that has not added it fails to compile here.
 * The value is cross-checked against RM_GetLaneTypeByRoadId, which reports the
 * same lane's type through a separate entry point that predates this step:
 * the new field has to agree with the old query, not merely exist.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiRMLib.hpp"

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s>\n", argv[0]);
        return 2;
    }
    const char* odr     = argv[1];
    id_t        road_id = static_cast<id_t>(std::atoi(argv[2]));
    int         lane_id = std::atoi(argv[3]);
    float       s       = static_cast<float>(std::atof(argv[4]));

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    std::fflush(stdout);
    if (rc != 0)
    {
        return 1;
    }

    std::printf("type_by_road=%d\n", RM_GetLaneTypeByRoadId(road_id, lane_id, s));
    std::fflush(stdout);

    int h = RM_CreatePosition();
    // Normalised the way rm_core.cpp and rm_lane.cpp do it: the road manager
    // reports 1 and 2 alongside 0 on success, so the case may only see the sign.
    std::printf("set_lane_ok=%d\n", RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true) >= 0 ? 1 : 0);
    std::fflush(stdout);

    RM_RoadLaneInfo li;
    std::memset(&li, 0, sizeof(li));
    std::printf("lane_info_rc=%d\n", RM_GetLaneInfo(h, 0.0f, &li, 0, true));
    std::printf("info_lane=%d\n", li.laneId);
    std::printf("info_lane_type=%d\n", li.lane_type);
    std::fflush(stdout);

    RM_RoadProbeInfo pi;
    std::memset(&pi, 0, sizeof(pi));
    std::printf("probe_rc=%d\n", RM_GetProbeInfo(h, 5.0f, &pi, 0, true));
    std::printf("probe_lane_type=%d\n", pi.road_lane_info.lane_type);
    std::fflush(stdout);

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

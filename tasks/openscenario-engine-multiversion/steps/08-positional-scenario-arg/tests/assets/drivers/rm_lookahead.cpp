/*
 * rm_lookahead - two road-manager behaviours observed from one placed
 * position: the world coordinates the position lands on, and what the
 * lookahead reports when the lookahead distance runs off the end of the road.
 *
 * usage: rm_lookahead <odr> <road_id> <lane_id> <s> <lookahead>
 *
 * Deliberately written so it compiles at BOTH ends of this step: it touches
 * no struct member and no entry point that changes shape here. The separation
 * is in the RETURNED VALUES, not in whether the file builds, which is what
 * makes it a test of behaviour rather than of a header.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiRMLib.hpp"

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s> <lookahead>\n", argv[0]);
        return 2;
    }
    const char* odr       = argv[1];
    id_t        road_id   = static_cast<id_t>(std::atoi(argv[2]));
    int         lane_id   = std::atoi(argv[3]);
    float       s         = static_cast<float>(std::atof(argv[4]));
    float       lookahead = static_cast<float>(std::atof(argv[5]));

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    std::fflush(stdout);
    if (rc != 0)
    {
        return 1;
    }

    int h = RM_CreatePosition();
    std::printf("handle=%d\n", h);
    std::printf("set_lane_rc=%d\n", RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true));
    std::fflush(stdout);

    /* Where the lane centre actually is in world coordinates. On a road with
     * superelevation this is the whole question: a road that is rotated puts
     * the lane centre closer to the reference line than its flat width, a
     * road whose edges are merely raised leaves y at the flat value. */
    RM_PositionData pd;
    std::memset(&pd, 0, sizeof(pd));
    RM_GetPositionData(h, &pd);
    std::printf("pos_x=%.6f\n", static_cast<double>(pd.x));
    std::printf("pos_y=%.6f\n", static_cast<double>(pd.y));
    std::printf("pos_z=%.6f\n", static_cast<double>(pd.z));
    std::printf("pos_r=%.6f\n", static_cast<double>(pd.r));
    std::fflush(stdout);

    RM_RoadProbeInfo pi;
    std::memset(&pi, 0, sizeof(pi));
    int probe_rc = RM_GetProbeInfo(h, lookahead, &pi, 0, true);
    std::printf("probe_rc=%d\n", probe_rc);
    std::printf("probe_road=%u\n", static_cast<unsigned int>(pi.road_lane_info.roadId));
    std::printf("probe_lane=%d\n", pi.road_lane_info.laneId);
    std::printf("probe_s=%.3f\n", static_cast<double>(pi.road_lane_info.s));
    std::fflush(stdout);

    RM_RoadLaneInfo li;
    std::memset(&li, 0, sizeof(li));
    int lane_rc = RM_GetLaneInfo(h, lookahead, &li, 0, true);
    std::printf("lane_rc=%d\n", lane_rc);
    std::printf("lane_s=%.3f\n", static_cast<double>(li.s));
    std::fflush(stdout);

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

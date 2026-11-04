/* rm_probe - the road-manager lookahead and the position setters, at 64-bit.
 *
 * usage: rm_probe <odr> <road_id> <lane_id> <s> <lookahead> <heading>
 *
 * This is the half of esminiRMLib that rm_double does not cover: the two
 * lookahead queries (RM_GetLaneInfo, RM_GetProbeInfo), the road/s/heading
 * setters (RM_SetRoadPosition, RM_SetS, RM_SetH, RM_SetWorldPositionMode,
 * RM_PositionMoveForward) and the structs they fill in.
 *
 * Compile time: static assertions on the declared distance and out-struct
 * member types.
 *
 * Run time: an s value that needs more than 24 mantissa bits is pushed in
 * through RM_SetS and read straight back out of RM_GetPositionData, and the
 * probe is asked for a lookahead at a distance that is itself not exactly
 * representable in 32 bits. On a straight road the probe's s is the position's
 * s plus the lookahead, to the last bit; a narrowed path loses the eighth
 * significant digit.
 */
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "esminiRMLib.hpp"

/* Lookahead distance is 64-bit on both queries. */
static_assert(std::is_same<decltype(&RM_GetLaneInfo), int (*)(int, double, RM_RoadLaneInfo*, int, bool)>::value,
              "RM_GetLaneInfo must take a double lookahead distance");
static_assert(std::is_same<decltype(&RM_GetProbeInfo), int (*)(int, double, RM_RoadProbeInfo*, int, bool)>::value,
              "RM_GetProbeInfo must take a double lookahead distance");

/* Position setters are 64-bit. */
static_assert(std::is_same<decltype(&RM_SetRoadPosition), int (*)(int, id_t, double, double, bool)>::value,
              "RM_SetRoadPosition must take (int, id_t, double s, double t, bool)");
static_assert(std::is_same<decltype(&RM_SetS), int (*)(int, double)>::value, "RM_SetS must take a double s");
static_assert(std::is_same<decltype(&RM_SetH), int (*)(int, double)>::value, "RM_SetH must take a double heading");
static_assert(std::is_same<decltype(&RM_SetHMode), int (*)(int, double, int)>::value, "RM_SetHMode must take a double heading");
static_assert(std::is_same<decltype(&RM_SetWorldPositionMode), int (*)(int, double, double, double, double, double, double, int)>::value,
              "RM_SetWorldPositionMode must take six doubles");
static_assert(std::is_same<decltype(&RM_PositionMoveForward), int (*)(int, double, double)>::value,
              "RM_PositionMoveForward must take (int, double dist, double angle)");
static_assert(std::is_same<decltype(&RM_GetRoadNumberOfLanes), int (*)(id_t, double, int)>::value,
              "RM_GetRoadNumberOfLanes must take a double s");
static_assert(std::is_same<decltype(&RM_GetRoadNumberOfDrivableLanes), int (*)(id_t, double)>::value,
              "RM_GetRoadNumberOfDrivableLanes must take a double s");

/* And so are the structs they fill in. */
static_assert(std::is_same<decltype(RM_PositionXYZ::x), double>::value, "RM_PositionXYZ::x must be double");
static_assert(std::is_same<decltype(RM_PositionData::s), double>::value, "RM_PositionData::s must be double");
static_assert(std::is_same<decltype(RM_PositionData::laneOffset), double>::value, "RM_PositionData::laneOffset must be double");
static_assert(std::is_same<decltype(RM_RoadLaneInfo::s), double>::value, "RM_RoadLaneInfo::s must be double");
static_assert(std::is_same<decltype(RM_RoadLaneInfo::width), double>::value, "RM_RoadLaneInfo::width must be double");
static_assert(std::is_same<decltype(RM_RoadLaneInfo::curvature), double>::value, "RM_RoadLaneInfo::curvature must be double");
static_assert(std::is_same<decltype(RM_RoadProbeInfo::relative_h), double>::value, "RM_RoadProbeInfo::relative_h must be double");
static_assert(std::is_same<decltype(RM_PositionDiff::ds), double>::value, "RM_PositionDiff::ds must be double");

int main(int argc, char** argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s> <lookahead> <heading>\n", argv[0]);
        return 2;
    }
    const int    road      = std::atoi(argv[2]);
    const int    lane      = std::atoi(argv[3]);
    const double s         = std::atof(argv[4]);
    const double lookahead = std::atof(argv[5]);
    const double heading   = std::atof(argv[6]);

    std::printf("init=%d\n", RM_Init(argv[1]));
    std::printf("nroads=%d\n", RM_GetNumberOfRoads());

    int h = RM_CreatePosition();
    std::printf("handle_ok=%d\n", h >= 0 ? 1 : 0);

    /* Road position setter, then the s setter with a value that needs more
     * than 24 mantissa bits. */
    std::printf("setroad=%d\n", RM_SetRoadPosition(h, static_cast<id_t>(road), 1.0, 0.0, true) >= 0 ? 1 : 0);
    std::printf("setlane=%d\n", RM_SetLanePosition(h, static_cast<id_t>(road), lane, 0.0, 1.0, true) >= 0 ? 1 : 0);
    std::printf("sets=%d\n", RM_SetS(h, s) >= 0 ? 1 : 0);

    RM_PositionData d;
    std::printf("getpos=%d\n", RM_GetPositionData(h, &d) >= 0 ? 1 : 0);
    std::printf("pos_s=%.17g\n", d.s);
    std::printf("pos_x=%.17g\n", d.x);
    std::printf("pos_lane=%d\n", d.laneId);

    /* Heading setter at 64 bits. */
    std::printf("seth=%d\n", RM_SetH(h, heading) >= 0 ? 1 : 0);
    RM_PositionData d2;
    RM_GetPositionData(h, &d2);
    std::printf("pos_h=%.17g\n", d2.h);

    /* Lane info and probe at a non-representable lookahead distance. */
    RM_RoadLaneInfo li;
    std::printf("laneinfo=%d\n", RM_GetLaneInfo(h, lookahead, &li, 0, true) >= 0 ? 1 : 0);
    std::printf("li_s=%.17g\n", li.s);
    std::printf("li_width=%.17g\n", li.width);
    std::printf("li_curv=%.17g\n", li.curvature);

    RM_RoadProbeInfo pi;
    std::printf("probe=%d\n", RM_GetProbeInfo(h, lookahead, &pi, 0, true) >= 0 ? 1 : 0);
    std::printf("probe_s=%.17g\n", pi.road_lane_info.s);
    std::printf("probe_local_x=%.17g\n", pi.relative_pos.x);
    std::printf("probe_rel_h=%.17g\n", pi.relative_h);

    /* Position difference between two handles, reported at 64 bits. */
    int h2 = RM_CreatePosition();
    RM_SetLanePosition(h2, static_cast<id_t>(road), lane, 0.0, 1.0, true);
    RM_SetS(h2, s);
    RM_PositionDiff diff;
    std::printf("diff_rc=%d\n", RM_SubtractAFromB(h, h2, &diff));
    std::printf("diff_ds=%.17g\n", diff.ds);

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

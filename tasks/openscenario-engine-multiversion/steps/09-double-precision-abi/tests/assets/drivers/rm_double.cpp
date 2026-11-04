/* rm_double - road-manager queries at 64-bit precision.
 *
 * usage: rm_double <odr> <road_id> <lane_id> <s> <x> <y>
 *
 * RM_GetLaneWidth, RM_GetLaneWidthByRoadId, RM_GetRoadLength, RM_GetSpeedLimit
 * and the world-position setters all take or return double, and the values they
 * report must round-trip to a tolerance no 32-bit representation can match.
 *
 * The signature assertion is two-level: a static_assert on the declared
 * parameter types, and a runtime check that the reported value preserves the
 * input precision. A build that narrows anywhere returns the wrong number.
 */
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "esminiRMLib.hpp"

static_assert(std::is_same<decltype(RM_GetRoadLength(0u)), double>::value, "RM_GetRoadLength must return double");
static_assert(std::is_same<decltype(RM_GetSpeedLimit(0)), double>::value, "RM_GetSpeedLimit must return double");
static_assert(std::is_same<decltype(&RM_GetLaneWidth), int (*)(int, int, double*)>::value,
              "RM_GetLaneWidth must take double* out-parameter");
static_assert(std::is_same<decltype(&RM_GetLaneWidthByRoadId), int (*)(id_t, int, double, double*)>::value,
              "RM_GetLaneWidthByRoadId must take (id_t, int, double s, double* width)");

int main(int argc, char** argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s> <x> <y>\n", argv[0]);
        return 2;
    }
    const int    road = std::atoi(argv[2]);
    const int    lane = std::atoi(argv[3]);
    const double s    = std::atof(argv[4]);
    const double x    = std::atof(argv[5]);
    const double y    = std::atof(argv[6]);

    std::printf("init=%d\n", RM_Init(argv[1]));
    std::printf("nroads=%d\n", RM_GetNumberOfRoads());

    /* Road length as double. */
    double len = RM_GetRoadLength((id_t)road);
    std::printf("road_length=%.17g\n", len);

    /* Lane width as double* out-parameter. */
    double wbr = -1.0;
    std::printf("wbr_rc=%d\n", RM_GetLaneWidthByRoadId((id_t)road, lane, s, &wbr));
    std::printf("width_by_road=%.17g\n", wbr);

    int    h = RM_CreatePosition();
    double w = -1.0;
    /* Position setters answer >= 0 on success: 1 and 2 are successes too, so
     * the sign is the contract and the exact code is not. Normalised here for
     * the same reason rm_probe.cpp and rm_core.cpp normalise theirs. */
    std::printf("set_lane=%d\n", RM_SetLanePosition(h, (id_t)road, lane, 0.0, s, true) >= 0 ? 1 : 0);
    std::printf("w_rc=%d\n", RM_GetLaneWidth(h, lane, &w));
    std::printf("width_handle=%.17g\n", w);

    /* The documented ERROR half of the same convention: these two query entry
     * points answer exactly 0 on success and a negative code on any error.
     * Road id 99999 is in neither fixture, and h + 1000 is not a position
     * handle the road manager ever handed out, so both calls take the error
     * path. Measured at v3.0.4 on both fixtures: each answers -1, leaves the
     * out-parameter untouched and does not abort. The case asserts the SIGN
     * only - the documents promise a negative code, not a magnitude, so a
     * solution answering -2 is equally correct.
     *
     * Note for anyone extending this: an out-of-range LANE id on an already
     * positioned handle is NOT an error path here - RM_GetLaneWidth(h, 99, ..)
     * answers 0, consistent with the documented rule that a zero-width lane is
     * a success and not an error. Measured; do not assert a negative there. */
    double bad_by_road = -12345.0;
    std::printf("wbr_bad_rc=%d\n", RM_GetLaneWidthByRoadId((id_t)99999, lane, s, &bad_by_road));
    double bad_by_handle = -12345.0;
    std::printf("w_bad_handle_rc=%d\n", RM_GetLaneWidth(h + 1000, lane, &bad_by_handle));

    /* The same lane width, read through the lookahead entry point at zero
     * lookahead. Both doors report one road model, so the two numbers are the
     * same double; an implementation that narrows anywhere between the road
     * model and either entry point makes them disagree by about 7e-8. */
    RM_RoadLaneInfo li;
    std::printf("li_rc=%d\n", RM_GetLaneInfo(h, 0.0, &li, 0, true) >= 0 ? 1 : 0);
    std::printf("li_width=%.17g\n", li.width);
    std::printf("li_s=%.17g\n", li.s);

    /* World position at double precision: the reported x must equal the input. */
    std::printf("setxyh=%d\n", RM_SetWorldXYHPosition(h, x, y, 0.0) >= 0 ? 1 : 0);
    RM_PositionData d;
    std::printf("getpos=%d\n", RM_GetPositionData(h, &d) >= 0 ? 1 : 0);
    std::printf("pos_x=%.17g\n", d.x);
    std::printf("pos_s=%.17g\n", d.s);

    std::printf("speed_limit=%.17g\n", RM_GetSpeedLimit(h));

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

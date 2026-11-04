/* rm_core - the road-manager answers that do NOT change across this step.
 *
 * usage: rm_core <odr> <road_id> <lane_id> <s>
 *
 * This driver is deliberately width-agnostic: it must compile against the
 * pre-migration header (32-bit out-parameters) and against the post-migration
 * header (64-bit out-parameters) alike, because the values it checks are the
 * same numbers before and after. Two techniques make that possible:
 *
 *   - arguments are passed as double literals and let the implicit conversion
 *     to whatever the declaration says happen at the call;
 *   - the one out-parameter whose pointee type changed is declared through a
 *     trait that reads the type back out of the declaration itself, so the
 *     driver never spells `float` or `double` for it.
 *
 * Everything is printed as a double so the case can compare numerically
 * without caring how many bits carried it.
 */
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "esminiRMLib.hpp"

/* Pointee type of the third parameter of a three-parameter function. */
template <typename F>
struct out_param;
template <typename R, typename A, typename B, typename C>
struct out_param<R (*)(A, B, C*)>
{
    using type = C;
};
using width_t = out_param<decltype(&RM_GetLaneWidth)>::type;

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s>\n", argv[0]);
        return 2;
    }
    const int    road = std::atoi(argv[2]);
    const int    lane = std::atoi(argv[3]);
    const double s    = std::atof(argv[4]);

    std::printf("init=%d\n", RM_Init(argv[1]));
    std::printf("nroads=%d\n", RM_GetNumberOfRoads());
    std::printf("road0_id=%u\n", static_cast<unsigned>(RM_GetIdOfRoadFromIndex(0)));

    /* Road length. The return type widened but the number did not. */
    const double len = static_cast<double>(RM_GetRoadLength(static_cast<id_t>(road)));
    std::printf("road_length=%.9g\n", len);

    std::printf("ndrivable=%d\n", RM_GetRoadNumberOfDrivableLanes(static_cast<id_t>(road), s));

    int hnd = RM_CreatePosition();
    std::printf("handle_ok=%d\n", hnd >= 0 ? 1 : 0);

    /* Position setters answer >= 0 on success, and the exact code carries
     * information: 0 is a plain success, 1 means the position landed on a
     * different road than the one asked for, 2 means it made a junction
     * choice. Both are printed RAW so the case can assert the convention
     * rather than the normalised sign of it. */
    const int setlane_rc = RM_SetLanePosition(hnd, static_cast<id_t>(road), lane, 0.0, s, true);
    std::printf("setlane=%d\n", setlane_rc);
    std::printf("setlane_ok=%d\n", setlane_rc >= 0 ? 1 : 0);

    RM_PositionData d;
    std::printf("getpos=%d\n", RM_GetPositionData(hnd, &d) >= 0 ? 1 : 0);
    std::printf("pos_x=%.9g\n", static_cast<double>(d.x));
    std::printf("pos_y=%.9g\n", static_cast<double>(d.y));
    std::printf("pos_s=%.9g\n", static_cast<double>(d.s));
    std::printf("pos_lane=%d\n", d.laneId);
    std::printf("pos_road=%u\n", static_cast<unsigned>(d.roadId));

    /* The transition code, printed RAW. Moving on from this particular start
     * crosses onto another road rather than staying put, and the road manager
     * reports how it got there: the convention is `< 0` for an error and
     * `>= 0` for a success, with the non-zero codes carrying the extra
     * information. The case supplies the start this needs and asserts the
     * number the road manager actually answers. */
    if (road == 5 && lane == -1 && s < 2.0)
    {
        int cross = 0;
        for (int i = 0; i < 10 && cross == 0; i++)
        {
            cross = RM_PositionMoveForward(hnd, 2.0, 0.0);
        }
        std::printf("cross_rc=%d\n", cross);
        std::printf("cross_ok=%d\n", cross >= 0 ? 1 : 0);
        RM_PositionData dc;
        RM_GetPositionData(hnd, &dc);
        std::printf("cross_road=%u\n", static_cast<unsigned>(dc.roadId));
        std::printf("cross_s=%.9g\n", static_cast<double>(dc.s));
    }

    /* The world-coordinate round trip: feed the reported x/y back in and the
     * road manager must re-derive the same road, lane and s. */
    const double rx = static_cast<double>(d.x);
    const double ry = static_cast<double>(d.y);
    int          h2 = RM_CreatePosition();
    std::printf("setxyh=%d\n", RM_SetWorldXYHPosition(h2, rx, ry, 0.0) >= 0 ? 1 : 0);
    RM_PositionData d2;
    RM_GetPositionData(h2, &d2);
    std::printf("rt_road=%u\n", static_cast<unsigned>(d2.roadId));
    std::printf("rt_lane=%d\n", d2.laneId);
    std::printf("rt_s=%.9g\n", static_cast<double>(d2.s));

    /* Lane width through both entry points, via the deduced out type. */
    width_t w = static_cast<width_t>(-1);
    std::printf("w_rc=%d\n", RM_GetLaneWidth(hnd, lane, &w));
    std::printf("width_handle=%.9g\n", static_cast<double>(w));

    width_t wbr = static_cast<width_t>(-1);
    std::printf("wbr_rc=%d\n", RM_GetLaneWidthByRoadId(static_cast<id_t>(road), lane, s, &wbr));
    std::printf("width_by_road=%.9g\n", static_cast<double>(wbr));

    /* The centre lane is "type none, width zero" rather than an error. */
    width_t wc = static_cast<width_t>(-1);
    std::printf("wc_rc=%d\n", RM_GetLaneWidthByRoadId(static_cast<id_t>(road), 0, s, &wc));
    std::printf("width_centre=%.9g\n", static_cast<double>(wc));
    std::printf("type_centre=%d\n", RM_GetLaneTypeByRoadId(static_cast<id_t>(road), 0, s));
    std::printf("type_lane=%d\n", RM_GetLaneTypeByRoadId(static_cast<id_t>(road), lane, s));

    std::printf("speed_limit=%.9g\n", static_cast<double>(RM_GetSpeedLimit(hnd)));

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

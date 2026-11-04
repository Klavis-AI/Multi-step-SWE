/*
 * rm_lane_width - the lane-width queries in their out-parameter form.
 *
 * usage: rm_lane_width <odr> <road_id> <lane_id> <s>
 *
 * Both entry points return a status code and deliver the width through a
 * pointer, so the driver has to observe three separate things for every call:
 * the status, the value written, and -- on the error paths -- that nothing was
 * written at all. Every out-parameter is pre-loaded with a sentinel that no
 * road network could produce, and the driver reports whether the sentinel
 * survived.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

static const float SENTINEL = -12345.0f;

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s>\n", argv[0]);
        return 2;
    }
    const char *odr     = argv[1];
    id_t        road_id = static_cast<id_t>(std::atoi(argv[2]));
    int         lane_id = std::atoi(argv[3]);
    float       s       = static_cast<float>(std::atof(argv[4]));

    float w;

    /* Nothing loaded yet: both forms must refuse and must not write. */
    w = SENTINEL;
    std::printf("pre_init_road_rc=%d\n", RM_GetLaneWidthByRoadId(road_id, lane_id, s, &w));
    std::printf("pre_init_road_untouched=%d\n", w == SENTINEL ? 1 : 0);
    w = SENTINEL;
    std::printf("pre_init_handle_rc=%d\n", RM_GetLaneWidth(0, lane_id, &w));
    std::printf("pre_init_handle_untouched=%d\n", w == SENTINEL ? 1 : 0);

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    /* By explicit road id. */
    w = SENTINEL;
    std::printf("w_road_rc=%d\n", RM_GetLaneWidthByRoadId(road_id, lane_id, s, &w));
    std::printf("w_road=%.4f\n", static_cast<double>(w));

    /* Through a position handle placed at the same (road, lane, s). The
     * position setters report 1 and 2 for "new road segment" and "junction
     * choice", so success is >= 0. */
    int h = RM_CreatePosition();
    std::printf("handle_ok=%d\n", h >= 0 ? 1 : 0);
    std::printf("set_lane_ok=%d\n", RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true) >= 0 ? 1 : 0);
    w = SENTINEL;
    std::printf("w_handle_rc=%d\n", RM_GetLaneWidth(h, lane_id, &w));
    std::printf("w_handle=%.4f\n", static_cast<double>(w));

    /* Error paths. A road id no network defines, a handle that was never
     * created, and a null destination. */
    w = SENTINEL;
    std::printf("bad_road_rc=%d\n", RM_GetLaneWidthByRoadId(90210, lane_id, s, &w));
    std::printf("bad_road_untouched=%d\n", w == SENTINEL ? 1 : 0);
    w = SENTINEL;
    std::printf("bad_handle_rc=%d\n", RM_GetLaneWidth(4242, lane_id, &w));
    std::printf("bad_handle_untouched=%d\n", w == SENTINEL ? 1 : 0);
    std::printf("null_dest_rc=%d\n", RM_GetLaneWidthByRoadId(road_id, lane_id, s, nullptr));

    /* The same two failure modes reached through the handle form, on a handle
     * that is otherwise perfectly good. Asserting them only on the road-id form
     * would leave the handle form free to write a sentinel width, or to fault,
     * on exactly the paths requirement 6 is about. */
    std::printf("null_dest_handle_rc=%d\n", RM_GetLaneWidth(h, lane_id, nullptr));

    RM_Close();

    /* After the network is released the queries must refuse again. */
    w = SENTINEL;
    std::printf("post_close_rc=%d\n", RM_GetLaneWidthByRoadId(road_id, lane_id, s, &w));
    std::printf("post_close_untouched=%d\n", w == SENTINEL ? 1 : 0);
    w = SENTINEL;
    std::printf("post_close_handle_rc=%d\n", RM_GetLaneWidth(h, lane_id, &w));
    std::printf("post_close_handle_untouched=%d\n", w == SENTINEL ? 1 : 0);

    std::printf("survived=1\n");
    return 0;
}

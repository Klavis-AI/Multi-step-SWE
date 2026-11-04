/*
 * rm_snap - RM_SetSnapLaneTypes.
 *
 * usage: rm_snap <odr> <road_id> <lane_id> <s> <mask>
 *
 * Anchors a position at the centre of a named lane, reads its world x, y, and
 * then places a SECOND handle at exactly that world point after restricting
 * which lane types that handle is allowed to see. Which lane the second
 * handle lands on is the observable: with the mask wide open it lands on the
 * lane the point is actually in, with the mask narrowed it lands on the
 * nearest lane of a permitted type.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s> <mask>\n", argv[0]);
        return 2;
    }
    int   road_id = std::atoi(argv[2]);
    int   lane_id = std::atoi(argv[3]);
    float s       = static_cast<float>(std::atof(argv[4]));
    int   mask    = std::atoi(argv[5]);

    int rc = RM_Init(argv[1]);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    /* Anchor: the world point at the centre of the requested lane. */
    int h0 = RM_CreatePosition();
    std::printf("anchor_set=%d\n", RM_SetLanePosition(h0, road_id, lane_id, 0.0f, s, true) >= 0 ? 1 : 0);
    RM_PositionData a;
    RM_GetPositionData(h0, &a);
    std::printf("anchor_road=%d\n", a.roadId);
    std::printf("anchor_lane=%d\n", a.laneId);
    std::printf("anchor_s=%.3f\n", static_cast<double>(a.s));

    /* Second handle, restricted, placed at the anchor's world coordinates. */
    int h = RM_CreatePosition();
    std::printf("snap_rc=%d\n", RM_SetSnapLaneTypes(h, mask));
    std::printf("set_world=%d\n", RM_SetWorldXYHPosition(h, a.x, a.y, a.h) >= 0 ? 1 : 0);

    RM_PositionData d;
    RM_GetPositionData(h, &d);
    std::printf("road=%d\n", d.roadId);
    std::printf("lane=%d\n", d.laneId);
    std::printf("lane_offset=%.4f\n", static_cast<double>(d.laneOffset));
    std::printf("s=%.3f\n", static_cast<double>(d.s));
    /* The world point itself is never moved by the snap; only its
     * road-relative interpretation changes. */
    std::printf("x=%.3f\n", static_cast<double>(d.x));
    std::printf("y=%.3f\n", static_cast<double>(d.y));

    /* A handle that was never created is an error, not a silent success. */
    std::printf("bad_handle_rc=%d\n", RM_SetSnapLaneTypes(9999, mask));

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

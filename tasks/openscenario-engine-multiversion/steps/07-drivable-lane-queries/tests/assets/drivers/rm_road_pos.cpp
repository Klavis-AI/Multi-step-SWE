/*
 * rm_road_pos - placing a position handle by road s/t coordinates.
 *
 * usage: rm_road_pos <odr> <road_id> <s> <t> <align:0|1>
 *
 * Reports everything the road manager derived from the (road, s, t) triple:
 * the world point, the lane the point fell in, the residual offset from that
 * lane's centre, and the heading. With align on, the heading is taken from the
 * road; with align off, the handle's previous heading survives, so the driver
 * always sets a distinctive heading first.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <s> <t> <align:0|1>\n", argv[0]);
        return 2;
    }
    const char *odr   = argv[1];
    id_t        road  = static_cast<id_t>(std::atoi(argv[2]));
    float       s     = static_cast<float>(std::atof(argv[3]));
    float       t     = static_cast<float>(std::atof(argv[4]));
    bool        align = std::atoi(argv[5]) != 0;

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int h = RM_CreatePosition();
    std::printf("handle_ok=%d\n", h >= 0 ? 1 : 0);

    /* A heading nothing in the network would produce, so "align kept the old
     * heading" and "align recomputed it" are distinguishable. */
    const float PRESET_H = 1.0f;
    RM_SetWorldXYHPosition(h, 0.0f, 0.0f, PRESET_H);

    int set_rc = RM_SetRoadPosition(h, road, s, t, align);
    std::printf("set_rc=%d\n", set_rc);
    std::printf("set_ok=%d\n", set_rc >= 0 ? 1 : 0);

    RM_PositionData d;
    std::printf("get_ok=%d\n", RM_GetPositionData(h, &d) == 0 ? 1 : 0);
    std::printf("road=%d\n", static_cast<int>(d.roadId));
    std::printf("lane=%d\n", d.laneId);
    std::printf("s=%.3f\n", static_cast<double>(d.s));
    std::printf("lane_offset=%.3f\n", static_cast<double>(d.laneOffset));
    std::printf("x=%.3f\n", static_cast<double>(d.x));
    std::printf("y=%.3f\n", static_cast<double>(d.y));
    std::printf("h=%.4f\n", static_cast<double>(d.h));
    std::printf("heading_preserved=%d\n", (d.h > PRESET_H - 0.0005f && d.h < PRESET_H + 0.0005f) ? 1 : 0);

    /* A road the network does not define is refused. */
    int hb = RM_CreatePosition();
    std::printf("bad_road_rc=%d\n", RM_SetRoadPosition(hb, 90210, s, t, align));
    std::printf("bad_handle_rc=%d\n", RM_SetRoadPosition(4242, road, s, t, align));

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

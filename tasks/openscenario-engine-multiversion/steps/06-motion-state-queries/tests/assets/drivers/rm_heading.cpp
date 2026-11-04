/*
 * rm_heading - setting the heading of an existing road-manager position
 * without touching anything else about it.
 *
 * usage: rm_heading <odr> <road_id> <lane_id> <s> <h> <mode>
 *
 * Places a handle with the ordinary lane-position setter, records what the
 * position reads as, then asks for a new heading -- through the plain form
 * when <mode> is 0, and through the explicit-mode form otherwise -- and
 * records what the position reads as afterwards. The case compares the two.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

static void dump(const char *tag, const RM_PositionData &d)
{
    std::printf("%s_x=%.4f\n", tag, static_cast<double>(d.x));
    std::printf("%s_y=%.4f\n", tag, static_cast<double>(d.y));
    std::printf("%s_h=%.5f\n", tag, static_cast<double>(d.h));
    std::printf("%s_hrel=%.5f\n", tag, static_cast<double>(d.hRelative));
    std::printf("%s_s=%.4f\n", tag, static_cast<double>(d.s));
    std::printf("%s_lane=%d\n", tag, d.laneId);
    std::printf("%s_road=%d\n", tag, static_cast<int>(d.roadId));
    std::printf("%s_laneoffset=%.4f\n", tag, static_cast<double>(d.laneOffset));
}

int main(int argc, char **argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s> <h> <mode>\n", argv[0]);
        return 2;
    }
    int rc = RM_Init(argv[1]);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int   road_id = std::atoi(argv[2]);
    int   lane_id = std::atoi(argv[3]);
    float s       = static_cast<float>(std::atof(argv[4]));
    float h       = static_cast<float>(std::atof(argv[5]));
    int   mode    = std::atoi(argv[6]);

    int handle = RM_CreatePosition();
    std::printf("handle=%d\n", handle);
    /* From 2.21.2 the position setters report 1 and 2 for "moved into a new
     * road segment" and "moved into a junction and made a choice", so success
     * is >= 0 and never == 0. */
    std::printf("set_lane_ok=%d\n", RM_SetLanePosition(handle, road_id, lane_id, 0.0f, s, true) >= 0 ? 1 : 0);

    RM_PositionData before;
    RM_GetPositionData(handle, &before);
    dump("before", before);

    int set_rc;
    if (mode == 0)
    {
        set_rc = RM_SetH(handle, h);
        std::printf("used_mode_form=0\n");
    }
    else
    {
        set_rc = RM_SetHMode(handle, h, mode);
        std::printf("used_mode_form=1\n");
    }
    std::printf("set_h_rc=%d\n", set_rc);
    std::printf("set_h_ok=%d\n", set_rc >= 0 ? 1 : 0);

    RM_PositionData after;
    RM_GetPositionData(handle, &after);
    dump("after", after);

    /* A handle that was never created. instruction.md's "How failures are
     * reported" names BOTH forms here, so both are probed: a rejection the
     * plain form gets right and the explicit-mode form gets wrong would
     * otherwise go unnoticed. 48 is RM_H_ABS. */
    std::printf("rc_bad_handle=%d\n", RM_SetH(999, h));
    std::printf("rc_bad_handle_mode=%d\n", RM_SetHMode(999, h, 48));

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

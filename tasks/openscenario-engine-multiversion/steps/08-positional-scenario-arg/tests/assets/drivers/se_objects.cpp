/*
 * se_objects - the shape of the object-injection entry points, and the
 * lane-type field this step adds to SE_RoadInfo.
 *
 * usage: se_objects <xosc>
 *
 * This driver exists because neither claim is visible from outside the
 * process. SE_AddObject and SE_AddObjectWithBoundingBox keep their names and
 * their C linkage across this step, so `nm -D` shows the same two symbols at
 * both ends and cannot see that a parameter was inserted; only a compile
 * against the candidate's own header can. Likewise SE_RoadInfo.lane_type is a
 * struct member, not a symbol.
 *
 * Both calls below are written with the parameter list the step requires -
 * model_3d immediately after model_id - so a tree that still carries the
 * narrower declaration fails to compile here rather than silently passing.
 */
#include <cstdio>
#include <cstring>

#include "esminiLib.hpp"

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <xosc>\n", argv[0]);
        return 2;
    }

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    std::fflush(stdout);
    if (rc != 0)
    {
        return 1;
    }

    std::printf("nobj_before=%d\n", SE_GetNumberOfObjects());
    std::fflush(stdout);

    for (int i = 0; i < 3; i++)
    {
        SE_StepDT(0.05f);
    }

    /* model_3d = nullptr means "no 3D model file", which is the documented way
     * to add an object in a headless build. Passing it at all is the point. */
    int add_id = SE_AddObject("probe_obj", 1, 0, 0, -1, nullptr);
    std::printf("add_id=%d\n", add_id);
    std::fflush(stdout);

    SE_OSCBoundingBox bb;
    std::memset(&bb, 0, sizeof(bb));
    bb.center_.x_            = 1.4f;
    bb.center_.y_            = 0.0f;
    bb.center_.z_            = 0.75f;
    bb.dimensions_.length_   = 4.5f;
    bb.dimensions_.width_    = 1.8f;
    bb.dimensions_.height_   = 1.5f;
    int add_bb_id = SE_AddObjectWithBoundingBox("probe_bb", 1, 0, 0, -1, nullptr, bb, 2);
    std::printf("add_bb_id=%d\n", add_bb_id);
    std::fflush(stdout);

    std::printf("nobj_after=%d\n", SE_GetNumberOfObjects());
    std::fflush(stdout);

    SE_StepDT(0.05f);

    /* lane_type on the lookahead result: the scenario runs in a driving lane,
     * so the reported type is LANE_TYPE_DRIVING (1 << 1). */
    SE_RoadInfo ri;
    std::memset(&ri, 0, sizeof(ri));
    std::printf("ri_rc=%d\n", SE_GetRoadInfoAtDistance(0, 10.0f, &ri, 0, true));
    std::printf("ri_lane=%d\n", ri.laneId);
    std::printf("ri_lane_type=%d\n", ri.lane_type);
    std::fflush(stdout);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

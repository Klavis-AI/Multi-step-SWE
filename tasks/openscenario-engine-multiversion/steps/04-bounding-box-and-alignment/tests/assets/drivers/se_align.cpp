/*
 * se_align - road-surface alignment of an externally reported object.
 *
 * usage: se_align <xosc> <x> <y> <mode> <which>
 *
 *   which = "z"       two-step: an explicit, deliberately-wrong z=100 is
 *                      reported first (via SE_ReportObjectPos, full pose),
 *                      then SE_SetAlignModeZ is toggled and the SAME point is
 *                      re-reported through SE_ReportObjectPosXYH, which has
 *                      no z parameter at all. Elevation only ever follows the
 *                      road through the no-z path: esmini's SetInertiaPos
 *                      honours an EXPLICIT z unconditionally (SetZ(z) fires
 *                      whenever z is not NaN, before any alignment mode is
 *                      consulted), so a case built on the full-pose call
 *                      alone could never tell "aligned" from "not aligned".
 *                      The before/after read lets a case see z=100 survive
 *                      under ALIGN_NONE and get replaced by the real road
 *                      elevation under ALIGN_HARD, at the identical point.
 *   which = "p"       explicit pitch=0 via SE_ReportObjectPos, toggle
 *                      SE_SetAlignModeP. Pitch does not need the two-step
 *                      trick: an explicit pitch is blended against the road's
 *                      own pitch by EvaluateOrientation on every report, so
 *                      the align mode already shows up in a single call.
 *   which = "all"     same two-step shape as "z", but toggling SE_SetAlignMode
 *                      (all four components at once) instead of SE_SetAlignModeZ.
 *   which = "missing" z omitted entirely via SE_ReportObjectPosXYH on a
 *                      freshly added object, no align call at all: whatever
 *                      the library defaults to without the caller ever
 *                      touching an align-mode entry point.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiLib.hpp"

static void dump(const char *prefix, int id)
{
    SE_ScenarioObjectState st;
    std::memset(&st, 0, sizeof(st));
    int rc = SE_GetObjectState(id, &st);
    std::printf("%s_state_rc=%d\n", prefix, rc);
    std::printf("%s_x=%.4f\n", prefix, static_cast<double>(st.x));
    std::printf("%s_y=%.4f\n", prefix, static_cast<double>(st.y));
    std::printf("%s_z=%.4f\n", prefix, static_cast<double>(st.z));
    std::printf("%s_p=%.4f\n", prefix, static_cast<double>(st.p));
    std::printf("%s_road=%d\n", prefix, st.roadId);
    std::printf("%s_lane=%d\n", prefix, st.laneId);
    std::printf("%s_s=%.3f\n", prefix, static_cast<double>(st.s));
}

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: %s <xosc> <x> <y> <mode> <z|p|all|missing>\n", argv[0]);
        return 2;
    }
    float       x     = static_cast<float>(std::atof(argv[2]));
    float       y     = static_cast<float>(std::atof(argv[3]));
    int         mode  = std::atoi(argv[4]);
    const char *which = argv[5];

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    SE_OSCBoundingBox bb;
    bb.dimensions_.length_ = 5.0f;
    bb.dimensions_.width_  = 2.0f;
    bb.dimensions_.height_ = 1.5f;
    bb.center_.x_          = 1.4f;
    bb.center_.y_          = 0.0f;
    bb.center_.z_          = 0.75f;

    int id = SE_AddObjectWithBoundingBox("ext", 1, 1, 0, 0, bb, 0);
    std::printf("add_id=%d\n", id);
    if (id < 0)
    {
        return 1;
    }

    if (std::strcmp(which, "z") == 0)
    {
        std::printf("report_rc=%d\n", SE_ReportObjectPos(id, 0.0f, x, y, 100.0f, 0.0f, 0.0f, 0.0f));
        SE_StepDT(0.05f);
        dump("before", id);
        SE_SetAlignModeZ(id, mode);
        std::printf("report2_rc=%d\n", SE_ReportObjectPosXYH(id, 0.0f, x, y, 0.0f));
    }
    else if (std::strcmp(which, "p") == 0)
    {
        SE_SetAlignModeP(id, mode);
        std::printf("report_rc=%d\n", SE_ReportObjectPos(id, 0.0f, x, y, 0.0f, 0.0f, 0.0f, 0.0f));
    }
    else if (std::strcmp(which, "all") == 0)
    {
        std::printf("report_rc=%d\n", SE_ReportObjectPos(id, 0.0f, x, y, 100.0f, 0.0f, 0.0f, 0.0f));
        SE_StepDT(0.05f);
        dump("before", id);
        SE_SetAlignMode(id, mode);
        std::printf("report2_rc=%d\n", SE_ReportObjectPosXYH(id, 0.0f, x, y, 0.0f));
    }
    else
    {
        /* No align-mode call at all: whatever the library defaults to. */
        std::printf("report_rc=%d\n", SE_ReportObjectPosXYH(id, 0.0f, x, y, 0.0f));
    }
    SE_StepDT(0.05f);

    dump("after", id);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

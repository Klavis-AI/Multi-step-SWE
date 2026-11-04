/*
 * se_snap - SE_SetSnapLaneTypes on a scenario-engine object.
 *
 * usage: se_snap <xosc> <x> <y> <mask>
 *
 * An externally reported world position has to be resolved onto SOME lane.
 * Which lanes are allowed to win that competition is what the lane-type mask
 * selects. The driver parks a freshly added object at a world point that sits
 * over a non-driving lane and prints the lane it ended up on, so the same
 * coordinates under two different masks give two different answers.
 *
 * Assumption recorded here because there is no one to ask: the mask constants
 * are passed in from the case as plain integers rather than being named in
 * the driver, because the enum that defines them lives in roadmanager.hpp and
 * is not part of the C ABI the checker compiles against.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <xosc> <x> <y> <mask>\n", argv[0]);
        return 2;
    }
    float x    = static_cast<float>(std::atof(argv[2]));
    float y    = static_cast<float>(std::atof(argv[3]));
    int   mask = static_cast<int>(std::strtol(argv[4], nullptr, 0));

    /* Before SE_Init there is no object to configure. */
    std::printf("snap_before_init=%d\n", SE_SetSnapLaneTypes(0, mask));

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int id = SE_AddObject("snapper", 1, 1, 0, 0);
    std::printf("add_id=%d\n", id);
    if (id < 0)
    {
        return 1;
    }

    std::printf("snap_rc=%d\n", SE_SetSnapLaneTypes(id, mask));
    /* An id no scenario defines must be rejected, not silently accepted. */
    std::printf("bad_id_rc=%d\n", SE_SetSnapLaneTypes(4242, mask));

    std::printf("report_rc=%d\n", SE_ReportObjectPosXYH(id, 0.0f, x, y, 0.0f));
    SE_StepDT(0.05f);

    SE_ScenarioObjectState st;
    std::memset(&st, 0, sizeof(st));
    std::printf("state_rc=%d\n", SE_GetObjectState(id, &st));
    std::printf("x=%.3f\n", static_cast<double>(st.x));
    std::printf("y=%.3f\n", static_cast<double>(st.y));
    std::printf("road=%d\n", st.roadId);
    std::printf("lane=%d\n", st.laneId);
    std::printf("lane_offset=%.4f\n", static_cast<double>(st.laneOffset));
    std::printf("s=%.3f\n", static_cast<double>(st.s));
    std::printf("t=%.4f\n", static_cast<double>(st.t));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

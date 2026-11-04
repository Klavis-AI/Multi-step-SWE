/*
 * se_status - scenario-engine object enumeration and state readback.
 *
 * usage: se_status <xosc> <steps>
 *
 * Every entry point used here predates the step, and every field printed
 * exists in SE_ScenarioObjectState at both ends of it. That is deliberate:
 * this driver is the control. It has to compile, link and print the same
 * numbers before and after the change, so a case built on it proves the
 * step did not disturb the object model it extended.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: %s <xosc> <steps>\n", argv[0]);
        return 2;
    }
    int steps = std::atoi(argv[2]);

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int n = SE_GetNumberOfObjects();
    std::printf("nobj=%d\n", n);

    for (int i = 0; i < steps; i++)
    {
        SE_StepDT(0.05f);
    }
    std::printf("quit=%d\n", SE_GetQuitFlag());

    for (int i = 0; i < n; i++)
    {
        const char *name = SE_GetObjectName(i);
        std::printf("name_%d=%s\n", i, name ? name : "(null)");
        std::printf("id_by_name_%d=%d\n", i, name ? SE_GetIdByName(name) : -1);

        SE_ScenarioObjectState st;
        std::memset(&st, 0, sizeof(st));
        std::printf("state_rc_%d=%d\n", i, SE_GetObjectState(i, &st));
        std::printf("obj_id_%d=%d\n", i, st.id);
        std::printf("type_%d=%d\n", i, st.objectType);
        std::printf("category_%d=%d\n", i, st.objectCategory);
        std::printf("ctrl_%d=%d\n", i, st.ctrl_type);
        std::printf("road_%d=%d\n", i, st.roadId);
        std::printf("lane_%d=%d\n", i, st.laneId);
        std::printf("junction_%d=%d\n", i, st.junctionId);
        std::printf("s_%d=%.3f\n", i, static_cast<double>(st.s));
        std::printf("t_%d=%.3f\n", i, static_cast<double>(st.t));
        std::printf("x_%d=%.3f\n", i, static_cast<double>(st.x));
        std::printf("y_%d=%.3f\n", i, static_cast<double>(st.y));
        std::printf("h_%d=%.4f\n", i, static_cast<double>(st.h));
        std::printf("speed_%d=%.3f\n", i, static_cast<double>(st.speed));
        std::printf("length_%d=%.3f\n", i, static_cast<double>(st.length));
        std::printf("width_%d=%.3f\n", i, static_cast<double>(st.width));
        std::printf("height_%d=%.3f\n", i, static_cast<double>(st.height));
        std::printf("cx_%d=%.3f\n", i, static_cast<double>(st.centerOffsetX));
        std::printf("cy_%d=%.3f\n", i, static_cast<double>(st.centerOffsetY));
        std::printf("cz_%d=%.3f\n", i, static_cast<double>(st.centerOffsetZ));
    }

    /* Out-of-range indices must be rejected, at both ends of the step. */
    SE_ScenarioObjectState bad;
    std::memset(&bad, 0, sizeof(bad));
    std::printf("bad_state_rc=%d\n", SE_GetObjectState(4242, &bad));
    std::printf("bad_id_by_name=%d\n", SE_GetIdByName("no_such_entity"));

    /* SE_RoadInfo's road and junction identity, looked up ahead of the first
     * entity. The struct declares roadId and junctionId as plain ints, and
     * junctionId reads -1 whenever the looked-up point is not inside a
     * junction -- which, on a plain straight road, it never is. Inherited:
     * identical at both ends of this step. */
    SE_RoadInfo ri;
    std::memset(&ri, 0, sizeof(ri));
    std::printf("ri_rc=%d\n", SE_GetRoadInfoAtDistance(0, 10.0f, &ri, 0, true));
    std::printf("ri_road=%d\n", ri.roadId);
    std::printf("ri_junction=%d\n", ri.junctionId);
    std::printf("ri_lane=%d\n", ri.laneId);
    std::printf("ri_s=%.3f\n", static_cast<double>(ri.s));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

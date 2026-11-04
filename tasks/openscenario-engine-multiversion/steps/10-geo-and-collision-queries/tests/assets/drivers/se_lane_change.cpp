/* se_lane_change.cpp - SE_ObjectCanChangeLanes, new at this step's target.
 *
 * usage: se_lane_change <scenario.xosc> <report|noreport>
 *
 * Prints, for every object the scenario declares, the lane the engine placed
 * it in and the legality code it returns for that placement. With "report" it
 * additionally re-places object 0 on three different lanes via
 * SE_ReportObjectPos and re-queries, so a stub that answers from a static
 * table rather than from the road network cannot pass.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esminiLib.hpp"

static void dump(const char* phase)
{
    int n = SE_GetNumberOfObjects();
    std::printf("nobj_%s=%d\n", phase, n);
    for (int i = 0; i < n; i++)
    {
        SE_ScenarioObjectState st;
        if (SE_GetObjectState(i, &st) != 0) { std::printf("obj%d state_unavailable\n", i); continue; }
        std::printf("%s%d lane=%d canchange=%d\n", phase, i, st.laneId, SE_ObjectCanChangeLanes(i));
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <scenario> <report|noreport>\n", argv[0]); return 2; }

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0) return 1;

    SE_StepDT(0.05f);
    dump("obj");

    /* An unknown id is a documented error, not "no lane change". */
    std::printf("badobj=%d\n", SE_ObjectCanChangeLanes(99));
    std::printf("badobj_negative=%d\n", SE_ObjectCanChangeLanes(-3));

    if (std::strcmp(argv[2], "report") == 0)
    {
        /* Lane centres on this network: -1 at y=-1.75, -2 at y=-5.25,
         * -3 at y=-8.75. Report positions on each and re-query. */
        const double ys[] = { -1.75, -5.25, -8.75 };
        for (int k = 0; k < 3; k++)
        {
            SE_ReportObjectPos(0, 150.0, ys[k], 0.0, 0.0, 0.0, 0.0);
            SE_StepDT(0.05f);
            SE_ScenarioObjectState st;
            SE_GetObjectState(0, &st);
            std::printf("moved lane=%d canchange=%d\n", st.laneId, SE_ObjectCanChangeLanes(0));
        }
    }

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

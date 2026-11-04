/*
 * se_dist_legacy - the uncached inter-object distance query and the object
 * state getter, both of which the workspace already satisfies. This is a
 * regression driver: nothing it calls is new at this release, so it must
 * compile and pass both before and after the step's work.
 *
 * usage: se_dist_legacy <xosc> <steps> <dt>
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <xosc> <steps> <dt>\n", argv[0]);
        return 2;
    }
    const char *xosc  = argv[1];
    int         steps = std::atoi(argv[2]);
    float       dt    = static_cast<float>(std::atof(argv[3]));

    int rc = SE_Init(xosc, 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    std::printf("n_objects=%d\n", SE_GetNumberOfObjects());

    SE_PositionDiff diff;
    std::printf("rc_t0=%d\n", SE_GetDistanceToObject(0, 1, false, &diff));
    std::printf("ds_t0=%.4f\n", static_cast<double>(diff.ds));
    std::printf("dt_t0=%.4f\n", static_cast<double>(diff.dt));

    for (int i = 0; i < steps; i++)
    {
        if (SE_StepDT(dt) != 0)
        {
            std::printf("step_failed_at=%d\n", i);
            break;
        }
    }

    std::printf("sim_time=%.4f\n", static_cast<double>(SE_GetSimulationTime()));
    std::printf("rc_end=%d\n", SE_GetDistanceToObject(0, 1, false, &diff));
    std::printf("ds_end=%.4f\n", static_cast<double>(diff.ds));
    std::printf("dt_end=%.4f\n", static_cast<double>(diff.dt));

    SE_ScenarioObjectState st;
    SE_GetObjectState(0, &st);
    std::printf("obj0_speed=%.4f\n", static_cast<double>(st.speed));
    std::printf("obj0_s=%.4f\n", static_cast<double>(st.s));
    SE_GetObjectState(1, &st);
    std::printf("obj1_speed=%.4f\n", static_cast<double>(st.speed));
    std::printf("obj1_s=%.4f\n", static_cast<double>(st.s));

    /* An object id no scenario defines is an error, not a number. */
    std::printf("rc_bad=%d\n", SE_GetDistanceToObject(0, 4242, false, &diff));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

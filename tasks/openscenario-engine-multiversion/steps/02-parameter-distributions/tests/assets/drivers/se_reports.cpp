/*
 * se_reports - three small scenario-engine additions that share a shape:
 * name lookup, a double-precision simulation clock, and an explicit
 * recording (.dat) path.
 *
 * usage: se_reports <xosc> <datpath> <steps> <dt>
 *
 * The simulation clock and the name lookup are read for a fresh scenario;
 * the .dat path is set before initialisation and the file is expected at
 * exactly the path given.
 *
 * None of the three entry points exists at the step's starting release, so
 * this driver must not compile at the starting tag. The group it backs is
 * `scored` in groups.txt, and that is what makes it fail there rather than
 * pass on inherited behaviour.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <xosc> <datpath> <steps> <dt>\n", argv[0]);
        return 2;
    }
    const char *xosc    = argv[1];
    const char *datpath = argv[2];
    int         steps   = std::atoi(argv[3]);
    float       dt      = static_cast<float>(std::atof(argv[4]));

    /* Nothing loaded: the name lookup has nothing to find and the clock has
     * no scenario to measure. */
    std::printf("id_before=%d\n", SE_GetIdByName("Ego"));
    std::printf("time_before=%.17g\n", SE_GetSimulationTimeDouble());

    SE_SetDatFilePath(datpath);

    int rc = SE_Init(xosc, 0, 0, 0, 1 /* record */);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    /* A scenario is loaded but has not advanced: the clock is exactly zero
     * and both precisions agree there. */
    std::printf("time_init_double=%.17g\n", SE_GetSimulationTimeDouble());
    std::printf("time_init_float=%.17g\n", static_cast<double>(SE_GetSimulationTime()));

    /* Name lookup answers for a real entity and fails for one that is not in
     * the scenario. Both are asked here so one constant cannot serve. */
    std::printf("id_ego=%d\n", SE_GetIdByName("Ego"));
    std::printf("id_target=%d\n", SE_GetIdByName("Target"));
    std::printf("id_unknown=%d\n", SE_GetIdByName("NoSuchEntityInThisScenario"));
    /* The name lookup for the first-declared entity must agree with the
     * existing index-based lookup for that same entity. */
    std::printf("id_by_index_0=%d\n", SE_GetId(0));

    for (int i = 0; i < steps; i++)
    {
        if (SE_StepDT(dt) != 0)
        {
            break;
        }
    }

    double t_d = SE_GetSimulationTimeDouble();
    double t_f = static_cast<double>(SE_GetSimulationTime());
    std::printf("time_double=%.17g\n", t_d);
    std::printf("time_float=%.17g\n", t_f);
    /* The single-precision clock rounds; the double does not. Printing both
     * makes the distinction observable without asserting the float value. */
    std::printf("time_delta=%.17g\n", t_d - t_f);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

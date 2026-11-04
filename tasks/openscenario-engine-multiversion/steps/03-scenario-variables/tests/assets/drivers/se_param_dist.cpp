/*
 * se_param_dist - the parameter value distribution API an earlier step already
 * established. This is a regression driver: nothing it calls is new at this
 * release, so it must compile and answer identically before and after.
 *
 * usage: se_param_dist <distribution.xosc> <scenario.xosc> <permutation index>
 *
 * SE_SetParameterDistribution is deliberately never handed a path that does
 * not exist. Upstream lets the XML reader's exception escape in that case
 * instead of returning an error code, and a check that relied on the
 * documented -1 would be grading a bug rather than the entry point.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <dist.xosc> <scenario.xosc> <index>\n", argv[0]);
        return 2;
    }
    int index = std::atoi(argv[3]);

    std::printf("perms_before=%d\n", SE_GetNumberOfPermutations());
    std::printf("set_dist=%d\n", SE_SetParameterDistribution(argv[1]));
    std::printf("perms=%d\n", SE_GetNumberOfPermutations());
    std::printf("select=%d\n", SE_SelectPermutation(index));

    int rc = SE_Init(argv[2], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    std::printf("index=%d\n", SE_GetPermutationIndex());

    /* The selected permutation is visible as the resolved parameter values. */
    double v = 0.0;
    std::printf("ego_speed_rc=%d\n", SE_GetParameterDouble("EgoSpeed", &v));
    std::printf("ego_speed=%.6f\n", v);
    v = 0.0;
    std::printf("target_speed_rc=%d\n", SE_GetParameterDouble("TargetSpeed", &v));
    std::printf("target_speed=%.6f\n", v);

    std::printf("ego_id=%d\n", SE_GetIdByName("Ego"));
    std::printf("absent_id_negative=%d\n", SE_GetIdByName("NotAnEntity") < 0 ? 1 : 0);

    /* ...and as the speed the entity actually runs at. */
    for (int i = 0; i < 20; i++)
    {
        SE_StepDT(0.1f);
    }
    std::printf("sim_time=%.9f\n", SE_GetSimulationTimeDouble());

    SE_ScenarioObjectState st;
    if (SE_GetObjectState(SE_GetIdByName("Ego"), &st) != 0)
    {
        std::printf("state=0\n");
        return 1;
    }
    std::printf("state=1\n");
    std::printf("measured_speed=%.4f\n", static_cast<double>(st.speed));

    SE_Close();
    SE_ResetParameterDistribution();
    std::printf("perms_after_reset=%d\n", SE_GetNumberOfPermutations());
    std::printf("survived=1\n");
    return 0;
}

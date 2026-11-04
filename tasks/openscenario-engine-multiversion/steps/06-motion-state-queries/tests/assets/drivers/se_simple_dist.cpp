/*
 * se_simple_dist - the cached inter-object distance query.
 *
 * usage: se_simple_dist <xosc> <steps> <dt> <tracking_limit> <dist_type>
 *
 * Advances the scenario and, once per step, asks for the distance between
 * object 0 and object 1 through the cached query. The same pair is also
 * measured through the uncached road-manager route, so a case can assert the
 * two agree.
 *
 * The cache is the point of the entry point: while the pair is further apart
 * than the tracking limit the sample is allowed to go stale, and the call says
 * so through its return code and through the timestamp it hands back.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: %s <xosc> <steps> <dt> <tracking_limit> <dist_type>\n", argv[0]);
        return 2;
    }
    const char *xosc      = argv[1];
    int         steps     = std::atoi(argv[2]);
    float       dt        = static_cast<float>(std::atof(argv[3]));
    double      limit     = std::atof(argv[4]);
    int         dist_type = std::atoi(argv[5]);

    int rc = SE_Init(xosc, 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    double distance = -1.0;
    double stamp    = -1.0;
    int    call_rc  = -99;

    int    fresh_calls = 0;
    int    stale_calls = 0;
    double last_stamp  = -1.0;
    int    stamp_moves = 0;

    for (int i = 0; i < steps; i++)
    {
        if (SE_StepDT(dt) != 0)
        {
            std::printf("step_failed_at=%d\n", i);
            break;
        }
        call_rc = SE_SimpleGetDistanceToObject(0, 1, static_cast<SE_RelativeDistanceType>(dist_type), limit, &distance, &stamp);
        if (call_rc == 0)
        {
            fresh_calls++;
        }
        else if (call_rc == -2)
        {
            stale_calls++;
        }
        if (stamp > last_stamp + 1e-6)
        {
            stamp_moves++;
            last_stamp = stamp;
        }
    }

    std::printf("sim_time=%.4f\n", static_cast<double>(SE_GetSimulationTime()));
    std::printf("rc=%d\n", call_rc);
    std::printf("distance=%.4f\n", distance);
    std::printf("stamp=%.4f\n", stamp);
    std::printf("stamp_lag=%.4f\n", static_cast<double>(SE_GetSimulationTime()) - stamp);
    std::printf("fresh_calls=%d\n", fresh_calls);
    std::printf("stale_calls=%d\n", stale_calls);
    std::printf("stamp_moves=%d\n", stamp_moves);

    /* The same pair measured the uncached way, for comparison. */
    SE_PositionDiff diff;
    std::printf("legacy_rc=%d\n", SE_GetDistanceToObject(0, 1, false, &diff));
    std::printf("legacy_ds=%.4f\n", static_cast<double>(diff.ds));
    std::printf("agrees=%d\n", std::fabs(static_cast<double>(diff.ds) - distance) < 0.01 ? 1 : 0);

    /* An object id no scenario defines. */
    double d2 = 0.0, t2 = 0.0;
    std::printf("rc_bad=%d\n", SE_SimpleGetDistanceToObject(0, 4242, static_cast<SE_RelativeDistanceType>(dist_type), limit, &d2, &t2));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

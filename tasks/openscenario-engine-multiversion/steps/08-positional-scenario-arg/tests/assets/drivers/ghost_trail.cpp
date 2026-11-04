/*
 * ghost_trail - does --ghost_trail_dt reach the ghost-trail sampler, or is it
 * only parsed?
 *
 * usage: ghost_trail <xosc> <dt_fine> <dt_coarse>
 *
 * The option sets the interval at which the engine records a vertex of the
 * ghost's trail. It is therefore observable at runtime WITHOUT pinning any
 * magnitude: run the same deterministic scenario twice, changing nothing but
 * --ghost_trail_dt, and read the trail back through
 * SE_GetRoadInfoAlongGhostTrail. A coarsely sampled trail is a different
 * polyline from a finely sampled one, so the timestamps it reports at the same
 * lookahead distances differ. An implementation that registers the option and
 * then ignores it produces two identical runs and `differ` comes back 0.
 *
 * Why no timestamp is pinned: the value returned for a given lookahead distance
 * is INTERPOLATED along the trail polyline, so it is a function of the ghost's
 * trajectory and of the sampling interval together, not of the interval alone.
 * It is not derivable from anything the documents publish, and it is not a
 * monotone function of dt. Only the disagreement is asserted.
 *
 * Why the caller picks the two intervals rather than this driver: how far apart
 * two trails land is a property of the pair, and NOT every pair separates. On
 * the shipped fixture 0.05 s against 0.50 s agrees to 0.0032 s - the polyline is
 * still close enough to the ghost's real path at both spacings that
 * interpolation hides the difference. 0.05 s against 3.00 s disagrees by 0.9474
 * s, because the coarse polyline cuts the corners of the two lane changes. The
 * case therefore passes a measured pair, and this driver only reports what the
 * pair it was handed produced.
 *
 * The option is fed through SE_InitWithArgs, which is the library's own
 * argument entry point and exists unchanged at both ends of this step - so this
 * driver compiles at both ends and the separation is in the values, not in
 * whether it builds.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiLib.hpp"

/* Lookahead distances, in metres, at which the trail is read back. Several are
 * used so the comparison does not rest on a single point of one polyline. */
static const float kProbes[] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f};
static const int   kNumProbes = 5;

/* Sample the ghost trail after running the scenario with one --ghost_trail_dt.
 * Returns 0 on success; fills ts[] with the timestamp at each probe distance. */
static int sample(const char *xosc, const char *dt, int nsteps, float *ts, int *has_ghost, int *rc_first)
{
    const char *args[] = {"esmini",
                          "--osc",
                          xosc,
                          "--headless",
                          "--fixed_timestep",
                          "0.05",
                          "--ghost_trail_dt",
                          dt,
                          "--disable_stdout"};
    int rc = SE_InitWithArgs(9, args);
    if (rc != 0)
    {
        return rc == 0 ? -1 : rc;
    }

    for (int i = 0; i < nsteps; i++)
    {
        SE_StepDT(0.05f);
    }

    *has_ghost = SE_ObjectHasGhost(0);

    for (int i = 0; i < kNumProbes; i++)
    {
        SE_RoadInfo ri;
        std::memset(&ri, 0, sizeof(ri));
        float speed_ghost = 0.0f;
        float timestamp   = 0.0f;
        int   r           = SE_GetRoadInfoAlongGhostTrail(0, kProbes[i], &ri, &speed_ghost, &timestamp);
        if (i == 0)
        {
            *rc_first = r;
        }
        ts[i] = timestamp;
    }

    SE_Close();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <xosc> <dt_fine> <dt_coarse>\n", argv[0]);
        return 2;
    }

    const int nsteps = 160; /* 8.0 s at the fixed 0.05 s step */

    float ts_fine[kNumProbes];
    float ts_coarse[kNumProbes];
    int   ghost_fine = -1, ghost_coarse = -1;
    int   rc_fine = 99, rc_coarse = 99;

    int init_fine = sample(argv[1], argv[2], nsteps, ts_fine, &ghost_fine, &rc_fine);
    std::printf("init_fine=%d\n", init_fine);
    if (init_fine != 0)
    {
        return 1;
    }

    int init_coarse = sample(argv[1], argv[3], nsteps, ts_coarse, &ghost_coarse, &rc_coarse);
    std::printf("init_coarse=%d\n", init_coarse);
    if (init_coarse != 0)
    {
        return 1;
    }

    std::printf("ghost_fine=%d\n", ghost_fine);
    std::printf("ghost_coarse=%d\n", ghost_coarse);
    std::printf("rc_fine=%d\n", rc_fine);
    std::printf("rc_coarse=%d\n", rc_coarse);

    /* Largest disagreement between the two runs, over the probe distances. */
    double maxdiff = 0.0;
    for (int i = 0; i < kNumProbes; i++)
    {
        double d = static_cast<double>(ts_fine[i]) - static_cast<double>(ts_coarse[i]);
        if (d < 0.0)
        {
            d = -d;
        }
        if (d > maxdiff)
        {
            maxdiff = d;
        }
        std::printf("ts_fine_%d=%.6f\n", i, static_cast<double>(ts_fine[i]));
        std::printf("ts_coarse_%d=%.6f\n", i, static_cast<double>(ts_coarse[i]));
    }
    std::printf("maxdiff=%.6f\n", maxdiff);

    /* Threshold = one 0.05 s simulation step, the interval the trail is sampled
     * on. It is a floor on what counts as evidence, not a claim that every pair
     * of intervals clears it: a disagreement smaller than one step could be
     * integration noise rather than a different polyline. It is not tuned to the
     * observed value - the fixture and the pair the case passes produce 0.9474
     * s, nineteen times the floor, byte-identical over repeated runs. */
    std::printf("differ=%d\n", maxdiff > 0.05 ? 1 : 0);

    std::printf("survived=1\n");
    return 0;
}

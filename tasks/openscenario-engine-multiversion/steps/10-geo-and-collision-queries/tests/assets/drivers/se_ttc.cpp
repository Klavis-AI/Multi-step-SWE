/* se_ttc.cpp - SE_GetTimeToCollision, new at this step's target release.
 *
 * usage: se_ttc <scenario.xosc> <mode>
 *   mode "static"  : probe every coordinate system, every distance type, both
 *                    free-space settings, the reversed argument order, and
 *                    three failure inputs, all at t=0.
 *   mode "dynamic" : step a closing-then-separating pair and print the TTC at
 *                    chosen simulation times.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esminiLib.hpp"

/* Each probe is reported twice: once as a single human-readable line, and once
 * as `key=value` lines so a case can compare the number with need_close
 * instead of pinning a printf format. */
static void probe(const char* tag, int a, int b, int cs, SE_RelativeDistanceType dt, bool fs)
{
    double ttc = -99.0;
    int r = SE_GetTimeToCollision(a, b, cs, dt, fs, &ttc);
    std::printf("%s rc=%d ttc=%.6f\n", tag, r, ttc);
    std::printf("%s_rc=%d\n", tag, r);
    std::printf("%s_ttc=%.6f\n", tag, ttc);
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <scenario> <static|dynamic>\n", argv[0]); return 2; }

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0) return 1;

    if (std::strcmp(argv[2], "static") == 0)
    {
        /* A null output pointer is a documented failure. */
        std::printf("null_ttc=%d\n", SE_GetTimeToCollision(0, 1, 3, REL_DIST_LONGITUDINAL, false, nullptr));

        for (int cs = 0; cs <= 5; cs++)
        {
            char tag[16];
            std::snprintf(tag, sizeof(tag), "cs%d", cs);
            probe(tag, 0, 1, cs, REL_DIST_LONGITUDINAL, false);
        }
        for (int dt = 0; dt <= 4; dt++)
        {
            char tag[16];
            std::snprintf(tag, sizeof(tag), "dt%d", dt);
            probe(tag, 0, 1, 3, (SE_RelativeDistanceType)dt, false);
        }
        for (int fs = 0; fs <= 1; fs++)
        {
            char tag[16];
            std::snprintf(tag, sizeof(tag), "fs%d", fs);
            probe(tag, 0, 1, 3, REL_DIST_LONGITUDINAL, fs != 0);
        }
        probe("reversed", 1, 0, 3, REL_DIST_LONGITUDINAL, false);
        double scratch = -99.0;
        std::printf("bad_target=%d\n", SE_GetTimeToCollision(0, 99, 3, REL_DIST_LONGITUDINAL, false, &scratch));
        std::printf("bad_source=%d\n", SE_GetTimeToCollision(99, 0, 3, REL_DIST_LONGITUDINAL, false, &scratch));
        std::printf("both_bad=%d\n", SE_GetTimeToCollision(99, 98, 3, REL_DIST_LONGITUDINAL, false, &scratch));
    }
    else
    {
        /* Sample at t=0, 2, 5 and 9 s. The follower brakes from 3 s, so the
         * pair closes early and separates late. */
        const double      want[] = { 0.0, 2.0, 5.0, 9.0 };
        const char* const key[]  = { "t0", "t2", "t5", "t9" };
        double next = want[0];
        int wi = 0;
        for (int step = 0; step <= 200 && wi < 4; step++)
        {
            if (step > 0) SE_StepDT(0.05f);
            double t = SE_GetSimulationTime();
            if (t + 1e-6 >= next)
            {
                double ttc = -99.0;
                int r = SE_GetTimeToCollision(0, 1, 3, REL_DIST_LONGITUDINAL, false, &ttc);
                std::printf("t%.2f rc=%d ttc=%.6f\n", t, r, ttc);
                std::printf("%s_rc=%d\n", key[wi], r);
                std::printf("%s_ttc=%.6f\n", key[wi], ttc);
                double ttcfs = -99.0;
                int rfs = SE_GetTimeToCollision(0, 1, 3, REL_DIST_LONGITUDINAL, true, &ttcfs);
                std::printf("t%.2f free rc=%d ttc=%.6f\n", t, rfs, ttcfs);
                std::printf("%s_free_rc=%d\n", key[wi], rfs);
                std::printf("%s_free_ttc=%.6f\n", key[wi], ttcfs);
                wi++;
                if (wi < 4) next = want[wi];
            }
        }
    }

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

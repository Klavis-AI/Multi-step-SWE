/*
 * se_wheel_record - proves SE_ReportObjectWheelStatus is actually stored, not
 * merely accepted.
 *
 * usage: se_wheel_record <xosc>
 *
 * A stub that returns the documented status codes without keeping the
 * reported angle anywhere would pass a check that only reads those codes
 * back. The only place a candidate's implementation can be observed keeping
 * the value is the recording it
 * writes: this driver records the run (SE_Init's own `record` flag, the same
 * mechanism `--record` on the CLI uses) while reporting a fixed, nonzero
 * wheel angle for object 0 every step and reporting nothing for object 1, so
 * the two entities' `wheel_angle` columns in the dat2csv output must differ.
 */
#include <cstdio>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <xosc>\n", argv[0]);
        return 2;
    }

    /* record=1: SE_Init writes FileNameWithoutExtOf(oscFilename) + ".dat" in
       the current working directory. */
    int rc = SE_Init(argv[1], 0, 0, 0, 1);
    std::printf("init=%d\n", rc);
    if (rc != 0)
        return 1;

    for (int i = 0; i < 8; i++)
    {
        /* Object 0 (Ego) gets a fixed, nonzero angle every step. Object 1
           (Target) is never told anything, so its column stays at whatever
           an implementation initializes it to. */
        int wrc = SE_ReportObjectWheelStatus(0, 1.2f, 0.35f);
        if (i == 0)
            std::printf("wheel_rc=%d\n", wrc);
        SE_Step();
    }

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

/* se_time - simulation time and the time step, at 64-bit precision.
 *
 * usage: se_time <xosc> <steps> <dt>
 *
 * Two separate claims are made here and both are checked:
 *
 *   - a static assertion on the *declared* return type of SE_GetSimulationTime
 *     and SE_GetSimTimeStep. This is a compile-time fact about the header, and
 *     a header that still hands back a 32-bit float does not get past it.
 *   - the *value*: stepping N times by dt must land on N*dt to a tolerance no
 *     32-bit accumulator can reach. 0.001 is not representable in binary
 *     floating point, so the error left after a few thousand additions is the
 *     direct fingerprint of the width the engine reports in.
 *
 * The driver never prints an expected value: the case supplies N and dt and
 * owns the arithmetic.
 */
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "esminiLib.hpp"

static_assert(std::is_same<decltype(SE_GetSimulationTime()), double>::value,
              "SE_GetSimulationTime must return double");
static_assert(std::is_same<decltype(SE_GetSimTimeStep()), double>::value, "SE_GetSimTimeStep must return double");

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <xosc> <steps> <dt>\n", argv[0]);
        return 2;
    }
    const int    steps = std::atoi(argv[2]);
    const double dt    = std::atof(argv[3]);

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    std::printf("t0=%.17g\n", SE_GetSimulationTime());

    int stepped = 0;
    for (int i = 0; i < steps; i++)
    {
        if (SE_StepDT(dt) != 0)
        {
            break;
        }
        stepped++;
    }
    std::printf("stepped=%d\n", stepped);
    std::printf("t=%.17g\n", SE_GetSimulationTime());
    /* Reading the clock must not advance it. */
    std::printf("t_again=%.17g\n", SE_GetSimulationTime());

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

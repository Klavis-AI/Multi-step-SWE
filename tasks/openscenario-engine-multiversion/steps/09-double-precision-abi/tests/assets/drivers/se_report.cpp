/* se_report - the Report* entry points: no timestamp argument, 64-bit values.
 *
 * usage: se_report <xosc> <x> <y> <speed> <rotation> <angle>
 *
 * The shape of the call is itself the assertion. SE_ReportObjectPos is invoked
 * with exactly seven arguments (id plus six coordinates) and SE_ReportObjectVel
 * with four; a declaration that still carries a leading timestamp does not
 * accept those calls and this translation unit does not compile against it.
 *
 * The width is asserted twice over: statically, on the declared parameter type,
 * and dynamically, by reporting a value whose decimal expansion needs more than
 * 24 bits of mantissa and reading it back through SE_GetObjectState. A build
 * that narrows anywhere along that path returns a different number.
 *
 * Everything printed is what the library returned. The case owns the
 * comparison against what it asked for.
 */
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "esminiLib.hpp"

static_assert(std::is_same<decltype(&SE_ReportObjectPos), int (*)(int, double, double, double, double, double, double)>::value,
              "SE_ReportObjectPos must take (int, double x6) - no timestamp, no float");
static_assert(std::is_same<decltype(&SE_ReportObjectSpeed), int (*)(int, double)>::value, "SE_ReportObjectSpeed must take (int, double)");
static_assert(std::is_same<decltype(&SE_ReportObjectWheelStatus), int (*)(int, double, double)>::value,
              "SE_ReportObjectWheelStatus must take (int, double, double)");
static_assert(std::is_same<decltype(&SE_ReportObjectRoadPos), int (*)(int, id_t, int, double, double)>::value,
              "SE_ReportObjectRoadPos must take (int, id_t, int, double, double)");

int main(int argc, char **argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr, "usage: %s <xosc> <x> <y> <speed> <rotation> <angle>\n", argv[0]);
        return 2;
    }
    const double x        = std::atof(argv[2]);
    const double y        = std::atof(argv[3]);
    const double speed    = std::atof(argv[4]);
    const double rotation = std::atof(argv[5]);
    const double angle    = std::atof(argv[6]);

    /* Nothing loaded: every Report* must say so rather than crash. */
    std::printf("pos_before=%d\n", SE_ReportObjectPos(0, x, y, 0.0, 0.0, 0.0, 0.0));
    std::printf("speed_before=%d\n", SE_ReportObjectSpeed(0, speed));
    std::printf("wheel_before=%d\n", SE_ReportObjectWheelStatus(0, rotation, angle));

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    SE_StepDT(0.05);

    /* An entity that exists, and one that does not. */
    std::printf("pos_rc=%d\n", SE_ReportObjectPos(0, x, y, 0.0, 0.0, 0.0, 0.0));
    std::printf("pos_rc_bad=%d\n", SE_ReportObjectPos(4242, x, y, 0.0, 0.0, 0.0, 0.0));
    std::printf("speed_rc=%d\n", SE_ReportObjectSpeed(0, speed));
    std::printf("speed_rc_bad=%d\n", SE_ReportObjectSpeed(4242, speed));
    std::printf("wheel_rc=%d\n", SE_ReportObjectWheelStatus(0, rotation, angle));
    std::printf("wheel_rc_bad=%d\n", SE_ReportObjectWheelStatus(4242, rotation, angle));

    /* The reported state becomes visible to the reader after a step. */
    SE_StepDT(0.05);

    SE_ScenarioObjectState st;
    std::printf("get_rc=%d\n", SE_GetObjectState(0, &st));
    std::printf("x=%.17g\n", st.x);
    std::printf("y=%.17g\n", st.y);
    std::printf("speed=%.17g\n", st.speed);
    /* The struct's wheel_angle / wheel_rot members are deliberately not read:
     * no document of this ladder requires SE_ScenarioObjectState to carry them
     * (the wheel state this project does publish is SE_ReportObjectWheelStatus
     * and the recording's wheel columns, both of which are graded elsewhere),
     * so a build that never added them is conforming and must not be failed by
     * a driver that would not compile against its header. */

    /* Road coordinates through the other Report* shape, on the second entity. */
    const double s_report = 234.56789012345;
    std::printf("roadpos_rc=%d\n", SE_ReportObjectRoadPos(1, 1u, -1, 0.0, s_report));
    SE_StepDT(0.05);
    SE_ScenarioObjectState st1;
    std::printf("get_rc1=%d\n", SE_GetObjectState(1, &st1));
    std::printf("s1=%.17g\n", st1.s);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

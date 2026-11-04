/* se_state - the scenario-engine state structs at 64-bit width.
 *
 * usage: se_state <xosc> <x> <y> <h> <dt>
 *
 * SE_ScenarioObjectState and SE_RoadInfo are the two structs a reader of the
 * scenario engine actually looks at, and every real-valued member of both is
 * 64-bit. Two independent claims are made and both are checked:
 *
 *   - compile time: static assertions on the *declared* member types and on the
 *     declared signatures of SE_StepDT, SE_ReportObjectPosXYH and
 *     SE_ReportObjectPosMode. A header that still declares those members as
 *     32-bit, or that still carries a leading timestamp parameter on the
 *     reporters, does not get past them and this translation unit does not
 *     build at all.
 *   - run time: a world position whose decimal expansion needs more than 24
 *     bits of mantissa is pushed in through SE_ReportObjectPosXYH and read back
 *     out through SE_GetObjectState. A narrowing anywhere on that path returns
 *     a visibly different number.
 *
 * The driver never prints an expected value; the case supplies the inputs and
 * owns the comparison.
 */
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "esminiLib.hpp"

/* Every real-valued member of the object-state struct is 64-bit. */
static_assert(std::is_same<decltype(SE_ScenarioObjectState::x), double>::value, "SE_ScenarioObjectState::x must be double");
static_assert(std::is_same<decltype(SE_ScenarioObjectState::y), double>::value, "SE_ScenarioObjectState::y must be double");
static_assert(std::is_same<decltype(SE_ScenarioObjectState::h), double>::value, "SE_ScenarioObjectState::h must be double");
static_assert(std::is_same<decltype(SE_ScenarioObjectState::s), double>::value, "SE_ScenarioObjectState::s must be double");
static_assert(std::is_same<decltype(SE_ScenarioObjectState::speed), double>::value, "SE_ScenarioObjectState::speed must be double");
static_assert(std::is_same<decltype(SE_ScenarioObjectState::timestamp), double>::value, "SE_ScenarioObjectState::timestamp must be double");
static_assert(std::is_same<decltype(SE_ScenarioObjectState::length), double>::value, "SE_ScenarioObjectState::length must be double");
/* The bounding-box dimensions widen with the rest of the struct: the
 * instruction names length, width and height together, so all three are
 * asserted rather than only the one the runtime check reads back. */
static_assert(std::is_same<decltype(SE_ScenarioObjectState::width), double>::value, "SE_ScenarioObjectState::width must be double");
static_assert(std::is_same<decltype(SE_ScenarioObjectState::height), double>::value, "SE_ScenarioObjectState::height must be double");

/* And of the road-info struct the lookahead fills in. */
static_assert(std::is_same<decltype(SE_RoadInfo::global_pos_x), double>::value, "SE_RoadInfo::global_pos_x must be double");
static_assert(std::is_same<decltype(SE_RoadInfo::s), double>::value, "SE_RoadInfo::s must be double");
static_assert(std::is_same<decltype(SE_RoadInfo::curvature), double>::value, "SE_RoadInfo::curvature must be double");
static_assert(std::is_same<decltype(SE_RoadInfo::speed_limit), double>::value, "SE_RoadInfo::speed_limit must be double");

/* The step and the position reporters: 64-bit, and no timestamp parameter. */
static_assert(std::is_same<decltype(&SE_StepDT), int (*)(double)>::value, "SE_StepDT must take a double timestep");
static_assert(std::is_same<decltype(&SE_ReportObjectPosXYH), int (*)(int, double, double, double)>::value,
              "SE_ReportObjectPosXYH must take (int, double x3) - no timestamp");
static_assert(std::is_same<decltype(&SE_ReportObjectPosMode), int (*)(int, double, double, double, double, double, double, int)>::value,
              "SE_ReportObjectPosMode must take (int, double x6, int mode) - no timestamp");
static_assert(std::is_same<decltype(&SE_GetRoadInfoAtDistance), int (*)(int, double, SE_RoadInfo*, int, bool)>::value,
              "SE_GetRoadInfoAtDistance must take a double lookahead distance");
/* The route-aware lookahead step 08 added is on the same surface and widens
 * with it; it fills the same SE_RoadInfo. */
static_assert(std::is_same<decltype(&SE_GetRoadInfoAlongRoute), int (*)(int, double, SE_RoadInfo*, int, bool)>::value,
              "SE_GetRoadInfoAlongRoute must take a double lookahead distance");

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: %s <xosc> <x> <y> <h> <dt>\n", argv[0]);
        return 2;
    }
    const double x  = std::atof(argv[2]);
    const double y  = std::atof(argv[3]);
    const double h  = std::atof(argv[4]);
    const double dt = std::atof(argv[5]);

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    std::printf("nobj=%d\n", SE_GetNumberOfObjects());

    /* Four arguments: id plus x, y, h. A declaration that still carries a
     * leading timestamp does not accept this call. */
    std::printf("xyh_rc=%d\n", SE_ReportObjectPosXYH(0, x, y, h));
    std::printf("step_rc=%d\n", SE_StepDT(dt));

    SE_ScenarioObjectState st;
    std::printf("get_rc=%d\n", SE_GetObjectState(0, &st));
    std::printf("x=%.17g\n", st.x);
    std::printf("y=%.17g\n", st.y);
    std::printf("h=%.17g\n", st.h);
    std::printf("length=%.17g\n", st.length);
    std::printf("width=%.17g\n", st.width);
    std::printf("height=%.17g\n", st.height);
    std::printf("timestamp=%.17g\n", st.timestamp);

    /* Eight arguments: id, six coordinates and the position mode. */
    std::printf("posmode_rc=%d\n", SE_ReportObjectPosMode(0, x, y, 0.0, h, 0.0, 0.0, SE_Z_ABS | SE_H_ABS | SE_P_ABS | SE_R_ABS));
    std::printf("step2_rc=%d\n", SE_StepDT(dt));
    SE_ScenarioObjectState st2;
    std::printf("get2_rc=%d\n", SE_GetObjectState(0, &st2));
    std::printf("x2=%.17g\n", st2.x);
    std::printf("y2=%.17g\n", st2.y);

    /* The lookahead takes a double distance and reports into a double struct. */
    SE_RoadInfo ri;
    std::printf("roadinfo_rc=%d\n", SE_GetRoadInfoAtDistance(0, 12.5, &ri, 0, true));
    std::printf("ri_s=%.17g\n", ri.s);
    std::printf("ri_x=%.17g\n", ri.global_pos_x);
    std::printf("ri_curv=%.17g\n", ri.curvature);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

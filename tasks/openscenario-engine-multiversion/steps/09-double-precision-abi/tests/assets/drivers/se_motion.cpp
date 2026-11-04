/* se_motion - the derivative getters and reporters at 64-bit width.
 *
 * usage: se_motion <xosc> <vx> <vy> <hrate> <steps> <dt>
 *
 * The motion half of the C ABI is the part that carries velocity, acceleration,
 * odometer and route length. All of it is 64-bit, and the three-value reporters
 * lost their leading timestamp parameter at the same time.
 *
 * Compile time: static assertions on the declared out-parameter and return
 * types, and on the *arity* of the reporters -- SE_ReportObjectVel and
 * SE_ReportObjectAngularVel take four arguments, not five.
 *
 * Run time: a velocity whose decimal expansion needs more than 24 bits of
 * mantissa is pushed in and read straight back out. A 32-bit path returns a
 * number that differs in the seventh significant digit.
 *
 * Assumption recorded by the author: the reported velocity is observable
 * through SE_GetObjectVelocityGlobalXYZ only for an entity under external
 * control of its own reported state, so the driver reports and reads back on
 * the same entity within the same step and prints whatever came out. The case
 * owns what that number has to be.
 */
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "esminiLib.hpp"

/* Scalar getters return 64-bit. */
static_assert(std::is_same<decltype(SE_GetObjectOdometer(0)), double>::value, "SE_GetObjectOdometer must return double");
static_assert(std::is_same<decltype(SE_GetObjectAcceleration(0)), double>::value, "SE_GetObjectAcceleration must return double");
static_assert(std::is_same<decltype(SE_GetRouteTotalLength(0)), double>::value, "SE_GetRouteTotalLength must return double");

/* Out-parameter getters hand back 64-bit. */
static_assert(std::is_same<decltype(&SE_GetObjectVelocityGlobalXYZ), int (*)(int, double*, double*, double*)>::value,
              "SE_GetObjectVelocityGlobalXYZ must take double* out-parameters");
static_assert(std::is_same<decltype(&SE_GetObjectAngularVelocity), int (*)(int, double*, double*, double*)>::value,
              "SE_GetObjectAngularVelocity must take double* out-parameters");
static_assert(std::is_same<decltype(&SE_GetObjectAccelerationGlobalXYZ), int (*)(int, double*, double*, double*)>::value,
              "SE_GetObjectAccelerationGlobalXYZ must take double* out-parameters");
static_assert(std::is_same<decltype(&SE_GetObjectAccelerationLocalLatLong), int (*)(int, double*, double*)>::value,
              "SE_GetObjectAccelerationLocalLatLong must take double* out-parameters");

/* Reporters: 64-bit, four arguments, no timestamp. */
static_assert(std::is_same<decltype(&SE_ReportObjectVel), int (*)(int, double, double, double)>::value,
              "SE_ReportObjectVel must take (int, double x3) - no timestamp");
static_assert(std::is_same<decltype(&SE_ReportObjectAngularVel), int (*)(int, double, double, double)>::value,
              "SE_ReportObjectAngularVel must take (int, double x3) - no timestamp");
static_assert(std::is_same<decltype(&SE_ReportObjectAcc), int (*)(int, double, double, double)>::value,
              "SE_ReportObjectAcc must take (int, double x3) - no timestamp");
static_assert(std::is_same<decltype(&SE_ReportObjectLateralPosition), int (*)(int, double)>::value,
              "SE_ReportObjectLateralPosition must take (int, double)");

int main(int argc, char** argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr, "usage: %s <xosc> <vx> <vy> <hrate> <steps> <dt>\n", argv[0]);
        return 2;
    }
    const double vx    = std::atof(argv[2]);
    const double vy    = std::atof(argv[3]);
    const double hrate = std::atof(argv[4]);
    const int    steps = std::atoi(argv[5]);
    const double dt    = std::atof(argv[6]);

    /* Nothing loaded: the getters must say so rather than crash. */
    double dummy = 0.0;
    std::printf("vel_before=%d\n", SE_GetObjectVelocityGlobalXYZ(0, &dummy, &dummy, &dummy));
    std::printf("rate_before=%d\n", SE_GetObjectAngularVelocity(0, &dummy, &dummy, &dummy));

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    /* Four arguments each. A declaration that still carries a leading
     * timestamp does not accept these calls. */
    std::printf("repvel_rc=%d\n", SE_ReportObjectVel(0, vx, vy, 0.0));
    std::printf("repvel_rc_bad=%d\n", SE_ReportObjectVel(4242, vx, vy, 0.0));
    std::printf("repang_rc=%d\n", SE_ReportObjectAngularVel(0, hrate, 0.0, 0.0));
    std::printf("repang_rc_bad=%d\n", SE_ReportObjectAngularVel(4242, hrate, 0.0, 0.0));

    double gx = 0.0, gy = 0.0, gz = 0.0;
    std::printf("vel_rc=%d\n", SE_GetObjectVelocityGlobalXYZ(0, &gx, &gy, &gz));
    std::printf("vel_x=%.17g\n", gx);
    std::printf("vel_y=%.17g\n", gy);

    double hr = 0.0, pr = 0.0, rr = 0.0;
    std::printf("rate_rc=%d\n", SE_GetObjectAngularVelocity(0, &hr, &pr, &rr));
    std::printf("rate_h=%.17g\n", hr);

    double alat = 0.0, alon = 0.0;
    std::printf("acclat_rc=%d\n", SE_GetObjectAccelerationLocalLatLong(0, &alat, &alon));
    std::printf("acc_lat=%.17g\n", alat);

    /* The odometer accumulates in 64 bits: entity 1 runs at a constant speed,
     * so after N steps of dt it has covered N*dt*speed exactly. */
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
    std::printf("odo0=%.17g\n", SE_GetObjectOdometer(0));
    std::printf("odo1=%.17g\n", SE_GetObjectOdometer(1));
    std::printf("acc0=%.17g\n", SE_GetObjectAcceleration(0));
    std::printf("routelen0=%.17g\n", SE_GetRouteTotalLength(0));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

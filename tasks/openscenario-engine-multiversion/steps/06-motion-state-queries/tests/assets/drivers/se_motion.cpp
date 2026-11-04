/*
 * se_motion - the motion-state queries this release adds to esminiLib:
 * travelled distance, global velocity vector, angular velocity and angular
 * acceleration.
 *
 * usage: se_motion <xosc> <steps> <dt> [object_id]
 *
 * Loads the scenario, advances it with fixed steps and reports the motion
 * state of the object at the end of the run, plus a few facts gathered over
 * the whole run (worst monotonicity violation of the odometer, largest
 * magnitude the heading acceleration reached). Nothing expected is printed --
 * only what the library returned.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <xosc> <steps> <dt> [object_id]\n", argv[0]);
        return 2;
    }
    const char *xosc  = argv[1];
    int         steps = std::atoi(argv[2]);
    float       dt    = static_cast<float>(std::atof(argv[3]));
    int         id    = (argc > 4) ? std::atoi(argv[4]) : 0;

    int rc = SE_Init(xosc, 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    /* State before any step has been taken: nothing has been travelled yet. */
    std::printf("odo_start=%.4f\n", static_cast<double>(SE_GetObjectOdometer(id)));

    float  prev_odo   = SE_GetObjectOdometer(id);
    int    monotonic  = 1;
    double hacc_peak  = 0.0;
    double hrate_peak = 0.0;

    float h_acc = 0.0f, p_acc = 0.0f, r_acc = 0.0f;
    float h_rate = 0.0f, p_rate = 0.0f, r_rate = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    int   rc_vel = -99, rc_angvel = -99, rc_angacc = -99;

    for (int i = 0; i < steps; i++)
    {
        if (SE_StepDT(dt) != 0)
        {
            std::printf("step_failed_at=%d\n", i);
            break;
        }

        float odo = SE_GetObjectOdometer(id);
        if (odo < prev_odo - 1e-4f)
        {
            monotonic = 0;
        }
        prev_odo = odo;

        rc_angacc = SE_GetObjectAngularAcceleration(id, &h_acc, &p_acc, &r_acc);
        if (std::fabs(static_cast<double>(h_acc)) > hacc_peak)
        {
            hacc_peak = std::fabs(static_cast<double>(h_acc));
        }
        rc_angvel = SE_GetObjectAngularVelocity(id, &h_rate, &p_rate, &r_rate);
        if (std::fabs(static_cast<double>(h_rate)) > hrate_peak)
        {
            hrate_peak = std::fabs(static_cast<double>(h_rate));
        }
    }

    std::printf("sim_time=%.4f\n", static_cast<double>(SE_GetSimulationTime()));

    SE_ScenarioObjectState st;
    SE_GetObjectState(id, &st);
    std::printf("speed=%.4f\n", static_cast<double>(st.speed));
    std::printf("pos_x=%.4f\n", static_cast<double>(st.x));
    std::printf("pos_y=%.4f\n", static_cast<double>(st.y));
    std::printf("pos_s=%.4f\n", static_cast<double>(st.s));
    std::printf("pos_h=%.5f\n", static_cast<double>(st.h));

    std::printf("odo=%.4f\n", static_cast<double>(SE_GetObjectOdometer(id)));
    std::printf("odo_monotonic=%d\n", monotonic);

    rc_vel = SE_GetObjectVelocityGlobalXYZ(id, &vx, &vy, &vz);
    std::printf("rc_vel=%d\n", rc_vel);
    std::printf("vel_x=%.4f\n", static_cast<double>(vx));
    std::printf("vel_y=%.4f\n", static_cast<double>(vy));
    std::printf("vel_z=%.4f\n", static_cast<double>(vz));
    std::printf("vel_norm=%.4f\n", std::sqrt(static_cast<double>(vx * vx + vy * vy + vz * vz)));

    std::printf("rc_angvel=%d\n", rc_angvel);
    std::printf("h_rate=%.5f\n", static_cast<double>(h_rate));
    std::printf("p_rate=%.5f\n", static_cast<double>(p_rate));
    std::printf("r_rate=%.5f\n", static_cast<double>(r_rate));
    std::printf("h_rate_peak=%.5f\n", hrate_peak);

    std::printf("rc_angacc=%d\n", rc_angacc);
    std::printf("h_acc=%.5f\n", static_cast<double>(h_acc));
    std::printf("p_acc=%.5f\n", static_cast<double>(p_acc));
    std::printf("r_acc=%.5f\n", static_cast<double>(r_acc));
    std::printf("h_acc_peak=%.5f\n", hacc_peak);

    /* The same four queries for an object id no scenario defines. The
     * pointer-out forms report failure through the return code; the odometer
     * has no return code to spend, so it answers with a NaN. */
    float dummy_a = 12345.0f, dummy_b = 12345.0f, dummy_c = 12345.0f;
    std::printf("rc_vel_bad=%d\n", SE_GetObjectVelocityGlobalXYZ(4242, &dummy_a, &dummy_b, &dummy_c));
    std::printf("rc_angvel_bad=%d\n", SE_GetObjectAngularVelocity(4242, &dummy_a, &dummy_b, &dummy_c));
    std::printf("rc_angacc_bad=%d\n", SE_GetObjectAngularAcceleration(4242, &dummy_a, &dummy_b, &dummy_c));
    std::printf("odo_bad_is_nan=%d\n", std::isnan(static_cast<double>(SE_GetObjectOdometer(4242))) ? 1 : 0);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

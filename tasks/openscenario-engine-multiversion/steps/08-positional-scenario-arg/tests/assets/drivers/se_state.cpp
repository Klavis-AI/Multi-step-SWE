/*
 * se_state - scenario-engine entry points an earlier step established.
 * Inherited-regression driver: it must compile and pass at BOTH tags of this
 * step, so nothing it calls may be new at the target release.
 *
 * usage: se_state <xosc> <max_deceleration>
 *
 * Covers three obligations in one run:
 *   - the scenario lifecycle (SE_Init / SE_StepDT / SE_GetObjectState /
 *     SE_GetSimulationTime / SE_Close) on a stepped headless scenario;
 *   - the externally reported wheel state and the pause query, both of which
 *     answer differently before and after a scenario is loaded;
 *   - the deceleration ceiling on the built-in simple vehicle.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: %s <xosc> <max_deceleration>\n", argv[0]);
        return 2;
    }
    float max_decel = static_cast<float>(std::atof(argv[2]));

    /* Nothing loaded yet. */
    std::printf("pause_before=%d\n", SE_GetPauseFlag());
    std::printf("wheel_before=%d\n", SE_ReportObjectWheelStatus(0, 1.0f, 0.1f));

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    std::printf("nobjects=%d\n", SE_GetNumberOfObjects());

    /* 4 s of a 0.05 s fixed step. */
    for (int i = 0; i < 80; i++)
    {
        SE_StepDT(0.05f);
    }
    std::printf("sim_time=%.3f\n", static_cast<double>(SE_GetSimulationTime()));
    std::printf("pause_after=%d\n", SE_GetPauseFlag());
    std::printf("quit_after=%d\n", SE_GetQuitFlag());

    SE_ScenarioObjectState st;
    std::printf("get_state=%d\n", SE_GetObjectState(0, &st));
    std::printf("obj0_id=%d\n", st.id);
    std::printf("obj0_speed=%.3f\n", static_cast<double>(st.speed));
    std::printf("obj0_x=%.3f\n", static_cast<double>(st.x));

    /* Three distinct answers from one entry point: before a load, for a live
     * object, and for an id that names nothing. */
    std::printf("wheel_valid=%d\n", SE_ReportObjectWheelStatus(0, 1.5f, 0.2f));
    std::printf("wheel_invalid=%d\n", SE_ReportObjectWheelStatus(4242, 1.5f, 0.2f));

    SE_Close();

    /* The simple vehicle is independent of any loaded scenario. */
    const float start_speed = 30.0f;
    void*       v           = SE_SimpleVehicleCreate(0.0f, 0.0f, 0.0f, 4.0f, start_speed);
    if (v == nullptr)
    {
        std::printf("create=0\n");
        return 1;
    }
    std::printf("create=1\n");
    SE_SimpleVehicleSetMaxDeceleration(v, max_decel);

    SE_SimpleVehicleState vs;
    SE_SimpleVehicleGetState(v, &vs);
    std::printf("speed_0=%.4f\n", static_cast<double>(vs.speed));

    const double dt         = 0.05;
    float        prev       = vs.speed;
    float        worst_drop = 0.0f;
    for (int i = 0; i < 20; i++)
    {
        SE_SimpleVehicleControlBinary(v, dt, -1 /* brake */, 0 /* straight */);
        SE_SimpleVehicleGetState(v, &vs);
        float drop = prev - vs.speed;
        if (drop > worst_drop)
        {
            worst_drop = drop;
        }
        prev = vs.speed;
    }
    std::printf("speed_final=%.4f\n", static_cast<double>(vs.speed));
    std::printf("worst_decel=%.4f\n", static_cast<double>(worst_drop) / dt);

    SE_SimpleVehicleDelete(v);
    std::printf("survived=1\n");
    return 0;
}

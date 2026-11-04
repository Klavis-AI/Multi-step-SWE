/*
 * se_simple_vehicle - SE_SimpleVehicleSetMaxDeceleration.
 *
 * usage: se_simple_vehicle <max_deceleration> <steps> <dt>
 *
 * Creates a simple vehicle at a known speed, applies full brake through
 * SE_SimpleVehicleControlBinary and prints the speed after each step. The
 * deceleration cap is observable as the slope of that sequence: with a cap of
 * D m/s^2 the speed cannot fall by more than D*dt in one step of dt seconds.
 *
 * Only SE_SimpleVehicleState::speed is read. The struct's other members are
 * not part of what this step is about, and two of them carry a spelling
 * upstream has never corrected.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <max_deceleration> <steps> <dt>\n", argv[0]);
        return 2;
    }
    float  max_decel = static_cast<float>(std::atof(argv[1]));
    int    steps     = std::atoi(argv[2]);
    double dt        = std::atof(argv[3]);

    const float start_speed = 30.0f;
    void       *v           = SE_SimpleVehicleCreate(0.0f, 0.0f, 0.0f, 4.0f, start_speed);
    if (v == nullptr)
    {
        std::printf("create=0\n");
        return 1;
    }
    std::printf("create=1\n");

    SE_SimpleVehicleSetMaxDeceleration(v, max_decel);

    SE_SimpleVehicleState st;
    SE_SimpleVehicleGetState(v, &st);
    std::printf("speed_0=%.4f\n", static_cast<double>(st.speed));

    float prev = st.speed;
    float worst_drop = 0.0f;
    for (int i = 1; i <= steps; i++)
    {
        SE_SimpleVehicleControlBinary(v, dt, -1 /* brake */, 0 /* straight */);
        SE_SimpleVehicleGetState(v, &st);
        std::printf("speed_%d=%.4f\n", i, static_cast<double>(st.speed));
        float drop = prev - st.speed;
        if (drop > worst_drop)
            worst_drop = drop;
        prev = st.speed;
    }
    std::printf("speed_final=%.4f\n", static_cast<double>(st.speed));
    std::printf("worst_drop_per_step=%.4f\n", static_cast<double>(worst_drop));
    std::printf("worst_decel=%.4f\n", static_cast<double>(worst_drop) / dt);

    SE_SimpleVehicleDelete(v);
    std::printf("survived=1\n");
    return 0;
}

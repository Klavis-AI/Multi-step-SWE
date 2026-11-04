/*
 * se_acc_steer - driving the built-in simple vehicle by explicit acceleration
 * and steering angle.
 *
 * usage: se_acc_steer <acceleration> <steering_angle> <steps> <dt>
 *
 * The existing binary/analog controls take a throttle in [-1, 1] and let the
 * model decide what acceleration that is. This entry point takes the
 * acceleration itself, so the speed after N steps is arithmetic rather than a
 * model output, and the driver prints the speed at every step so the case can
 * see the whole ramp rather than only its endpoint.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"
/* TEST_INTERFACE.md section 3 requires each header to be includable on its own
 * AND both together from one C++17 translation unit. This driver takes both, so
 * the "together" half is compiled rather than assumed; the road-manager entry
 * points are not called here. */
#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <acceleration> <steering_angle> <steps> <dt>\n", argv[0]);
        return 2;
    }
    double acc   = std::atof(argv[1]);
    double steer = std::atof(argv[2]);
    int    steps = std::atoi(argv[3]);
    double dt    = std::atof(argv[4]);

    const float start_speed = 10.0f;
    void       *v           = SE_SimpleVehicleCreate(0.0f, 0.0f, 0.0f, 4.0f, start_speed);
    if (v == nullptr)
    {
        std::printf("create=0\n");
        return 1;
    }
    std::printf("create=1\n");

    SE_SimpleVehicleState st;
    SE_SimpleVehicleGetState(v, &st);
    std::printf("speed_0=%.4f\n", static_cast<double>(st.speed));
    std::printf("h_0=%.5f\n", static_cast<double>(st.h));

    for (int i = 1; i <= steps; i++)
    {
        SE_SimpleVehicleControlAccAndSteer(v, dt, acc, steer);
        SE_SimpleVehicleGetState(v, &st);
        std::printf("speed_%d=%.4f\n", i, static_cast<double>(st.speed));
    }

    std::printf("speed_final=%.4f\n", static_cast<double>(st.speed));
    std::printf("h_final=%.5f\n", static_cast<double>(st.h));
    std::printf("x_final=%.4f\n", static_cast<double>(st.x));
    std::printf("y_final=%.4f\n", static_cast<double>(st.y));

    /* A null handle is ignored rather than dereferenced. */
    SE_SimpleVehicleControlAccAndSteer(nullptr, dt, acc, steer);

    SE_SimpleVehicleDelete(v);
    std::printf("survived=1\n");
    return 0;
}

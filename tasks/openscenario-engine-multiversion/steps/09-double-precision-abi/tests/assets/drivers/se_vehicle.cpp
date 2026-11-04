/* se_vehicle - the simple vehicle model at 64-bit precision.
 *
 * usage: se_vehicle <x> <y> <h> <speed>
 *
 * The simple vehicle model is part of the public numeric surface: it is created
 * at a world coordinate, driven at a speed, and read back through
 * SE_SimpleVehicleState. Every one of those numbers is float at v2.59.0, so the
 * static assertions below do not compile there.
 *
 * The runtime half checks that a coordinate needing more than 24 mantissa bits
 * survives creation and readback. It does not need a scenario: the model is a
 * standalone kinematic integrator.
 */
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "esminiLib.hpp"

static_assert(std::is_same<decltype(&SE_SimpleVehicleCreate), void* (*)(double, double, double, double, double)>::value,
              "SE_SimpleVehicleCreate must take five doubles");
static_assert(std::is_same<decltype(&SE_SimpleVehicleSetSpeed), void (*)(void*, double)>::value,
              "SE_SimpleVehicleSetSpeed must take a double");
static_assert(std::is_same<decltype(&SE_SimpleVehicleSetMaxSpeed), void (*)(void*, double)>::value,
              "SE_SimpleVehicleSetMaxSpeed must take a double");
static_assert(std::is_same<decltype(&SE_SimpleVehicleSetMaxAcceleration), void (*)(void*, double)>::value,
              "SE_SimpleVehicleSetMaxAcceleration must take a double");
static_assert(std::is_same<decltype(&SE_SimpleVehicleSetMaxDeceleration), void (*)(void*, double)>::value,
              "SE_SimpleVehicleSetMaxDeceleration must take a double");
/* The integrator entry point is called below to advance the model, so its
 * declared signature is part of the surface this driver exercises. Its three
 * numeric parameters were already `double` before this step - so this one pins
 * the signature against unrelated drift rather than against the widening - and
 * it is asserted here because a signature that is merely called is otherwise
 * free to change under a driver that would keep compiling. */
static_assert(std::is_same<decltype(&SE_SimpleVehicleControlAnalog), void (*)(void*, double, double, double)>::value,
              "SE_SimpleVehicleControlAnalog must take a handle and three doubles");
static_assert(std::is_same<decltype(SE_SimpleVehicleState::x), double>::value, "SE_SimpleVehicleState::x must be double");
static_assert(std::is_same<decltype(SE_SimpleVehicleState::y), double>::value, "SE_SimpleVehicleState::y must be double");
static_assert(std::is_same<decltype(SE_SimpleVehicleState::z), double>::value, "SE_SimpleVehicleState::z must be double");
static_assert(std::is_same<decltype(SE_SimpleVehicleState::h), double>::value, "SE_SimpleVehicleState::h must be double");
static_assert(std::is_same<decltype(SE_SimpleVehicleState::p), double>::value, "SE_SimpleVehicleState::p must be double");
static_assert(std::is_same<decltype(SE_SimpleVehicleState::speed), double>::value,
              "SE_SimpleVehicleState::speed must be double");
static_assert(std::is_same<decltype(SE_SimpleVehicleState::wheel_rotation), double>::value,
              "SE_SimpleVehicleState::wheel_rotation must be double");
static_assert(std::is_same<decltype(SE_SimpleVehicleState::wheel_angle), double>::value,
              "SE_SimpleVehicleState::wheel_angle must be double");

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <x> <y> <h> <speed>\n", argv[0]);
        return 2;
    }
    const double x   = std::atof(argv[1]);
    const double y   = std::atof(argv[2]);
    const double h   = std::atof(argv[3]);
    const double spd = std::atof(argv[4]);

    void* v = SE_SimpleVehicleCreate(x, y, h, 5.0, 0.0);
    std::printf("created=%d\n", v != nullptr ? 1 : 0);
    if (v == nullptr)
    {
        return 1;
    }

    /* The creation coordinate comes straight back out: nothing has integrated
     * yet, so any difference at all is a narrowing on the way in or out. */
    SE_SimpleVehicleState s;
    SE_SimpleVehicleGetState(v, &s);
    std::printf("x0=%.17g\n", s.x);
    std::printf("y0=%.17g\n", s.y);
    std::printf("h0=%.17g\n", s.h);

    /* A speed set through the double entry point reads back at double. */
    SE_SimpleVehicleSetMaxSpeed(v, 100.0);
    SE_SimpleVehicleSetSpeed(v, spd);
    SE_SimpleVehicleGetState(v, &s);
    std::printf("speed0=%.17g\n", s.speed);

    /* One second of coasting at 0.01 s per tick. The model integrates, so this
     * only has to stay sane - the precision claim is on the two reads above. */
    for (int i = 0; i < 100; i++)
    {
        SE_SimpleVehicleControlAnalog(v, 0.01, 0.0, 0.0);
    }
    SE_SimpleVehicleGetState(v, &s);
    std::printf("x1=%.17g\n", s.x);
    std::printf("speed1=%.17g\n", s.speed);

    SE_SimpleVehicleDelete(v);
    std::printf("survived=1\n");
    return 0;
}

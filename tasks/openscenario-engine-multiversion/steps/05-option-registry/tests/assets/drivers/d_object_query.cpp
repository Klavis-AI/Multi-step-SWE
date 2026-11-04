// Extended object queries, new at this release.
//
// All of these return their value directly rather than through an out-pointer:
// wheel count as int, acceleration as float (nan when unknown), and variable
// enumeration by index.
//
// The scenario fixture (brake_car.xosc) is told to be at 20 m/s from t=0
// (a step-shaped speed action), holds that until t=2 s, then brakes linearly
// to 0 over 2 s. That gives acceleration a different, predictable sign in two
// windows from the same run, so a stub returning one constant cannot satisfy
// the case. The braking window's magnitude IS fixture-determined: the ramp is
// a linear 20 -> 0 m/s over exactly 2.0 s and the vehicle's declared
// maxDeceleration is 10 m/s^2, so the commanded rate and the
// performance-limited rate coincide at -10. The case grades that magnitude as
// well as the sign. The cruise window's magnitude is not: it reads ~0 on an
// engine that applies the step instantly and a small positive value on one
// that ramps under the vehicle's performance limits, so the driver reports a
// sign flag there and the case grades the sign, not a magnitude.
#include <esminiLib.hpp>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <xosc>\n", argv[0]);
        return 1;
    }

    // R4: the variable count is negative when no scenario is loaded. Probed
    // HERE, before the first SE_Init, because that is the only point in the
    // process at which "no scenario has ever been loaded" is true. Sign only --
    // R4 promises a negative value, not a particular one.
    printf("nvars_no_scenario_neg=%d\n", SE_GetNumberOfVariables() < 0 ? 1 : 0);

    if (SE_Init(argv[1], 0, 0, 0, 0) != 0) {
        fprintf(stderr, "SE_Init failed\n");
        return 1;
    }

    printf("nobjs=%d\n", SE_GetNumberOfObjects());

    // A live car object has four wheels; an id that does not exist must not.
    printf("wheels=%d\n", SE_GetObjectNumberOfWheels(0));
    printf("wheels_bad=%d\n", SE_GetObjectNumberOfWheels(99));

    // Variables declared by the scenario, reachable by index. The instruction
    // says the enumeration ORDER is the engine's own and not normative, so the
    // driver reports the name SET (sorted) and per-name types, and the case
    // asserts those rather than any position. Per-index lines are kept for a
    // human reading the log, but the case must not grade them.
    int nvars = SE_GetNumberOfVariables();
    printf("nvars=%d\n", nvars);
    std::vector<std::pair<std::string, int>> vars;
    for (int i = 0; i < nvars && i < 16; i++) {
        int ti = -1;
        const char* ni = SE_GetVariableName(i, &ti);
        printf("varname%d=%s\n", i, ni ? ni : "(null)");
        printf("vartype%d=%d\n", i, ti);
        if (ni) vars.emplace_back(ni, ti);
    }
    std::sort(vars.begin(), vars.end());
    printf("nvars_named=%zu\n", vars.size());
    printf("varnames_sorted=");
    for (size_t i = 0; i < vars.size(); i++) {
        printf("%s%s", i ? "," : "", vars[i].first.c_str());
    }
    printf("\n");
    for (size_t i = 0; i < vars.size(); i++) {
        printf("vartype_of_%s=%d\n", vars[i].first.c_str(), vars[i].second);
    }

    // Fixed timestep so the measured values are reproducible. Sample
    // acceleration while cruising and again mid-brake.
    const float dt = 0.05f;
    float a_cruise = 99.0f, a_brake = 99.0f;
    int   cruise_known = 0, brake_known = 0;
    float speed_cruise = 0.0f;
    SE_ScenarioObjectState st;

    for (int i = 0; i < 200 && SE_GetSimulationTime() < 6.0; i++) {
        SE_StepDT(dt);
        float t = SE_GetSimulationTime();
        if (t < 1.0f) continue;              // past the init step, before braking
        float a = SE_GetObjectAcceleration(0);
        if (std::isnan(a)) continue;
        if (t < 1.9f && !cruise_known) {
            a_cruise = a;
            SE_GetObjectState(0, &st);
            speed_cruise = st.speed;
            cruise_known = 1;
        } else if (t > 2.6f && t < 3.4f && !brake_known) {
            a_brake = a;
            brake_known = 1;
        }
    }

    printf("advance_ok=%d\n", SE_GetSimulationTime() > 5.0 ? 1 : 0);
    printf("speed_cruise=%.2f\n", speed_cruise);
    printf("cruise_known=%d\n", cruise_known);
    printf("accel_cruise=%.2f\n", a_cruise);
    // Not braking during the cruise window: zero or positive. A tiny negative
    // from numerical noise on an instantly-applied step must not count as
    // braking, hence the -0.05 floor rather than a strict >= 0.
    printf("accel_cruise_nonneg=%d\n", (cruise_known && a_cruise > -0.05f) ? 1 : 0);
    printf("brake_known=%d\n", brake_known);
    printf("accel_brake=%.2f\n", a_brake);
    printf("accel_brake_negative=%d\n", (brake_known && a_brake < 0.0f) ? 1 : 0);

    SE_Close();
    printf("survived=1\n");
    return 0;
}

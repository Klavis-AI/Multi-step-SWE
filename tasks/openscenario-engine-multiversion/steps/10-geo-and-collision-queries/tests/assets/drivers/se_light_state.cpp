/* se_light_state.cpp - SE_GetObjectLightState, new at this step's target.
 *
 * usage: se_light_state <scenario.xosc> <object_id>
 *
 * Steps the scenario in 0.05 s increments and, whenever simulation time has
 * moved on since the previous sample, prints the mode and emitting flag of
 * all fourteen vehicle light types. A counter over the whole run proves the
 * driver reached the stop trigger. Sampling is gated on time actually having
 * advanced (rather than a fixed step count) because once the storyboard
 * completes, SE_StepDT keeps returning successfully but simulation time no
 * longer moves - a fixed count would re-sample the same frozen instant
 * dozens of times.
 *
 * The sampling window is "t is within one step of a whole or of a half
 * second", and at 0.05 s per step each window is wide enough to admit TWO
 * steps, not one: the step that lands on the offset itself and the one after
 * it -- N.00 and N.05, N.50 and N.55. So a six-second run emits on the order
 * of twenty-four sample sets, not six, which is what emitted_samples reports
 * and what cases assert against. The window is written as a width rather than
 * as an equality on purpose: an exact match on t == N.00 would depend on the
 * float accumulation of 0.05 across 121 steps, which is not something any
 * document fixes.
 *
 * The half-second offset is not decoration. A flashing lamp in this step's
 * fixture runs a 0.5 s on / 0.5 s off cycle, i.e. a 1.0 s period, so a grid of
 * whole-second offsets alone is phase-locked to it: every sample lands in the
 * same half of the cycle and the emitting flag never changes across a whole
 * run, whatever the lamp is doing. Two offsets half a period apart cannot
 * share a phase whichever instant the cycle starts from, so the pair
 * N.00 / N.50 is what makes the alternation observable at all.
 *
 * A null light_state pointer is deliberately never passed to
 * SE_GetObjectLightState: the implementation dereferences it unconditionally
 * (no null check), so that call crashes rather than returning -1. That is
 * genuine undefined behaviour, not a documented failure mode, so it is not
 * exercised here.
 */
#include <cstdio>
#include <cstdlib>
#include "esminiLib.hpp"

static const char* light_name(int t)
{
    switch (t)
    {
        case DAYTIME_RUNNING_LIGHTS:     return "daytime";
        case LOW_BEAM:                   return "lowbeam";
        case HIGH_BEAM:                  return "highbeam";
        case FOG_LIGHTS:                 return "fog";
        case FOG_LIGHTS_FRONT:           return "fogfront";
        case FOG_LIGHTS_REAR:            return "fogrear";
        case BRAKE_LIGHTS:               return "brake";
        case WARNING_LIGHTS:             return "warning";
        case INDICATOR_LEFT:             return "ind_left";
        case INDICATOR_RIGHT:            return "ind_right";
        case REVERSING_LIGHTS:           return "reversing";
        case TAIL_LIGHTS:                return "tail";
        case LICENSE_PLATE_ILLUMINATION: return "license";
        case SPECIAL_PURPOSE_LIGHTS:     return "special";
        default:                         return "unknown";
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <scenario> <object_id>\n", argv[0]); return 2; }

    int oid = std::atoi(argv[2]);
    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0) return 1;

    int emitted_samples = 0;
    double last_t = -1.0;
    for (int step = 0; step <= 120; step++)
    {
        if (step > 0) SE_StepDT(0.05f);
        double t = SE_GetSimulationTime();
        int    whole = (int)(t + 1e-6);
        double frac  = t - whole;
        bool   near_whole_second = (t + 1e-6 >= whole && frac < 0.05 + 1e-6);
        bool   near_half_second  = (frac >= 0.5 - 1e-6 && frac < 0.55 + 1e-6);
        bool   time_advanced = (step == 0 || t > last_t + 1e-6);
        if ((near_whole_second || near_half_second) && time_advanced)
        {
            for (int ty = 0; ty < 14; ty++)
            {
                SE_LightState ls;
                ls.light_mode = -9;
                ls.emitting = false;
                int r = SE_GetObjectLightState(oid, (SE_VehicleLightType)ty, &ls);
                std::printf("t%.2f %s rc=%d mode=%d emit=%d\n", t, light_name(ty), r, ls.light_mode, ls.emitting ? 1 : 0);
            }
            emitted_samples++;
        }
        last_t = t;
        /* Once the storyboard completes, SE_StepDT keeps succeeding but
         * simulation time stops advancing; there is nothing further to
         * sample, so stop stepping rather than spin to the step budget. */
        if (SE_GetQuitFlag() != 0 && !time_advanced)
        {
            break;
        }
    }

    /* Unknown object is a documented failure. */
    SE_LightState ls;
    std::printf("badobj=%d\n", SE_GetObjectLightState(99, BRAKE_LIGHTS, &ls));
    std::printf("emitted_samples=%d\n", emitted_samples);

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

// Injected speed actions, new at this release.
//
// SE_InjectSpeedAction takes a struct by pointer and returns void; the caller
// observes the effect through the object's speed. SE_InjectedActionOngoing
// reports whether an injected action of a given type is still running
// (action_type -1 means "any type").
//
// The driver steps with a fixed SE_StepDT(dt), never plain SE_Step(): plain
// SE_Step() advances by wall-clock elapsed time, which is unusable for a
// deterministic driver run in a tight loop (it barely advances simulation
// time at all between two back-to-back calls).
//
// usage: d_inject <xosc> <target1> <time1> [<delay_steps> <target2> <time2>]
//        d_inject <xosc> lateral
//
// With three arguments, injects one SpeedAction to <target1> over <time1>
// seconds (linear, time-dimensioned) and steps until it settles.
// With six, injects the first action, steps <delay_steps> steps of dt=0.05s,
// then injects a second SpeedAction to <target2> over <time2> seconds -- this
// is how the release-note claim "injected action cancels any ongoing action
// of the same type" is exercised: the final speed must reach target2, never
// target1.
// With "lateral" as the second argument, runs the *type-filtered* half of the
// SE_InjectedActionOngoing contract instead: a lane offset action (type 6) and
// then a lane change action (type 5) are injected, and the ongoing query is
// made once per type, so a build that only ever returned the any-type answer
// (-1) or that ignored the action_type argument cannot produce the pattern.
// See the per-line comments below for what each printed key means.
#include <esminiLib.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const float DT = 0.05f;

// Action.hpp::ActionType -- private-action indices, which is what
// SE_InjectedActionOngoing compares against.
static const int LAT_LANE_CHANGE = 5;
static const int LAT_LANE_OFFSET = 6;
static const int LONG_SPEED      = 2;

static void print_ongoing(const char* key, int action_type) {
    printf("%s=%d\n", key, SE_InjectedActionOngoing(action_type) ? 1 : 0);
}

// The type-filtered half of the query. Injecting an offset action and a lane
// change action and querying each type in turn pins three things at once: the
// query answers per type rather than "is any action running", the type numbers
// are the ActionType indices the ABI documents, and a query for a type that is
// not running answers false even while another type is.
static int run_lateral(const char* osc) {
    if (SE_Init(osc, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "SE_Init failed\n");
        return 1;
    }
    SE_StepDT(DT);

    print_ongoing("any_before", -1);
    print_ongoing("lane_change_before", LAT_LANE_CHANGE);
    print_ongoing("lane_offset_before", LAT_LANE_OFFSET);

    SE_LaneOffsetActionStruct offset;
    offset.id               = 0;
    offset.offset           = -0.45f;
    offset.maxLateralAcc    = 0.5f;
    offset.transition_shape = 0;  // cubic
    SE_InjectLaneOffsetAction(&offset);

    print_ongoing("any_at_offset_inject", -1);
    print_ongoing("lane_offset_at_offset_inject", LAT_LANE_OFFSET);
    // The discriminating line: a lane offset action is running, but the query
    // for the lane change type must still answer false.
    print_ongoing("lane_change_at_offset_inject", LAT_LANE_CHANGE);
    // ...and a type that no action of this run could ever carry must also be
    // false, so an implementation that returns true for every positive type is
    // excluded as well.
    print_ongoing("speed_at_offset_inject", LONG_SPEED);

    SE_ScenarioObjectState st;
    SE_GetObjectState(0, &st);
    printf("lane_offset_before_mid=%.3f\n", st.laneOffset);

    // Step most of the offset transition, then stack a lane change of a
    // different type on top. The offset action is still running, so the
    // per-type answers must now differ in the other direction as well.
    for (int i = 0; i < 20; i++) {
        SE_StepDT(DT);
    }
    SE_GetObjectState(0, &st);
    // Sign, not magnitude: the offset asked for is negative (-0.45 m), so the
    // object must have moved to the NEGATIVE side of the lane center by the
    // time the lane change is stacked on top. An implementation that accepted
    // the injector's arguments and dropped them reads 0.000 here; one that
    // ignored the sign reads a positive number. The exact value depends on the
    // cubic transition's progress, which R05.5 does not promise.
    printf("lane_offset_mid_neg=%d\n", st.laneOffset < -0.05f ? 1 : 0);
    printf("lane_offset_mid=%.3f\n", st.laneOffset);

    SE_LaneChangeActionStruct change;
    change.id               = 0;
    change.mode             = 1;  // relative (own vehicle)
    change.target           = 1;  // target lane id (relative)
    change.transition_shape = 2;  // sinusoidal
    change.transition_dim   = 2;  // time
    change.transition_value = 3.0f;
    SE_InjectLaneChangeAction(&change);

    print_ongoing("any_at_change_inject", -1);
    print_ongoing("lane_change_at_change_inject", LAT_LANE_CHANGE);
    print_ongoing("lane_offset_at_change_inject", LAT_LANE_OFFSET);

    const int total_steps = 400;  // 20 s at dt=0.05
    for (int i = 0; i < total_steps; i++) {
        SE_StepDT(DT);
    }

    SE_GetObjectState(0, &st);
    printf("lane_id_final=%d\n", st.laneId);
    printf("lane_offset_final=%.3f\n", st.laneOffset);
    print_ongoing("any_after", -1);
    print_ongoing("lane_change_after", LAT_LANE_CHANGE);
    print_ongoing("lane_offset_after", LAT_LANE_OFFSET);

    SE_Close();
    printf("survived=1\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[2], "lateral") == 0) {
        return run_lateral(argv[1]);
    }
    if (argc != 4 && argc != 7) {
        fprintf(stderr, "usage: %s <xosc> <target1> <time1> [<delay_steps> <target2> <time2>]\n", argv[0]);
        fprintf(stderr, "       %s <xosc> lateral\n", argv[0]);
        return 1;
    }
    const char* osc = argv[1];
    float target1 = static_cast<float>(atof(argv[2]));
    float time1   = static_cast<float>(atof(argv[3]));

    if (SE_Init(osc, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "SE_Init failed\n");
        return 1;
    }
    SE_StepDT(DT);

    printf("ongoing_before=%d\n", SE_InjectedActionOngoing(-1) ? 1 : 0);
    SE_ScenarioObjectState st;
    SE_GetObjectState(0, &st);
    printf("speed_before=%.3f\n", st.speed);

    SE_SpeedActionStruct act1;
    act1.id = 0;
    act1.speed = target1;
    act1.transition_shape = 1;  // linear
    act1.transition_dim = 2;    // time
    act1.transition_value = time1;
    SE_InjectSpeedAction(&act1);
    printf("ongoing_at_inject=%d\n", SE_InjectedActionOngoing(-1) ? 1 : 0);

    // Enough total simulated time to let either transition settle with margin.
    const int total_steps = 400;  // 20 s at dt=0.05

    if (argc == 7) {
        int   delay_steps = atoi(argv[4]);
        float target2      = static_cast<float>(atof(argv[5]));
        float time2         = static_cast<float>(atof(argv[6]));

        for (int i = 0; i < delay_steps; i++) {
            SE_StepDT(DT);
        }
        SE_GetObjectState(0, &st);
        printf("speed_mid=%.3f\n", st.speed);

        SE_SpeedActionStruct act2;
        act2.id = 0;
        act2.speed = target2;
        act2.transition_shape = 1;
        act2.transition_dim = 2;
        act2.transition_value = time2;
        SE_InjectSpeedAction(&act2);
        printf("ongoing_at_second_inject=%d\n", SE_InjectedActionOngoing(-1) ? 1 : 0);

        for (int i = 0; i < total_steps - delay_steps; i++) {
            SE_StepDT(DT);
        }
    } else {
        for (int i = 0; i < total_steps; i++) {
            SE_StepDT(DT);
        }
    }

    printf("ongoing_after=%d\n", SE_InjectedActionOngoing(-1) ? 1 : 0);
    SE_GetObjectState(0, &st);
    printf("speed_final=%.3f\n", st.speed);

    SE_Close();
    printf("survived=1\n");
    return 0;
}

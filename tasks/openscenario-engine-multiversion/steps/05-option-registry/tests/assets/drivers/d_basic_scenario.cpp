// Catalog-free scenario execution -- an inherited obligation, unchanged here.
// Must compile and pass at both tags.
//
// Steps with a fixed SE_StepDT(dt), never plain SE_Step(): plain SE_Step()
// advances by wall-clock elapsed time, so a tight loop of bare SE_Step()
// calls barely advances simulation time at all and "run for N seconds of
// sim time" cannot be relied on. SE_StepDT(dt) is deterministic regardless
// of how fast the driver itself runs.
#include <esminiLib.hpp>
#include <cstdio>

static const float DT = 0.05f;

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <xosc>\n", argv[0]);
        return 1;
    }

    int ret = SE_Init(argv[1], 0, 0, 0, 0);
    printf("init_ret=%d\n", ret);
    if (ret != 0) {
        return 1;
    }

    printf("nobjs=%d\n", SE_GetNumberOfObjects());

    SE_ScenarioObjectState st;
    SE_GetObjectState(0, &st);
    printf("x0=%.3f\n", st.x);
    printf("y0=%.3f\n", st.y);

    // 100 steps of 0.05 s -- exactly 5.0 s of simulated time, regardless of
    // how long the driver itself takes to run those steps.
    for (int i = 0; i < 100; i++) {
        SE_StepDT(DT);
    }

    printf("simtime=%.3f\n", SE_GetSimulationTime());
    SE_GetObjectState(0, &st);
    printf("x_known=%d\n", st.x == 0.0 ? 0 : 1);
    printf("speed_ge0=%d\n", st.speed >= 0.0 ? 1 : 0);
    printf("speed_final=%.3f\n", st.speed);

    SE_Close();
    printf("survived=1\n");
    return 0;
}

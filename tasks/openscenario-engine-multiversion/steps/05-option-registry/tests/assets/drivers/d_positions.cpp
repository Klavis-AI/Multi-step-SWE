// Road-manager position queries -- an inherited obligation, unchanged here.
// Position setters return >= 0 (1 and 2 mean "moved into a new road segment"
// and "moved into a junction and made a choice"), so assert >= 0.
#include <esminiRMLib.hpp>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <xodr>\n", argv[0]);
        return 1;
    }

    if (RM_Init(argv[1]) != 0) {
        fprintf(stderr, "RM_Init failed\n");
        return 1;
    }

    int h = RM_CreatePosition();
    printf("handle_ge0=%d\n", h >= 0 ? 1 : 0);

    int r1 = RM_SetWorldPosition(h, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    printf("setworld_ge0=%d\n", r1 >= 0 ? 1 : 0);

    RM_PositionData pd;
    int r2 = RM_GetPositionData(h, &pd);
    printf("getdata_ge0=%d\n", r2 >= 0 ? 1 : 0);
    // roadId is a signed int at the start tag and the unsigned id_t at the
    // target, so ">= 0" is a tautology at the target. Compare against the
    // "no road" sentinel instead, which is -1 as int and 0xffffffff as id_t
    // -- the same bit pattern either way.
    printf("roadid_valid=%d\n", static_cast<unsigned int>(pd.roadId) != 0xffffffffu ? 1 : 0);

    // A world position well along the road reports a larger s than the origin.
    int r3 = RM_SetWorldPosition(h, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    printf("setworld2_ge0=%d\n", r3 >= 0 ? 1 : 0);
    RM_GetPositionData(h, &pd);
    printf("s_positive=%d\n", pd.s > 0.0f ? 1 : 0);

    // Lane position: road 0, right lane, 30 m along, no offset.
    // Signature is (handle, roadId, laneId, laneOffset, s, align) -- offset
    // first, s second.
    int r4 = RM_SetLanePosition(h, 0, -1, 0.0f, 30.0f, true);
    printf("setlane_ge0=%d\n", r4 >= 0 ? 1 : 0);
    RM_GetPositionData(h, &pd);
    printf("lane_roadid=%d\n", (int)pd.roadId);
    printf("lane_s_close=%d\n", (pd.s > 29.0f && pd.s < 31.0f) ? 1 : 0);

    RM_DeletePosition(h);
    RM_Close();
    printf("survived=1\n");
    return 0;
}

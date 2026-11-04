// The per-component alignment setters RM_SetAlignMode / RM_SetAlignModeH /
// RM_SetAlignModeP / RM_SetAlignModeR / RM_SetAlignModeZ are REMOVED at this
// release. Their replacement is the position-mode bitmask family:
//
//     RM_SetObjectPositionMode(handle, type, mode)
//     RM_SetObjectPositionModeDefault(handle, type)
//     RM_SetWorldPositionMode(handle, x, y, z, h, p, r, mode)
//
// where `type` is 1 = SET (explicit set-functions) / 2 = UPDATE (positions
// updated by controllers) and `mode` combines the RM_PositionMode bits
// (RM_Z_ABS = 3, RM_Z_REL = 7, ...).
//
// This driver deliberately exercises the POST-removal contract rather than the
// removed symbol: a driver that still called RM_SetAlignModeZ would fail to
// compile against the TARGET build, which is backwards -- the group has to
// fail at the starting release and pass at the target one. The "the old symbol
// is really gone" half of the obligation is checked by the case script, which
// reads the dynamic symbol table of the shared library instead.
//
// usage: d_removed <xodr> <roadId> <s>
//
// The measurement: park on the road to learn the road-surface elevation under
// a known lane position, then re-set the same x/y with the same explicit
// z = 7.0 through RM_SetWorldPositionMode, once with RM_Z_ABS and once with
// RM_Z_REL. Under RM_Z_ABS the z asked for is the z reported; under RM_Z_REL
// the z is measured from the road surface, so the two answers differ by
// exactly the road elevation at that point. A build that accepted the mode
// argument and ignored it cannot produce both numbers.
//
// NOTE: RM_SetWorldPosition (no -Mode suffix) hard-codes absolute mode in
// esminiRMLib.cpp and does NOT consult the object's stored mode, so the
// Z_ABS/Z_REL difference is only observable through the -Mode entry point.
// Do not "simplify" this back to RM_SetWorldPosition; it measures nothing.
#include <esminiRMLib.hpp>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s <xodr> <roadId> <s>\n", argv[0]);
        return 1;
    }
    if (RM_Init(argv[1]) != 0)
    {
        fprintf(stderr, "RM_Init failed\n");
        return 1;
    }

    // int, not id_t: the id_t typedef is itself new at this release, and the
    // group must fail at the start tag on the MISSING REPLACEMENT API, not on
    // an incidental typedef. int converts to id_t at the target tag.
    int   road = atoi(argv[2]);
    float s    = static_cast<float>(atof(argv[3]));

    int h = RM_CreatePosition();
    printf("handle_ge0=%d\n", h >= 0 ? 1 : 0);

    int r = RM_SetLanePosition(h, road, -1, 0.0f, s, true);
    printf("setlane_ge0=%d\n", r >= 0 ? 1 : 0);

    RM_PositionData pd;
    RM_GetPositionData(h, &pd);
    const float px = pd.x, py = pd.y, road_z = pd.z;
    printf("road_z=%.3f\n", road_z);

    // Absolute Z.
    RM_SetWorldPositionMode(h, px, py, 7.0f, 0.0f, 0.0f, 0.0f, RM_Z_ABS);
    RM_GetPositionData(h, &pd);
    printf("z_abs=%.3f\n", pd.z);

    // Relative Z: same request, now measured from the road surface.
    RM_SetWorldPositionMode(h, px, py, 7.0f, 0.0f, 0.0f, 0.0f, RM_Z_REL);
    RM_GetPositionData(h, &pd);
    printf("z_rel=%.3f\n", pd.z);
    printf("z_rel_minus_z_abs=%.3f\n", pd.z - 7.0f);

    // The stored per-object mode and the reset to the build's own default must
    // both exist and must leave the position object usable.
    RM_SetObjectPositionMode(h, 1 /* SET */, RM_Z_REL | RM_H_ABS);
    RM_SetWorldPositionMode(h, px, py, 7.0f, 0.0f, 0.0f, 0.0f, RM_Z_ABS);
    RM_GetPositionData(h, &pd);
    printf("z_after_objmode=%.3f\n", pd.z);

    RM_SetObjectPositionModeDefault(h, 1 /* SET */);
    RM_SetLanePosition(h, road, -1, 0.0f, s, true);
    RM_GetPositionData(h, &pd);
    printf("z_back_on_road=%.3f\n", pd.z);

    RM_DeletePosition(h);
    RM_Close();
    printf("survived=1\n");
    return 0;
}

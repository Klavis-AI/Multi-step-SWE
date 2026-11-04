/*
 * rm_drivable - the drivable-only lane queries.
 *
 * usage: rm_drivable <odr> <road_id> <s>
 *
 * The drivable-only pair must answer exactly what the general pair answers
 * when it is handed the any-drivable mask, so the driver asks both and reports
 * whether they agreed element by element as well as reporting the raw numbers.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

static const int ANY_DRIVING = 1966594;
static const int ANY         = -1;
static const int SENTINEL    = -31337;

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <s>\n", argv[0]);
        return 2;
    }
    const char *odr  = argv[1];
    id_t        road = static_cast<id_t>(std::atoi(argv[2]));
    float       s    = static_cast<float>(std::atof(argv[3]));

    int id;

    std::printf("pre_init_n=%d\n", RM_GetRoadNumberOfDrivableLanes(road, s));
    id = SENTINEL;
    std::printf("pre_init_id_rc=%d\n", RM_GetDrivableLaneIdByIndex(road, 0, s, &id));

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int nd = RM_GetRoadNumberOfDrivableLanes(road, s);
    int nm = RM_GetRoadNumberOfLanes(road, s, ANY_DRIVING);
    int na = RM_GetRoadNumberOfLanes(road, s, ANY);
    std::printf("n_drivable=%d\n", nd);
    std::printf("n_mask_driving=%d\n", nm);
    std::printf("n_any=%d\n", na);
    std::printf("drivable_fewer_than_any=%d\n", (nd >= 0 && na > nd) ? 1 : 0);

    // Both id getters are required to return exactly 0 here, not merely a
    // non-negative code. That is the contract instruction.md's "How the road
    // manager's int returns are read" note states for the status-returning
    // entry points, and it is narrower than the non-negative family the note
    // reserves for the POSITION SETTERS; these two are getters, so the strict
    // comparison is the documented one and is not tightened past it.
    int agree = (nd == nm) ? 1 : 0;
    for (int i = 0; i < nd; i++)
    {
        int a = SENTINEL, b = SENTINEL;
        int rca = RM_GetDrivableLaneIdByIndex(road, i, s, &a);
        int rcb = RM_GetLaneIdByIndex(road, i, s, ANY_DRIVING, &b);
        if (rca != 0 || rcb != 0 || a != b)
        {
            agree = 0;
        }
        std::printf("d_id_%d=%d\n", i, a);
    }
    std::printf("agrees_with_mask=%d\n", agree);

    id = SENTINEL;
    std::printf("oob_rc=%d\n", RM_GetDrivableLaneIdByIndex(road, nd, s, &id));
    std::printf("oob_untouched=%d\n", id == SENTINEL ? 1 : 0);

    std::printf("bad_road_n=%d\n", RM_GetRoadNumberOfDrivableLanes(90210, s));
    id = SENTINEL;
    std::printf("bad_road_id_rc=%d\n", RM_GetDrivableLaneIdByIndex(90210, 0, s, &id));

    /* A call that cannot be answered has to reach that conclusion before it
     * writes anything, so the same two failures still come back as a status
     * when the caller supplied no destination at all. An implementation that
     * writes first and validates afterwards faults here instead of answering. */
    std::printf("oob_null_rc=%d\n", RM_GetDrivableLaneIdByIndex(road, nd, s, nullptr));
    std::printf("bad_road_null_rc=%d\n", RM_GetDrivableLaneIdByIndex(90210, 0, s, nullptr));

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

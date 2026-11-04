/*
 * rm_lane_by_type - counting and enumerating a road's lanes by lane type.
 *
 * usage: rm_lane_by_type <odr> <road_id> <s> <type_mask>
 *
 * Prints the number of matching lanes and, for every index in range, the lane
 * id that index maps to. The enumeration order is reported as printed ids
 * rather than as a set, because the order is part of the contract: index 0 is
 * the leftmost matching lane.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

static const int SENTINEL = -31337;

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <s> <type_mask>\n", argv[0]);
        return 2;
    }
    const char *odr  = argv[1];
    id_t        road = static_cast<id_t>(std::atoi(argv[2]));
    float       s    = static_cast<float>(std::atof(argv[3]));
    int         mask = static_cast<int>(std::strtol(argv[4], nullptr, 0));

    int id;

    /* Nothing loaded yet. */
    std::printf("pre_init_n=%d\n", RM_GetRoadNumberOfLanes(road, s, mask));
    id = SENTINEL;
    std::printf("pre_init_id_rc=%d\n", RM_GetLaneIdByIndex(road, 0, s, mask, &id));
    std::printf("pre_init_id_untouched=%d\n", id == SENTINEL ? 1 : 0);

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int n = RM_GetRoadNumberOfLanes(road, s, mask);
    std::printf("n=%d\n", n);

    /* Every index in 0 .. n-1 answers, and the ids are printed in the order
     * the enumeration produced them. */
    int all_ok = 1;
    for (int i = 0; i < n; i++)
    {
        id      = SENTINEL;
        int irc = RM_GetLaneIdByIndex(road, i, s, mask, &id);
        if (irc != 0)
        {
            all_ok = 0;
        }
        std::printf("id_%d=%d\n", i, id);
    }
    std::printf("all_indices_ok=%d\n", all_ok);

    /* One past the last index is an error and leaves the destination alone. */
    id = SENTINEL;
    std::printf("oob_rc=%d\n", RM_GetLaneIdByIndex(road, n, s, mask, &id));
    std::printf("oob_untouched=%d\n", id == SENTINEL ? 1 : 0);
    id = SENTINEL;
    std::printf("neg_rc=%d\n", RM_GetLaneIdByIndex(road, -1, s, mask, &id));

    /* A road the network does not define. */
    std::printf("bad_road_n=%d\n", RM_GetRoadNumberOfLanes(90210, s, mask));
    id = SENTINEL;
    std::printf("bad_road_id_rc=%d\n", RM_GetLaneIdByIndex(90210, 0, s, mask, &id));
    std::printf("bad_road_untouched=%d\n", id == SENTINEL ? 1 : 0);

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

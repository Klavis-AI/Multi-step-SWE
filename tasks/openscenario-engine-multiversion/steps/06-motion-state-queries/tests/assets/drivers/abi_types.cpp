/*
 * abi_types - the index/id type migration and the road-info struct growth that
 * this release completes in the published C ABI.
 *
 * usage: abi_types <xosc> <odr> <road_id> <lane_id> <s>
 *
 * Two kinds of evidence:
 *
 *   - COMPILE TIME. The four function-pointer initialisations below only bind
 *     if the entry point is declared with exactly the parameter and return
 *     types requirement 18 states. A signed `int` where the release declares
 *     `unsigned int` is a different function type, not a convertible one, so a
 *     tree that has not made the migration fails to build this driver rather
 *     than failing an assertion inside it. Likewise the two struct members
 *     read below do not exist before the release adds them.
 *
 *   - RUN TIME. The added `road_type` / `road_rule` members are read back from
 *     both the scenario-engine and the road-manager road-info structs after a
 *     real query, so declaring the members without filling them is not enough.
 *
 * Nothing expected is printed -- only what the library returned.
 */
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"
#include "esminiRMLib.hpp"

/* Requirement 18: the permutation entry points became unsigned. */
static unsigned int (*const kNumPermutations)()             = &SE_GetNumberOfPermutations;
static int (*const kSelectPermutation)(unsigned int)        = &SE_SelectPermutation;

/* Requirement 18: the road-sign index parameters became unsigned. */
static int (*const kGetRoadSign)(id_t, unsigned int, SE_RoadSign *)                            = &SE_GetRoadSign;
static int (*const kGetRoadSignValidity)(id_t, unsigned int, unsigned int, SE_RoadObjValidity *) = &SE_GetRoadSignValidityRecord;

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: %s <xosc> <odr> <road_id> <lane_id> <s>\n", argv[0]);
        return 2;
    }
    const char *xosc    = argv[1];
    const char *odr     = argv[2];
    int         road_id = std::atoi(argv[3]);
    int         lane_id = std::atoi(argv[4]);
    float       s       = static_cast<float>(std::atof(argv[5]));

    /* The pointers are only interesting for having bound at all, but an unused
     * static would be free to disappear, so their addresses are observed. */
    std::printf("bound_type_migrated_entry_points=%d\n",
                (kNumPermutations != nullptr && kSelectPermutation != nullptr && kGetRoadSign != nullptr &&
                 kGetRoadSignValidity != nullptr)
                    ? 1
                    : 0);

    /* The two new members are the last two of each struct, after `t`. */
    std::printf("se_roadinfo_members_appended=%d\n",
                (offsetof(SE_RoadInfo, road_type) > offsetof(SE_RoadInfo, t) &&
                 offsetof(SE_RoadInfo, road_rule) > offsetof(SE_RoadInfo, road_type))
                    ? 1
                    : 0);
    std::printf("rm_lane_info_members_appended=%d\n",
                (offsetof(RM_RoadLaneInfo, road_type) > offsetof(RM_RoadLaneInfo, t) &&
                 offsetof(RM_RoadLaneInfo, road_rule) > offsetof(RM_RoadLaneInfo, road_type))
                    ? 1
                    : 0);

    /* ---- road manager: RM_RoadLaneInfo grew the same two members ---- */
    int rc = RM_Init(odr);
    std::printf("rm_init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    int h = RM_CreatePosition();
    std::printf("rm_set_lane_ok=%d\n", RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true) >= 0 ? 1 : 0);

    RM_RoadLaneInfo li;
    li.road_type = -12345;
    li.road_rule = -12345;
    std::printf("rm_lane_info_rc=%d\n", RM_GetLaneInfo(h, 0.0f, &li, 0, true));
    std::printf("rm_road_type=%d\n", li.road_type);
    std::printf("rm_road_rule=%d\n", li.road_rule);
    std::printf("rm_lane_width=%.3f\n", static_cast<double>(li.width));
    RM_Close();

    /* ---- scenario engine: SE_RoadInfo grew the same two members ---- */
    rc = SE_Init(xosc, 0, 0, 0, 0);
    std::printf("se_init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    SE_StepDT(0.05f);

    SE_RoadInfo ri;
    ri.road_type = -12345;
    ri.road_rule = -12345;
    std::printf("se_road_info_rc=%d\n", SE_GetRoadInfoAtDistance(0, 5.0f, &ri, 0, true));
    std::printf("se_road_type=%d\n", ri.road_type);
    std::printf("se_road_rule=%d\n", ri.road_rule);
    std::printf("se_speed_limit=%.3f\n", static_cast<double>(ri.speed_limit));

    /* The permutation count is unsigned from here on. Without a parameter
     * distribution loaded there is nothing to permute; what matters is that
     * the value arrives through an unsigned-returning entry point. */
    std::printf("permutations=%u\n", SE_GetNumberOfPermutations());

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

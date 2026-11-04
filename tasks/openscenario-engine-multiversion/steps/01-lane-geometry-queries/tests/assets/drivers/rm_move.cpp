/*
 * rm_move - the road-manager position-move return-code widening.
 *
 * usage: rm_move <odr> <road_id> <lane_id> <s> <dist>
 *
 * From this release RM_PositionMoveForward reports which kind of move
 * happened: 0 for an ordinary move that changes nothing about which road or
 * junction the position is on, 1 for a move that crosses onto a new plain road
 * segment with no choice involved, and 2 for a move that enters a junction and
 * has to pick a connecting road. All three are success; only a negative return
 * is failure. instruction.md section 8 states this as a numbered obligation.
 *
 * The position setters are NOT part of it at this release: RM_SetLanePosition
 * and RM_SetWorldXYHPosition both end in a literal `return 0`. Later releases
 * widen them too, which is why the caveat tells callers to test the sign.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "usage: %s <odr> <road_id> <lane_id> <s> <dist>\n", argv[0]);
        return 2;
    }
    const char *odr     = argv[1];
    int         road_id = std::atoi(argv[2]);
    int         lane_id = std::atoi(argv[3]);
    float       s       = static_cast<float>(std::atof(argv[4]));
    float       dist    = static_cast<float>(std::atof(argv[5]));

    int rc = RM_Init(odr);
    std::printf("init=%d\n", rc);
    if (rc != 0)
        return 1;

    int h       = RM_CreatePosition();
    int set_rc  = RM_SetLanePosition(h, road_id, lane_id, 0.0f, s, true);
    std::printf("set_rc=%d\n", set_rc);

    RM_PositionData before;
    RM_GetPositionData(h, &before);
    std::printf("before_road=%d\n", before.roadId);
    std::printf("before_junction=%d\n", before.junctionId);

    int move_rc = RM_PositionMoveForward(h, dist, 0.0f);
    std::printf("move_rc=%d\n", move_rc);

    RM_PositionData after;
    RM_GetPositionData(h, &after);
    std::printf("after_road=%d\n", after.roadId);
    std::printf("after_junction=%d\n", after.junctionId);

    RM_Close();
    std::printf("survived=1\n");
    return 0;
}

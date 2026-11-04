/*
 * rm_overlap - the overlapping-road queries.
 *
 * usage: rm_overlap <odr> <x> <y> [net]
 *
 * A fifth argument `net` additionally prints the network's own road inventory
 * (`nroads`, `network_id_<i>`) so a case can check the ids the overlap query
 * returns against the ids the network actually contains, instead of against a
 * list baked into the case. The four-argument form is unchanged.
 *
 * Places a position at a world (x, y) and reports which roads the road
 * manager considers to overlap there. Inside a junction several connecting
 * roads cover the same ground, so the count is > 1; on an ordinary road it
 * is 1.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiRMLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5)
    {
        std::fprintf(stderr, "usage: %s <odr> <x> <y> [net]\n", argv[0]);
        return 2;
    }
    int rc = RM_Init(argv[1]);
    std::printf("init=%d\n", rc);
    if (rc != 0)
        return 1;

    if (argc == 5)
    {
        /* The network's own road ids, so the caller can check that every id the
         * overlap query returns is a road this network really contains. */
        int nroads = RM_GetNumberOfRoads();
        std::printf("nroads=%d\n", nroads);
        for (int i = 0; i < nroads; i++)
        {
            std::printf("network_id_%d=%d\n", i, RM_GetIdOfRoadFromIndex(i));
        }
    }

    int h = RM_CreatePosition();
    std::printf("set_world=%d\n",
                RM_SetWorldXYHPosition(h,
                                       static_cast<float>(std::atof(argv[2])),
                                       static_cast<float>(std::atof(argv[3])),
                                       0.0f));

    RM_PositionData d;
    RM_GetPositionData(h, &d);
    std::printf("road_id=%d\n", d.roadId);
    std::printf("junction_id=%d\n", d.junctionId);

    int n = RM_GetNumberOfRoadsOverlapping(h);
    std::printf("n_overlap=%d\n", n);
    for (int i = 0; i < n; i++)
    {
        std::printf("overlap_%d=%d\n", i, RM_GetOverlappingRoadId(h, i));
    }
    /* Out-of-range index must not crash the process. Whatever it returns,
       reaching this line at all is the observation. */
    std::printf("overlap_oob=%d\n", RM_GetOverlappingRoadId(h, n + 100));
    std::printf("survived=1\n");

    RM_Close();
    return 0;
}

/* se_collision_legacy.cpp - SE_GetObjectNumberOfCollisions / SE_GetObjectCollision,
 * present since well before this step's start tag. Regression material only:
 * every value asserted here must be measured identical at both ends of the step.
 *
 * usage: se_collision_legacy <scenario.xosc> <steps>
 *
 * None of the fixture scenarios ever bring two bounding boxes into contact on
 * their own (the follower in two_car_brake.xosc slows down long before
 * reaching the lead car; the three lanes in tri_lane_cars.xosc keep every car
 * clear of the others), so the no-collision and bad-object paths are the only
 * ones a plain run of either can exercise. To also exercise the positive
 * path, object 1 is force-relocated on top of object 0 via SE_ReportObjectPos
 * after the fixture's own init, which is guaranteed to overlap two 5 m x 2 m
 * boxes regardless of the fixture's own story.
 */
#include <cstdio>
#include <cstdlib>
#include "esminiLib.hpp"

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <scenario> <steps>\n", argv[0]); return 2; }

    int nsteps = std::atoi(argv[2]);

    /* Collision bookkeeping is gated behind global collision detection, which
     * defaults to disabled; without this call the two collision counters
     * below never leave zero regardless of how the objects overlap. */
    SE_CollisionDetection(true);

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0) return 1;

    for (int i = 0; i < nsteps; ++i)
    {
        SE_StepDT(0.05f);
    }

    int n0 = SE_GetObjectNumberOfCollisions(0);
    std::printf("ncoll_obj0_before=%d\n", n0);

    /* Force object 1 onto object 0's position: two identical 5x2 boxes at the
     * same point are guaranteed to overlap. */
    SE_ScenarioObjectState st0;
    SE_GetObjectState(0, &st0);
    SE_ReportObjectPos(1, st0.x, st0.y, st0.z, st0.h, st0.p, st0.r);
    SE_StepDT(0.05f);

    int n0b = SE_GetObjectNumberOfCollisions(0);
    std::printf("ncoll_obj0_after=%d\n", n0b);
    if (n0b > 0)
    {
        int other = SE_GetObjectCollision(0, 0);
        std::printf("coll0_partner=%d\n", other);
    }
    else
    {
        std::printf("coll0_partner=none\n");
    }

    /* An unknown object id is a documented -1 failure. */
    std::printf("badobj_ncoll=%d\n", SE_GetObjectNumberOfCollisions(99));
    std::printf("badobj_coll=%d\n", SE_GetObjectCollision(99, 0));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

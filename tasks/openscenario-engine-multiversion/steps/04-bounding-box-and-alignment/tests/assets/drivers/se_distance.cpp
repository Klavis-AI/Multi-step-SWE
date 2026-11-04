/*
 * se_distance - SE_GetDistanceToObject.
 *
 * usage: se_distance <xosc> <a> <b>
 *
 * Both measurement modes are asked for the same pair, because the difference
 * between them is the whole point of the free_space flag: reference-point to
 * reference-point first, then bounding-box to bounding-box.
 *
 * Each call also prints the same A -> B offset as `dx`/`dy`, its components in
 * the global frame rather than the road frame. With free_space false they are
 * that offset exactly -- one displacement in two frames. With free_space true
 * the engine recomputes ds/dt onto the nearer box edges but leaves dx/dy
 * untouched, so the printout keeps the stale values; the cases pin what comes
 * back on each fixture rather than a rule the engine does not implement.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiLib.hpp"

static void ask(const char *prefix, int a, int b, bool free_space)
{
    SE_PositionDiff d;
    std::memset(&d, 0, sizeof(d));
    int rc = SE_GetDistanceToObject(a, b, free_space, &d);
    std::printf("%s_rc=%d\n", prefix, rc);
    if (rc != 0)
    {
        return;
    }
    std::printf("%s_ds=%.4f\n", prefix, static_cast<double>(d.ds));
    std::printf("%s_dt=%.4f\n", prefix, static_cast<double>(d.dt));
    /* The same offset in the GLOBAL frame rather than the road frame: with
     * free_space false, dx is B's world x minus A's. Printed to four decimals
     * like ds/dt so a case can compare them against the object states the
     * driver dumps above. */
    std::printf("%s_dx=%.4f\n", prefix, static_cast<double>(d.dx));
    std::printf("%s_dy=%.4f\n", prefix, static_cast<double>(d.dy));
    std::printf("%s_dlane=%d\n", prefix, d.dLaneId);
    std::printf("%s_opposite=%d\n", prefix, d.oppositeLanes ? 1 : 0);
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <xosc> <a> <b>\n", argv[0]);
        return 2;
    }
    int a = std::atoi(argv[2]);
    int b = std::atoi(argv[3]);

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    SE_StepDT(0.05f);
    std::printf("nobj=%d\n", SE_GetNumberOfObjects());

    /* Where the two objects actually are, so a case can tell a wrong answer
     * from a wrong fixture. */
    SE_ScenarioObjectState st;
    std::memset(&st, 0, sizeof(st));
    SE_GetObjectState(a, &st);
    std::printf("a_lane=%d\n", st.laneId);
    std::printf("a_s=%.3f\n", static_cast<double>(st.s));
    std::printf("a_length=%.3f\n", static_cast<double>(st.length));
    std::memset(&st, 0, sizeof(st));
    SE_GetObjectState(b, &st);
    std::printf("b_lane=%d\n", st.laneId);
    std::printf("b_s=%.3f\n", static_cast<double>(st.s));
    std::printf("b_length=%.3f\n", static_cast<double>(st.length));

    ask("ref", a, b, false);
    ask("free", a, b, true);
    /* The measurement is directed: asking the other way round flips the sign. */
    ask("rev", b, a, false);

    /* An object id no scenario defines is an error, not a zero distance. The
     * instruction requires that rejection of BOTH queries -- an id no scenario
     * defines has no box for the free-space scan either -- so both are asked. */
    SE_PositionDiff d;
    std::memset(&d, 0, sizeof(d));
    std::printf("unknown_rc=%d\n", SE_GetDistanceToObject(a, 4242, false, &d));
    std::printf("unknown_free_rc=%d\n", SE_GetDistanceToObject(a, 4242, true, &d));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

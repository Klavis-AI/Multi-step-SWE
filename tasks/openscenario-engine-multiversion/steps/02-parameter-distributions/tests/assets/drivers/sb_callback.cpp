/*
 * sb_callback - the storyboard-element state-change callback.
 *
 * usage: sb_callback <xosc> [preinit]
 *
 * Registers the callback, runs the scenario to its stop trigger, and prints
 * every (name, type, state) triple it receives.
 *
 * The callback is normally registered after SE_Init, which is what a host
 * application does. With the optional second argument `preinit` it is
 * registered BEFORE SE_Init instead: instruction.md section 6 states that a
 * registration made before SE_Init does not survive it, so that run must
 * deliver no calls at all. The `preinit` key says which order was used.
 *
 * SE_RegisterStoryBoardElementStateChangeCallback does not exist at the
 * step's starting release, so this driver must not compile at the starting
 * tag. The group it backs is `scored` in groups.txt, and that is what makes
 * it fail there rather than pass on inherited behaviour.
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "esminiLib.hpp"

static int n_calls = 0;
static int n_complete = 0;
static int n_running = 0;

/* The fixture's Story/Act/Group nest a single named manoeuvre. Its own state
 * sequence is recorded separately from the raw call log below so a case can
 * assert the running-then-complete order directly, rather than trying to
 * recover ordering from need_kv's last-line-wins semantics on a repeated
 * key. */
static std::string keep_speed_states;
static int         keep_speed_type = -1;

static void on_state_change(const char *name, int type, int state)
{
    std::printf("cb_name=%s\n", name);
    std::printf("cb_type=%d\n", type);
    std::printf("cb_state=%d\n", state);
    n_calls++;
    if (state == 3)
    {
        n_complete++;
    }
    if (state == 2)
    {
        n_running++;
    }
    if (std::strcmp(name, "KeepSpeedManeuver") == 0)
    {
        if (!keep_speed_states.empty())
        {
            keep_speed_states += ",";
        }
        keep_speed_states += std::to_string(state);
        keep_speed_type = type;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        std::fprintf(stderr, "usage: %s <xosc> [preinit]\n", argv[0]);
        return 2;
    }

    const bool preinit = (argc == 3 && std::strcmp(argv[2], "preinit") == 0);
    std::printf("preinit=%d\n", preinit ? 1 : 0);

    if (preinit)
    {
        SE_RegisterStoryBoardElementStateChangeCallback(on_state_change);
    }

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    if (!preinit)
    {
        SE_RegisterStoryBoardElementStateChangeCallback(on_state_change);
    }

    for (int i = 0; i < 400; i++)
    {
        if (SE_StepDT(0.05f) != 0)
        {
            break;
        }
    }

    std::printf("n_calls=%d\n", n_calls);
    std::printf("n_running=%d\n", n_running);
    std::printf("n_complete=%d\n", n_complete);
    std::printf("keep_speed_states=%s\n", keep_speed_states.c_str());
    std::printf("keep_speed_type=%d\n", keep_speed_type);
    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

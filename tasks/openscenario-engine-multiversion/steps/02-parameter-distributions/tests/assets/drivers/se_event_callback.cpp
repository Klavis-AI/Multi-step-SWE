/*
 * se_event_callback - the storyboard-element callback migration.
 *
 * usage: se_event_callback <xosc>
 *
 * This driver deliberately registers the pre-existing per-event callback.
 * At the step's starting release it compiles and links; at the target
 * release that entry point no longer exists, so the link fails and the case
 * that builds this file is the assertion.
 */
#include <cstdio>

#include "esminiLib.hpp"

static void on_event(const char *name, double timestamp, bool start)
{
    std::printf("event_name=%s\n", name);
    std::printf("event_time=%.3f\n", timestamp);
    std::printf("event_start=%d\n", start ? 1 : 0);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <xosc>\n", argv[0]);
        return 2;
    }

    SE_RegisterEventCallback(on_event);

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }
    for (int i = 0; i < 400; i++)
    {
        if (SE_StepDT(0.05f) != 0)
        {
            break;
        }
    }
    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

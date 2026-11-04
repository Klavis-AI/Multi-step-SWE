/*
 * se_pause_wheel - the two small scenario-engine status entry points this
 * release adds: SE_GetPauseFlag and SE_ReportObjectWheelStatus.
 *
 * usage: se_pause_wheel <xosc>
 *
 * Both have a documented behaviour with no scenario loaded, so the driver
 * asks before SE_Init as well as after.
 */
#include <cstdio>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <xosc>\n", argv[0]);
        return 2;
    }

    /* Nothing loaded yet. */
    std::printf("pause_before=%d\n", SE_GetPauseFlag());
    std::printf("wheel_before=%d\n", SE_ReportObjectWheelStatus(0, 1.0f, 0.1f));

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
        return 1;

    SE_Step();
    std::printf("pause_after=%d\n", SE_GetPauseFlag());

    /* Object 0 exists in every scenario the cases use. */
    std::printf("wheel_valid=%d\n", SE_ReportObjectWheelStatus(0, 1.5f, 0.2f));
    /* An id no scenario defines. */
    std::printf("wheel_invalid=%d\n", SE_ReportObjectWheelStatus(4242, 1.5f, 0.2f));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

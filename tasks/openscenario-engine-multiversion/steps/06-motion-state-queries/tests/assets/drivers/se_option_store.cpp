/*
 * se_option_store - the option store as the workspace already provides it:
 * set a value, read it back, ask whether a flag option is set, unset it.
 * This is a regression driver -- nothing it calls is new at this release.
 *
 * usage: se_option_store <name> <value> <flag_name>
 */
#include <cstdio>
#include <cstring>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <name> <value> <flag_name>\n", argv[0]);
        return 2;
    }
    const char *name  = argv[1];
    const char *value = argv[2];
    const char *flag  = argv[3];

    const char *before = SE_GetOptionValue(name);
    std::printf("value_before=%s\n", (before != nullptr && before[0] != '\0') ? before : "(empty)");
    std::printf("flag_before=%d\n", SE_GetOptionSet(flag));

    std::printf("set_rc=%d\n", SE_SetOptionValue(name, value));
    const char *after = SE_GetOptionValue(name);
    std::printf("value_after=%s\n", after != nullptr ? after : "(null)");
    std::printf("flag_after_value_set=%d\n", SE_GetOptionSet(name));

    std::printf("set_flag_rc=%d\n", SE_SetOption(flag));
    std::printf("flag_after=%d\n", SE_GetOptionSet(flag));

    std::printf("unset_rc=%d\n", SE_UnsetOption(name));
    const char *cleared = SE_GetOptionValue(name);
    std::printf("value_cleared=%s\n", (cleared != nullptr && cleared[0] != '\0') ? cleared : "(empty)");

    std::printf("unset_flag_rc=%d\n", SE_UnsetOption(flag));
    std::printf("flag_cleared=%d\n", SE_GetOptionSet(flag));

    /* A name the option registry does not know about. */
    std::printf("set_unknown_rc=%d\n", SE_SetOptionValue("no_such_option_name_at_all", "x"));

    std::printf("survived=1\n");
    return 0;
}

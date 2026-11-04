/*
 * se_option_count - the multi-value read side of the option store.
 *
 * usage: se_option_count <name> <value...>
 *
 * Sets the named option once per value supplied and then asks the store how
 * many values it is holding for that name, and what the first one reads back
 * as. Also asks about a name that was never set.
 *
 * The count is the whole point: the store already accepted repeated writes
 * before this release, but there was no way to find out that it had kept more
 * than one.
 */
#include <cstdio>
#include <cstring>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <name> <value...>\n", argv[0]);
        return 2;
    }
    const char *name = argv[1];

    /* Before anything is set. */
    std::printf("count_unset=%d\n", SE_GetOptionValuesCount(name));
    std::printf("count_never=%d\n", SE_GetOptionValuesCount("no_such_option_name_at_all"));

    int n_set = 0;
    for (int i = 2; i < argc; i++)
    {
        if (SE_SetOptionValue(name, argv[i]) == 0)
        {
            n_set++;
        }
        std::printf("count_after_%d=%d\n", n_set, SE_GetOptionValuesCount(name));
    }
    std::printf("n_set=%d\n", n_set);
    std::printf("count=%d\n", SE_GetOptionValuesCount(name));

    const char *v = SE_GetOptionValue(name);
    std::printf("read_value=%s\n", v != nullptr ? v : "(null)");

    /* Clearing the option must take the count back down with it. The store
     * hands back the most recently written value, not the first. */
    std::printf("unset_rc=%d\n", SE_UnsetOption(name));
    std::printf("count_cleared=%d\n", SE_GetOptionValuesCount(name));

    std::printf("survived=1\n");
    return 0;
}

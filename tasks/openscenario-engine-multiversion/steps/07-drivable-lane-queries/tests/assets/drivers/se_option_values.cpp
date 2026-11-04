/*
 * se_option_values - reading back an option that carries several values.
 *
 * usage: se_option_values <name> <enum_value> <value0> [<value1> ...]
 *
 * Sets the named option once per value supplied and then reads the store back
 * by index. The enum_value argument is the option's index in the published
 * option enumeration, used to read the same value back through the
 * enum-addressed accessor. Nothing here loads a scenario: the option store is
 * configuration state that exists before and independently of SE_Init.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

static const char *show(const char *s)
{
    return s == nullptr ? "(null)" : s;
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <name> <enum_value> <value0> [<value1> ...]\n", argv[0]);
        return 2;
    }
    const char      *name   = argv[1];
    const unsigned   enum_v = static_cast<unsigned>(std::atoi(argv[2]));
    const int        base   = 3;
    const int        nvals  = argc - base;

    /* Untouched option: no values, and index 0 answers with a null pointer
     * rather than with an empty string or a stale one. */
    std::printf("count_before=%d\n", SE_GetOptionValuesCount(name));
    std::printf("idx0_before_null=%d\n", SE_GetOptionValueByIndex(name, 0) == nullptr ? 1 : 0);

    int all_set_ok = 1;
    for (int i = 0; i < nvals; i++)
    {
        if (SE_SetOptionValue(name, argv[base + i]) != 0)
        {
            all_set_ok = 0;
        }
    }
    std::printf("all_set_ok=%d\n", all_set_ok);
    std::printf("count=%d\n", SE_GetOptionValuesCount(name));

    for (int i = 0; i < nvals; i++)
    {
        std::printf("idx_%d=%s\n", i, show(SE_GetOptionValueByIndex(name, static_cast<unsigned int>(i))));
    }

    /* One past the last stored value is a null pointer, not the last value
     * again and not a crash. */
    std::printf("past_end_null=%d\n", SE_GetOptionValueByIndex(name, static_cast<unsigned int>(nvals)) == nullptr ? 1 : 0);

    /* An option name the registry does not know at all. */
    std::printf("unknown_null=%d\n", SE_GetOptionValueByIndex("no_such_option_at_all", 0) == nullptr ? 1 : 0);

    /* Every stored value must be distinct from every other: a store that
     * returns one value under every index does not pass this. */
    int distinct = 1;
    for (int i = 0; i < nvals; i++)
    {
        const char *a = SE_GetOptionValueByIndex(name, static_cast<unsigned int>(i));
        if (a == nullptr)
        {
            distinct = 0;
            continue;
        }
        for (int j = i + 1; j < nvals; j++)
        {
            const char *b = SE_GetOptionValueByIndex(name, static_cast<unsigned int>(j));
            if (b == nullptr)
            {
                distinct = 0;
            }
            else
            {
                const char *p = a;
                const char *q = b;
                while (*p != '\0' && *p == *q)
                {
                    p++;
                    q++;
                }
                if (*p == *q)
                {
                    distinct = 0;
                }
            }
        }
    }
    std::printf("all_distinct=%d\n", distinct);

    /* The enum-addressed accessor reads the same store. It takes the option's
     * published enumeration index, not its name, and answers with the value
     * that has priority for that option - the most recently stored one. */
    const char *by_enum = SE_GetOptionValueByEnum(enum_v);
    std::printf("by_enum_last=%s\n", show(by_enum));
    int same_text = 0;
    const char *idx0 = SE_GetOptionValueByIndex(name, 0);
    if (by_enum != nullptr && idx0 != nullptr)
    {
        const char *p = by_enum;
        const char *q = idx0;
        while (*p != '\0' && *p == *q) { p++; q++; }
        same_text = (*p == *q) ? 1 : 0;
    }
    std::printf("by_enum_same_text_as_idx0=%d\n", same_text);
    std::printf("by_enum_unknown_null=%d\n", SE_GetOptionValueByEnum(9999u) == nullptr ? 1 : 0);

    std::printf("survived=1\n");
    return 0;
}

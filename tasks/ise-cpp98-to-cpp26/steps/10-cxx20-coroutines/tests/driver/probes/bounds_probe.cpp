// Known-answer probe for audit_array_bounds.py.
//
// Exactly two loops are wrong: the one at line 20 (bound 4, table of 3) and
// the one at line 44 (bound 2, table of 3). Everything else is a control that
// must not be flagged.

#include "drv.h"

DRV_CASE(probe_correct)
{
    static const char *INPUTS[] = { "a", "b", "c" };
    for (int i = 0; i < 3; i++)
        outf("%s", INPUTS[i]);
}

DRV_CASE(probe_overrun)
{
    static const char *INPUTS[] = { "a", "b", "c" };
    for (int i = 0; i < 4; i++)
        outf("%s", INPUTS[i]);
}

DRV_CASE(probe_trailing_comma)
{
    // A trailing comma does not add an element.
    static const int VALUES[] = { 1, 2, 3, };
    for (int i = 0; i < 3; i++)
        outf("%d", VALUES[i]);
}

DRV_CASE(probe_nested)
{
    // A nested brace group is one element, and the commas inside it are not
    // separators at the top level.
    struct Pair { int a; int b; };
    static const Pair PAIRS[] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };
    for (int i = 0; i < 3; i++)
        outf("%d %d", PAIRS[i].a, PAIRS[i].b);
}

DRV_CASE(probe_undercount)
{
    static const int VALUES[] = { 1, 2, 3 };
    for (int i = 0; i < 2; i++)
        outf("%d", VALUES[i]);
}

DRV_CASE(probe_literal_with_braces)
{
    // Braces and commas inside a string literal must not be counted.
    static const char *INPUTS[] = { "{a, b}", "c,d", "}" };
    for (int i = 0; i < 3; i++)
        outf("%s", INPUTS[i]);
}

DRV_CASE(probe_computed_bound)
{
    // A DRV_COUNT bound is not a literal, so there is nothing to check.
    static const int VALUES[] = { 1, 2, 3, 4, 5 };
    for (int i = 0; i < DRV_COUNT(VALUES); i++)
        outf("%d", VALUES[i]);
}

DRV_CASE(probe_unrelated_loop)
{
    // A loop that indexes nothing declared here must not borrow a count.
    static const int VALUES[] = { 1, 2, 3 };
    for (int j = 0; j < 100; j++)
        outf("%d", j);
    outf("%d", VALUES[0]);
}

DRV_CASE(probe_two_tables)
{
    // Two tables of the same length walked by one loop: both must agree.
    static const char *NAMES[] = { "x", "y" };
    static const int VALUES[] = { 1, 2 };
    for (int i = 0; i < 2; i++)
        outf("%s=%d", NAMES[i], VALUES[i]);
}

DRV_CASE(probe_rows_correct)
{
    // A table of rows: the loop counts rows, not fields.
    static const int ROWS[][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    for (int i = 0; i < 2; i++)
        outf("%d %d %d", ROWS[i][0], ROWS[i][1], ROWS[i][2]);
}

DRV_CASE(probe_rows_overrun)
{
    static const int ROWS[][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    for (int i = 0; i < 3; i++)
        outf("%d %d %d", ROWS[i][0], ROWS[i][1], ROWS[i][2]);
}

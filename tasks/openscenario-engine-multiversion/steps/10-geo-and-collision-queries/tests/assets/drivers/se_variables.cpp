/* se_variables.cpp - SE_GetVariableInt / SE_GetVariableBool / SE_GetVariableDouble
 * / SE_GetVariableString, present since well before this step's start tag.
 * Regression material only.
 *
 * usage: se_variables <scenario.xosc>
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esminiLib.hpp"

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <scenario>\n", argv[0]); return 2; }

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0) return 1;

    int ival = -1;
    int irc = SE_GetVariableInt("Counter", &ival);
    std::printf("counter_rc=%d counter_val=%d\n", irc, ival);

    bool bval = true;
    int brc = SE_GetVariableBool("BrakeRequest", &bval);
    std::printf("brake_rc=%d brake_val=%d\n", brc, static_cast<int>(bval));

    /* Wrong-type access is a documented failure (0 success / -1 not, e.g.
     * wrong type), not a silent coercion. */
    double dwrong = -999.0;
    std::printf("counter_as_double_rc=%d\n", SE_GetVariableDouble("Counter", &dwrong));

    /* An unknown variable name is a documented failure. */
    int unk = -1;
    std::printf("unknown_rc=%d\n", SE_GetVariableInt("NoSuchVariable", &unk));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

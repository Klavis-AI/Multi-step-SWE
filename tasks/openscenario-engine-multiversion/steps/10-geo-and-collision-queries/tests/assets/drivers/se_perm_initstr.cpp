/* se_perm_initstr.cpp - SE_SetParameterDistribution / SE_GetNumberOfPermutations
 * / SE_SelectPermutation / SE_GetPermutationIndex, and SE_InitWithString, all
 * present since well before this step's start tag. Regression material only.
 *
 * usage: se_perm_initstr <dist.xosc> <plain.xosc> <index>
 *
 * index_before_init is printed for the log's sake and deliberately NOT
 * asserted by the case: what SE_GetPermutationIndex reports between
 * SE_SelectPermutation and SE_Init is not stated by any document of any step,
 * and the two reference ends disagree - the release that introduced the call
 * reports the selected index there and step 02's own scored case reads it
 * back, while the reference moves to -1 inside step 03's range. Neither
 * reading can be graded without failing one of the two conforming trees.
 */
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "esminiLib.hpp"

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <dist.xosc> <plain.xosc> <index>\n", argv[0]);
        return 2;
    }
    const char* dist = argv[1];
    const char* plain = argv[2];
    int index = std::atoi(argv[3]);

    int n_before = SE_GetNumberOfPermutations();
    std::printf("n_perm_before=%d\n", n_before);

    int load = SE_SetParameterDistribution(dist);
    std::printf("load=%d\n", load);

    int n = SE_GetNumberOfPermutations();
    std::printf("n_perm=%d\n", n);

    if (load == 0)
    {
        std::printf("select_oob=%d\n", SE_SelectPermutation(n));

        int sel = SE_SelectPermutation(index);
        std::printf("select=%d\n", sel);
        if (sel == 0)
        {
            /* Not applied until SE_Init runs the distribution. */
            std::printf("index_before_init=%d\n", SE_GetPermutationIndex());

            int rc = SE_Init(dist, 0, 0, 0, 0);
            std::printf("init=%d\n", rc);
            if (rc == 0)
            {
                std::printf("index_after_init=%d\n", SE_GetPermutationIndex());
                SE_Close();
            }
        }
    }

    SE_ResetParameterDistribution();
    std::printf("n_perm_after_reset=%d\n", SE_GetNumberOfPermutations());

    /* Separate, unrelated exercise of SE_InitWithString on a plain scenario's
     * XML contents (no distribution involved). */
    std::ifstream f(plain);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", plain); return 2; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string xml = ss.str();

    int irc = SE_InitWithString(xml.c_str(), 0, 0, 0, 0);
    std::printf("initstr=%d\n", irc);
    if (irc == 0)
    {
        SE_StepDT(0.05f);
        SE_ScenarioObjectState st;
        if (SE_GetObjectState(0, &st) == 0)
        {
            std::printf("initstr_ego_speed=%.4f\n", static_cast<double>(st.speed));
        }
        SE_Close();
    }

    std::printf("survived=1\n");
    return 0;
}

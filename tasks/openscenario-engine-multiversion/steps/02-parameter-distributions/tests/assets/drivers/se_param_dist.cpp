/*
 * se_param_dist - the scenario-engine parameter-distribution API.
 *
 * usage: se_param_dist <xosc> <dist> <index>
 *
 * Loads a parameter distribution file, reports the permutation set it
 * describes, selects one permutation, and starts the scenario so the effect
 * of the selection is observable in the entities' initial state.
 *
 * Half of what this driver calls does not exist at the step's starting
 * release, so this driver must not compile at the starting tag. The groups
 * it backs are `scored` in groups.txt, and that is what makes them fail
 * there rather than pass on inherited behaviour.
 */
#include <cstdio>
#include <cstdlib>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <xosc> <dist> <index>\n", argv[0]);
        return 2;
    }
    const char *xosc  = argv[1];
    const char *dist  = argv[2];
    int         index = std::atoi(argv[3]);

    /* Before anything is loaded there is no distribution: the count is 0 and
     * the index is the default. These two are read before SE_Init, so they
     * cannot be satisfied by anything the scenario does later. */
    std::printf("n_perm_before=%d\n", SE_GetNumberOfPermutations());
    std::printf("index_before=%d\n", SE_GetPermutationIndex());

    int load = SE_SetParameterDistribution(dist);
    std::printf("load=%d\n", load);

    /* The permutation count is queried unconditionally, whether or not the
     * load was accepted: a refused distribution enumerates nothing, and that
     * is itself part of what a case may assert, so it cannot live behind an
     * early return. */
    int n = SE_GetNumberOfPermutations();
    std::printf("n_perm=%d\n", n);
    std::printf("index_loaded=%d\n", SE_GetPermutationIndex());

    if (load == 0)
    {
        /* Selecting an index that does not exist is an error return, not a
         * silent clamp to a valid one. This holds even when n is 0: there is
         * no valid index into an empty enumeration. */
        std::printf("select_oob=%d\n", SE_SelectPermutation(n));
        std::printf("select_negative=%d\n", SE_SelectPermutation(-1));

        int sel = SE_SelectPermutation(index);
        std::printf("select=%d\n", sel);
        if (sel == 0)
        {
            std::printf("index_selected=%d\n", SE_GetPermutationIndex());

            /* Refuse again, this time with a selection already in place. The
             * n_perm printed above was read before any index was refused, so
             * it cannot witness that a refusal leaves the enumeration and the
             * standing selection alone; these four lines can. A clamping
             * implementation is distinguishable here and nowhere else: it
             * would move the index to 0 or to n-1 instead of leaving it. */
            std::printf("select_oob_after=%d\n", SE_SelectPermutation(n));
            std::printf("select_negative_after=%d\n", SE_SelectPermutation(-1));
            std::printf("n_perm_after_refusal=%d\n", SE_GetNumberOfPermutations());
            std::printf("index_after_refusal=%d\n", SE_GetPermutationIndex());

            int rc = SE_Init(xosc, 0, 0, 0, 0);
            std::printf("init=%d\n", rc);
            if (rc == 0)
            {
                /* The distribution survives initialisation and keeps the
                 * selection that was asked for, rather than falling back to
                 * the first permutation. */
                std::printf("n_perm_after_init=%d\n", SE_GetNumberOfPermutations());
                std::printf("index_after_init=%d\n", SE_GetPermutationIndex());

                SE_Step();
                SE_ScenarioObjectState st;
                if (SE_GetObjectState(0, &st) == 0)
                {
                    std::printf("ego_speed=%.4f\n", static_cast<double>(st.speed));
                }
                if (SE_GetObjectState(1, &st) == 0)
                {
                    std::printf("target_speed=%.4f\n", static_cast<double>(st.speed));
                    std::printf("target_x=%.4f\n", static_cast<double>(st.x));
                }
                SE_Close();
            }
        }
    }

    /* Resetting clears the distribution regardless of what happened above -
     * a stub that only ever returns a constant fails either the counts
     * above or this one. */
    SE_ResetParameterDistribution();
    std::printf("n_perm_after_reset=%d\n", SE_GetNumberOfPermutations());
    std::printf("survived=1\n");
    return 0;
}

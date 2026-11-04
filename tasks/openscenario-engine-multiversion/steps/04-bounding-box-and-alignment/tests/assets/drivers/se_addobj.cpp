/*
 * se_addobj - SE_AddObject and the id it returns.
 *
 * usage: se_addobj <xosc> <role_a> <role_b>
 *
 * SE_AddObject takes a vehicle type, a vehicle category, a role and a model
 * id, and returns the id of the object it created -- which is whatever the
 * next free id happens to be, not a fixed 0. Two objects are added, so the
 * case can insist that the two ids differ from each other and from the ids the
 * scenario already declared. One of them is then deleted, and the count drops
 * back by exactly one.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esminiLib.hpp"

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: %s <xosc> <role_a> <role_b>\n", argv[0]);
        return 2;
    }
    int role_a = std::atoi(argv[2]);
    int role_b = std::atoi(argv[3]);

    /* Adding is refused before a scenario exists. */
    std::printf("add_before_init=%d\n", SE_AddObject("early", 1, 1, role_a, 0));

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0)
    {
        return 1;
    }

    int n0 = SE_GetNumberOfObjects();
    std::printf("nobj_0=%d\n", n0);

    int id_a = SE_AddObject("added_a", 1 /* VEHICLE */, 1 /* CAR */, role_a, 0);
    std::printf("id_a=%d\n", id_a);
    std::printf("nobj_1=%d\n", SE_GetNumberOfObjects());

    int id_b = SE_AddObject("added_b", 1 /* VEHICLE */, 1 /* CAR */, role_b, 0);
    std::printf("id_b=%d\n", id_b);
    std::printf("nobj_2=%d\n", SE_GetNumberOfObjects());

    /* Each id is a fresh one: distinct from the other, and never a re-use of
     * an id the scenario's own entities already occupy. */
    std::printf("ids_distinct=%d\n",
                (id_a >= 0 && id_b >= 0 && id_a != id_b && id_a >= n0 && id_b >= n0) ? 1 : 0);

    SE_ReportObjectPos(id_a, 0.0f, 40.0f, -1.5f, 0.0f, 0.0f, 0.0f, 0.0f);
    SE_ReportObjectPos(id_b, 0.0f, 60.0f, -1.5f, 0.0f, 0.0f, 0.0f, 0.0f);
    SE_StepDT(0.05f);

    SE_ScenarioObjectState st;
    std::memset(&st, 0, sizeof(st));
    std::printf("state_a_rc=%d\n", SE_GetObjectState(id_a, &st));
    std::printf("type_a=%d\n", st.objectType);
    std::printf("category_a=%d\n", st.objectCategory);
    std::printf("id_a_state=%d\n", st.id);
    std::memset(&st, 0, sizeof(st));
    std::printf("state_b_rc=%d\n", SE_GetObjectState(id_b, &st));
    std::printf("type_b=%d\n", st.objectType);
    std::printf("category_b=%d\n", st.objectCategory);
    std::printf("id_b_state=%d\n", st.id);

    /* The name lookup is called and printed one id at a time: at this release
     * two names fetched into the same expression do not stay independent. */
    const char *na = SE_GetObjectName(id_a);
    std::printf("name_a=%s\n", na != nullptr ? na : "(null)");
    const char *nb = SE_GetObjectName(id_b);
    std::printf("name_b=%s\n", nb != nullptr ? nb : "(null)");

    /* Deleting an object that exists takes it back out of the scenario. */
    std::printf("delete_rc=%d\n", SE_DeleteObject(id_b));
    SE_StepDT(0.05f);
    std::printf("nobj_3=%d\n", SE_GetNumberOfObjects());
    /* The one that was not deleted is untouched. */
    std::printf("state_a_rc_after=%d\n", SE_GetObjectState(id_a, &st));
    /* A delete of an id that no longer exists is refused. */
    std::printf("delete_again_rc=%d\n", SE_DeleteObject(id_b));

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

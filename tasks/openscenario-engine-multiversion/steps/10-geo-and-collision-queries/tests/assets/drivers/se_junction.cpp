/* se_junction.cpp - where an entity with no route comes out of a junction.
 *
 * usage: se_junction <scenario.xosc> <steps> <dt>
 *
 * Steps the scenario and records, per object, the ordered sequence of
 * DISTINCT road ids it occupies. On a network whose only junction is a
 * four-way crossing that sequence is `<incoming> > <connecting> > <outgoing>`,
 * so it names the exit the entity took without depending on any world
 * coordinate, and it is the same shape whichever exit was chosen.
 *
 * Every entry point used here predates this step (SE_Init, SE_StepDT,
 * SE_GetNumberOfObjects, SE_GetObjectState, SE_Close), so the driver compiles
 * and runs at both ends of the step: what changes between them is the answer,
 * not the availability of the call.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "esminiLib.hpp"

#define MAX_OBJ 8

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <scenario> <steps> <dt>\n", argv[0]);
        return 2;
    }

    int    steps = std::atoi(argv[2]);
    double dt    = std::atof(argv[3]);

    int rc = SE_Init(argv[1], 0, 0, 0, 0);
    std::printf("init=%d\n", rc);
    if (rc != 0) return 1;

    int n = SE_GetNumberOfObjects();
    if (n > MAX_OBJ) n = MAX_OBJ;
    std::printf("objects=%d\n", n);

    std::vector<unsigned int> path[MAX_OBJ];

    for (int step = 0; step <= steps; step++)
    {
        if (step > 0) SE_StepDT(static_cast<float>(dt));
        for (int i = 0; i < n; i++)
        {
            SE_ScenarioObjectState st;
            std::memset(&st, 0, sizeof(st));
            if (SE_GetObjectState(SE_GetId(i), &st) != 0) continue;
            unsigned int r = static_cast<unsigned int>(st.roadId);
            if (path[i].empty() || path[i].back() != r) path[i].push_back(r);
        }
    }

    for (int i = 0; i < n; i++)
    {
        std::printf("obj%d_path=", i);
        for (size_t k = 0; k < path[i].size(); k++)
        {
            std::printf("%s%u", k ? ">" : "", path[i][k]);
        }
        std::printf("\n");

        SE_ScenarioObjectState st;
        std::memset(&st, 0, sizeof(st));
        SE_GetObjectState(SE_GetId(i), &st);
        std::printf("obj%d_final_road=%u\n", i, static_cast<unsigned int>(st.roadId));
        std::printf("obj%d_hops=%d\n", i, static_cast<int>(path[i].size()));
    }

    SE_Close();
    std::printf("survived=1\n");
    return 0;
}

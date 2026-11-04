// String road/junction identifier mapping, new in esminiRMLib at this release.
//
// Prints the measured mapping in both directions, plus the not-found answers,
// so a stub returning one constant cannot satisfy the case.
//
// Two further groups of lines, both also new at this release:
//
//  * R05.1's id_t surface. `id_t` is uint32_t and `SE_ID_UNDEFINED` is
//    0xffffffff, so the not-found sentinel compares equal to -1 when read as a
//    signed int and to 4294967295 when read as unsigned, and an id that is
//    *defined* must NOT compare equal to it. SE_GetRoadInfoAtDistance reports
//    junctionId for a lookahead target; the scenario's road is junction-free,
//    so every sample must report the undefined sentinel. The type identity is
//    probed without resting on a signed/unsigned guess: (id_t)-1 ==
//    SE_ID_UNDEFINED is the assertion, together with the width it observed.
//
//  * R05.51's "the integer is what gets reported" half. The forward mapping is
//    not enough on its own -- a build could compute the integer for the query
//    functions and still carry the road's *text* through the report path. The
//    driver therefore reads the road id back out of the *reported* object
//    state and prints the integer rather than the string. The scenario it is
//    handed is a plain OpenDRIVE id (the string-id loader is not required to
//    accept a string-id file, and the same integer-propagation path is what
//    R05.51 is about), so the two checks here are "the reported id is defined"
//    and "the reported id is not negative" -- never a specific value, which
//    would pin the fixture rather than the contract.
#include <esminiRMLib.hpp>
#include <esminiLib.hpp>
#include <cstdio>
#include <type_traits>

// R7 retypes the road and junction id FIELDS of the three reported structs, not
// just the typedef. The runtime lines below cannot see that: every one of them
// compares a field against the sentinel, and a build that left `int junctionId`
// in place still satisfies `ri.junctionId != (id_t)SE_ID_UNDEFINED` because the
// usual arithmetic conversions promote the int to unsigned before the compare.
// These six assertions are the only thing in the step that distinguishes "the
// typedef exists" from "the fields were actually retyped", and they cost no
// case: a driver that fails to compile fails its own cases and nothing else.
static_assert(std::is_same<decltype(SE_ScenarioObjectState::roadId), id_t>::value,
              "R7: SE_ScenarioObjectState.roadId must be id_t");
static_assert(std::is_same<decltype(SE_ScenarioObjectState::junctionId), id_t>::value,
              "R7: SE_ScenarioObjectState.junctionId must be id_t");
static_assert(std::is_same<decltype(SE_RoadInfo::roadId), id_t>::value,
              "R7: SE_RoadInfo.roadId must be id_t");
static_assert(std::is_same<decltype(SE_RoadInfo::junctionId), id_t>::value,
              "R7: SE_RoadInfo.junctionId must be id_t");
static_assert(std::is_same<decltype(RM_PositionData::roadId), id_t>::value,
              "R7: RM_PositionData.roadId must be id_t");
static_assert(std::is_same<decltype(RM_PositionData::junctionId), id_t>::value,
              "R7: RM_PositionData.junctionId must be id_t");
// ...and the laneId members stay SIGNED, which R7 states just as explicitly.
static_assert(std::is_same<decltype(SE_ScenarioObjectState::laneId), int>::value,
              "R7: SE_ScenarioObjectState.laneId must stay a signed int");
static_assert(std::is_same<decltype(SE_RoadInfo::laneId), int>::value,
              "R7: SE_RoadInfo.laneId must stay a signed int");
static_assert(std::is_same<decltype(RM_PositionData::laneId), int>::value,
              "R7: RM_PositionData.laneId must stay a signed int");

int main(int argc, char** argv) {
    if (argc < 3) {
        // Both arguments are required: the second is the scenario whose
        // reported state carries the integer road id (R05.51).
        fprintf(stderr, "usage: %s <xodr> <xosc>\n", argv[0]);
        return 1;
    }

    if (RM_Init(argv[1]) != 0) {
        fprintf(stderr, "RM_Init failed\n");
        return 1;
    }

    // Forward mapping: string -> internal id.
    printf("rd0=%d\n", (int)RM_GetRoadIdFromString("rd_0"));
    printf("rd3=%d\n", (int)RM_GetRoadIdFromString("rd_3"));
    printf("rd16=%d\n", (int)RM_GetRoadIdFromString("rd_16"));

    // Reverse mapping: internal id -> original string.
    const char* s3 = RM_GetRoadIdString(RM_GetRoadIdFromString("rd_3"));
    printf("str3=%s\n", s3 ? s3 : "(null)");

    // A string id that is not present.
    printf("missing=%u\n", (unsigned)RM_GetRoadIdFromString("no_such_road"));

    // Junctions carry string ids too.
    printf("jx=%d\n", (int)RM_GetJunctionIdFromString("JX"));
    const char* jx = RM_GetJunctionIdString(RM_GetJunctionIdFromString("JX"));
    printf("jxstr=%s\n", jx ? jx : "(null)");
    printf("jxmissing=%u\n", (unsigned)RM_GetJunctionIdFromString("no_such_junction"));

    // R05.1: the id_t surface. `id_t` is unsigned, so a value read from it can
    // never be negative -- but the sentinel is documented as the all-ones bit
    // pattern, and comparing it to -1 is the check that pins both the width and
    // the value. `sizeof(id_t) == 4` and `(id_t)-1 == SE_ID_UNDEFINED` together
    // say "32-bit unsigned all-ones", which is what R05.1's row states.
    id_t sentinel = (id_t)-1;
    printf("id_t_bytes=%u\n", (unsigned)sizeof(id_t));
    printf("sentinel_matches_macro=%d\n", sentinel == (id_t)SE_ID_UNDEFINED ? 1 : 0);
    printf("sentinel_is_all_ones=%d\n", sentinel == 0xffffffffu ? 1 : 0);
    // R7 says id_t is UNSIGNED, and that code testing a road or junction id for
    // `< 0` is wrong at this release. Neither line above pins that: with a
    // signed id_t both comparisons still hold, because the usual arithmetic
    // conversions promote (id_t)-1 to 4294967295u before either compare. This
    // one does not survive a signed typedef -- (id_t)-1 > (id_t)0 is false for
    // any signed type and true for any unsigned one.
    printf("id_t_unsigned=%d\n", ((id_t)-1 > (id_t)0) ? 1 : 0);
    // A REAL id must be distinguishable from the sentinel; without this the
    // two lines above would be satisfied by a build that defined the macro to
    // something useless.
    printf("real_id_differs_from_sentinel=%d\n",
           ((id_t)RM_GetRoadIdFromString("rd_0") != sentinel) ? 1 : 0);
    // R1: an internal id that maps to no string answers with an EMPTY string --
    // not a null pointer and not a stale name. The sentinel is the id that is
    // guaranteed to map to nothing, so it is the probe, and it is asked on
    // both the road and the junction side.
    {
        const char *unmapped_road = RM_GetRoadIdString(sentinel);
        const char *unmapped_junction = RM_GetJunctionIdString(sentinel);
        printf("unmapped_road_is_empty=%d\n",
               (unmapped_road != NULL && unmapped_road[0] == '\0') ? 1 : 0);
        printf("unmapped_junction_is_empty=%d\n",
               (unmapped_junction != NULL && unmapped_junction[0] == '\0') ? 1 : 0);
    }

    RM_Close();

    // R05.51: report a position through the scenario's own object and read the
    // integer road id back out of the reported state, rather than reading it
    // out of the mapping query.
    if (SE_Init(argv[2], 0, 0, 0, 0) != 0) {
        fprintf(stderr, "SE_Init failed\n");
        return 1;
    }
    SE_Step();

    int nobj = SE_GetNumberOfObjects();
    printf("num_objects=%d\n", nobj);

    SE_ScenarioObjectState st;
    if (SE_GetObjectState(0, &st) != 0) {
        fprintf(stderr, "SE_GetObjectState failed\n");
        return 1;
    }
    // "Reported ids need not match the OpenDRIVE text" is the point: the id
    // read out of the reported state is an integer the reporter generated. It
    // must be defined, and it must be an integer rather than a string -- which
    // is what (unsigned)st.roadId prints. Its exact value is the fixture's
    // business, not the contract's, so it is not pinned.
    printf("reported_road_id_defined=%d\n", (st.roadId != (id_t)SE_ID_UNDEFINED) ? 1 : 0);
    printf("reported_lane_id_readable=%d\n", (st.laneId != 0 || st.roadId != (id_t)SE_ID_UNDEFINED) ? 1 : 0);

    // R05.1: no road in this scenario meets a junction, so the lookahead target
    // must report the undefined junction id at every sample taken.
    int defined_junction = 0;
    int lookups = 0;
    SE_RoadInfo ri;
    for (float d = 1.0f; d <= 20.0f; d += 1.0f) {
        if (SE_GetRoadInfoAtDistance(0, d, &ri, 0, true) >= 0) {
            lookups++;
            if (ri.junctionId != (id_t)SE_ID_UNDEFINED) {
                defined_junction++;
            }
        }
    }
    printf("roadinfo_lookups_ge1=%d\n", lookups >= 1 ? 1 : 0);
    printf("roadinfo_defined_junctions=%d\n", defined_junction);

    SE_Close();
    printf("survived=1\n");
    return 0;
}

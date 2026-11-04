#!/usr/bin/env bash
# New. Roads with string `id` attributes must map to stable internal ids and
# back, in file order, and an unknown string id must report the documented
# not-found sentinel rather than crashing or aliasing to a real road. The
# closing sentence of R1 (odrplot must sample a string-id OpenDRIVE file) is
# checked here too, because it is a NEW capability of this release: the start
# build never returns on string_ids.xodr, so the check cannot live in the
# gate odrplot group, which has to keep passing at the start tag.
source /tests/caselib.sh

cp "$FIXTURES/string_ids.xodr" "$WORK/string_ids.xodr"
cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_string_ids string_ids.xodr elev_cruise.xosc
need_status 0 $? "d_string_ids"

# Internal ids are assigned in file order: rd_0..rd_3 map to 0..3 (rd_4 is
# absent from the fixture on purpose, so rd_5 onward do NOT line up with their
# suffix -- only rd_0..rd_3 and rd_16 are asserted here, which is why rd_16
# maps to 15, not 16).
need_kv rd0 0 "rd_0 internal id"
need_kv rd3 3 "rd_3 internal id"
need_kv rd16 15 "rd_16 internal id"
need_kv str3 rd_3 "reverse mapping of rd_3's internal id"
need_kv missing 4294967295 "unknown road id string returns the not-found sentinel"
need_kv survived 1 "driver completed"

# R05.1: the id_t surface. `id_t` is declared as a 32-bit unsigned type and
# `SE_ID_UNDEFINED` as the all-ones bit pattern; the case pins the width, the
# macro's value, and that a real id is distinguishable from the sentinel --
# which is what makes "not found" reportable at all. The junction half is
# asserted in b-junction-id-mapping.sh, where the junction-bearing fixture
# lives.
need_kv id_t_bytes 4 "id_t is a 32-bit type"
need_kv sentinel_matches_macro 1 "SE_ID_UNDEFINED is the all-ones value"
# R1 promises an EMPTY string for an id that maps to nothing -- not a null
# pointer the caller would have to test for. The sentinel is that id.
need_kv unmapped_road_is_empty 1 "RM_GetRoadIdString of the sentinel answers an empty string"
need_kv unmapped_junction_is_empty 1 "RM_GetJunctionIdString of the sentinel answers an empty string"
need_kv sentinel_is_all_ones 1 "the not-found sentinel is 0xffffffff"
# R7 also says id_t is UNSIGNED and that testing an id for `< 0` is wrong at
# this release. The three lines above do NOT pin that -- a signed 32-bit id_t
# satisfies all three, because (id_t)-1 is promoted to 4294967295u before each
# compare. This one fails for any signed typedef.
need_kv id_t_unsigned 1 "id_t is an unsigned type, so no id is ever negative"
need_kv real_id_differs_from_sentinel 1 "a real road id is distinguishable from the sentinel"

# R05.51: the integer generated for a road id is what the report path carries,
# not the OpenDRIVE text. Read out of the object's reported state rather than
# out of the mapping query, so a build that answered the queries but carried
# the string through the reporter is excluded. The scenario's road is a plain
# integer id, so the value itself is the fixture's business and is not pinned:
# only that is defined, and that the integer is readable alongside it, are.
need_kv reported_road_id_defined 1 "the reported road id is a defined integer id"
need_kv reported_lane_id_readable 1 "the reported lane id is readable alongside it"

# R1: odrplot on string road ids. string_ids.xodr has 16 <road> elements;
# odrplot must load and sample it and write at least one "lane" header per
# road. Row values are not pinned: R1 promises that the file loads and
# samples, not any particular numbering in the output.
run_tool "$ODRPLOT" string_ids.xodr sid.csv 5.0
need_status 0 $? "odrplot on string road ids"
need_file "$WORK/sid.csv"
nlane=$(grep -c '^lane' "$WORK/sid.csv")
[ "$nlane" -ge 16 ] || fail "expected at least one lane header per road (16 roads) in string-id odrplot output, got $nlane"
exit 0

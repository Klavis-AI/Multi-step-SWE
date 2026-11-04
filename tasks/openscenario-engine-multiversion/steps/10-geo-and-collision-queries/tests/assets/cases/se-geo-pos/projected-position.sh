#!/usr/bin/env bash
# SE_GetGeoPos(object_id, lat, lon) is the embedding ABI's geodetic query. Two
# halves are checked here, and the build profile decides which of them can say
# anything: the symbol half always can, the value half cannot.
#
# The graded build configures -DUSE_PROJ=False, so no projection library is
# linked in and the network's projection string can never be turned into
# latitude/longitude. The engine's own answer in that configuration -- measured,
# not assumed, on a network that HAS a "+proj=utm ... +datum=WGS84"
# geoReference and on one that has none at all -- is the documented -1 for
# every object, with the caller's storage left alone and no crash. That is the
# contract this case pins; it deliberately does not assert any latitude or
# longitude, because this build never produces one.
source /tests/caselib.sh

# The C-linkage half of the claim, read where it actually lives.
find_lib() {  # find_lib <soname>
    local d dirs
    IFS=: read -r -a dirs <<< "$LIBDIR"
    for d in "${dirs[@]}"; do
        [ -f "$d/$1" ] && { printf '%s\n' "$d/$1"; return 0; }
    done
    return 1
}
se_so=$(find_lib libesminiLib.so) || fail "libesminiLib.so not found under LIBDIR=$LIBDIR"
nm -D --defined-only "$se_so" > "$WORK/se.sym" 2>"$WORK/se.err" || fail "nm failed on $se_so"
awk '$2 ~ /^[TWBDR]$/ && $3 == "SE_GetGeoPos" { f = 1 } END { exit !f }' "$WORK/se.sym" \
    || fail "SE_GetGeoPos is not exported from libesminiLib.so with C linkage"
grep -q '_Z.*SE_GetGeoPos' "$WORK/se.sym" && fail "SE_GetGeoPos also appears C++-mangled"

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" \
   "$FIXTURES/tri_lane_cars.xosc" "$FIXTURES/tri_lane_600m.xodr" "$WORK/"

# straight_500m.xodr carries a full projection string in its <geoReference>.
run_driver se_geo_pos "$WORK/two_car_brake.xosc"
need_status 0 $? "se_geo_pos on a network with a projection string"
need_kv init 0 "SE_Init"
need_kv survived 1 "SE_Close"
need_grep '^rc0=-1 lat0=-999\.000000 lon0=-999\.000000$' "$WORK/last.out" \
    "no PROJ support: failure reported and the caller's storage left alone"
need_kv null_lat -1 "null latitude pointer"
need_kv null_lon -1 "null longitude pointer"
need_grep '^badobj=-1 badobj_lat_untouched=1 badobj_lon_untouched=1$' "$WORK/last.out" \
    "unknown object id"

# tri_lane_600m.xodr has no <geoReference> at all. The answer is the same
# failure, reported the same way, and still without a crash: a missing
# projection is an ordinary error path, not a special one.
run_driver se_geo_pos "$WORK/tri_lane_cars.xosc"
need_status 0 $? "se_geo_pos on a network with no projection string"
need_kv init 0 "SE_Init on tri_lane_cars"
need_kv survived 1 "SE_Close on tri_lane_cars"
need_grep '^rc0=-1 lat0=-999\.000000 lon0=-999\.000000$' "$WORK/last.out" \
    "no geoReference: failure reported and the caller's storage left alone"
need_kv null_lat -1 "null latitude pointer, no geoReference"
need_kv null_lon -1 "null longitude pointer, no geoReference"
need_grep '^badobj=-1 badobj_lat_untouched=1 badobj_lon_untouched=1$' "$WORK/last.out" \
    "unknown object id, no geoReference"

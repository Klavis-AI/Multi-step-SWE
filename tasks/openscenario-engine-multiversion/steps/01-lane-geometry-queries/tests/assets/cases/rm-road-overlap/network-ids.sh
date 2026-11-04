#!/usr/bin/env bash
# RM_GetOverlappingRoadId addressed by index, over the whole range the count
# declares. The junction case asserts a set of ids this case does not know; what
# this one adds is the range itself and the meaning of the ids:
#
#   * every index 0 .. n-1 answers, and answers with a road id the loaded
#     network actually contains -- read back through RM_GetNumberOfRoads and
#     RM_GetIdOfRoadFromIndex rather than compared against a list baked in here,
#     so a build that invents an id outside the network is caught;
#   * no index in range answers -1, which is the value the contract reserves for
#     an out-of-range index and for the empty case -- an implementation that
#     returned -1 for a valid index as well would otherwise pass every other
#     case in this group.
source /tests/caselib.sh

# The ids the network declares, one per line. `net` makes the driver print them.
network_ids() {
    sed -n 's/^network_id_[0-9]*=//p' "$WORK/last.out"
}

# overlapping_ids <fixture> <x> <y> <what>: runs the driver and echoes the ids
# it returned for indices 0 .. n_overlap-1, one per line.
overlapping_ids() {
    run_driver rm_overlap "$1" "$2" "$3" net || fail "$4: rm_overlap exited $?"
    need_kv init 0 "$4: RM_Init"
    need_kv survived 1 "$4: driver ran to the end"
    local i n
    n=$(sed -n 's/^n_overlap=//p' "$WORK/last.out" | tail -1)
    [ -n "$n" ] || fail "$4: no n_overlap line"
    case "$n" in ''|*[!0-9]*) fail "$4: n_overlap is not a count: $n" ;; esac
    for ((i = 0; i < n; i++)); do
        # Every in-range index must produce a record. A driver that stopped
        # printing early, or an implementation that made the call fail, leaves
        # no line and is a failure rather than a shorter list.
        got=$(sed -n "s/^overlap_$i=//p" "$WORK/last.out" | tail -1)
        [ -n "$got" ] || fail "$4: index $i of $n produced no overlap_$i line"
        printf '%s\n' "$got"
    done
}

# --- a junction: several overlapping roads, all of them in the network ---
ids=$(overlapping_ids "$FIXTURES/fabriksgatan.xodr" 25 0 "junction")
n=$(sed -n 's/^n_overlap=//p' "$WORK/last.out" | tail -1)
[ "$n" -ge 2 ] || fail "junction: expected more than one overlapping road, got $n"
declare -A net=()
while read -r id; do [ -n "$id" ] && net[$id]=1; done < <(network_ids)
[ "${#net[@]}" -ge 2 ] || fail "junction: network reported ${#net[@]} road ids"
count=0
while read -r id; do
    [ -n "$id" ] || continue
    count=$((count + 1))
    [ "$id" = -1 ] && fail "junction: an in-range index returned -1"
    [ -n "${net[$id]:-}" ] || fail "junction: returned road id $id, which is not in the network's ${#net[@]}-road inventory"
done <<< "$ids"
[ "$count" = "$n" ] || fail "junction: $count in-range ids for a declared count of $n"

# --- a plain road: n = 1, and the single in-range index is the road itself ---
ids=$(overlapping_ids "$FIXTURES/straight_500m.xodr" 250 0 "plain road")
n=$(sed -n 's/^n_overlap=//p' "$WORK/last.out" | tail -1)
[ "$n" = 1 ] || fail "plain road: expected a count of 1, got $n"
road=$(sed -n 's/^road_id=//p' "$WORK/last.out" | tail -1)
[ "$ids" = "$road" ] || fail "plain road: index 0 returned $ids, but the position reports road $road"
declare -A net=()
while read -r id; do [ -n "$id" ] && net[$id]=1; done < <(network_ids)
[ -n "${net[$road]:-}" ] || fail "plain road: road $road is not in the network's road inventory"

# --- the empty case: n = 0, so there is no in-range index at all, and the
# --- driver's own out-of-range probe still answers -1 rather than crashing ---
ids=$(overlapping_ids "$FIXTURES/fabriksgatan.xodr" 0 0 "off-surface point")
n=$(sed -n 's/^n_overlap=//p' "$WORK/last.out" | tail -1)
[ "$n" = 0 ] || fail "off-surface point: expected a count of 0, got $n"
[ -z "$ids" ] || fail "off-surface point: count is 0 but in-range ids were returned: $ids"
need_kv overlap_oob -1 "off-surface point: out-of-range index is still -1"

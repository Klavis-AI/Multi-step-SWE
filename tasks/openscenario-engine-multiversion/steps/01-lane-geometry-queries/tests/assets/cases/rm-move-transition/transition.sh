#!/usr/bin/env bash
# instruction.md section 8 states the obligation this case scores:
# RM_PositionMoveForward returns 1 for "moved into a new road segment", 2 for
# "moved into a junction and made a choice", and 0 for a move that stays
# inside the current road. The position SETTERS are not where those codes come
# from at this release -- RM_SetLanePosition and RM_SetWorldXYHPosition both
# end in a literal `return 0` -- so do not "fix" this case to call one of
# them. fabriksgatan.xodr's junction 4 is built from roads 5..16 connecting
# roads 0/1 (predecessors) to roads 2/3 (successors).
source /tests/caselib.sh

# Road 14 is one of junction 4's connectors, length ~15.47 m. Starting at
# s=10 and moving 8 m forward crosses its end into road 0 -- an unambiguous
# continuation onto a plain road, no choice involved: code 1.
run_driver rm_move "$FIXTURES/fabriksgatan.xodr" 14 -1 10 8 || fail "rm_move exited $?"
need_kv init 0 "RM_Init"
need_kv before_road 14 "start position on connector road 14"
need_kv before_junction 4 "start position inside junction 4"
need_kv move_rc 1 "RM_PositionMoveForward onto a new plain segment"
need_kv after_road 0 "landed on road 0"
need_kv after_junction -1 "left the junction"

# Road 2 is a plain leg feeding junction 4, length ~304 m. Starting near its
# end (s=300) and moving 6 m forward crosses into the junction, where more
# than one connecting road could be chosen: code 2.
run_driver rm_move "$FIXTURES/fabriksgatan.xodr" 2 -1 300 6 || fail "rm_move exited $?"
need_kv before_road 2 "start position on plain road 2"
need_kv before_junction -1 "start position outside any junction"
need_kv move_rc 2 "RM_PositionMoveForward entering a junction"
need_kv after_junction 4 "landed inside junction 4"

# A move that stays inside the same plain road, nowhere near a boundary,
# still returns plain 0 -- the widening adds values, it does not remove 0.
run_driver rm_move "$FIXTURES/fabriksgatan.xodr" 0 -1 2 6 || fail "rm_move exited $?"
need_kv before_road 0 "start position on plain road 0"
need_kv move_rc 0 "RM_PositionMoveForward with no road/junction transition"
need_kv after_road 0 "stayed on road 0"
need_kv survived 1 "driver ran to the end"

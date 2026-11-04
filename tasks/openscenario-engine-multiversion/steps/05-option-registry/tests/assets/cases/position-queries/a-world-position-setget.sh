#!/usr/bin/env bash
# Inherited. RM_SetWorldPosition and RM_GetPositionData must remain stable:
# creating a position handle, setting a world position, and reading it back
# must all succeed, and moving further along the road must report a larger s.
#
# Position setters may return 0, 1 (moved into a new road segment) or 2 (moved
# into a junction and made a choice), so success is checked as >= 0, never
# == 0; the driver already normalizes this to 1/0 before printing.
source /tests/caselib.sh

cp "$FIXTURES/e6mini.xodr" "$WORK/e6mini.xodr"

run_driver d_positions e6mini.xodr
need_status 0 $? "d_positions"

need_kv handle_ge0 1 "position handle created"
need_kv setworld_ge0 1 "world position set at the origin"
need_kv getdata_ge0 1 "position data read back"
need_kv roadid_valid 1 "a valid road id (not the no-road sentinel) is reported"
need_kv setworld2_ge0 1 "world position set further along the road"
need_kv s_positive 1 "moving along the road increases s"
need_kv survived 1 "driver completed"
exit 0

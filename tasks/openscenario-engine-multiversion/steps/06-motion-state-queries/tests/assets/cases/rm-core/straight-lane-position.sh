#!/usr/bin/env bash
# Road-manager entry points that predate this step, on a two-lane straight
# road. Nothing called here is new at v2.46.4, so a tree that adds the
# motion-state queries but disturbs the road manager fails this case while a
# tree that is still at the starting release passes it.
source /tests/caselib.sh

run_driver rm_core "$FIXTURES/straight_500m.xodr" 1 -1 250 || fail "rm_core exited $?"
need_kv survived 1 "driver ran to completion"
need_kv init 0 "RM_Init accepted the OpenDRIVE file"

# Network shape. straight_500m.xodr carries a single road of 500 m with two
# driving lanes; odrplot independently reports 7 lane entries of which 1 and
# -1 are driving, which is the same network seen through the other tool.
need_kv nroads 1 "road count"
need_kv first_road_id 1 "id of the only road"
need_kv all_roads_have_length 1 "every road index maps to an id with positive length"
need_close road_length 500.000 0.001 "road length"
need_kv nlanes 2 "driving lane count at s=250"

# The handle is checked for VALIDITY, not for a particular number. Requirement
# 14 names RM_CreatePosition and requires the road-manager surface to work
# unchanged; no agent-visible document states that the first handle is 0, and
# an implementation that numbers handles from 1 is conforming. What the handle
# has to be is usable, and that is proved by the calls below: every subsequent
# assertion in this case is made through this handle, so a handle that does not
# address the position it created fails them.
need_nonneg handle "RM_CreatePosition returned a usable handle"
# 2.21.2 widened the road-manager setter return code: 0 still means OK, but 1
# and 2 report "moved into a new road segment" and "entered a junction and
# chose". The driver therefore reports >= 0; an `== 0` check here would be a
# latent failure rather than a real assertion.
need_kv set_lane_ok 1 "RM_SetLanePosition succeeded"
need_kv get_position 1 "RM_GetPositionData succeeded"
need_kv pos_road 1 "position road id"
need_kv pos_lane -1 "position lane id"
need_close pos_s 250.000 0.001 "position s"

# The world coordinates the road manager produced must map back onto the same
# road and lane, which a placement that silently ignored the lane argument
# could not satisfy.
need_kv xyh_ok 1 "RM_SetWorldXYHPosition succeeded"
need_kv roundtrip_road 1 "world coordinates map back to the same road"
need_kv roundtrip_lane -1 "world coordinates map back to the same lane"

#!/usr/bin/env bash
source /tests/caselib.sh

# The scenario file is documented as an optional argument, so it can be given on
# its own. The parser still rejects what it does not understand, and a run with
# nothing to run is still an error.

run_esmini --help
need_status 255 $? "--help exit status"
need_grep '^  \[--osc\] <filename>' "$WORK/last.out" "usage marks --osc optional"
need_not_grep '^  --osc <filename>$' "$WORK/last.out" "usage no longer requires --osc"

run_esmini --bogus_option "$FIXTURES/two_car_brake.xosc"
need_status 255 $? "unknown option"
need_grep 'Unrecognized arguments' "$WORK/last.out" "unknown option reported"

run_esmini --headless
need_status 255 $? "no scenario given"

# ---------------------------------------------------------------------------
# The `[]` notation the usage block acquires here is not specific to --osc.
# It marks every option whose value may be omitted, and this step introduces
# one of those: --ghost_trail_dt, whose value defaults to 0.2 s when the flag
# is given bare. Both halves are asserted - that the option is registered at
# all, and that the usage block states the default it falls back to - because
# an option that parses but documents no default leaves a caller guessing.
run_esmini --help
need_status 255 $? "--help exit status, second read"
need_grep '^  --ghost_trail_dt \[dt\]' "$WORK/last.out" \
    "usage lists --ghost_trail_dt with an optional value"
need_grep '^  --ghost_trail_dt \[dt\] +\(default if value omitted: 0\.200000\)' "$WORK/last.out" \
    "usage states the --ghost_trail_dt default"

# The shipped command reference is the same usage block written down, and
# requirement 6 pins its content. Only the content requirement 6 publishes is
# asserted -- its total line count is not part of the contract (the reference
# file grows by entries for options this step does not ask anyone to add), and
# the instruction says so. This reads a file the candidate wrote, not the
# candidate's source.
treeroot=$(dirname "$RESOURCES")
cmds=$treeroot/docs/commands.txt
need_file "$cmds" "the shipped command reference"
need_grep '^  \[--osc\] <filename>' "$cmds" "command reference marks --osc optional"
need_grep '^  --ghost_trail_dt \[dt\]' "$cmds" "command reference lists --ghost_trail_dt"

# ---------------------------------------------------------------------------
# The other half of --ghost_trail_dt: the assertions above prove the option is
# registered and documented, which a candidate can satisfy by parsing the flag
# into a variable nothing reads. This proves the value reaches the sampler that
# records the ghost's trail.
#
# The method needs no magnitude. Run one deterministic scenario twice, changing
# nothing but --ghost_trail_dt, and read the trail back through
# SE_GetRoadInfoAlongGhostTrail: a coarsely sampled trail is a different
# polyline from a finely sampled one, so the timestamps it reports at the same
# lookahead distances differ. An implementation that parses the option and drops
# it produces two identical runs, and `differ` comes back 0.
#
# No timestamp is pinned because none is publishable: the value returned for a
# lookahead distance is interpolated along the trail polyline, so it depends on
# the ghost's trajectory and the sampling interval together and is not even
# monotone in the interval. The pair below is measured, not arbitrary - 0.05 s
# against 0.50 s would agree to 0.0032 s, because at both spacings the polyline
# still hugs the ghost's real path; 3.00 s is coarse enough to cut the corners
# of the fixture's two lane changes and lands 0.9474 s away, nineteen times the
# one-simulation-step floor the driver compares against.
run_driver ghost_trail "$FIXTURES/ghost_trail.xosc" 0.05 3.0
need_status 0 $? "ghost trail driver ran to completion"
need_kv init_fine 0 "scenario initialised with the fine ghost_trail_dt"
need_kv init_coarse 0 "scenario initialised with the coarse ghost_trail_dt"
need_kv ghost_fine 1 "FollowGhostController produced a ghost, fine run"
need_kv ghost_coarse 1 "FollowGhostController produced a ghost, coarse run"
# Only the sign of the trail query's return code is graded. It reports where the
# probe ended up, and unlike requirement 7's junction geometry nothing this step
# publishes fixes which non-negative code the ghost's own path yields.
need_nonneg rc_fine "ghost trail query succeeded, fine run"
need_nonneg rc_coarse "ghost trail query succeeded, coarse run"
need_kv differ 1 "the sampling interval changes the recorded ghost trail"
need_kv survived 1 "driver reached its end"

#!/usr/bin/env bash
# The recording path can be chosen by the host application before the scenario
# is initialised, instead of being derived from the scenario's own filename.
# The file has to appear at exactly the name that was asked for, be a real
# recording that the decoder accepts, and the derived default name must not be
# written alongside it.
source /tests/caselib.sh

run_driver se_reports "$FIXTURES/param_cars.xosc" chosen_name.dat 20 0.1 \
    || fail "se_reports exited $?"
need_kv init 0 "the fixture scenario initialises with recording enabled"
need_kv survived 1 "the engine survives a recording run"

need_file "$WORK/chosen_name.dat"
# Without an explicit path the recording is named after the scenario. That name
# appearing here would mean the explicit one was ignored.
[ -e "$WORK/param_cars.dat" ] && fail "the scenario-derived default recording name was written too"

run_tool "$DAT2CSV" "$WORK/chosen_name.dat"
need_status 0 $? "dat2csv on the recording written to the chosen path"
need_file "$WORK/chosen_name.csv"
# The header's exact spelling, asserted verbatim. It is an INHERITED format
# obligation -- the `dat2csv` output lines must not regress, per instruction.md's
# out-of-scope note -- and the header is its own line, not the CSV's first line:
# dat2csv writes `Version: ...` above it, so the csv_column helper in caselib.sh
# (which reads NR==1 only) cannot see these names at all. The verbatim grep is
# therefore the only form that works here. WORKSPACE_HANDOFF.md's "What carries
# forward" now states this header, so the spelling is agent-visible.
#
# The `Version:` line itself is asserted too, not just skipped over: it names
# the format version the recording is in (`2`) and the OpenDRIVE file the run
# used, and both are stated in WORKSPACE_HANDOFF.md's "What carries forward".
# The 3DModel field is empty in this profile (no scene graph is loaded), so the
# match stops at its colon-space rather than pinning a trailing value.
need_grep '^Version: 2, OpenDRIVE: straight_500m\.xodr, 3DModel: ' \
    "$WORK/chosen_name.csv" "the decoded recording names its format version and road network"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' \
    "$WORK/chosen_name.csv" "the recording decodes to the documented column header"
need_grep '^0.000, 0, Ego, ' "$WORK/chosen_name.csv" "the recording holds the scenario's first sample"

#!/usr/bin/env bash
# Gate (proof of life). The player's log timestamp format and the --save_xosc / --record
# interaction are asserted here because this step replaces the logging backend
# (spdlog -> fmt), which is exactly what could disturb them. Every property
# asserted was measured identical at v2.46.4 and at v2.51.1, so a solution that
# leaves these surfaces alone passes without doing anything.
#
# Only structural properties are asserted - the SHAPE of the timestamp bracket
# and which level tags appear - never a diagnostic message's text. Diagnostic
# wording is free to change; the envelope is not.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$WORK/two_car_brake.xosc"
cp "$FIXTURES/straight_500m.xodr" "$WORK/straight_500m.xodr"

# --- log timestamp format -------------------------------------------------
# Every diagnostic line carries a bracketed time and a bracketed level. Before
# the simulation clock advances the time bracket is EMPTY ([] [info] ...); once
# the clock runs, it is seconds to three decimal places ([0.050] [info] ...).
# Both shapes must appear in one ordinary run, because a backend swap that
# dropped timestamps entirely would satisfy the empty-bracket form alone. Both
# grep patterns are anchored at line start so a message body that happens to
# contain a bracketed timestamp cannot satisfy them.
run_esmini --osc two_car_brake.xosc --headless --fixed_timestep 0.05
need_status 0 $? "esmini on the two-car scenario"
need_grep '^\[\] \[(info|warn|error|debug)\]' "$WORK/last.out" \
    "pre-simulation lines have an empty time bracket"
need_grep '^\[[0-9]+\.[0-9]{3}\] \[(info|warn|error|debug)\]' "$WORK/last.out" \
    "in-simulation lines carry a three-decimal-place time bracket"

# The level tag is a prefix, not the whole line: a message body follows it in
# both time-bracket shapes. A backend swap that emitted the envelope and
# dropped the payload would satisfy the two patterns above. Only the PRESENCE
# of a body is asserted, never its wording. The empty-time pattern additionally
# excludes a leading '[' so that it cannot be satisfied by the metadata field
# checked below instead of by a real message.
need_grep '^\[[0-9]+\.[0-9]{3}\] \[(info|warn|error|debug)\] [^ ]' "$WORK/last.out" \
    "a message body follows the level tag on in-simulation lines"
need_grep '^\[\] \[(info|warn|error|debug)\] [^ []' "$WORK/last.out" \
    "a message body follows the level tag on pre-simulation lines"

# By default the envelope carries no source-location field. This is asserted so
# that the --log_meta_data check below observes an option doing something,
# rather than a field that was always present.
need_not_grep '^\[[0-9.]*\] \[(info|warn|error|debug)\] \[[^]]+::[^]]+::[0-9]+\]' "$WORK/last.out" \
    "the default envelope carries no file::function::line field"

# --- log envelope with --log_meta_data ------------------------------------
# --log_meta_data inserts a third bracket group - file::function::line -
# between the level tag and the message. This is the other envelope variant the
# logging backend has to keep producing across the spdlog -> fmt swap, and it
# is the variant most likely to break, because it is the one that formats
# __FILE__ / __FUNCTION__ / __LINE__ rather than just copying a string.
#
# Both time-bracket shapes are required again, for the same reason as above.
# Measured at both bounding tags: 10 empty-time lines and 47 (v2.46.4) / 48
# (v2.51.1) running-clock lines carry the field, so neither assertion is
# counting anything version-specific. The file name, the function name and the
# line number are matched as shapes, never as values - line numbers move
# between the two tags (StoryboardElement.cpp::SetState::335 -> ::334) and that
# is not this step's business.
run_esmini --osc two_car_brake.xosc --headless --fixed_timestep 0.05 --log_meta_data
need_status 0 $? "esmini with --log_meta_data"
need_grep '^\[\] \[(info|warn|error|debug)\] \[[^]]+::[^]]+::[0-9]+\] .' "$WORK/last.out" \
    "pre-simulation lines carry file::function::line and a message"
need_grep '^\[[0-9]+\.[0-9]{3}\] \[(info|warn|error|debug)\] \[[^]]+::[^]]+::[0-9]+\] .' "$WORK/last.out" \
    "in-simulation lines carry file::function::line and a message"

# --- --save_xosc quit with --record ---------------------------------------
# Both flags together: the run stops at the first frame, the resolved scenario
# is written, and no recording is produced because nothing was simulated.
# This is the documented interaction between the two options, and it is
# deliberately checked in the same invocation - two separate runs would not
# catch a --record that writes a partial file before --save_xosc fires.
run_esmini --osc two_car_brake.xosc --headless --fixed_timestep 0.05 \
    --save_xosc quit --record q.dat
need_status 0 $? "esmini with --save_xosc quit and --record"

# The resolved scenario lands beside the input, named for the base name and the
# parameter-distribution index and count - NOT the entity count. Both are 0 for a
# scenario with no distribution file, which is why a two-entity scenario still
# gives _0_of_0. It must be a real file, not an empty one.
need_file "$WORK/two_car_brake_0_of_0.xosc" "--save_xosc wrote the resolved scenario"
need_grep '<OpenSCENARIO' "$WORK/two_car_brake_0_of_0.xosc" "the saved scenario is an OpenSCENARIO document"

# No recording: the simulation never stepped, so there is no data to write.
[ -e "$WORK/q.dat" ] && fail "--record wrote q.dat even though --save_xosc quit stopped the run at the first frame"
exit 0

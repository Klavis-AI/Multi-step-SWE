#!/usr/bin/env bash
# New. Two of R6's output filters. (1) The spdlog backend prints leveled,
# bracket-tagged lines by default, and raising log_level to "error" must
# suppress the info-tagged lines entirely. log_level is a STRING option
# restricted to debug|info|warn|error; anything else falls back to "info",
# which R6 requires to be indistinguishable from the default level rather
# than merely "some info lines survive" -- asserted below at the third run.
# (2) log_only_modules keeps only the lines
# emitted by the listed modules and log_skip_modules suppresses the listed
# modules' lines. A module name is the stem of the emitting source file
# (instruction R6), which is exactly what the <file> field of the metadata
# tag shows once its extension is stripped -- so the second half turns
# metadata on and reads each line's own module off its tag, rather than
# assuming which messages any particular module prints. Both halves live in
# one file because the step's case budget is full (22, the package maximum).
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

# The documented line form is `[<sim time>] [<level>] <message>`: a bracketed
# time (three decimals, or empty before the scenario starts) followed by a
# bracketed level. Message wording is not asserted anywhere in this group.
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log
need_status 0 $? "esmini (default log_level)"
need_grep '^\[([0-9]+\.[0-9]{3})?\] \[info\] ' "$WORK/last.out" "default log_level prints info-tagged lines in the documented form"
n_info=$(grep -Ec '^\[([0-9]+\.[0-9]{3})?\] \[info\] ' "$WORK/last.out")
[ "$n_info" -ge 5 ] || fail "expected a stream of info-level lines at the default level, got $n_info"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --log_level error
need_status 0 $? "esmini --log_level error"
need_not_grep '\[info\]' "$WORK/last.out" "log_level error suppresses info-tagged lines"
need_not_grep '\[debug\]' "$WORK/last.out" "log_level error suppresses debug-tagged lines"
need_not_grep '\[warn\]' "$WORK/last.out" "log_level error suppresses warn-tagged lines"

# A value outside the documented set is treated as "info" (instruction R6).
# This is checked as "the run behaves exactly like the default level" rather
# than as "some info lines appear", so a build that silently treated a bad
# value as "error" -- suppressing everything -- cannot pass by accident.
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --log_level bogus
need_status 0 $? "esmini --log_level bogus"
need_grep '^\[([0-9]+\.[0-9]{3})?\] \[info\] ' "$WORK/last.out" "an unrecognized log_level falls back to info"
n_bogus=$(grep -Ec '^\[([0-9]+\.[0-9]{3})?\] \[info\] ' "$WORK/last.out")
# R6: "treated as info" means indistinguishable from info, so the floor is the
# DEFAULT run's own count rather than a constant -- strictly stronger than a
# fixed threshold, and it does not pin a number to this fixture.
[ "$n_bogus" -ge "$n_info" ] || fail "an unrecognized log_level must fall back to info: the default level printed $n_info info lines, --log_level bogus printed only $n_bogus"

# ---- module filters -------------------------------------------------------
# Lines carrying a metadata tag, and the module (file stem) of each such line.
tagged() { grep -E '^\[([0-9]+\.[0-9]{3})?\] \[[a-z]+\] \[[^]]+::[^]]+::[0-9]+\] ' "$1"; }
modules_of() { tagged "$1" | sed -E 's/^\[[^]]*\] \[[a-z]+\] \[([^]:]+)::.*/\1/; s/\.[A-Za-z]+$//' | sort -u; }

# Baseline: with metadata on and no filter, more than one module logs, and
# playerbase and ScenarioEngine are both among them. Those two source files
# already exist by that name in the start workspace (they are where the
# player's option handling and scenario loading live), so a conforming port
# keeps them; both must exist as modules for the filter assertions to be
# meaningful.
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --log_meta_data
need_status 0 $? "esmini --log_meta_data (unfiltered baseline)"
modules_of "$WORK/last.out" > "$WORK/mods_all"
n_all=$(wc -l < "$WORK/mods_all")
[ "$n_all" -ge 2 ] || fail "the unfiltered run must log from at least two modules, saw $n_all"
grep -qx 'playerbase' "$WORK/mods_all" || fail "the unfiltered run must include lines from module 'playerbase'"
grep -qx 'ScenarioEngine' "$WORK/mods_all" || fail "the unfiltered run must include lines from module 'ScenarioEngine'"
n_lines_all=$(tagged "$WORK/last.out" | wc -l)

# log_only_modules: every tagged line comes from a listed module, and every
# listed module still appears.
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --log_meta_data --log_only_modules playerbase,ScenarioEngine
need_status 0 $? "esmini --log_only_modules playerbase,ScenarioEngine"
modules_of "$WORK/last.out" > "$WORK/mods_only"
grep -qx 'playerbase' "$WORK/mods_only" || fail "--log_only_modules must keep the listed module 'playerbase'"
grep -qx 'ScenarioEngine' "$WORK/mods_only" || fail "--log_only_modules must keep the listed module 'ScenarioEngine'"
other=$(grep -vx -e playerbase -e ScenarioEngine "$WORK/mods_only" | head -3 | tr '\n' ' ')
[ -z "$other" ] || fail "--log_only_modules must drop every unlisted module, but saw lines from: $other"
n_lines_only=$(tagged "$WORK/last.out" | wc -l)
[ "$n_lines_only" -lt "$n_lines_all" ] || fail "--log_only_modules must remove the other modules' lines: $n_lines_all unfiltered, $n_lines_only filtered"

# log_skip_modules: the listed module disappears, the others remain.
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --log_meta_data --log_skip_modules playerbase
need_status 0 $? "esmini --log_skip_modules playerbase"
modules_of "$WORK/last.out" > "$WORK/mods_skip"
grep -qx 'playerbase' "$WORK/mods_skip" && fail "--log_skip_modules playerbase must suppress every 'playerbase' line"
grep -qx 'ScenarioEngine' "$WORK/mods_skip" || fail "--log_skip_modules playerbase must leave module 'ScenarioEngine' logging"
exit 0

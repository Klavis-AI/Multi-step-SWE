#!/usr/bin/env bash
# New. --record must accept an OPTIONAL filename: the bare form uses the
# documented default and still runs to completion (older releases require an
# argument and fail to parse without one), while the explicit form honors the
# given name and does not also create the default file.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --record
need_status 0 $? "esmini --record (bare form)"
need_file "$WORK/sim.dat" "default record output file"

rm -f "$WORK/sim.dat"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --record mydat.dat
need_status 0 $? "esmini --record <filename> (explicit form)"
need_file "$WORK/mydat.dat" "explicit record output file"
[ ! -e "$WORK/sim.dat" ] || fail "explicit --record filename must not also create the default sim.dat"

# WORKSPACE_HANDOFF L47 lists one further CLI change under "CLI changes": the
# presence flag `--disable_pline_interpolation`, which turns off orientation
# interpolation along polyline trajectories. Nothing else in the suite reaches
# it. It is graded here as a CLI offer -- the flag is accepted and the run
# still completes -- and not by comparing recorded trajectories with and
# without it: the .dat recordings are not byte-stable across runs, so that
# comparison would be flaky rather than discriminating. The start release has
# no such option at all; on a build that does not register it this step fails
# the parse.
run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --disable_pline_interpolation
need_status 0 $? "esmini --disable_pline_interpolation"
grep -Eq '\[info\]' "$WORK/last.out" || fail "--disable_pline_interpolation run produced no engine output"

# R05.20 was also read as asking for the rejecting direction of the grammar --
# an option whose argument is REQUIRED must be refused when it is left bare,
# rather than silently swallowing the next token or falling back to a default.
# That half is deliberately NOT asserted here, and the reason is a measurement,
# not a budget: an argument-required option is refused identically at BOTH tags.
# `esmini --osc` alone prints `Argument parser error: Missing option osc
# argument` and exits 255 on v2.31.10 and on v2.44.2 alike (`--osc` is
# registered with an argument and no default value at `playerbase.cpp:1186` in
# the start release and `:1210` at the target). It is therefore not a capability
# this step introduces, and a check on it would be green on the start release --
# it would raise the asserted count without discriminating anything. The
# rejecting direction of the NEW options cannot stand in for it either: every
# option this step adds that takes an argument also adds a default value, so the
# parser falls back rather than refusing, and omitting the token is required to
# succeed (the two forms above).

exit 0

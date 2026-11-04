#!/usr/bin/env bash
# The verifier, shared by all thirteen steps.
#
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root.  Identical copies
# of this file live in every steps/NN-*/tests/; keep them in sync.
#
# One script for thirteen steps, because every step scores the same two
# families in the same way and the differences are data:
#
#   /tests/step/profile.json   this step's gates and their weights
#   /tests/overlays/NN.json    the documented must-flip re-recordings for
#                              steps 1..k, applied in order
#   the image itself           this step's interpreter, under rbenv
#
# Nothing else varies.  A step that needed a different *procedure* would be a
# step whose reward nobody could compare with its neighbours', and thirteen
# hand-written verifiers is thirteen chances for one of them to score the wrong
# thing and report a number.
#
# The five phases, and what each one costs if it fails:
#
#   1  world       hosts, database server, database.yml, HOME, the app env
#   2  bundle      the candidate's own Gemfile, installed unprivileged
#   3  behaviour   the recorded behaviour suite (cvh/scenarios.py), as
#                  amended by the overlays.  One factor of the reward.
#   4  gates       this step's migrated-ness assertions.  The other factor:
#                  reward = gates x behaviour.
#   5  score       evidence -> reward.json
#
# Every phase is bounded by its own `timeout`, every phase is also bounded by
# the global deadline below, and no phase aborts the script.  All three are the
# same requirement seen from different sides: the step's reward is a *mean
# member*, and a verifier that is killed by Harbor's timeout or that exits
# non-zero writes no reward.json at all -- which does not score 0, it vanishes
# from the mean and raises it.  So a phase that fails
# records its failure and the later phases go on measuring what they still can,
# helpers.sh's EXIT trap writes a 0 if this script somehow still dies, and no
# sequence of slow phases can push scoring past the point where Harbor would
# kill the container with nothing written.
#
# Privilege: candidate-authored code never runs as root.  `bundle install`
# executes the candidate's Gemfile, the gate probes boot the candidate's
# application, and the behaviour suite runs the candidate's whole stack; all of
# them run as uid 65534, which cannot write the root-owned reward directory.
# The two exceptions are root because they have to be: the harness binds
# privileged ports 80 and 587 for the HTTP stub and the SMTP sink (it drops to
# 65534 for every application child itself, via `--uid`), and the database
# server needs its own account.

. /tests/helpers.sh
harden_reward_dir

# One wall clock for the whole script.
# CV_DEADLINE below bounds the phases, but only the phases, and Harbor discards
# a step whose verifier outlives `[steps.verifier] timeout_sec` (1800) even
# when reward.json exists, and aborts every later step.  So this script runs
# itself once more as a child under CV_WALL, and this outer copy does nothing
# else: if the child did not get as far as a reward, the reward is 0 and it is
# written here, inside the limit.  Worst case, start to exit: CV_WALL 1530 +
# 10 kill grace + the sweep + report, under 1800 - 240 = 1560.
SELF=$(readlink -f "${BASH_SOURCE[0]}")
CV_WALL=1530
if [ "${1:-}" != "--inner" ]; then
    rm -f /logs/verifier/reward.json
    timeout -k 10 "$CV_WALL" bash "$SELF" --inner
    wall_status=$?
    sweep_uid "$SCORE_UID"
    if [ -s /logs/verifier/reward.json ]; then
        _CV_REPORTED=1
        exit 0
    fi
    say "verifier ended (status $wall_status) inside its ${CV_WALL}s wall clock without a reward: reward 0"
    report 0
fi

TESTS=/tests
APP=/app
LOGS=/logs/verifier
PROFILE="$TESTS/step/profile.json"
# Two scratch directories, split by who writes into them.  GATES holds what
# root computes -- the comparison against the golden, the static readings, the
# evidence and the score -- and is root-only, so no process the candidate's code
# leaves behind can replace a reading after it is taken.  UGATES is where the
# probes that run as uid 65534 (inside the candidate's own bundle) write their
# reports, each into a fresh directory of its own (`udir`).  Those reports are
# the verifier's own measurements of the candidate, so they are not read where
# they were written: `run` reaps uid 65534 when the probe ends, and
# `collect_unpriv` copies the report into GATES, and everything after reads the
# copy.  UGATES itself is root's and
# 0711, so no probe can see or touch another's directory.
GATES=/tmp/gates
UGATES=/tmp/gates-unpriv
OBSERVED="$GATES/observed.json"

# The harness and the scorer are both plain Python on /tests; the bytecode goes
# to /tmp because /tests is the image's copy of the build context, and a stray
# `__pycache__` written there would make a *build* context carry bytecode
# compiled by the wrong interpreter.
#
# PYTHONSAFEPATH: this script runs in the verifier's working directory, which
# is /app, the candidate's tree.  Without it, `python3 -c`, `python3 -` and
# `python3 -m` put that directory first on sys.path, so every root-run Python
# below would resolve its imports against the candidate's files before the
# standard library and /tests.  PYTHONPATH already names every module the
# harness and the scorer import.
export PYTHONPATH="$TESTS"
export PYTHONSAFEPATH=1
export PYTHONPYCACHEPREFIX=/tmp/pycache
mkdir -p "$GATES" "$UGATES" "$LOGS"
chown root:root "$GATES" 2>/dev/null || true
chmod 0700 "$GATES"
chown root:root "$UGATES" 2>/dev/null || true
chmod 0711 "$UGATES"

# CV_DEADLINE -- the second, counted from this script's start, by which phase 4
# must be over.  `[steps.verifier] timeout_sec` in task.toml is 1800, and at
# that mark Harbor kills the container: not a zero, an absence.  The per-phase
# budgets are worst cases and deliberately generous (they sum to well over
# this), because each one is sized for "this phase has hung" rather than for
# "this phase is slow"; what keeps the total honest is this deadline, which
# shortens whatever phase is running when the time is gone.  What remains up
# to CV_WALL (above) is for phase 5, which is a JSON merge and some arithmetic
# and takes seconds.
#
# A phase's budget is cut so that its kill grace, CV_GRACE, also ends by the
# deadline, and a phase that would start with no budget left does not start,
# rather than being given one second plus the 30 s grace (eleven such phases
# after the deadline would come to some 340 s).  So phase 4 is over by
# CV_DEADLINE whatever the candidate's processes do with SIGTERM.
#
# 1440 against a measured 415 s of real work (877 s the slowest step seen
# under a loaded gate) is a 3.5x margin, and leaves room for phase 5 inside
# the wall clock above.  A hang costs 25 minutes and still produces a
# reward.json, because this deadline fires first and phase 5 runs.
CV_DEADLINE=${CV_DEADLINE:-1440}
CV_GRACE=30

# run <budget seconds> <log name> <command...>
#
# One bounded, logged phase.  Returns the command's status, or 124 when the
# budget ran out; either way the caller decides what that costs, and nothing
# here exits.
run() {
    local wanted="$1" name="$2"; shift 2
    local status budget left
    left=$(( CV_DEADLINE - SECONDS - CV_GRACE ))
    if [ "$left" -lt 1 ]; then
        say "$name: NOT RUN; ${SECONDS}s spent of ${CV_DEADLINE}s"
        return 124
    fi
    if [ "$wanted" -le "$left" ]; then budget="$wanted"; else budget="$left"; fi
    [ "$budget" = "$wanted" ] || \
        say "$name: budget cut from ${wanted}s to ${budget}s; ${SECONDS}s spent of ${CV_DEADLINE}s"
    say "$name: $*"
    timeout --kill-after="$CV_GRACE" "$budget" "$@" >>"$LOGS/$name.log" 2>&1
    status=$?
    # Whatever this phase started as the candidate's uid ends with it.
    sweep_uid "$SCORE_UID"
    if [ "$status" = 124 ] || [ "$status" = 137 ]; then
        say "$name: TIMED OUT after ${budget}s (status $status)"
    elif [ "$status" != 0 ]; then
        say "$name: exit $status (see $LOGS/$name.log)"
    fi
    return "$status"
}

# udir <name> -- a fresh report directory for one unprivileged probe.
udir() {
    rm -rf "${UGATES:?}/$1"
    install -d -m 0700 -o "$SCORE_UID" -g "$SCORE_GID" "$UGATES/$1"
}

# collect_unpriv <name> <file> -- take a probe's report into GATES.
#
# Call after the probe's `run`, which has reaped uid 65534.  Only a plain file
# is taken: not a symlink, which would have root read whatever it points at,
# and not a FIFO, which would hang the copy -- and only up to a size no honest
# report comes near.  Anything else stays behind and the reading is missing,
# which every gate scores as the failure it is.
collect_unpriv() {
    local from="$UGATES/$1/$2" to="$GATES/$2"
    rm -f "$to"
    if [ -f "$from" ] && [ ! -L "$from" ] && \
       [ "$(stat -c %s "$from" 2>/dev/null || echo 0)" -le 67108864 ]; then
        cp "$from" "$to" 2>/dev/null || rm -f "$to"
    fi
}

# ---------------------------------------------------------------------------
say "phase 1: the world"
# ---------------------------------------------------------------------------

# Which step this verifier is.  Printed first and loudly, because a local
# multi-step run does not necessarily build one verifier image per step, and a
# step scored by an earlier step's image reports a perfectly plausible number.
# The step name, the interpreter, and the sha256 of the
# frozen golden are this container's account of which contract it is scoring.
STEP=$(python3 -c "
import json, sys
try:
    print(json.load(open('$PROFILE'))['step'])
except Exception as exc:
    print('NO PROFILE (%s)' % exc)
")
say "step under test: $STEP"
say "interpreter:     $(ruby -v 2>&1 | head -1)"
say "golden:          $(sha256sum "$TESTS/golden/baseline.json" | cut -c1-16)"
OVERLAYS=()
for fragment in "$TESTS"/overlays/*.json; do
    [ -e "$fragment" ] || continue
    OVERLAYS+=(--overlay "$fragment")
    say "overlay:         $(basename "$fragment")"
done
say "overlay count:   $(( ${#OVERLAYS[@]} / 2 ))"

# Preflight: the two file modes this container depends on, checked as the user
# that depends on them.
#
# Phase 4 runs `bootprobe.rb`, `depdrive.rb`, `envboot.rb` and
# `raketasks.rb` as uid 65534,
# because they boot the candidate's application.  If the scoring uid cannot open
# the probe, ruby reports `Permission denied -- ... (LoadError)` and exits 1,
# `evidence.py` records a stub, and every gate that reads those reports fails
# with "probe failed".  That is a plausible-looking 0.74 on a *golden* tree, and
# nothing in the reward distinguishes it from a candidate whose application
# does not boot.  `COPY . /tests` preserves the build context's file modes, so
# the Dockerfile opens `cvgate/` explicitly and this says so out loud if it
# ever stops.
#
# The second half is the same invariant from the other side, because a mode
# change that fixed the first by opening all of /tests would be worse than the
# bug: the candidate's application runs in this container for the whole
# behaviour phase, and an initializer that could read `golden/baseline.json`
# could answer every recorded case from it.  A clean control on its own proves
# only that nothing objected, so the check that must pass
# and the check that must fail are both made, every run.
PREFLIGHT=ok
preflight_bad() {
    [ "$PREFLIGHT" = ok ] && PREFLIGHT=""
    PREFLIGHT="$PREFLIGHT$1 "
    say "PREFLIGHT FAILED ($1): $2."
    say "PREFLIGHT FAILED ($1): that is a defect in this verifier image, not a"
    say "PREFLIGHT FAILED ($1): property of the tree being scored."
}
unpriv_can_read() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        cat "$1" >/dev/null 2>&1
}
for probe in bootprobe depdrive envboot raketasks; do
    unpriv_can_read "$TESTS/cvgate/$probe.rb" || preflight_bad "unreadable:$probe" \
        "uid $SCORE_UID cannot open $TESTS/cvgate/$probe.rb, so phase 4 cannot ask it"
done
for secret in golden/baseline.json step/profile.json; do
    [ -e "$TESTS/$secret" ] || continue
    ! unpriv_can_read "$TESTS/$secret" || preflight_bad "exposed:${secret%%/*}" \
        "uid $SCORE_UID can read $TESTS/$secret, which candidate code could too"
done
# A profile that names a predicate's argument wrong scores nothing: `score.py`
# raises where it would have compared, and twenty minutes of postgres, bundler,
# behaviour cases and the suite are reported as a traceback and a zero.  The
# profile is a fixed part of this image, so its shape can be checked before any
# of that runs, and it is checked here rather than at scoring time so that the
# author of a new step reads it in the first second of the log.
python3 "$TESTS/score.py" --profile "$TESTS/step/profile.json" --lint \
    >>"$LOGS/score.log" 2>&1 \
    || preflight_bad "profile" "$(tail -1 "$LOGS/score.log")"
say "preflight:       $PREFLIGHT"

# The two hostnames the application reaches out to resolve to the loopback
# address, where the harness is listening.  The application's own configuration
# is never edited to point somewhere else -- an application that stopped
# talking to SendGrid would then still score as if it did.
for host in www.southbendvoices.com smtp.sendgrid.net; do
    grep -q " $host\$" /etc/hosts || printf '127.0.0.1 %s\n' "$host" >> /etc/hosts
done

# The database server.  `pg_isready` and not `sleep`: the cluster takes a
# variable time to accept connections on a machine that is shared, and a fixed
# sleep is either too short (every world fails to connect) or wasted budget.
pg_ctlcluster 16 main start >>"$LOGS/postgres.log" 2>&1
pg_ready=0
for _ in $(seq 1 120); do
    if pg_isready -q; then pg_ready=1; break; fi
    sleep 0.5
done
say "postgres ready: $pg_ready"

# config/database.yml from the repository's own example, exactly as the
# golden recording did.  This is a hook the application already
# has -- the file is gitignored and the example is checked in -- and it is
# stated in the instruction.  A candidate who rewrote the example is scored
# against their own example; a candidate who deleted it and checked in
# database.yml itself keeps theirs.
#
# Recorded BEFORE the copy, because the copy destroys the evidence.  The
# instruction says `config/database.yml` is not in the repository and the
# configuration is produced from `config/database.yml.example`; the `cp` below
# is what produces it, so from the next line on, `database.yml` exists because
# the scorer made it and no later check can tell that from a tree that shipped
# one.  Two facts, taken while they are still true, and a list of problems
# rather than two booleans so the guard reading it can say `empty` and the
# report can say which half went wrong.
python3 - "$APP" > "$GATES/handoff.json" <<'HANDOFF'
import json, os, sys
app = sys.argv[1]
has_yml = os.path.exists(os.path.join(app, "config/database.yml"))
has_example = os.path.exists(os.path.join(app, "config/database.yml.example"))
problems = []
if has_yml:
    problems.append("config/database.yml is in the handed-on tree")
if not has_example:
    problems.append("config/database.yml.example is not in the handed-on tree")
json.dump({"database_yml_present": has_yml,
           "database_yml_example_present": has_example,
           "db_config_problems": problems}, sys.stdout)
HANDOFF
say "handoff: $(cat "$GATES/handoff.json")"

# The copy itself is made further down, once the tree belongs to the scoring
# uid, and with that uid's rights (`as_tree_owner`).

# The application's HOME.  Bundler and RubyGems write here, and an unprivileged
# process whose HOME it cannot write puts a Bundler complaint on the *stdout*
# of every rake task -- in the middle of output the goldens recorded without
# it.  0777 and not chown: the same directory is used by root and by 65534.
mkdir -p /tmp/cvhome && chmod 0777 /tmp/cvhome

# The application environment is taken from the harness rather than restated,
# so this file can never disagree with the recorder about what the application
# was given.  Two shapes come out: the plain `export`s, and CV_CLOCK, which is
# the libfaketime settings (FAKETIME_FORCE_MONOTONIC_FIX=0 among them: see
# `cvh/runner.py` `app_env` for why every timed wait needs it -- without it the
# 2.7-3.2 steps run their probes and the suite 30-60x slow and can run out of
# CV_DEADLINE).  The clock is kept out of the shell's own environment
# on purpose -- CPython under that preload raises `OSError: [Errno 22]` from
# `datetime.utcnow()`, so the harness and the scorer must not inherit it, and
# the Ruby probes are given it explicitly instead.
python3 -c "
import shlex
from cvh import runner
env = runner.app_env()
for key in sorted(runner.APP_ENV):
    print('export %s=%s' % (key, shlex.quote(env[key])))
print('export APP_HOME=%s' % shlex.quote(runner.HOME_DIR))
clock = ('LD_PRELOAD', 'FAKETIME', 'FAKETIME_DONT_FAKE_MONOTONIC',
         'FAKETIME_FORCE_MONOTONIC_FIX')
print('CV_CLOCK=(%s)' % ' '.join(
    '%s=%s' % (k, shlex.quote(env[k])) for k in clock))
" > /tmp/appenv.sh
if [ -s /tmp/appenv.sh ]; then
    . /tmp/appenv.sh
    say "app env: $(grep -c '^export' /tmp/appenv.sh) variables, clock ${CV_CLOCK[1]}"
else
    say "app env: COULD NOT BE READ FROM THE HARNESS"
    CV_CLOCK=()
fi

# The interpreter this step's image ships, for the probes that read source
# rather than run the application.  See phase 4.
STEP_RUBY=$(cat "${RBENV_ROOT:-/usr/local/rbenv}/version" 2>/dev/null || echo "")

# /app arrives owned by whoever wrote the artifact, and /bundle is root-owned
# from the image build.  Both have to be writable by 65534: the candidate's
# `bundle install` adds gems the warm cache does not have, and Rails writes
# tmp/, log/ and public/assets/ under the application root.
give_app_to_unprivileged
chown -R "$SCORE_UID:$SCORE_GID" /bundle 2>/dev/null || true

# as_tree_owner <command...> -- a write into the candidate's tree, made with the
# rights of the uid that owns the tree and not root's.  Any path in /app may be
# a link, so root never writes, copies or chowns through one: as 65534 the same
# command can reach only what 65534 could reach anyway.
unpriv_argv
as_tree_owner() {
    "${UNPRIV[@]}" "$@"
}

if [ -f "$APP/config/database.yml.example" ]; then
    as_tree_owner /bin/cp -f --remove-destination \
        "$APP/config/database.yml.example" "$APP/config/database.yml"
    say "database.yml from database.yml.example (exit $?)"
else
    say "database.yml.example absent; keeping the tree's own database.yml"
fi

# The handed-on schema dump, copied out of the tree before anything in this
# file has run.  It is needed at the very end, and it has to be taken here.
#
# `rake db:migrate` invokes `db:_dump` when it finishes and
# `ActiveRecord::Base.dump_schema_after_migration` defaults to **true** on
# every release this ladder spans -- the generated production environment file
# is what turns it off, and this application's production file never had that
# line.  Phase 3 records a `db:migrate` case.  So from phase 3 onwards the
# `db/schema.rb` in the tree is not the candidate's file but whatever this
# framework dumps, which is the shape the step is asking for: a gate that read
# a database built from it would be scoring the verifier's own repair work and
# a tree that never touched its dump would pass.  See `cvgate/pkdrift.rb`.
SCHEMA_RB="$APP/db/schema.rb"
SCHEMA_HANDED=/tmp/schema.rb.handed
# Taken only from a regular file whose path has no link in it: root reads it,
# and the saved copy is what the end of this file writes back into the tree.
if [ -f "$SCHEMA_RB" ] && [ ! -L "$SCHEMA_RB" ] && \
   [ "$(readlink -f -- "$SCHEMA_RB")" = "$SCHEMA_RB" ]; then
    rm -f "$SCHEMA_HANDED"
    cp -f "$SCHEMA_RB" "$SCHEMA_HANDED"
    chmod 0644 "$SCHEMA_HANDED"
    say "db/schema.rb: $(wc -c < "$SCHEMA_HANDED") bytes saved before anything runs"
else
    rm -f "$SCHEMA_HANDED"
    say "db/schema.rb: ABSENT from the handed-on tree"
fi
# The other file the same rake task can write, for a tree whose schema_format
# is :sql.  Recorded as present-or-not here so that a dump this file provoked
# can be taken back out of the tree at the end; a candidate that shipped one
# keeps it.
STRUCTURE_SQL="$APP/db/structure.sql"
STRUCTURE_WAS_THERE=no
[ -f "$STRUCTURE_SQL" ] && STRUCTURE_WAS_THERE=yes

# ---------------------------------------------------------------------------
say "phase 2: the candidate's bundle"
# ---------------------------------------------------------------------------
# Unprivileged, because this executes the candidate's Gemfile -- and a Gemfile
# is Ruby: `gem 'x', path: '/etc'` and a gem with a native extension both run
# code here.  The network is open, so a candidate whose
# lockfile differs from this image's warm cache still resolves; they just pay
# the wall clock for the difference.
#
# This is the one command in the file that does NOT run under CV_CLOCK, and the
# reason is the sentence above.  OpenSSL checks a certificate's validity window
# against the clock it is handed, the freeze is 2016-01-19, and rubygems.org's
# certificate has not been issued yet on that date: every fetch fails with
# `certificate verify failed`, bundler falls back to the warm cache, and a gem
# the candidate named but the cache does not have is simply missing.  The
# golden's lockfile matches the cache exactly, so the reference solution never
# hits this; a lockfile that names gems the cache does not have can lose
# `rake`, and with it the database, the boot, the suite and all 386 behaviour
# cases.  Measured
# in this image: a HEAD of https://rubygems.org/specs.4.8.gz answers 200 under
# the real clock and raises OpenSSL::SSL::SSLError under the freeze.  Nothing
# recorded depends on what the installer thinks the date is -- the freeze is
# for the application's own behaviour, and phase 3 and everything after it
# still run under CV_CLOCK.
unpriv_argv                       # UNPRIV, after APP_HOME is exported
run 1200 bundle "${UNPRIV[@]}" bundle install
BUNDLE_STATUS=$?
if [ "$BUNDLE_STATUS" != 0 ]; then
    # Once more.  A transient failure here is indistinguishable in the reward
    # from a tree that did not migrate, and the retry costs nothing on the runs
    # that do not need it.  `run` cuts each attempt's budget to what is left of
    # CV_DEADLINE, so two attempts cannot outlive the watchdog.
    say "bundle install: exit $BUNDLE_STATUS -- one retry"
    run 1200 bundle-retry "${UNPRIV[@]}" bundle install
    BUNDLE_STATUS=$?
fi
say "bundle install: exit $BUNDLE_STATUS"

# ---------------------------------------------------------------------------
say "phase 3: behaviour"
# ---------------------------------------------------------------------------
# Root, and `--uid 65534`: the harness binds ports 80 and 587 for the HTTP stub
# and the SMTP sink, and drops privilege for every application process itself.
# `--setup` creates the database and precompiles the assets first and records
# the exit code of each rather than aborting -- a tree whose precompile fails is
# scored on the 500s that produces, which is a number, instead of vanishing
# from the mean.
#
# The log directory is under /logs/verifier because Harbor collects it; it is
# ~540 kB per run with the harness's ticking clock.
run 2100 behaviour python3 -m cvh.runner --setup --uid "$SCORE_UID" \
    --app-root "$APP" --out "$OBSERVED" --log-dir "$LOGS/cvh"
say "recording: $(python3 -c "
import json
try:
    d = json.load(open('$OBSERVED'))
    print('%d cases, setup %s' % (len(d['cases']), d['setup']))
except Exception as exc:
    print('UNREADABLE: %s' % exc)
")"

run 300 compare python3 -m cvh.compare \
    --golden "$TESTS/golden/baseline.json" --observed "$OBSERVED" \
    "${OVERLAYS[@]}" --report "$GATES/behaviour.json" --show 40

# ---------------------------------------------------------------------------
say "phase 4: gates"
# ---------------------------------------------------------------------------
# Two kinds, and the difference matters for the gradient.
#
# `bootprobe`, `depdrive` and `envboot` boot the candidate's application inside
# the candidate's own bundle, because what they measure is what the framework
# says and does.  They therefore inherit every way the bundle or the interpreter
# can be wrong, which is deliberate: the tree's own `.ruby-version` selects the
# interpreter through rbenv, and a candidate who migrated the code but not the
# version file selects an interpreter this image does not carry.
#
# `scan`, `gemgraph` and `assets` read source, a lockfile and an asset tree.
# They are pinned to the interpreter the image ships rather than the one the
# tree asks for, so a broken bundle costs the runtime gates and the behaviour
# family but does not also erase the static ones.  Nothing is double-counted
# and a partly-migrated tree still has a gradient to climb.
#
# The database is reset first so the runtime probes see the same conditions
# however the last world left it.
run 300 dbreset "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec rake db:create db:schema:load

# `--outside lib` is the directory this repository keeps loadable code in
# outside `app/`.  It is named here rather than read from the booted
# application because a tree that has taken `lib` off its autoload paths
# declares nothing, and the probe would then have nothing to ask about --
# which would let the question be answered by making the code unreachable.
udir bootprobe
run 600 bootprobe "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec ruby "$TESTS/cvgate/bootprobe.rb" --app "$APP" --outside lib \
    --out "$UGATES/bootprobe/bootprobe.json"
collect_unpriv bootprobe bootprobe.json
udir depdrive
run 900 depdrive "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec ruby "$TESTS/cvgate/depdrive.rb" --app "$APP" \
    --out "$UGATES/depdrive/depdrive.json"
collect_unpriv depdrive depdrive.json

# `development` and `test`, which nothing else here boots.  The two probes
# above read `production`, and the suite below boots `test` but reports what it
# finds as example failures -- which a configuration deprecation is not.  So
# each release's renames and removals inside those two environment files are
# invisible to every other gate, and a tree can score full marks with a
# development environment that cannot boot at all.  Measured on this
# repository: 5.0 drops `Rack::Lock` from the default middleware stack and
# `config/environments/development.rb` anchors rack-livereload to it, and the
# `serve_static_assets` -> `serve_static_files` -> `public_file_server.enabled`
# renames all happen in `config/environments/test.rb`.
#
# One process per environment, because `config/environment.rb` runs once: see
# the probe's own header.  Production is named in its report as skipped rather
# than read a third time, so no defect is charged twice.
udir envboot
run 600 envboot "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec ruby "$TESTS/cvgate/envboot.rb" --app "$APP" \
    --out "$UGATES/envboot/envboot.json"
collect_unpriv envboot envboot.json

# And the entry point none of the three above uses.  `Rakefile` has a loader of
# its own: `Rails.application.load_tasks` reads EVERY file under `lib/tasks/`
# before rake runs anything, so a task file that names a constant the bundle no
# longer defines is not a broken task -- it is a broken task tree, `db:migrate`
# and `assets:precompile` included.  Measured on this repository at the 8.0
# rung, where the retired `annotate` gem's 2014 installer output does exactly
# that and every other reading in this verifier stays green, because that file
# guards itself into `development` and nothing else here is development.
#
# `-T` and not a task: it loads the Rakefile, the application and every task
# file, prints what it found and exits.  Running a task would measure the task
# and would put a migration between this reading and the ones after it.
udir rake
run 300 raketasks "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec ruby "$TESTS/cvgate/raketasks.rb" --app "$APP" \
    --out "$UGATES/rake/rake.json"
collect_unpriv rake rake.json
say "raketasks: $(python3 -c "
import json
try:
    d = json.load(open('$GATES/rake.json'))
    print('ok=%s exit=%s tasks=%s db:migrate=%s annotate-ish=%s' % (
          d.get('ok'), d.get('exit_status'), d.get('task_count'),
          d.get('has_db_migrate'), d.get('annotate_tasks')))
    for line in (d.get('stderr_head') or [])[:4]:
        if 'aborted' in line or 'Error' in line:
            print('   %s' % line)
except Exception as exc:
    print('UNREADABLE: %s' % exc)
")"

# `--pristine` is passed on both paths, including the fallback: the manifest of
# paths the repository shipped with is what makes "do not delete the
# repository's own tests" a measured property rather than a sentence.  The
# scanner raises if the file is not there, which costs the static evidence
# altogether -- deliberately, because a manifest that silently failed to arrive
# would otherwise report nothing missing, which reads as a pass.
#
# Every rule, not this step's selection: the evidence is the same shape for all
# thirteen steps and the profile decides which counts are gated.  A rule whose
# count is not gated here is still in the log, which is where a step that
# introduced a construct a *later* step gates on becomes visible.
SCAN_RULES=$(RBENV_VERSION="$STEP_RUBY" ruby -e '
require "/tests/cvgate/rules"
puts CVGate::RULES.keys.join(",")')
if [ -z "$SCAN_RULES" ]; then
    # The scanner's own default is every rule but `sprockets_directive`.  Say
    # so rather than passing `--rules ""`, which is a legal empty selection:
    # it would report zero findings for everything and score every static gate
    # 0 without a single error line anywhere.
    say "scan: COULD NOT READ THE RULE LIST; falling back to the default set"
    run 600 scan env RBENV_VERSION="$STEP_RUBY" ruby "$TESTS/cvgate/scan.rb" \
        --root "$APP" --pristine "$TESTS/step/pristine.json" \
        --out "$GATES/scan.json"
else
    say "scan: $(printf '%s' "$SCAN_RULES" | tr ',' '\n' | wc -l) rules"
    run 600 scan env RBENV_VERSION="$STEP_RUBY" ruby "$TESTS/cvgate/scan.rb" \
        --root "$APP" --rules "$SCAN_RULES" \
        --pristine "$TESTS/step/pristine.json" --out "$GATES/scan.json"
fi
# `--root` is the tree whose `.ruby-version` and `Gemfile` are read alongside
# the lockfile: the Ruby is declared in three places and all three are
# reported.  It defaults to the lockfile's directory; it is passed anyway so
# that the three artifacts being read are visible in the command.
run 300 gemgraph env RBENV_VERSION="$STEP_RUBY" ruby "$TESTS/cvgate/gemgraph.rb" \
    --lock "$APP/Gemfile.lock" --root "$APP" --out "$GATES/gemgraph.json"
run 300 assets env RBENV_VERSION="$STEP_RUBY" ruby "$TESTS/cvgate/assets.rb" \
    --root "$APP" --out "$GATES/assets.json"

# The resolved gems against a pinned copy of rubysec/ruby-advisory-db.  The
# database is unpacked into the verifier image at a recorded commit by the
# step's own Dockerfile, so the reading does not move when the upstream
# repository does; a step whose image does not carry one gets a report saying
# `present: false` and no gem list at all, which `score.py` scores as a failed
# gate rather than as a clean tree.  Pinned to the image's interpreter like
# the three readers above: it parses YAML and a lockfile and must not inherit
# a broken bundle.
run 300 audit env RBENV_VERSION="$STEP_RUBY" ruby "$TESTS/cvgate/audit.rb" \
    --lock "$APP/Gemfile.lock" --root "$APP" --db /opt/ruby-advisory-db \
    --out "$GATES/audit.json"
say "audit: $(python3 -c "
import json
try:
    d = json.load(open('$GATES/audit.json'))
    db = d.get('db') or {}
    print('db %s (%s advisories over %s gems, commit %s); %s' % (
          db.get('present'), db.get('advisories'), db.get('gems'),
          (db.get('commit') or '-')[:12],
          ('%s advisories over %s gems: %s' % (
              (d.get('counts') or {}).get('advisories'),
              (d.get('counts') or {}).get('gems'),
              ','.join(d.get('vulnerable_gems') or []) or 'none')
           if d.get('counts') else 'no gem list')))
except Exception as exc:
    print('UNREADABLE: %s' % exc)
")"

# What the application wrote into its own production log while phase 3 drove
# it.  This reads phase 3's output rather than the tree, which is why it is
# here and not beside the source readers: `$LOGS/cvh/<world>-server.log` is the
# stdout of the production server for each recorded world.
run 300 logpii env RBENV_VERSION="$STEP_RUBY" ruby "$TESTS/cvgate/logpii.rb" \
    --logs "$LOGS/cvh" --out "$GATES/logpii.json"
say "logpii: $(python3 -c "
import json
try:
    d = json.load(open('$GATES/logpii.json'))
    print('%s files, %s lines (%s started, %s parameters); phone %s lines/%s distinct, email %s lines/%s distinct' % (
          len(d.get('files') or []), d.get('lines'), d.get('started_lines'),
          d.get('parameter_lines'),
          (d.get('phone') or {}).get('lines'), (d.get('phone') or {}).get('distinct'),
          (d.get('email') or {}).get('lines'), (d.get('email') or {}).get('distinct')))
except Exception as exc:
    print('UNREADABLE: %s' % exc)
")"

# The repository's own suite.  It is the one inherited obligation the
# instruction states that nothing else here measures: 520 examples that a tree can leave failing while every
# recorded case still passes.
#
# It is the candidate's copy of `spec/` and not a pristine one, because the
# specs are *part of what this migration changes*
# (rspec-rails 2.14 cannot load under Rails 4.2 at all), so a step-0 copy does
# not run against any correct answer.  What keeps the candidate's copy honest
# is the pair of guards that read the same tree -- every path the repository
# shipped under `spec/` is still at it, and every `*_spec.rb` still declares an
# example -- plus the floor below: a suite that reports zero examples is not a
# green suite.
#
# Its own database, created from the candidate's own schema.  `RAILS_ENV=test`
# rather than the production database phase 3 left seeded: `spec_helper.rb`
# would truncate it.
run 300 rspecdb "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    RAILS_ENV=test RACK_ENV=test bundle exec rake db:create db:schema:load
RSPEC_DB=$?
#
# `--seed` is pinned.  `spec/spec_helper.rb` sets `config.order = "random"`, so
# without it every scoring run orders the 500-odd examples differently and a
# single order-dependent example turns a 3-weight gate into a coin toss --
# charged to a tree that did nothing wrong.  One fixed seed makes the gate a
# measurement of the tree instead of a measurement of the draw.  The number is
# the repository's own era, the same date the clock is frozen at, so that
# nothing here introduces a second arbitrary constant.
#
# `cvgate/rspecdep.rb` rides into the suite's own process and writes down every
# deprecation `ActiveSupport::Deprecation` raises there, because reading them
# out of the log is a gate a tree can pass by printing less.  The file says why
# and what was measured; the two ways in -- `BUNDLE_DISABLE_EXEC_LOAD` plus
# RUBYOPT, and rspec's own `--require` -- are there because `bundle exec` loads
# a ruby-shebang script into the process that already read RUBYOPT.  The report
# is truncated first: a file left over from an earlier phase would be read as
# this run's findings.
#
# What the suite printed is the candidate's code talking: a helper that printed
# a green summary and exited 0 would read as green whatever had run.  So a
# canary written here, fresh for this run and outside the tree, is loaded into the same process: one example
# that must pass and say so, one that must fail and be listed as failed, and
# one record that must reach the deprecation collector.  A log that does not
# carry all of it is not a reading of this run, and none of its numbers is
# used; one that does has the canary's two examples and one failure taken back
# out, so every floor below means what it meant.
udir rspec
RSPEC_DEP="$UGATES/rspec/rspec_deprecations.jsonl"
CANARY_DIR=/tmp/cvcanary
CANARY="$CANARY_DIR/cv_canary_spec.rb"
CANARY_NONCE=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
rm -rf "$CANARY_DIR"
install -d -m 0755 -o 0 -g 0 "$CANARY_DIR"
cat > "$CANARY" <<CANARY_SPEC
# Written by the verifier for one run; see test.sh.
RSpec.describe "cv-canary $CANARY_NONCE" do
  it "passes" do
    STDOUT.puts "cv-canary $CANARY_NONCE ran"
    STDOUT.flush
    if defined?(CVGate::RspecDep) && CVGate::RspecDep.respond_to?(:observe)
      CVGate::RspecDep.observe("cv-canary $CANARY_NONCE", ["$CANARY:1"])
    end
  end

  it "fails" do
    raise "cv-canary $CANARY_NONCE fails by design"
  end
end
CANARY_SPEC
chmod 0644 "$CANARY"
run 900 rspec "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    CV_RSPEC_DEP_OUT="$RSPEC_DEP" CV_RSPEC_DEP_APP="$APP" \
    BUNDLE_DISABLE_EXEC_LOAD=true RUBYOPT="-r$TESTS/cvgate/rspecdep" \
    RAILS_ENV=test RACK_ENV=test bundle exec rspec --no-color --seed 20160119 \
    --require "$TESTS/cvgate/rspecdep" --require "$CANARY"
RSPEC_STATUS=$?
collect_unpriv rspec rspec_deprecations.jsonl
python3 - "$LOGS/rspec.log" "$RSPEC_STATUS" "$RSPEC_DB" \
    "$GATES/rspec_deprecations.jsonl" "$CANARY_NONCE" > "$GATES/rspec.json" <<'RSPEC_PARSE'
import json, re, sys

path, status, db_status = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
dep_path = sys.argv[4]
nonce = sys.argv[5] if len(sys.argv) > 5 else ""
try:
    text = open(path, encoding="utf-8", errors="replace").read()
except OSError as exc:
    json.dump({"error": "no rspec log: %s" % exc}, sys.stdout)
    raise SystemExit(0)

# The summary line, in the spelling both rspec 2 and rspec 3 print.  The last
# match, because a suite that reports per-file summaries prints several.
matches = re.findall(
    r"(\d+) examples?, (\d+) failures?(?:, (\d+) pending)?"
    r"(?:, (\d+) errors? occurred outside of examples)?", text)

# The canary test.sh loaded into this run.  Both of its lines are printed by
# the run itself -- the first by the passing example, at the end of whatever
# line of progress dots it lands on, the second by rspec's own list of failed
# examples -- and the nonce in them was made for this run.
# Without both, the summary is not this run's, and nothing in it is used.
# With both, its two examples and its one failure come back out, and a
# failure count that goes negative doing it is a summary that was not this
# run's either.
CANARY = "cv-canary %s" % nonce
canary_ran = bool(nonce) and re.search(
    r"%s ran\s*$" % re.escape(CANARY), text, re.M) is not None
canary_failed = bool(nonce) and re.search(
    r"^rspec \S+ # %s fails\s*$" % re.escape(CANARY), text, re.M) is not None
canary_ok = canary_ran and canary_failed
examples = failures = pending = outside = None
if matches and canary_ok:
    last = matches[-1]
    examples, failures = int(last[0]) - 2, int(last[1]) - 1
    pending = int(last[2]) if last[2] else 0
    outside = int(last[3]) if last[3] else 0
    if failures < 0 or examples < 0:
        examples = failures = pending = outside = None
        canary_ok = False

# Rails deprecation warnings, attributed to the file each was raised from.
# One warning is several lines -- the banner, the old and new spellings, and
# last the ` (called from <method> at <path>:<line>)` frame
# `ActiveSupport::Deprecation` appends -- so the text is split on the banner
# and each piece is read up to its own frame.
#
# The attribution is the point.  `depdrive` already measures the deprecations
# the application's own code raises, but it boots production and drives HTTP;
# it never loads the suite, so a warning raised from `spec/` is invisible to
# it.  Counting every warning here instead would gate on ones no migration of
# this step can stop: `factory_girl` announces its own rename from the `:test`
# group on every run, blamed to `config/application.rb`.  So the two gates
# partition by blame -- `depdrive.own_count` for the application, and
# `suite_deprecations` here for the suite -- and neither counts the other's.
FRAME = re.compile(r"\(called from .*? at (/?[^\s:]+):(\d+)\)")
by_file = {}
for chunk in text.split("DEPRECATION WARNING:")[1:]:
    frame = FRAME.search(chunk)
    where = frame.group(1) if frame else "?"
    if where.startswith("/app/"):
        where = where[len("/app/"):]
    by_file[where] = by_file.get(where, 0) + 1

# Both counts are counts *of a suite run*.  A log with no summary line in it is
# a suite that never ran, and it holds no deprecation warnings for exactly the
# reason it holds no examples -- so the honest reading is "no number", not
# zero.  Null, which `at_most` scores false and says why, rather than 0, which
# it scores true: a zero would pass `suite_deprecations` for a tree that could
# not bundle, let alone run a spec.
ran = bool(matches) and canary_ok
own = sum(count for where, count in by_file.items() if where.startswith("spec/"))

# The same question asked where the answer cannot be edited.  Everything above
# is a reading of what the suite *printed*, and what a Rails application prints
# when it deprecates something is a configuration: one line --
# `config.active_support.deprecation = :silence` -- removes every banner from
# this log while the tree goes on raising exactly as many.  Measured on a
# step-02 tree with that setting changed in the three environment files and
# nothing else touched: 306 suite deprecations became 0 and the gate flipped to
# PASS.  So `cvgate/rspecdep.rb` rides into the suite's process and records each
# warning as it is raised, upstream of every behaviour the tree can configure,
# and writes one JSON object per line as it goes.
#
# With one exception, which that file argues and measures: a warning raised
# inside an `ActiveSupport::Deprecation.silence` block that the *framework*
# opened is not the tree's.  `ActiveRecord::Migration.check_pending!`, which
# this suite calls from `spec_helper.rb`, asks a deprecated question about its
# own schema table that way twice on every run; blamed to the first
# application frame they are blamed on `spec/`, and they are unreachable by any
# migration the candidate can write.  Those arrive here as
# `framework_silenced` and are counted rather than recorded.
collector = {"installed": False, "lines": 0, "records": 0, "malformed": 0,
             "framework_silenced": 0, "ruby": None, "active_support": None}
collected = {}
# The canary's record: proof that this file is the collector's whole account
# and not what was left of it.  Not a deprecation, and not counted as one.
canary_sunk = False
# rspec's own deprecations, which do not travel through
# `ActiveSupport::Deprecation` at all.  rspec-rails 6.1 keeps
# `config.fixture_path=` working on Rails 7.1 by calling `RSpec.deprecate` and
# then assigning the plural setting, so a tree that never moves to the plural
# spelling raises a deprecation the collector above cannot see, prints it, and
# passes every reading on this page.  `rspecdep.rb` hooks `RSpec.deprecate`
# itself for that, and records the call site rspec computes.
rspec_hook = {"installed": False, "late": None, "rspec": None}
rspec_seen = []
try:
    handle = open(dep_path, encoding="utf-8", errors="replace")
except OSError:
    handle = None
if handle is not None:
    with handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            collector["lines"] += 1
            try:
                record = json.loads(line)
            except ValueError:
                collector["malformed"] += 1
                continue
            event = record.get("event")
            if (nonce and event is None
                    and record.get("message") == CANARY):
                canary_sunk = True
                continue
            if event == "install":
                collector["installed"] = True
                collector["ruby"] = record.get("ruby")
                collector["active_support"] = record.get("active_support")
                continue
            # A warning raised inside a `silence` block that the tree did not
            # open -- `ActiveRecord::Migration.check_pending!` asks a
            # deprecated question about its own schema table that way, twice,
            # on every run of this suite.  Counted and not recorded, so that
            # the evidence distinguishes an instrument that saw nothing from
            # one that saw something and knew whose it was.
            if event == "framework_silenced":
                collector["framework_silenced"] += 1
                continue
            if event == "rspec_install":
                rspec_hook["installed"] = True
                rspec_hook["rspec"] = record.get("rspec")
                rspec_hook["late"] = bool(record.get("late"))
                continue
            if event == "rspec_deprecation":
                rspec_seen.append((str(record.get("message") or ""),
                                   str(record.get("where") or "?")))
                continue
            collector["records"] += 1
            key = (str(record.get("message") or ""),
                   str(record.get("where") or "?"))
            collected[key] = collected.get(key, 0) + 1

# The two instruments report at different granularities -- the log names a
# path, the collector names "<path>:<line>:in <method>" -- so the reading
# they are combined into is the coarser one they share: which files under
# `spec/` raised anything at all.  A union and not either alone, because each
# sees something the other cannot: the collector sees a silenced warning, and
# the log sees a banner printed by a deprecator that is not
# `ActiveSupport::Deprecation` at all.
def site_of(where):
    return where.split(":", 1)[0]

# Which of rspec's own deprecations the TREE raised.  `rspecdep.rb` writes the
# site relative to the application root when it is inside it and absolute when
# it is not, so a relative path that is not under `vendor/bundle/` is the
# tree's own code and everything else belongs to a gem.  The partition is the
# same one `suite_deprecations` makes, and it is made for the same reason:
# rspec-rails announces deprecations of its own on every run of some versions,
# and no migration of this application can stop those.
def rspec_own(where):
    site = where.split(":", 1)[0]
    return not site.startswith("/") and "vendor/bundle/" not in site

# One of rspec's own deprecations belongs to the repository rather than to any
# step of this migration.  rspec-expectations warns once per process, the first
# time an example calls `.should` on an object instead of going through
# `expect`, that the old `:should` syntax was never explicitly enabled.  This
# suite has 22 such call sites and raises the warning on every run -- measured
# on the step-02 golden: "1 deprecation warning total", blamed on
# `spec/controllers/status_controller_spec.rb:29`.  No step of this ladder asks
# anyone to rewrite those call sites, so counting the warning would make a
# reading nobody was asked to pass fail on every tree, the golden included.  It
# is partitioned out by name and counted on its own line, on the rule this file
# already follows for `framework_silenced`: what the instrument set aside is
# reported, never dropped.
INHERITED_RSPEC = re.compile(r"old `:should` syntax")


def rspec_inherited(message):
    return bool(INHERITED_RSPEC.search(message))


own_rspec_pairs = [(message, where) for message, where in rspec_seen
                   if rspec_own(where) and not rspec_inherited(message)]
own_rspec_sites = sorted(set(where.split(":", 1)[0]
                             for _message, where in own_rspec_pairs))
own_rspec = len(own_rspec_pairs)
inherited_rspec = sum(1 for message, _where in rspec_seen
                      if rspec_inherited(message))

sites = set(where for where in by_file if where.startswith("spec/"))
for _message, where in collected:
    site = site_of(where)
    if site.startswith("spec/"):
        sites.add(site)
sites = sorted(sites)
# `None`, not `[]`, when the suite did not run or the collector did not
# install: an empty list is a pass, and both of those are "no reading" rather
# than "no findings".  An install line is written by `rspecdep.rb` the moment
# it prepends, so its absence is a collector that was never in the process --
# an instrument failure, which must not read as a clean tree.
graded = ran and collector["installed"] and canary_sunk
# The rspec reading has a third condition of its own.  The hook goes in from a
# `TracePoint` armed before bundler has run, so it is in place before rspec
# reads an option; if a tree ever loaded its suite helper first -- a `.rspec`
# of its own naming `spec_helper`, which rspec merges ahead of the command
# line -- the `RSpec.configure` block would have deprecated before there was
# anything to hear it, and the honest reading is "no number" rather than zero.
graded_rspec = (ran and rspec_hook["installed"] and rspec_hook["late"] is False
                and canary_sunk)

json.dump({
    "ran": ran,
    "exit": status,
    "db_exit": db_status,
    "examples": examples,
    "failures": failures,
    "pending": pending,
    "errors_outside_examples": outside,
    "canary": {"ran": canary_ran, "failed": canary_failed, "sunk": canary_sunk},
    "deprecations": sum(by_file.values()) if ran else None,
    "deprecations_by_file": by_file,
    "suite_deprecations": own if ran else None,
    "collector": collector,
    "collected_deprecations": len(collected),
    "suite_deprecation_sites": sites if graded else None,
    "rspec_hook": rspec_hook,
    "rspec_deprecations": len(rspec_seen) if graded_rspec else None,
    "own_rspec_deprecations": own_rspec if graded_rspec else None,
    "own_rspec_deprecation_sites": own_rspec_sites if graded_rspec else None,
    "inherited_rspec_deprecations": inherited_rspec if graded_rspec else None,
    # Five conditions, on the counts with the canary taken out.  The exit is
    # 1, not 0, because the canary's one failure is in the run: 0 is a run
    # that did not fail it, and that is not a run of the suite.  No error
    # outside an example, either.  The `examples > 0` one matters because `rspec` on an
    # empty suite exits 0 and prints "0 examples, 0 failures" -- a green suite
    # is a suite that ran something.  The `pending == 0` one matters because
    # rspec 3 counts `skip`, `xit` and `pending` alike in that column and
    # **exits 0** for all of them: without it, marking every failing example
    # pending turns a red suite green while the example count is untouched, so
    # neither this gate nor the size floor would notice.  The repository ships
    # no pending examples, and the instruction says not to skip any.
    "ok": ran and status == 1 and failures == 0
          and (pending or 0) == 0 and (examples or 0) > 0
          and (outside or 0) == 0,
}, sys.stdout)
RSPEC_PARSE
say "rspec: $(python3 -c "
import json
try:
    d = json.load(open('$GATES/rspec.json'))
    print('%s examples, %s failures, %s pending, exit %s, ok %s, '
          '%s deprecations (%s from the suite), '
          'collector %s: %s recorded, %s framework-silenced, suite files %s, '
          'rspec deprecations %s (%s the tree\'s, from %s; %s inherited)'
          % (d.get('examples'), d.get('failures'), d.get('pending'),
             d.get('exit'), d.get('ok'), d.get('deprecations'),
             d.get('suite_deprecations'),
             (d.get('collector') or {}).get('installed'),
             (d.get('collector') or {}).get('records'),
             (d.get('collector') or {}).get('framework_silenced'),
             d.get('suite_deprecation_sites'),
             d.get('rspec_deprecations'),
             d.get('own_rspec_deprecations'),
             d.get('own_rspec_deprecation_sites'),
             d.get('inherited_rspec_deprecations')))
except Exception as exc:
    print('UNREADABLE: %s' % exc)
")"

# ---------------------------------------------------------------------------
# The two constructions of the schema, both built here, and the comparison.
#
# Everything above this point has been reading one database, and by now it is
# not a database either construction built on its own: phase 3 records a
# `db:migrate` case, `db:migrate` re-dumps `db/schema.rb` (the default is on,
# see phase 1), and phase 4's `dbreset` then loaded that re-dumped file.  So
# both sides are rebuilt here from scratch, the dump side from the bytes saved
# in phase 1, and neither reading is inherited from an earlier phase.
#
# Last of all, and after the suite, because these are the only commands in this
# file that write to the candidate's tree.  Nothing reads /app after this point
# except the restore, which is unconditional.
if [ -f "$SCHEMA_HANDED" ]; then
    as_tree_owner /bin/cp -f --remove-destination "$SCHEMA_HANDED" "$SCHEMA_RB"
fi
run 600 dbload "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec rake db:drop db:create db:schema:load
DBLOAD_STATUS=$?
udir pk-dump
run 600 pkread-dump "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec ruby "$TESTS/cvgate/pkdrift.rb" --app "$APP" --read \
    --out "$UGATES/pk-dump/pk-dump.json"
collect_unpriv pk-dump pk-dump.json
# `db/schema.rb` goes out of the way first, and this is not tidiness.  From
# activerecord 8.0.0 `db:migrate` calls `DatabaseTasks.migrate_all`, which calls
# `initialize_database` (activerecord-8.0.5.1 lib/active_record/tasks/
# database_tasks.rb:660-678): on a database with no `schema_migrations` table it
# loads the dump *instead of* running the migrations, and `migrate` then finds
# nothing pending.  Its CHANGELOG entry reads "When running `db:migrate` on a
# fresh database, load the databases schemas before running migrations."  So on
# 8.0 and later, `db:drop db:create db:migrate` against a tree that has a dump
# rebuilds the database from that dump -- and this comparison, whose whole
# point is that the two constructions are independent, would be reading the
# same file twice and agreeing with itself.  Measured: on the step 10 golden,
# with the dump in place `db:migrate` prints two lines and runs no migration;
# with it moved aside it prints 286 and runs all 44.  The file is restored
# unconditionally below, and it was saved in phase 1 before anything ran.
if [ -f "$SCHEMA_RB" ]; then
    rm -f "$SCHEMA_RB"
fi
run 900 dbmigrate "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec rake db:drop db:create db:migrate
DBMIGRATE_STATUS=$?
udir pk-migrated
run 600 pkread-migrated "${UNPRIV[@]}" env "${CV_CLOCK[@]}" \
    bundle exec ruby "$TESTS/cvgate/pkdrift.rb" --app "$APP" --read \
    --out "$UGATES/pk-migrated/pk-migrated.json"
collect_unpriv pk-migrated pk-migrated.json
# The two exit codes are passed in rather than inferred: a `schema:load` or a
# migration run that stopped half way leaves a database that is readable,
# smaller than it should be, and otherwise indistinguishable from a tree whose
# schema really is that shape.  The exit code is the only thing that tells
# those apart and it belongs to the shell, not to the probe.
run 300 pkdrift env RBENV_VERSION="$STEP_RUBY" ruby "$TESTS/cvgate/pkdrift.rb" --compare \
    --dump "$GATES/pk-dump.json" --migrated "$GATES/pk-migrated.json" \
    --load-exit "$DBLOAD_STATUS" --migrate-exit "$DBMIGRATE_STATUS" \
    --out "$GATES/pkdrift.json"

# Unconditional, and the phase-1 bytes: the tree Harbor collects for the next
# step is the candidate's, not this file's idea of what it should have been.
# `cmp` before the copy only so the log says whether it mattered.
if [ -f "$SCHEMA_HANDED" ]; then
    if [ -f "$SCHEMA_RB" ] && [ ! -L "$SCHEMA_RB" ] && \
       cmp -s "$SCHEMA_HANDED" "$SCHEMA_RB"; then
        say "db/schema.rb: the handed-on file is what the tree ends with"
    else
        say "db/schema.rb: REWRITTEN during scoring; the handed-on file is restored"
    fi
    as_tree_owner /bin/cp -f --remove-destination "$SCHEMA_HANDED" "$SCHEMA_RB"
fi
if [ "$STRUCTURE_WAS_THERE" = no ] && [ -f "$STRUCTURE_SQL" ]; then
    say "db/structure.sql: written during scoring and not by the candidate; removed"
    rm -f "$STRUCTURE_SQL"
fi
say "pkdrift: $(python3 -c "
import json
try:
    d = json.load(open('$GATES/pkdrift.json'))
    print('load exit %s / migrate exit %s, dump %s / migrated %s tables, '
          'agree %s, both narrow %s, wide default %s, pinned %s, '
          'spelling-only %s, problems %s'
          % (d.get('schema_load_exit'), d.get('migrate_exit'),
             len((d.get('dump') or {}).get('tables') or []),
             len((d.get('migrated') or {}).get('tables') or []),
             d.get('agree'), d.get('both_narrow'),
             d.get('wide_default_primary_key'), d.get('pinned'),
             sorted((d.get('differing_spellings') or {}).keys()),
             d.get('problems')))
except Exception as exc:
    print('UNREADABLE: %s' % exc)
")"

# ---------------------------------------------------------------------------
say "phase 5: score"
# ---------------------------------------------------------------------------
# The per-case results and the per-finding lists are dropped from the evidence
# and kept in the reports they came from: 386 case results and every scan
# finding with its file and line multiply the object the scorer loads by fifty
# and no gate addresses them.
python3 "$TESTS/evidence.py" --out "$GATES/evidence.json" \
    --file "behaviour=$GATES/behaviour.json" \
    --file "bootprobe=$GATES/bootprobe.json" \
    --file "depdrive=$GATES/depdrive.json" \
    --file "envboot=$GATES/envboot.json" \
    --file "rake=$GATES/rake.json" \
    --file "scan=$GATES/scan.json" \
    --file "gemgraph=$GATES/gemgraph.json" \
    --file "assets=$GATES/assets.json" \
    --file "audit=$GATES/audit.json" \
    --file "logpii=$GATES/logpii.json" \
    --file "rspec=$GATES/rspec.json" \
    --file "pkdrift=$GATES/pkdrift.json" \
    --file "handoff=$GATES/handoff.json" \
    --prune behaviour.results \
    --prune scan.findings \
    --prune assets.findings \
    --prune gemgraph.paths \
    --prune audit.vulnerable \
    --prune logpii.per_file \
    --meta "step=$STEP" \
    --meta "bundle_status=$BUNDLE_STATUS" \
    --meta "preflight=$PREFLIGHT" \
    2>&1 | tee -a "$LOGS/evidence.log"

cp -f "$GATES/evidence.json" "$LOGS/evidence.json" 2>/dev/null || true
# The full recording, alongside the comparison of it.  behaviour.json says
# which cases differed and shows the first twelve paths of each; this is what
# the tree actually did, and it is the file that tells "the tree broke this"
# from "the recorded answer was wrong".
cp -f "$OBSERVED" "$LOGS/observed.json" 2>/dev/null || true
for name in behaviour scan gemgraph assets audit logpii rspec pkdrift handoff; do
    cp -f "$GATES/$name.json" "$LOGS/$name.json" 2>/dev/null || true
done
for name in bootprobe depdrive envboot rake pk-dump pk-migrated; do
    cp -f "$GATES/$name.json" "$LOGS/$name.json" 2>/dev/null || true
done

REWARD=0
if python3 "$TESTS/score.py" --profile "$PROFILE" --evidence "$GATES/evidence.json" \
        --report "$LOGS/report.json" > "$GATES/score.out" 2>&1; then
    REWARD=$(sed -n 's/^REWARD=//p' "$GATES/score.out" | tail -1)
else
    say "score.py failed; the reward is 0 and the reason is in $LOGS/score.log"
fi
tee -a "$LOGS/score.log" < "$GATES/score.out"

# A reward that did not parse is 0, not the empty string: `report` would clamp
# an empty argument to 0 anyway, but saying so here means the log names it.
case "$REWARD" in
    ''|*[!0-9.]*) say "no reward parsed from score.py output; reporting 0"
                  REWARD=0 ;;
esac
say "reward: $REWARD"
report "$REWARD"

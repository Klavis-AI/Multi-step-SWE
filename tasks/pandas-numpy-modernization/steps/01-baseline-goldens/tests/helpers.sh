#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Every step's copy
# is identical.
#
# The first four functions are the harbor_template originals, unchanged except
# for the base-image note. Everything below the divider is this task's shared
# verifier plumbing, which every step's test.sh needs identically.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# the python:*-slim-bullseye and python:*-slim-bookworm images these verifiers
# are built from, at the same uid/gid as in debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
#
# This runs after Harbor's chmod, so this is the mode that sticks.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Use it everywhere candidate code runs -- a
# Makefile, a linked library's static initializer, a probe compiled against
# candidate source -- not just a service.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        --no-new-privs \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output directory, so every candidate scores 0 and the step
# looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# report <score>
#
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every
#           step that produced a verifier_result.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# remaining steps even without min_reward.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, so the candidate
    # still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# ---------------------------------------------------------------------------
# Shared verifier plumbing for this task. Everything below is identical in all
# eight steps by construction, which is the point of it living here.

# Where the scorer keeps its own files. Never under /app: /app is the
# candidate's, and a candidate that writes over the scorer's scratch would be
# grading itself.
#
# Three directories rather than one, because "the scorer's scratch" has two
# audiences with opposite rights:
#
#   $SCRATCH  root-owned, 0755. /tmp is 1777 and sticky, so an entry's owner is
#             who may rename it; root-owning this one is what stops uid 65534
#             moving the whole tree aside and putting a symlink farm in its
#             place.
#   $WORK     the only path uid 65534 may write. Every stage that runs
#             candidate code writes here.
#   $OUT      root-owned, 0755. What the scoring stages read. Nothing the
#             candidate can influence is written here except through collect().
SCRATCH=/tmp/verify
WORK="$SCRATCH/work"
OUT="$SCRATCH/out"
HARNESS=/tests/harness

# Build that layout. Call after harden_reward_dir and before anything else.
make_scratch() {
    rm -rf "$SCRATCH"
    mkdir -p "$WORK" "$OUT"
    chown root:root "$SCRATCH" "$OUT" 2>/dev/null || true
    chmod 755 "$SCRATCH" "$OUT"
    chown "$SCORE_UID:$SCORE_GID" "$WORK" 2>/dev/null || true
    chmod 755 "$WORK"
}

# _cap <limit>
#
# A per-stage limit, cut to what is left before DEADLINE. Each stage is bounded
# twice: by its own budget, so a hang costs one family, and by the whole run's
# deadline, so the *sum* of the stages cannot outrun the verifier's timeout.
#
# A single stage capping its own hang is not enough -- the per-stage caps sum
# well past [steps.verifier].timeout_sec, and Harbor discards a verifier that
# overruns even after it has written reward.json, dropping the step from the
# mean and aborting every step after it. test.sh
# sets DEADLINE to an epoch second under that timeout; every candidate stage is
# then held to min(its budget, time left), so the last one ends at DEADLINE and
# the quick scoring and report still fit inside the timeout. Floored at 5 s so a
# stage always gets to start and fail rather than be skipped into a count that
# moved. 0 (unset) means no deadline.
_cap() {
    local limit="$1" left
    if [ "${DEADLINE:-0}" -gt 0 ]; then
        left=$(( DEADLINE - $(date +%s) ))
        [ "$left" -lt "$limit" ] && limit="$left"
        [ "$limit" -lt 5 ] && limit=5
    fi
    printf '%s' "$limit"
}

# run_stage <seconds> <command...>
#
# run_unprivileged with a clock on it. Same uid, same cleared groups, same
# HOME/TMPDIR; the difference is that the stage cannot run forever.
#
# A stage that hangs is worse than a stage that fails. Harbor kills the verifier
# at [steps.verifier].timeout_sec, reward.json is never written, and a step that
# writes no reward is dropped from the multi-step mean instead of scoring zero
# -- so an infinite loop anywhere in the candidate's code would quietly
# raise the run's average. Each stage therefore gets a budget far under the
# step's, and a stage that blows it costs its own family and nothing else -- and
# _cap holds it to the whole run's deadline besides, so the stages together
# cannot overrun the step either.
#
# TERM first so the interpreter can flush what it has, KILL ten seconds later
# for the case where TERM is what the candidate installed a handler for.
#
# PYTHONNOUSERSITE: a uid-65534 interpreter never reads a user site under
# HOME=/tmp, so a usercustomize/.pth an earlier stage (the build) dropped there
# cannot run inside a later observing stage. After the stage returns, every
# process of that uid is reaped, so nothing it left detached can rewrite this
# stage's output (or drop a file for the next) before collect() reads it.
#
# --no-new-privs (here and in run_unprivileged): /app arrives as the agent, which
# ran as root, left it, and the copy keeps file modes. A root-owned set-user-ID
# binary, or one carrying file capabilities, in that tree would otherwise run
# as root when a stage of uid 65534 executes it, and root can rewrite
# reward.json whatever harden_reward_dir did. With no_new_privs set, execve
# ignores both, so nothing a stage starts can gain a privilege it lacks.
#
# Its own output goes to a file of its own, never to a descriptor the stage
# inherited: a candidate process holding the verifier's stdout could grow
# whatever that is -- /logs/verifier, which Harbor downloads inside the step's
# timeout, included. The file is root-made in $OUT,
# every file the stage writes is capped at 256 MiB (RLIMIT_FSIZE; nothing an
# honest stage writes comes near it), and only the last 64 KiB of it is passed
# on to whatever the caller pointed run_stage's stdout at, once the stage has
# been reaped.
run_stage() {
    local limit rc log
    limit=$(_cap "$1")
    shift
    log=$(mktemp "$OUT/stage.XXXXXX") || return 125
    ( ulimit -f 262144
      exec timeout -k 10 "$limit" \
        setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        --no-new-privs \
        env HOME=/tmp TMPDIR=/tmp PYTHONNOUSERSITE=1 "$@" ) > "$log" 2>&1
    rc=$?
    reap_unprivileged
    tail -c 65536 "$log" 2>/dev/null
    return "$rc"
}

# reap_unprivileged: kill every process whose real uid is SCORE_UID, by exact
# pid read from /proc, until none is left. This runs inside the verifier
# container, whose only SCORE_UID processes are the candidate's.
reap_unprivileged() {
    local i d uid left
    for i in $(seq 1 50); do
        left=0
        for d in /proc/[0-9]*; do
            uid=$(awk '/^Uid:/ {print $2; exit}' "$d/status" 2>/dev/null) \
                || continue
            if [ "$uid" = "$SCORE_UID" ]; then
                left=1
                kill -s KILL "${d#/proc/}" 2>/dev/null || true
            fi
        done
        [ "$left" = 0 ] && return 0
        sleep 0.1
    done
    echo "helpers.sh: WARNING uid $SCORE_UID processes survived the sweep" >&2
    return 0
}

# scoring_stage <seconds> <command...>
#
# A root scoring stage with the same deadline clock on it. The scoring stages
# are pure arithmetic over JSON and fast, but they still run after the candidate
# phase, so they are bounded by DEADLINE too: a candidate that spent the whole
# budget hanging an observing stage leaves each of these its 5 s floor, which is
# enough for JSON arithmetic and keeps the grade/report inside the timeout.
scoring_stage() {
    local limit
    limit=$(_cap "$1")
    shift
    timeout -k 10 "$limit" "$@"
}

# The candidate's own interpreter environment, and the only place its wheel is
# ever installed.
#
# The wheel is never `pip install`ed by root into the scorer's interpreter --
# the one every root scoring stage runs, grade.py included. A wheel runs no code
# at install time, but what it installs is the candidate's choice: a top-level
# `*.pth` or `sitecustomize`, a `<name>.data/purelib|platlib|scripts` entry, a
# directory named after another distribution (`numpy/`, `pandas/`). Any of
# them, written into that interpreter's site-packages or bin dir, runs as root
# at the start of -- or inside -- the next root `python`. Removing hook *names*
# afterwards cannot be complete, so the class is closed by placement instead:
#
#   $CAND  a virtual environment that uid 65534 creates and fills (run_stage).
#          Its one link to the pinned stack is a `.pth` line that adds the
#          stack's own site directories after its own, so $CPY, its
#          interpreter, sees the stack plus the wheel, exactly as the scorer's
#          interpreter would after a direct install. Every process that imports
#          or executes the candidate's package is started from $CPY.
#   root   never runs $CPY, never has $CAND on a path and never writes inside
#          it, so nothing the wheel contains can reach the scorer's own
#          site-packages, bin dir or sys.path, whatever its RECORD says.
CAND="$WORK/candidate-env"
CPY="$CAND/bin/python"

# install_candidate_wheel <wheel>
install_candidate_wheel() {
    local wheel="$1" ver sites
    # Read by root from the scorer interpreter itself, isolated and from /:
    # the minor version the venv's site dir is named for, and where the pinned
    # stack is installed.
    ver=$(cd / && python -I -c \
        'import sys; print("%d.%d" % sys.version_info[:2])') || return 1
    sites=$(cd / && python -I -c \
        'import site; print(repr(list(site.getsitepackages())))') || return 1
    run_stage 120 python -I -m venv --clear --without-pip "$CAND" || return 1
    run_stage 60 python -I -c \
        'import sys; open(sys.argv[1], "w").write(sys.argv[2] + "\n")' \
        "$CAND/lib/python$ver/site-packages/00-verifier-stack.pth" \
        "import site; list(map(site.addsitedir, $sites))" || return 1
    run_stage 600 "$CPY" -m pip install -q --no-deps --no-index --no-compile \
        --disable-pip-version-check "$wheel"
}

# collect <path-under-$WORK> <path-under-$OUT>
#
# Root taking a copy of what an unprivileged stage produced, with O_NOFOLLOW so
# that the name cannot be redirected between the stage exiting and the copy.
#
# The attack this closes is cheap and specific: leave a background process
# running during an observing stage, have it replace `actual.json` with a
# symlink to the recorded expectation, and a plain `cp` would hand the scorer
# the answer sheet as the candidate's own output -- a perfect behaviour score
# for a tree that does nothing. O_NOFOLLOW makes the check and the open the same
# syscall, so there is no window to race. A hard link would sidestep it, which is
# why the expectations live in a directory uid 65534 cannot even traverse.
#
# O_NONBLOCK is here for a different attack with the same shape. `mkfifo
# actual.json` costs the candidate nothing, and a blocking open on a FIFO waits
# for a writer that never comes: the verifier hangs as root until Harbor's
# timeout kills it, the step writes no reward.json at all, and a step that never
# reports vanishes from the mean rather than scoring zero. On a regular
# file the flag does nothing; on a FIFO it returns at once and the S_ISREG test
# below rejects it.
collect() {
    python -I - "$1" "$2" <<'COLLECT_PY'
import os
import shutil
import stat
import sys

src, dst = sys.argv[1], sys.argv[2]
LIMIT = 256 * 1024 * 1024

try:
    fd = os.open(src, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
except OSError as exc:
    sys.stderr.write("collect: cannot open %s: %s\n" % (src, exc))
    sys.exit(1)
try:
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode):
        sys.stderr.write("collect: %s is not a regular file\n" % src)
        sys.exit(1)
    if info.st_size > LIMIT:
        sys.stderr.write("collect: %s is %d bytes, over the limit\n"
                         % (src, info.st_size))
        sys.exit(1)
    handle = os.fdopen(fd, "rb")
    fd = -1
    out = open(dst, "wb")
    try:
        shutil.copyfileobj(handle, out)
    finally:
        out.close()
        handle.close()
finally:
    if fd >= 0:
        os.close(fd)
os.chmod(dst, 0o644)
COLLECT_PY
}

# Remove the build products the artifact carried over from the agent container.
#
# Two reasons:
#
#  * `build/` is not in the gate scanner's skip list: the scanner excludes
#    nothing. But `python setup.py build` copies every .py in the
#    package into `build/lib.*/`, so a tree that has ever been built that way
#    reports each finding twice and the entry counts in manifest.json stop
#    matching anything.
#  * a stale .so or .egg-info makes `import skutil` resolve to something that
#    was compiled from source the candidate has since edited, so the behaviour
#    family would grade code that is no longer in the tree.
#
# `skutil/metrics/_kernel_fast.c` is deliberately NOT removed: it is checked
# into the upstream repository and is therefore a source file in the pristine
# tree, not a build product.
clean_build_products() {
    local root="${1:-/app}"
    rm -rf -- "$root/build" "$root/dist" "$root/.eggs" \
              "$root/.pytest_cache" "$root/.ruff_cache" "$root/.mypy_cache" \
              "$root/cythonize.dat" 2>/dev/null || true
    find "$root" -maxdepth 2 -name '*.egg-info' -prune -exec rm -rf {} + \
        2>/dev/null || true
    find "$root" -name '__pycache__' -prune -exec rm -rf {} + 2>/dev/null || true
    find "$root" \( -name '*.so' -o -name '*.pyc' -o -name '*.pyo' \) \
        -delete 2>/dev/null || true
}

# The environment every scored child runs under.
#
# Same freeze as the recorder used (harness/skutil_probe/run.py): BLAS threads
# pinned so a threaded reduction cannot reorder, hash seed fixed, clock and
# locale and timezone frozen, Matplotlib headless. A case that failed because
# the verifier and the recorder disagreed about the timezone would be a finding
# about this file, not about the candidate.
export_frozen_env() {
    export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
    export NUMEXPR_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1
    export PYTHONHASHSEED=0 PYTHONDONTWRITEBYTECODE=1
    export MPLBACKEND=Agg TZ=UTC LC_ALL=C.UTF-8
    export SOURCE_DATE_EPOCH=1514764800
    # Keeps bytecode for the harness out of /tests, which is the verifier
    # image's own build context in every step. PYTHONDONTWRITEBYTECODE above
    # already means none is written; this is where it would go if some child
    # unset that. It is also where every interpreter *reads* cached bytecode
    # from, the root scoring stages included, so it lives under the root-owned
    # $OUT: under $WORK, a uid-65534 stage could plant a .pyc there that a root
    # stage would then run in place of /tests/harness source.
    export PYTHONPYCACHEPREFIX="$OUT/pyc"
    export PYTHONPATH="$HARNESS"
}

# stamp_step <nn> <manifest-step>
#
# Leave this step's number inside its own reward directory, on every exit path.
#
# A Harbor backend that builds ONE verifier image for a multi-step task runs an
# EARLIER step's test.sh from some step onward. A reference tree that implements
# every milestone passes that earlier verifier trivially, so the run reports 1.0
# at every step and nothing else looks wrong. The only trace is which step's
# files are in which step's directory, so each verifier writes
# step<nn>-verifier.log here and
#
#   ls jobs/<job>/*/*/steps/<NN>-*/verifier | grep -oE 'step[0-9]+' | sort -u
#
# must print exactly step<NN>. Called before anything that can fail, so a
# verifier that dies early still says who it was.
#
# The second argument is the step number the shipped manifest.json claims. It is
# logged rather than enforced: if the hardcoded number and the manifest ever
# disagree, the step was staged wrong, and the log is where that shows up.
STEP_LOG=""
stamp_step() {
    STEP_LOG="/logs/verifier/step$1-verifier.log"
    : > "$STEP_LOG" 2>/dev/null || STEP_LOG=""
    [ -n "$STEP_LOG" ] && chmod 644 "$STEP_LOG" 2>/dev/null || true
    note "verifier identity: step $1 (manifest says step ${2:-?})"
}

# note <text...>
#
# One line of verifier log. Goes to stdout, which Harbor captures next to
# reward.json, so a score that looks wrong can be read back without a rerun,
# and -- once stamp_step has run -- also into this step's own log file there.
#
# One line is at most 2000 characters: several notes quote text a candidate
# stage chose (a build log line, a grader line), and the step log lives in
# /logs/verifier.
note() {
    local msg="$*"
    msg="${msg:0:2000}"
    printf '[verify] %s\n' "$msg"
    if [ -n "${STEP_LOG:-}" ]; then
        printf '[verify] %s\n' "$msg" >> "$STEP_LOG" 2>/dev/null || true
    fi
}

#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the Docker
# build context, so it cannot COPY from the task root.  Keep every copy in sync.
set -u

# report <score>
#   reward: this step's local score in [0, 1].  Harbor's
#   multi_step_reward_strategy = "mean" averages this key over the steps.
# Always exits 0: a non-zero exit is a verifier crash and Harbor then skips the
# remaining steps even without min_reward.
_reward_sweep() {
    # SAFETY: only ever sweep inside the verifier container.  Run on the shared host by
    # accident (e.g. sourcing this file and calling report() locally) it would kill every
    # process of this user not in the ancestor chain -- other jobs, other builders'
    # work.  The host's pid 1 is systemd/init; the verifier
    # container's is not.  Err toward running (skip ONLY on a confirmed host), so the
    # defence is never silently disabled inside the container.
    local _p1
    _p1=$(cat /proc/1/comm 2>/dev/null || echo systemd)
    case "$_p1" in systemd|"") return 0 ;; esac   # confirmed host (or unreadable): skip
    # A candidate can spawn a DETACHED process during its turn that outlives this scorer
    # and overwrites the reward file.  It runs as root, is reparented to pid 1, forges
    # nothing and names no candidate path -- so neither the uid drop nor a nonce nor a
    # pattern sweep stops it; it wins purely by writing last, and a candidate that builds
    # nothing could score 1.0 this way.  Kill it before the final write, the one ordering
    # it cannot beat.  Allowlist only -- pid 1 and this verifier's own ancestor chain --
    # and kill by PID, never by pattern, which would match this very shell.
    local keep p line ppid pid d _c _p1 _full _exe
    keep=" 1 $$ "
    p=$$
    while [ -r "/proc/$p/stat" ]; do
        read -r line < "/proc/$p/stat" || break
        line=${line#*) }
        set -- $line
        ppid=${2:-}
        case "$ppid" in ""|*[!0-9]*) break ;; esac
        keep="$keep$ppid "
        [ "$ppid" -le 1 ] && break
        p=$ppid
    done
    for d in /proc/[0-9]*; do
        pid=${d#/proc/}
        case "$keep" in *" $pid "*) continue ;; esac
        # /proc/*/cmdline always stat()s as zero-length, so `[ -s ]` is useless here;
        # read it and skip genuinely-empty ones (kernel threads).  Reset _c first: a
        # failed read (the process exited between the glob and here) leaves the PREVIOUS
        # process's cmdline in _c, which would kill this pid on stale evidence.
        _c=""
        { IFS= read -rd "" _c < "$d/cmdline"; } 2>/dev/null || true
        [ -n "$_c" ] || continue
        # Never kill Harbor's environment keepalive.  In SHARED mode the sweep
        # runs inside the agent's OWN container.  Under the docker
        # environment pid 1 is `sh -c "sleep infinity"` and the sleep is a SEPARATE
        # child: killing it makes pid 1 exit 137, the container stops, and every later
        # step's `docker compose exec` returns 1 -- the chain loses every step after
        # this one.  (On GKE pid 1 IS `sleep infinity`, already in the keep list.)
        # argv[0] alone is not enough -- match the full NUL-joined command line.
        #
        # But the command line is text the PROCESS supplies: `exec -a sleep /app/w
        # infinity` produces cmdline "sleep\0infinity\0" while running the candidate's
        # own binary, and from step 02 on the candidate can read /tests/helpers.sh and
        # see exactly which string is spared.  So check IDENTITY, not just the name:
        # the real keepalive runs /usr/bin/sleep (or /bin/sleep), which writes nothing,
        # while the attack needs the candidate's OWN binary wearing the name, whose
        # /proc/<pid>/exe still points at it.  This does NOT also require a parent
        # of init: inside this package's own container the
        # keepalive IS pid 1 (exe /usr/bin/sleep), which the allowlist above already
        # covers -- so such a test buys nothing here and would kill a keepalive that is
        # merely parented differently, the very chain break this exemption prevents.
        _full=$(tr "\0" " " < "$d/cmdline" 2>/dev/null)
        _exe=$(readlink "$d/exe" 2>/dev/null)
        case "$_full" in
            "sleep infinity "*)
                case "$_exe" in /usr/bin/sleep|/bin/sleep) continue ;; esac
                ;;
        esac
        kill -9 "$pid" 2>/dev/null || true
    done
}

# _reap_uid <uid>: kill every process running as <uid>, and keep at it until none is left.
# Called between phases (after the build, after the canary, after each test set), so
# nothing one phase started is still running while the next is measured.  kill(-1) from
# a shell that has become that uid reaches all of its processes at once, which a fork
# loop cannot outrun; the /proc scan confirms the uid is empty.  Zombies are skipped:
# they are already dead, and pid 1 in a container does not necessarily reap them.
_reap_uid() {
    local uid="$1" i d st live=0
    for i in 1 2 3 4 5 6 7 8 9 10; do
        setpriv --reuid "$uid" --regid "$uid" --clear-groups bash -c 'kill -9 -1' \
            >/dev/null 2>&1 || true
        live=0
        for d in /proc/[0-9]*; do
            st=$(awk '/^State:/ { s = $2 } /^Uid:/ { print s, $2; exit }' "$d/status" 2>/dev/null)
            case "$st" in "Z "*|"X "*|"") continue ;; esac
            [ "${st#* }" = "$uid" ] && live=$((live + 1))
        done
        [ "$live" = 0 ] && return 0
        sleep 0.2
    done
    echo "warning: $live process(es) of uid $uid survived the sweep" >&2
    return 0
}

report() {
    local raw reward
    raw="${1-}"
    mkdir -p /logs/verifier
    # Clamping is correct as fault tolerance; clamping *silently* is not.  An empty or
    # non-numeric argument here means the scorer did not produce a number -- a crash, a
    # missing file -- and that must not be indistinguishable from a candidate that
    # genuinely scored zero.  Say so, in the log and on stdout.
    if ! printf '%s' "$raw" | grep -Eq '^[0-9]+(\.[0-9]+)?$'; then
        echo "SCORE NOT COMPUTED: report() received '${raw}', which is not a number;" \
             "reporting 0 -- this is a verifier failure, not a candidate score" \
             | tee -a /logs/verifier/score-anomaly.txt
        raw=0
    fi
    reward=$(awk -v v="$raw" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
    _reward_sweep
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    echo "reward=$reward"
    exit 0
}

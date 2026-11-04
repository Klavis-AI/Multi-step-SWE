#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# The uid every piece of candidate code runs as: the build, each corpus case and
# each gate measurement.
CAND_UID=65534

# Take the reward directory back before any candidate code runs. Harbor chmods
# /logs/verifier to 0777 just before this container starts, and in a 0777
# directory the candidate uid can unlink root's reward.json and put its own in
# its place -- from a detached child that outlives everything below. report()
# reopens it to 0755, still root-owned, at the very end.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# strip_setid <dir>
#
# Everything Harbor restored under /app came out of the agent's container, where
# the agent ran as root, and the copy keeps mode bits. A set-user-ID or
# set-group-ID file there would run with its owner's privileges for the
# unprivileged uid that runs the build and the cases, so the bits go before any
# candidate code runs: for every path under the directory, not only the paths
# the build reads. find does not follow symbolic links, and nothing the
# candidate wrote is running yet, so the path chmod receives is the file find
# saw. Fails if any set-id file is left.
strip_setid() {
    find "$1" ! -type l \( -perm -4000 -o -perm -2000 \) -exec chmod u-s,g-s -- {} + 2>/dev/null
    [ -z "$(find "$1" ! -type l \( -perm -4000 -o -perm -2000 \) -print -quit 2>/dev/null)" ]
}

# Pids of every live process running as the candidate uid. Read from
# /proc/<pid>/status rather than from the owner of /proc/<pid>, which a process
# that marks itself non-dumpable turns into root; zombies are skipped, since
# nothing reaps them here and they can do nothing.
candidate_pids() {
    awk -v u="$CAND_UID" '
        FNR == 1 { z = 0 }
        /^State:/ { z = ($2 == "Z") }
        /^Uid:/ {
            if (!z) for (i = 2; i <= 5; i++) if ($i == u) {
                n = split(FILENAME, a, "/"); print a[3]; break
            }
            nextfile
        }' /proc/[0-9]*/status 2>/dev/null
}

# Kill every process the candidate uid still has in this container, and keep at
# it until none is left: a detached child of the build or of a case outlives the
# call that started it, and nothing it does may overlap the scoring.
sweep_candidate() {
    local i p left
    for i in $(seq 1 100); do
        left=$(candidate_pids)
        [ -z "$left" ] && return 0
        for p in $left; do kill -KILL "$p" 2>/dev/null; done
        sleep 0.1
    done
    echo "[verify] candidate processes still alive after the sweep: $(candidate_pids | tr '\n' ' ')"
}

# The decoy launchers' marker (see the Dockerfile) as size, ctime and inode, or
# "missing". Any write moves the ctime, so an unchanged stamp means no decoy ran
# and nothing touched the file.
DECOY_LOG=/var/lib/jvm-decoy/log
decoy_stamp() {
    stat -c '%s %z %i' "$DECOY_LOG" 2>/dev/null || echo missing
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
    sweep_candidate
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    # Reopen so a reader outside the container can collect it. Still root-owned,
    # so the candidate still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# score_from <corpus.log> [gate_pass gate_total]
#
# The product of two factors, each in [0, 1]:
#   T1  this milestone's own work -- its conformance set, and on the milestone
#       that has them the two instruction-count gates, weighted 2 : 1 (the
#       gates are a third of T1);
#   T2  the regression set.
# reward = T1 x T2, so a predecessor that keeps every earlier behaviour and does
# none of this milestone's work scores 0. A milestone with no regression set is
# T1 alone.
#
# Within a set the unit is the behaviour family, not the case. Sixty class-file
# mutations and one thread test are one behaviour each, so padding a family
# cannot buy a milestone and a program that answers every case the same way
# collects only the families whose answer that is. One implementation, shared by
# every step, because six copies of a scorer is six chances for one of them to
# be last month's.
score_from() {
    local log="$1"
    awk -v gp="${2:-0}" -v gt="${3:-0}" '
        /^FAM / { fp[$2 SUBSEP $3] = $4; ft[$2 SUBSEP $3] = $5; seen[$2] = 1 }
        END {
            for (key in ft) {
                split(key, a, SUBSEP)
                if (ft[key] > 0) { acc[a[1]] += fp[key] / ft[key]; cnt[a[1]] += 1 }
            }
            own = 0; nown = 0; reg = 0; nreg = 0;
            for (r in seen) {
                m = (cnt[r] > 0) ? acc[r] / cnt[r] : 0;
                if (r ~ /r$/) { reg += m; nreg += 1 } else { own += m; nown += 1 }
            }
            if (nown == 0) { print 0; exit }
            o = own / nown;
            t1 = o;
            if (gt > 0) t1 = (2 * o + gp / gt) / 3;
            if (nreg == 0) { printf "%.6f", t1; exit }
            printf "%.6f", t1 * (reg / nreg);
        }' "$log"
}

# score_control [gate_total]
#
# A computed number needs both ends pinned before any partial result is worth
# reading: the same function, on a synthetic all-correct log and a synthetic
# all-wrong one, has to give exactly 1.000000 and 0.000000.
score_control() {
    local gt="${1:-0}" hi lo hi1 lo1 mix t
    t=$(mktemp)
    # with a regression slice, the shape steps 2-6 use
    printf 'FAM a f1 6 6\nFAM a f2 1 1\nFAM ar g1 4 4\n' > "$t"; hi=$(score_from "$t" "$gt" "$gt")
    printf 'FAM a f1 0 6\nFAM a f2 0 1\nFAM ar g1 0 4\n' > "$t"; lo=$(score_from "$t" 0 "$gt")
    # the product: all of this milestone's own work and none of the regression set
    printf 'FAM a f1 6 6\nFAM a f2 1 1\nFAM ar g1 0 4\n' > "$t"; mix=$(score_from "$t" "$gt" "$gt")
    # and without one, the shape step 1 uses -- a branch that is otherwise
    # never exercised by the step that depends on it
    printf 'FAM a f1 6 6\nFAM a f2 1 1\n' > "$t"; hi1=$(score_from "$t" "$gt" "$gt")
    printf 'FAM a f1 0 6\nFAM a f2 0 1\n' > "$t"; lo1=$(score_from "$t" 0 "$gt")
    rm -f "$t"
    echo "CONTROL all-correct=$hi none=$lo own-only=$mix  (no-regression shape: $hi1 / $lo1)"
    [ "$hi" = "1.000000" ] && [ "$lo" = "0.000000" ] && [ "$mix" = "0.000000" ] \
        && [ "$hi1" = "1.000000" ] && [ "$lo1" = "0.000000" ]
}

#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the Docker
# build context, so it cannot COPY from the task root. Keep every copy in sync.
set -u

# report <score>
#
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every step
#           that produced a verifier_result.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips the
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
    printf 'REWARD %s\n' "$reward" >> /logs/verifier/verifier.log
    exit 0
}

# score_from_groups <groups.txt> <weights.json>
#
# reward = T1 x T2.
#   T1 = sum over groups of weight * passed/total, over this milestone's own cases. A
#        group named in the weights but absent from groups.txt contributes 0, so a
#        phase that never ran costs its weight instead of silently vanishing.
#   T2 = the same weighted fraction over the cases carried forward from earlier
#        milestones (groups.txt lines `regress:<group>`), renormalised over the groups
#        that carry such cases; 1 when this step scores none.
score_from_groups() {
    local groups="$1" weights="$2"
    # Root runs this, and test.sh runs from the workdir /app, which is the
    # candidate's tree: `python3 -` would put the cwd first on sys.path, so a
    # /app/json.py would be imported here and could print any score. Isolated
    # mode from /, so only the standard library is importable.
    ( cd / && python3 -I - "$groups" "$weights" ) <<'PY'
import json, sys
groups, weights = sys.argv[1], sys.argv[2]
counts = {}
try:
    for line in open(groups):
        parts = line.split()
        if len(parts) == 3:
            g, p, t = parts[0], int(parts[1]), int(parts[2])
            gp, gt = counts.get(g, (0, 0))
            counts[g] = (gp + p, gt + t)
except FileNotFoundError:
    pass
w = json.load(open(weights))
RP = "regress:"
regress = {g[len(RP):]: v for g, v in counts.items() if g.startswith(RP)}
counts = {g: v for g, v in counts.items() if not g.startswith(RP)}
t1 = 0.0
for g, weight in w.items():
    p, t = counts.get(g, (0, 0))
    frac = (p / t) if t else 0.0
    t1 += weight * frac
    print(f"group {g}: {p}/{t} weight {weight} -> {weight*frac:.6f}", file=sys.stderr)
# T2: the inherited cases, weighted like their groups and renormalised over the groups
# that carry them. A step that scores none has nothing to multiply by.
t2 = 1.0
if regress:
    num = den = 0.0
    for g in sorted(regress):
        p, t = regress[g]
        frac = (p / t) if t else 0.0
        weight = w.get(g, 0.0)
        num += weight * frac
        den += weight
        print(f"regress {g}: {p}/{t} weight {weight}", file=sys.stderr)
    if den > 0:
        t2 = num / den
    else:
        p_all = sum(p for p, _ in regress.values())
        t_all = sum(t for _, t in regress.values())
        t2 = (p_all / t_all) if t_all else 0.0
print(f"T1 {t1:.6f} x T2 {t2:.6f}", file=sys.stderr)
total = t1 * t2
# A group that RAN but carries no weight contributes nothing to the reward. That only
# happens when a group is added to cases.json without a matching weights.json entry, and
# the reward still looks plausible, so say so explicitly.
for g in sorted((set(counts) | set(regress)) - set(w)):
    p_, t_ = counts.get(g, regress.get(g, (0, 0)))
    print(f"WARNING: group {g!r} ran {p_}/{t_} cases but has NO WEIGHT -- it contributed "
          f"nothing to the reward. Add it to weights.json or remove it from cases.json.",
          file=sys.stderr)
print(f"{min(max(total, 0.0), 1.0):.6f}")
PY
}

# ------------------------------------------------------------------ containment
# The scored utilities must be the candidate's own C. Gates elsewhere look at what
# the build put in /app/build; these make a trampoline from there to some OTHER
# implementation useless:
#   * build_launcher       -- the verifier's own launcher (launch.c) runs every
#                             utility: same privilege drop as setpriv, and no
#                             further execve, inet socket, ptrace or io_uring;
#   * reap_uid             -- nothing the candidate's build started outlives it;
#   * lock_workspace       -- after the build /app is root's, read-only to the uid;
#   * purge_foreign_code   -- no ELF file in /app survives except the utilities;
#   * wipe_uid_files       -- nothing the uid wrote outside /app survives;
#   * lock_foreign_runtimes-- interpreters and the JVM are unreadable to the uid,
#                             so they cannot be loaded in-process either.
CAND_LAUNCH=/run/pdfl/launch

build_launcher() {
    rm -rf /run/pdfl
    mkdir -m 0755 /run/pdfl || return 1
    ( cd / && gcc -O2 -Wall -o /run/pdfl/launch /tests/launch.c ) || return 1
    chown root:root /run/pdfl/launch && chmod 0755 /run/pdfl/launch
}

# _uid_pids <uid>: live (non-zombie) processes whose real or effective uid is <uid>.
_uid_pids() {
    local want="$1" p s r e
    for p in /proc/[0-9]*; do
        read -r s r e < <(awk '/^State:/{s=$2} /^Uid:/{r=$2; e=$3} END{print s, r, e}' \
                              "$p/status" 2>/dev/null)
        [ -n "${s:-}" ] || continue
        [ "$s" = "Z" ] && continue
        if [ "$r" = "$want" ] || [ "$e" = "$want" ]; then echo "${p#/proc/}"; fi
    done
}

# reap_uid <uid>: SIGKILL every process of <uid>, from inside the uid and from root,
# until none is left. Non-zero if one survives 50 rounds.
reap_uid() {
    local u="$1" i p
    for i in $(seq 1 50); do
        [ -z "$(_uid_pids "$u")" ] && return 0
        setpriv --reuid="$u" --regid="$u" --clear-groups sh -c 'kill -KILL -1' 2>/dev/null
        for p in $(_uid_pids "$u"); do kill -KILL "$p" 2>/dev/null; done
        sleep 0.2
    done
    [ -z "$(_uid_pids "$u")" ]
}

lock_workspace() {
    chown -R root:root /app 2>/dev/null
    chmod -R a+rX,go-w /app 2>/dev/null
    return 0
}

# purge_foreign_code <utility>...: delete every ELF file under /app other than
# /app/build/<utility>. The utilities link nothing beyond the C runtime, so no other
# compiled file in the workspace is needed once the build is done. Regular files only;
# symlinks and special files are never opened.
purge_foreign_code() {
    local f t keep n=0
    while IFS= read -r -d '' f; do
        keep=0
        for t in "$@"; do [ "$f" = "/app/build/$t" ] && keep=1; done
        [ "$keep" = 1 ] && continue
        if [ "$(head -c4 -- "$f" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 7f454c46 ]; then
            rm -f -- "$f" && n=$((n + 1))
        fi
    done < <(find /app -type f -print0 2>/dev/null)
    echo "$n"
}

# wipe_uid_files <uid>: remove every file and directory <uid> owns outside /app.
wipe_uid_files() {
    find / \( -path /proc -o -path /sys -o -path /app \) -prune -o -user "$1" -print0 2>/dev/null \
        | xargs -0 -r rm -rf -- 2>/dev/null
    return 0
}

lock_foreign_runtimes() {
    local f
    for f in /usr/bin/python3 /usr/bin/python3.* /usr/bin/python /usr/bin/perl /usr/bin/perl5* \
             /usr/bin/ruby /usr/bin/node /usr/bin/php /usr/bin/tclsh \
             /usr/lib/x86_64-linux-gnu/libpython3* /usr/lib/x86_64-linux-gnu/libperl.so* \
             /usr/lib/jvm /opt/verapdf; do
        [ -e "$f" ] && chmod o-rwx "$f" 2>/dev/null
    done
    return 0
}

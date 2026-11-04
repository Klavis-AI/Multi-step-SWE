#!/usr/bin/env bash
# The behaviour-equivalence runner: build the candidate tree, drive it through
# the recorded probe, and count how many transcripts came back identical.
#
#   score.sh
#
# Everything is passed in the environment so a step's test.sh reads as a list of
# decisions rather than a line of positional arguments:
#
#   RUNNER_TREE      candidate tree root                 (default /app/arquillian)
#   RUNNER_BUILD     build command, run inside the tree  (required)
#   RUNNER_PUBLISHED where the build leaves its jars     (default ~/.m2/repository)
#   RUNNER_ASSETS    scorer-owned payload                (default /tests/assets)
#   RUNNER_ERA       recording subdirectory under assets/rec  (required)
#   RUNNER_IDS       id list under assets/                (default ids.txt)
#   RUNNER_OUT       where to write everything            (default /tmp/runner)
#   RUNNER_BUILD_TIMEOUT   seconds before the build is killed  (default 1500)
#   RUNNER_PROBE_TIMEOUT   seconds before the probe is killed  (default 1200)
#
# Writes RUNNER_OUT/behavior.summary:
#
#   behavior.build=ok|failed|timeout
#   behavior.build.sec=<n>       wall clock, for the per-step budget
#   behavior.jars=<n>            module jars the build published
#   behavior.probe=ok|failed|timeout
#   behavior.probe.sec=<n>
#   behavior.expected=<n>        transcripts the id list asks for
#   behavior.produced=<n>        transcripts the run actually wrote
#   behavior.match=<n>           transcripts identical to the recording
#   behavior.differ[i]=<id>      produced, but not identical
#   behavior.missing[i]=<id>     asked for, never written
#
# It always writes a summary and always exits 0. A build that does not build is
# a score of zero for this family, not a crashed verifier: the step writes a
# reward and exits 0 whatever happened, and a verifier that dies here takes its
# step out of the mean entirely (which reads as a better result than it is).
set -uo pipefail

tree="${RUNNER_TREE:-/app/arquillian}"
build_cmd="${RUNNER_BUILD:?RUNNER_BUILD is required}"
published="${RUNNER_PUBLISHED:-${HOME:-/root}/.m2/repository}"
assets="${RUNNER_ASSETS:-/tests/assets}"
era="${RUNNER_ERA:?RUNNER_ERA is required}"
ids="${assets}/${RUNNER_IDS:-ids.txt}"
out="${RUNNER_OUT:-/tmp/runner}"
build_timeout="${RUNNER_BUILD_TIMEOUT:-1500}"
probe_timeout="${RUNNER_PROBE_TIMEOUT:-1200}"

rec="${assets}/rec/${era}"
summary="${out}/behavior.summary"

mkdir -p "${out}"
: > "${summary}"

say() { echo "$*" >> "${summary}"; }
note() { echo "runner: $*" >&2; }

# Said once, at most. A second behavior.expected= line would be a gift to a
# parser that reads the first one and a trap for a parser that reads the last.
expected_said=0

# --------------------------------------------------------------- preflight
#
# Every one of these is a way to produce a confident zero, or a confident
# one, from an instrument that was not actually pointed at anything. They are
# checked before the build so a missing asset reads as a broken scorer rather
# than as a failing candidate.
fatal()
{
   note "SCORER BROKEN: $*"
   say "behavior.scorer=broken"
   say "behavior.scorer.reason=$*"
   say "behavior.match=0"
   [ "${expected_said}" -eq 1 ] || say "behavior.expected=0"
   exit 0
}

[ -d "${tree}" ]      || fatal "no candidate tree at ${tree}"
[ -d "${assets}" ]    || fatal "no scorer assets at ${assets}"
[ -f "${ids}" ]       || fatal "no id list at ${ids}"
[ -d "${rec}" ]       || fatal "no recordings for era '${era}' at ${rec}"
# -r, not -x: the probe is invoked as an argument to bash, so the execute bit is
# not what makes it work, and a payload that arrived through a COPY or a
# read-only mount may not carry one.
[ -r "${assets}/probe/run.sh" ] || fatal "cannot read the probe runner at ${assets}/probe/run.sh"

expected="$(grep -c . "${ids}")"
recorded="$(find "${rec}" -name '*.txt' -type f | wc -l | tr -d ' ')"
[ "${expected}" -gt 0 ] || fatal "the id list is empty"
# A recording directory with fewer files than the id list would score every
# missing one as a failure of the candidate. That is the scorer's fault, and
# saying so is cheaper than explaining a mysterious 0.4 later.
[ "${recorded}" -ge "${expected}" ] \
   || fatal "era '${era}' has ${recorded} recordings for ${expected} ids"
say "behavior.expected=${expected}"
expected_said=1

# --------------------------------------------------------------- privilege
#
# The runner itself is root; the build and the probe are not. Those two are the
# only places where code the candidate wrote gets executed, and demoting just
# them is what lets assets/rec stay unreadable to 65534: the comparison happens
# up here, so nothing a build script can reach ever holds a path to the answers.
#
# It also avoids two failures. Run entirely as 65534, the runner cannot read
# its own /tests/runner/score.sh -- the payload is 0700 root:root -- and family
# A scores 0 with "Permission denied" as the only evidence. Run entirely as
# root, publishToMavenLocal resolves user.home to /root and publishes there, so
# the build reports success and the scorer finds no jars where it is looking.
#
# RUNNER_DEMOTE=0 turns it off for recording runs outside the verifier, where
# the tree is a bind mount that 65534 does not own.
SCORE_UID="${SCORE_UID:-65534}"
SCORE_GID="${SCORE_GID:-65534}"
if [ "${RUNNER_DEMOTE:-1}" = 1 ] && [ "$(id -u)" = 0 ]; then
   # Nothing the build or the probe starts outlives the call: every
   # uid-${SCORE_UID} process in this container is stopped and killed by pid
   # once the command returns, before root reads its output.
   #
   # And none of this runner's own RUNNER_* settings go with it: the build
   # and the probe are handed everything they need as arguments, and
   # RUNNER_ASSETS/RUNNER_ERA name the directory the recordings live in.
   as_untrusted()
   {
      local rc=0 round p u pids v
      local drop=()
      for v in $(compgen -e); do
         case "${v}" in RUNNER_*) drop+=( -u "${v}" ) ;; esac
      done
      setpriv --reuid "${SCORE_UID}" --regid "${SCORE_GID}" --clear-groups \
         env ${drop[@]+"${drop[@]}"} HOME=/tmp TMPDIR=/tmp "$@" || rc=$?
      for round in 1 2 3 4 5 6 7 8 9 10; do
         pids=""
         for p in /proc/[0-9]*; do
            u=$(awk '/^Uid:/ {print $2; exit}' "$p/status" 2>/dev/null) || continue
            [ "$u" = "${SCORE_UID}" ] && pids="$pids ${p#/proc/}"
         done
         [ -n "$pids" ] || break
         kill -s STOP $pids 2>/dev/null || true
         kill -s KILL $pids 2>/dev/null || true
         sleep 0.2
      done
      return "$rc"
   }
   note "build and probe run as ${SCORE_UID}:${SCORE_GID}"
else
   as_untrusted() { "$@"; }
   note "build and probe run as $(id -u):$(id -g) (no demotion)"
fi

# --------------------------------------------------------------- build

note "building: ${build_cmd}"
build_log="${out}/build.log"
build_status=ok
# Status captured from the command itself, not from inside `if ! cmd; then`:
# there, $? is the status of the negation, which is 0 by construction, and the
# timeout arm below would never be reached. Measured on this box:
# `timeout -s KILL` returns 137 on a kill, 124 on a TERM, and the child's own
# status otherwise.
t0="${SECONDS}"
# setpriv outside timeout, not inside: timeout kills its own child, and if that
# child were setpriv the build would be orphaned rather than stopped.
as_untrusted timeout -s KILL "${build_timeout}" \
   bash -c "cd '${tree}' && ${build_cmd}" > "${build_log}" 2>&1
rc=$?
if [ "${rc}" -ne 0 ]; then
   build_status=failed
   { [ "${rc}" -eq 137 ] || [ "${rc}" -eq 124 ]; } && build_status=timeout
   note "build ${build_status} (rc=${rc}); last 40 lines:"
   tail -40 "${build_log}" >&2
fi
say "behavior.build=${build_status}"
say "behavior.build.sec=$(( SECONDS - t0 ))"

# The tree's own module jars, and only those. A local repository that has also
# seen the project's 2011 nightlies holds timestamped jars of the same version;
# those are somebody else's build. probe/run.sh applies the same rule to the
# classpath -- this count is here so "the build produced nothing" is visible as
# a number rather than as a compile error sixty lines later.
jars=0
if [ -d "${published}" ]; then
   jars="$(find "${published}" -name 'arquillian-*-SNAPSHOT.jar' ! -name '*osgi-bundle*' \
      2>/dev/null | wc -l | tr -d ' ')"
fi
say "behavior.jars=${jars}"

if [ "${build_status}" != ok ] || [ "${jars}" -eq 0 ]; then
   say "behavior.probe=skipped"
   say "behavior.produced=0"
   say "behavior.match=0"
   i=0
   while IFS= read -r id; do
      [ -n "${id}" ] || continue
      say "behavior.missing[${i}]=${id}"
      i=$((i + 1))
   done < "${ids}"
   note "nothing to run: build=${build_status} jars=${jars}"
   exit 0
fi

# --------------------------------------------------------------- probe

transcripts="${out}/transcripts"
# Emptied, not just created. A probe that is killed half way through leaves the
# transcripts it had already written; if an earlier run's files were still
# sitting there, the ones it never reached would compare equal to the recording
# and the run would score as a success it did not have. In a verifier the out
# directory is fresh anyway -- this is here so that stays true of the runner
# rather than of the container it happens to be in.
rm -rf "${transcripts}"
mkdir -p "${transcripts}" "${out}/probe-work"
# Handed over explicitly. The probe compiles the harness and writes a transcript
# per fixture, and it is about to stop being root.
chown -R "${SCORE_UID}:${SCORE_GID}" "${transcripts}" "${out}/probe-work" 2>/dev/null || :
probe_status=ok
probe_log="${out}/probe.log"
t0="${SECONDS}"
as_untrusted timeout -s KILL "${probe_timeout}" \
   bash "${assets}/probe/run.sh" \
      "${published}" \
      "${assets}/probe-lib" \
      "${assets}/probe/src" \
      "${assets}/inputs" \
      "${ids}" \
      "${transcripts}" \
      "${out}/probe-work" > "${probe_log}" 2>&1
rc=$?
if [ "${rc}" -ne 0 ]; then
   probe_status=failed
   { [ "${rc}" -eq 137 ] || [ "${rc}" -eq 124 ]; } && probe_status=timeout
   note "probe ${probe_status} (rc=${rc}); last 40 lines:"
   tail -40 "${probe_log}" >&2
fi
say "behavior.probe=${probe_status}"
say "behavior.probe.sec=$(( SECONDS - t0 ))"

# --------------------------------------------------------------- compare
#
# The comparison is per id, not a directory diff: a run that wrote half the
# transcripts and stopped must score as half missing, not as a diff that
# happened to be empty over the files that exist.
#
# Root does the reading, and the transcripts directory belongs to the uid that
# ran the probe -- so a transcript is believed only if it is a regular file in
# its own right. A symbolic link there would have root read whatever it points
# at, the root-only recording included, and compare that with itself; a FIFO
# would block the read. Either is counted as a transcript that differs, and
# nothing is opened through it. Nothing of that uid is alive by now
# (as_untrusted reaps it), so the entry cannot change between the test and the
# read.
produced=0
match=0
differ=0
missing=0
while IFS= read -r id; do
   [ -n "${id}" ] || continue
   flat="$(printf '%s' "${id}" | tr '/' '.')"
   got="${transcripts}/${flat}.txt"
   want="${rec}/${flat}.txt"
   if [ ! -f "${want}" ]; then
      fatal "no recording for id ${id} (looked for ${want})"
   fi
   if [ -L "${got}" ] || { [ -e "${got}" ] && [ ! -f "${got}" ]; }; then
      note "refused ${flat}.txt: not a regular file"
      produced=$((produced + 1))
      say "behavior.differ[${differ}]=${id}"
      differ=$((differ + 1))
      continue
   fi
   if [ ! -f "${got}" ]; then
      say "behavior.missing[${missing}]=${id}"
      missing=$((missing + 1))
      continue
   fi
   produced=$((produced + 1))
   if cmp -s "${got}" "${want}"; then
      match=$((match + 1))
   else
      say "behavior.differ[${differ}]=${id}"
      differ=$((differ + 1))
   fi
done < "${ids}"

say "behavior.produced=${produced}"
say "behavior.match=${match}"
note "expected=${expected} produced=${produced} match=${match} differ=${differ} missing=${missing}"
exit 0

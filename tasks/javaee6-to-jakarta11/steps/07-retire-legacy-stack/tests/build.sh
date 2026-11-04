#!/usr/bin/env bash
# Build the candidate tree with the verifier's own toolchain and find what it
# produced.
#
# Per-step copy: every step's tests/ holds an identical copy of this file;
# keep them in sync.
#
# The commands are the ones the instruction names and nothing else -- the
# project's own build tool, run from the repository root. The verifier supplies
# the JDK and the Maven, because a candidate that edits its own toolchain must
# not be able to edit the one that grades it.
#
# Writes, all under /score/out:
#   artifact.path   absolute path of the deployable artifact, or an empty file
#   build.rc        exit status of `package`
#   tests.rc        exit status of `test`
#   tests.tsv       one line per surefire report: tests, failures, errors, skips
# and the full transcripts to /score/log/.
set -u
source /tests/helpers.sh
cd /app

: "${ARTIFACT_KIND:?tests/Dockerfile must set ARTIFACT_KIND to war or bootjar}"
: "${M2_REPO:=/opt/m2}"

# Build output the tree arrived with is deleted before anything is built or
# scanned: the image warms third-party caches only and project code is built
# from clean, and here that is load-bearing rather than tidy. `class-release`
# asks the major version of every class under every `target/` and asks
# `exactly`, so a single class file left over from a run the candidate made
# before fixing the release flag fails a tree that is in fact correct -- and a
# class file is just a file, so one could also be shipped by hand. The set
# removed is the same set scan.sh goes on to scan, the maxdepth-3 `target/`
# directories, so everything graded is what this image's Maven produced from
# the candidate's source. `mvn clean`
# would be the idiomatic spelling and is not used: the clean plugin's version is
# the candidate's to manage, and an instrument the thing under test gets to
# choose is not an instrument.
stale=0
while IFS= read -r d; do
    rm -rf "$d" && stale=$((stale + 1))
done < <(find /app -maxdepth 3 -type d -name target -prune -print 2>/dev/null)
record build.stale-target-dirs "$stale"

# Test reports are evidence only when this build wrote them. `tests.tsv` below
# reads every `TEST-*.xml` under a `surefire-reports` or `failsafe-reports`
# directory, wherever it sits, so any such directory the tree arrived with --
# including one outside the `target/` directories removed above -- is removed
# here, and a suite that did not run cannot be stood in for by reports
# committed beside it.
reports=0
while IFS= read -r d; do
    rm -rf "$d" && reports=$((reports + 1))
done < <(find /app -type d \( -name surefire-reports -o -name failsafe-reports \) \
              -not -path '*/.git/*' -prune -print 2>/dev/null)
record build.stale-report-dirs "$reports"

give_app_to_unprivileged
: > /score/out/artifact.path

MVN=(mvn -B -ntp -Dmaven.repo.local="$M2_REPO" -Dstyle.color=never)

say "package"
# Maven reads no configuration of the candidate's beyond the poms (helpers.sh,
# maven_inputs_reset and run_unprivileged).
maven_inputs_reset /app package
# Bounded: the candidate's own build must not outlast the verifier.
# The guard rides along in report mode only (no plugin named, nothing
# changed): it logs what this session is -- the active profiles per project,
# and each compiler execution's effective configuration -- which the
# instrument runs are held to and which lint-is-enforced reads (helpers.sh,
# guard_report). The command is helpers.sh's SCORED_PACKAGE, which the two
# instrument runs start from as well.
run_unprivileged MAVEN_OPTS="-Xmx3g -Djava.awt.headless=true" timeout -k 30 600 \
    "${SCORED_PACKAGE[@]}" package > /score/log/build.log 2>&1
rc=$?
record build.rc "$rc"
echo "$rc" > /score/out/build.rc
say "package rc=$rc ($(wc -l < /score/log/build.log) lines)"
guard_report /score/log/build.log javac > /score/out/compiler.jsonl
guard_report /score/log/build.log profiles | LC_ALL=C sort \
    > /score/out/profiles.package.txt
record build.guard "$(wc -l < /score/out/profiles.package.txt) project(s), $(wc -l < /score/out/compiler.jsonl) compiler execution(s)"

# The tests the tree ships, run by the scorer with its own Maven and JDK. Kept
# separate from `package` so a failing test never hides a failing build: the
# behaviour suite needs the artifact, and a combined command would not produce
# one.
say "test"
# Nothing the package run started is still running, and nothing it wrote for
# Maven to read (a .mvn/, a ~/.m2/settings.xml) is read by this one.
reap_unprivileged
maven_inputs_reset /app test
run_unprivileged MAVEN_OPTS="-Xmx3g -Djava.awt.headless=true" timeout -k 30 600 \
    "${MVN[@]}" test > /score/log/tests.log 2>&1
rc=$?
record tests.rc "$rc"
echo "$rc" > /score/out/tests.rc
say "test rc=$rc"

# Surefire's own XML, not its console summary: the summary is a human string
# whose wording has changed between plugin versions, and the candidate chooses
# the plugin version. Read as the unprivileged user, in isolated mode: this
# shell's cwd is /app, and a plain `python3 -` there would import a planted
# /app/xml/ ahead of the standard library's; as root that would be the whole
# verifier. Unprivileged, a report symlinked at a root-only file reads as
# unparsable rather than leaking into tests.tsv.
reap_unprivileged
: > /score/out/tests.tsv
run_unprivileged python3 -I - <<'PY' >> /score/out/tests.tsv 2>/dev/null || true
import os
import xml.etree.ElementTree as ET
for base, dirs, files in os.walk("/app"):
    dirs[:] = sorted(d for d in dirs if d != ".git")
    if os.path.basename(base) not in ("surefire-reports", "failsafe-reports"):
        continue
    for f in sorted(files):
        if not (f.startswith("TEST-") and f.endswith(".xml")):
            continue
        try:
            r = ET.parse(os.path.join(base, f)).getroot()
        except Exception:
            print("\t".join([f, "unparsable", "1", "1", "0"]))
            continue
        g = r.attrib.get
        print("\t".join([g("name", f), g("tests", "0"), g("failures", "0"),
                         g("errors", "0"), g("skipped", "0")]))
PY
record tests.reports "$(wc -l < /score/out/tests.tsv)"

# ---------------------------------------------------------------------------
# What did it build?
#
# Located by shape, never by name: the artifact id is the candidate's to
# choose, and a step that renamed a module would otherwise score zero for a
# reason the instruction never mentioned.
# ---------------------------------------------------------------------------
find_war() {
    find /app -maxdepth 4 -type f -name '*.war' -path '*/target/*' \
        -not -path '*/target/*/*' -printf '%s\t%p\n' 2>/dev/null \
        | sort -nr | head -1 | cut -f2
}

find_bootjar() {
    # Which file is the deployable -- not whether it is a good one. Reading
    # the manifest rather than matching a filename is what makes this survive
    # the candidate renaming the module, and what keeps a plain library jar --
    # of which this build produces several -- from being mistaken for the
    # deployable.
    #
    # Either attribute is enough, and that is deliberately looser than
    # score.py's `executable-jar` gate, which wants `Spring-Boot-Version` and
    # Boot's loader at the root of the jar and a `Main-Class` pointing at it.
    # The two do not disagree by accident: discovery is not a gate. A tree
    # that ported everything except the framework and made its jar executable
    # some other way should have its behaviour measured and then be judged on
    # the migration rows. Tightening this test would instead score it 0 of 428
    # observations for a server that answers every request, and
    # `reward = migration * behaviour` would turn one mistake into a reward of
    # exactly zero. "Is this a Boot jar" is `executable-jar`'s question; this
    # function only decides which file to ask it about.
    local best= bestsize=0 j sz
    while IFS= read -r j; do
        unzip -p "$j" META-INF/MANIFEST.MF 2>/dev/null \
            | tr -d '\r' \
            | grep -qE '^(Spring-Boot-Version|Start-Class):' || continue
        sz=$(stat -c %s "$j")
        if [ "$sz" -gt "$bestsize" ]; then best="$j"; bestsize="$sz"; fi
    done < <(find /app -maxdepth 4 -type f -name '*.jar' -path '*/target/*' \
                  -not -path '*/target/*/*' \
                  -not -name '*-sources.jar' -not -name '*-javadoc.jar' \
                  -not -name '*-tests.jar' 2>/dev/null)
    printf '%s' "$best"
}

case "$ARTIFACT_KIND" in
    war)     ART=$(find_war) ;;
    bootjar) ART=$(find_bootjar) ;;
    *)       say "unknown ARTIFACT_KIND=$ARTIFACT_KIND"; ART= ;;
esac

if [ -n "${ART:-}" ] && [ -s "$ART" ]; then
    printf '%s' "$ART" > /score/out/artifact.path
    record artifact "$ART ($(stat -c %s "$ART") bytes)"
    say "artifact $ARTIFACT_KIND $ART"
else
    record artifact "NONE ($ARTIFACT_KIND)"
    say "no $ARTIFACT_KIND artifact; last 40 lines of the build follow"
    tail -40 /score/log/build.log >&2 || true
fi
exit 0

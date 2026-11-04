#!/usr/bin/env bash
#
# Reference solution, step 01 -- Java 11 `--release`, Maven 3.9-era build,
# JUnit 5 Jupiter.
#
# The rung is a diff against the pinned tree and is shipped as one: `tree/`
# holds the 40 files whose contents change, `REMOVED` names what goes away.
# Nothing here is generated at solve time, so the oracle run and the tree in
# git are the same bytes, and `tree.sha256` is what says so out loud -- if a
# copy silently fails, or /app turns out to be empty (the 17-byte build.log
# failure mode), the manifest check below exits non-zero instead of letting a
# partial overlay be scored as a solution.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="${1:-/app}"

[ -d "$app" ] || { echo "solve: no $app" >&2; exit 1; }
[ -f "$app/pom.xml" ] || { echo "solve: $app holds no reactor root" >&2; exit 1; }

# --- things this rung deletes -------------------------------------------
#
# HookTestCase is abstract, declares no test method, has no subclass anywhere
# in the reactor, and is the only thing that pulls in
# com.madgag:org.eclipse.jgit.junit -- a test jar that puts junit:junit back
# onto the test classpath unless it is declared with that exclusion. It is
# removed rather than ported; `tools` is the only file in that package.
#
# Porting it is equally legal, and takes both halves rather than one: the file
# names `org.junit.Assert` and `org.junit.Before`, so the class has to be
# rewritten onto JUnit 5 *and* the fork's test jar has to be declared with
# `junit:junit` among its exclusions. The rewrite alone leaves `junit:junit` on
# the resolved test classpath; the exclusion alone leaves a JUnit 4 type named
# in the source and in the compiled class. Deleting a file that nothing calls
# clears both at once, which is why this rung deletes it.
rm -rf "$app/mini-git-server-server/src/test/java/com/google/gerrit/server/tools"

# --- things this rung rewrites ------------------------------------------
tar -C "$here/tree" -cf - . | tar -C "$app" -xf -

# --- assert every file landed -------------------------------------------
#
# Byte-for-byte against the manifest, in $app and not in tree/, so that a
# checksum that passes is evidence about the workspace the verifier will grade
# and not about the copy that was read a line ago.
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

if [ -e "$app/mini-git-server-server/src/test/java/com/google/gerrit/server/tools" ]; then
  echo "solve: HookTestCase package still present" >&2
  missing=$((missing + 1))
fi

expected="$(wc -l < "$here/tree.sha256")"
echo "solve: $expected files in manifest, $missing did not land"
[ "$missing" -eq 0 ]

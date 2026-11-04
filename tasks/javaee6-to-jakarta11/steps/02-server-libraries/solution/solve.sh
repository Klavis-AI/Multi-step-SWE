#!/usr/bin/env bash
#
# Reference solution, step 02 -- upstream JGit 5.13, Apache MINA SSHD 2.x, a
# container-provided Servlet 3.1 API, and the two client libraries that were
# only ever on the classpath to parse things the server libraries can now
# parse themselves.
#
# Cumulative, like every rung after the first: /app arrives holding whatever
# step 01 left, and this overlays the 29 files step 02 changes plus the one it
# adds. Nothing is generated at solve time, so the oracle run and the tree in
# git are the same bytes; `tree.sha256` is what says so out loud -- if a copy
# silently fails, or /app turns out to be empty (the 17-byte build.log failure
# mode), the manifest check below exits non-zero rather than letting a partial
# overlay be scored as a solution.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="${1:-/app}"

[ -d "$app" ] || { echo "solve: no $app" >&2; exit 1; }
[ -f "$app/pom.xml" ] || { echo "solve: $app holds no reactor root" >&2; exit 1; }

# The jsch host-key class this rung stops depending on is replaced by one of
# this project's own, which `tree/` adds; the libraries it drops are dropped in
# the poms, which `tree/` rewrites.
#
# One thing does have to go, and only because /app is whatever the previous
# rung left rather than a tree this solution wrote. `com.madgag:org.eclipse
# .jgit.junit` is the fork's test jar, and the hook tests under
# `server/src/test/java/.../tools` are the only code that uses it. Step 01's
# reference solution removed them there; a step-01 tree that instead kept them
# arrives here with them still in place, and this rung's requirement that
# nothing resolve under the fork's group means they cannot stay.
#
# Keeping them is a correct way to clear that rung, and it takes two things
# rather than one, because the file as handed over names `org.junit.Assert`
# and `org.junit.Before`. `HookTestCase` has to be rewritten onto JUnit 5,
# *and* the fork's test jar has to be declared with `junit:junit` among its
# exclusions, because it drags JUnit 4 in behind it whatever the source says.
# Either half alone still fails step 01: the rewrite without the exclusion
# leaves `junit:junit` on the resolved test classpath, and the exclusion
# without the rewrite leaves a JUnit 4 type named in the source and in the
# compiled class. A blind step-01 run did both -- its `HookTestCase` imports
# `org.junit.jupiter.api.Assertions.fail` and
# `org.junit.jupiter.api.BeforeEach`, and its `mini-git-server-server/pom.xml`
# declares the test jar at test scope with `com.madgag:org.eclipse.jgit` and
# `junit:junit` excluded -- and what that file still imports is
# `org.eclipse.jgit.junit.LocalDiskRepositoryTestCase`, from the group this
# rung retires. That is the tree the line below exists for.
#
# The removal is unconditional because it has to be idempotent: on the
# reference step-01 tree the directory is already gone. `tests_min` is 19 and
# `test_classes` names the five that remain, so nothing this rung is scored on
# depends on the tests being removed here.
rm -rf "$app/mini-git-server-server/src/test/java/com/google/gerrit/server/tools"

# --- things this rung rewrites and adds ---------------------------------
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

expected="$(wc -l < "$here/tree.sha256")"
echo "solve: $expected files in manifest, $missing did not land"
[ "$missing" -eq 0 ]

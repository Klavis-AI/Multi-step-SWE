#!/usr/bin/env bash
#
# Reference solution, step 05 -- the servlet namespace moves. `javax.servlet`
# becomes `jakarta.servlet` in the seven files that name it, and the three
# version properties that decide the platform move with it: Spring Boot 2.7.18
# to 3.5.16, JGit 5.13 to 7.8.0, and `maven.compiler.release` 11 to 17.
#
# Ten files overlaid and nothing retired, which makes this the smallest solve
# in the task and is the point: the edit is mechanical and the difficulty is
# entirely in knowing which `javax` moves and which does not. The `javax.xml`
# and `javax.net` imports in `HtmlDomUtil` and `BlindSSLSocketFactory` are
# untouched here on purpose -- they are java.se packages with no jakarta
# spelling, and `jdk-javax-intact` is the gate that says so.
#
# Cumulative, like every rung after the first: /app arrives holding whatever
# step 04 left. Nothing is generated at solve time, so the oracle run and the
# tree in git are the same bytes; `tree.sha256` is what says so out loud -- if
# a copy silently fails, or /app turns out to be empty (the 17-byte build.log
# failure mode), the manifest check below exits non-zero rather than letting a
# partial overlay be scored as a solution.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="${1:-/app}"

[ -d "$app" ] || { echo "solve: no $app" >&2; exit 1; }
[ -f "$app/pom.xml" ] || { echo "solve: $app holds no reactor root" >&2; exit 1; }
[ -s "$here/tree.sha256" ] || { echo "solve: no manifest beside solve.sh" >&2; exit 1; }

# --- what this rung rewrites --------------------------------------------
#
# Driven from the manifest rather than from the directory, for the reason step
# 04's solve.sh records at length: Harbor's oracle agent uploads each step's
# `solution/` into one shared directory inside the environment
# (`harbor/agents/oracle.py` calls `upload_dir` into `env_paths.solution_dir`;
# on Docker that is `docker compose cp <dir>/. <service>:<target>`, which
# merges and never deletes). By the time this rung runs, `$here/tree` is the
# union of steps 01 through 05, so it still holds step 01's `UrlModule.java`,
# step 02's `SshModule.java` and step 03's `WEB-INF/web.xml` -- every one of
# them retired by a later rung. `tar -cf - .` over that directory would put
# them all back.
#
# The manifest names exactly the ten paths step 05 owns, so extracting by name
# overlays this rung and nothing else. Where an earlier rung shipped the same
# path -- `pom.xml` is in all five trees, `mini-git-server-httpd/pom.xml` in
# four -- the merged directory holds the last upload, which is this one, and
# the checksum pass below is what proves it.
mani() { sed 's/^[0-9a-f]\{64\}  //' "$here/tree.sha256"; }
mani | tar -C "$here/tree" --no-recursion -T - -cf - | tar -C "$app" -xf -

# --- what this rung retires ---------------------------------------------
#
# Nothing. This is the only rung in the task with no removal list: a rename is
# not a deletion, every file that named a servlet type still names one, and
# §1.5 is explicit that the module holding every servlet in this application
# does not get smaller. The assertions below are correspondingly about what is
# still there rather than about what is gone.

# --- assert every edit landed -------------------------------------------
#
# Byte-for-byte against the manifest, in $app and not in tree/, so that a
# checksum that passes is evidence about the workspace the verifier will grade
# and not about the copy that was read a line ago. It is also what catches a
# merged solution directory serving an earlier rung's `pom.xml` under a path
# this rung shares with it.
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

# The rung's own postcondition, asserted on the workspace rather than inferred
# from the manifest: no .java in the tree names `javax.servlet`, and the files
# that used to name it name `jakarta.servlet` now. A checksum pass already
# implies this for the ten paths above; what it does not imply is that no
# *other* file in the merged tree names the old package, which is exactly the
# failure a partially-applied overlay produces.
oldns="$(grep -rl 'javax\.servlet' --include='*.java' "$app" 2>/dev/null | sort || true)"
if [ -n "$oldns" ]; then
  echo "solve: STILL ON javax.servlet:" >&2
  printf '  %s\n' $oldns >&2
  missing=$((missing + $(printf '%s\n' "$oldns" | wc -l)))
fi

newns="$(grep -rl 'jakarta\.servlet' --include='*.java' "$app" 2>/dev/null | wc -l)"
if [ "$newns" -ne 7 ]; then
  echo "solve: expected 7 files naming jakarta.servlet, found $newns" >&2
  missing=$((missing + 1))
fi

# The other half of §1.1, and the one a blanket rename breaks. These two files
# name java.se packages that have no jakarta spelling; a solve that touched
# them would still pass every checksum above, because neither is in the
# manifest, and would fail to compile.
for f in mini-git-server-httpd/src/main/java/com/google/gerrit/httpd/HtmlDomUtil.java \
         mini-git-server-util-ssl/src/main/java/com/google/gerrit/util/ssl/BlindSSLSocketFactory.java; do
  [ -f "$app/$f" ] || { echo "solve: MISSING (not this rung's to touch) $f" >&2; missing=$((missing + 1)); continue; }
  grep -q 'javax\.\(xml\|net\)' "$app/$f" || {
    echo "solve: JDK javax renamed out of $f" >&2; missing=$((missing + 1)); }
done

# The three properties that move the platform. Asserted by value rather than
# by checksum so that the failure message names which one did not land: a root
# pom that took the jakarta imports but kept Boot 2.7 is a tree that does not
# compile, and the compiler error names a Spring class rather than the pom.
for kv in 'springBootVersion>3\.' 'jgitVersion>7\.' 'maven.compiler.release>17<'; do
  grep -q "$kv" "$app/pom.xml" || {
    echo "solve: root pom missing $kv" >&2; missing=$((missing + 1)); }
done

# The four rungs below this one retired Guice, the war module and the
# container bootstrap classes, and a merged solution directory is the one
# thing that could quietly bring a piece of any of them back. Nothing here
# extracts those paths, so this is a tripwire rather than a fix: if it ever
# fires, the extraction above went back to copying the whole directory.
stale="$(find "$app" -path '*/target' -prune -o -type f \
           \( -name 'UrlModule.java' -o -name 'WebModule.java' \
              -o -name 'Lifecycle*.java' -o -name 'FactoryModule.java' \
              -o -name '*GlobalModule.java' -o -name 'GerritRequestModule.java' \
              -o -name 'GerritServerConfigModule.java' \
              -o -name 'CanonicalWebUrlModule.java' -o -name 'CommandModule.java' \
              -o -name 'CommandName.java' -o -name '*SshModule.java' \
              -o -name '*DefaultCommandModule.java' \
              -o -name 'WebAppInitializer.java' -o -name 'UrlMapping.java' \
              -o -name 'web.xml' \) -print | sort)"
if [ -n "$stale" ]; then
  echo "solve: STALE (retired before this rung, back in the tree):" >&2
  printf '  %s\n' $stale >&2
  missing=$((missing + $(printf '%s\n' "$stale" | wc -l)))
fi

if [ -e "$app/mini-git-server-war" ]; then
  echo "solve: NOT REMOVED: mini-git-server-war is back" >&2
  missing=$((missing + 1))
fi

expected="$(wc -l < "$here/tree.sha256")"
echo "solve: $expected files in manifest, $missing did not land"
[ "$missing" -eq 0 ]

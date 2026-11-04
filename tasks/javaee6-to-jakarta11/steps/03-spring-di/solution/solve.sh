#!/usr/bin/env bash
#
# Reference solution, step 03 -- Guice 2.0 out, Spring Framework 5.3 Java
# configuration in. Still a WAR, still javax.servlet, still JDK 11.
#
# Cumulative, like every rung after the first: /app arrives holding whatever
# step 02 left, and this overlays the 63 files step 03 rewrites plus the 8 it
# adds, then removes the 18 it retires. Nothing is generated at solve time, so
# the oracle run and the tree in git are the same bytes; `tree.sha256` is what
# says so out loud -- if a copy silently fails, or /app turns out to be empty
# (the 17-byte build.log failure mode), the manifest check below exits
# non-zero rather than letting a partial overlay be scored as a solution.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="${1:-/app}"

[ -d "$app" ] || { echo "solve: no $app" >&2; exit 1; }
[ -f "$app/pom.xml" ] || { echo "solve: $app holds no reactor root" >&2; exit 1; }

# --- things this rung rewrites and adds ---------------------------------
#
# This comes first, and the removals come after it, which is the opposite of
# the order the prose above reads in. The reason is Harbor's oracle agent: it
# uploads each step's `solution/` into one shared directory inside the
# environment (`harbor/agents/oracle.py`, `upload_dir` into
# `env_paths.solution_dir`) and the upload merges rather than replaces. By the
# time this rung runs, `$here/tree` is the union of steps 01, 02 and 03's
# trees, not step 03's alone -- step 01 ships `UrlModule.java` and
# `LifecycleManager.java`, step 02 ships `CommandModule.java`,
# `SshModule.java`, `ToySshModule.java` and `web.xml`, and all six are on this
# rung's removal list. Remove first and the tar puts them straight back.
#
# Extracting first and removing after is correct either way round: none of the
# eighteen paths below appears in this step's own `tree/`, so the removals can
# never undo an edit this overlay just made, and the order holds whether the
# solution directory was merged or not.
tar -C "$here/tree" -cf - . | tar -C "$app" -xf -

# --- what this rung retires ---------------------------------------------
#
# Seventeen Java files and one descriptor, and every one of them is Guice's.
#
# Fifteen are `AbstractModule` subclasses -- the twelve that stood alone in a
# file and, with them, the `FactoryModule` base that generated the
# assisted-inject factory and the `CommandName` binding annotation that only
# `CommandModule` and `Commands.NestedCommandNameImpl` ever named. The three
# nested inside the class they configure (`ProjectServlet.Module`,
# `RequireSslFilter.Module`, `CacheModule`'s inner binder) are not here,
# because deleting a nested class is an edit to the file that holds it and
# those files are in `tree/`.
#
# The `com.google.gerrit.lifecycle` package is the fourth kind: an interface, a
# manager that walked a list of its implementors up and back down, and a module
# that existed to give Guice a multibinder to put them in. Spring's container
# starts `SmartLifecycle` beans at refresh in ascending phase and stops them at
# close in descending phase, which is the same order from the other side, so
# the package has no work left.
#
# And `WEB-INF/web.xml`, whose entire content is `GuiceFilter`, its `/*`
# mapping and the Guice context listener. A Servlet 3.1 container finds the
# application's own registrations without it, which is the capability step 02
# bought and this step is the first to spend. `WEB-INF/extra/jetty7/` stays,
# so the directory does not go empty.
#
# Unconditional, so that this is idempotent and so that a partially-solved tree
# reaches the same place as a freshly-handed-over one.
rm -f \
  "$app/mini-git-server-httpd/src/main/java/com/google/gerrit/httpd/UrlModule.java" \
  "$app/mini-git-server-httpd/src/main/java/com/google/gerrit/httpd/WebModule.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/lifecycle/LifecycleListener.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/lifecycle/LifecycleManager.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/lifecycle/LifecycleModule.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/config/CanonicalWebUrlModule.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/config/FactoryModule.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/config/GerritGlobalModule.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/config/GerritRequestModule.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/config/GerritServerConfigModule.java" \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/config/ToyGerritGlobalModule.java" \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/CommandModule.java" \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/CommandName.java" \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/SshModule.java" \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/ToySshModule.java" \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/commands/DefaultCommandModule.java" \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/commands/ToyDefaultCommandModule.java" \
  "$app/mini-git-server-war/src/main/webapp/WEB-INF/web.xml"

# The package directory itself, now that nothing is in it. `rmdir` and not
# `rm -rf`, so that a tree which kept a file here that this rung did not name
# stops the solve rather than losing it silently.
rmdir "$app/mini-git-server-server/src/main/java/com/google/gerrit/lifecycle" 2>/dev/null || true

# --- assert every edit landed -------------------------------------------
#
# Byte-for-byte against the manifest, in $app and not in tree/, so that a
# checksum that passes is evidence about the workspace the verifier will grade
# and not about the copy that was read a line ago. The removals are asserted
# too: a `rm -f` that matched nothing is silent, and on this rung an unremoved
# module class is the difference between a port and a tree with two containers
# in it.
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

left="$(find "$app" -path '*/target' -prune -o -type f \
          \( -name 'UrlModule.java' -o -name 'WebModule.java' \
             -o -name 'Lifecycle*.java' -o -name 'CanonicalWebUrlModule.java' \
             -o -name 'FactoryModule.java' -o -name 'GerritGlobalModule.java' \
             -o -name 'GerritRequestModule.java' \
             -o -name 'GerritServerConfigModule.java' \
             -o -name 'ToyGerritGlobalModule.java' -o -name 'CommandModule.java' \
             -o -name 'CommandName.java' -o -name 'SshModule.java' \
             -o -name 'ToySshModule.java' -o -name 'DefaultCommandModule.java' \
             -o -name 'ToyDefaultCommandModule.java' -o -name 'web.xml' \) \
          -print | sort)"
if [ -n "$left" ]; then
  echo "solve: NOT REMOVED:" >&2
  printf '  %s\n' $left >&2
  missing=$((missing + $(printf '%s\n' "$left" | wc -l)))
fi

expected="$(wc -l < "$here/tree.sha256")"
echo "solve: $expected files in manifest, $missing did not land"
[ "$missing" -eq 0 ]

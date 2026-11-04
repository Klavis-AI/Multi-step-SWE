#!/usr/bin/env bash
#
# Reference solution, step 04 -- the WAR becomes a Spring Boot 2.7 executable
# jar. Still javax.servlet 3.1, still Spring Framework 5.3, still JDK 11.
#
# Cumulative, like every rung after the first: /app arrives holding whatever
# step 03 left, and this overlays the 3 files step 04 rewrites plus the 13 the
# renamed module brings, then removes the 15 it retires. Nothing is generated
# at solve time, so the oracle run and the tree in git are the same bytes;
# `tree.sha256` is what says so out loud -- if a copy silently fails, or /app
# turns out to be empty (the 17-byte build.log failure mode), the manifest
# check below exits non-zero rather than letting a partial overlay be scored
# as a solution.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="${1:-/app}"

[ -d "$app" ] || { echo "solve: no $app" >&2; exit 1; }
[ -f "$app/pom.xml" ] || { echo "solve: $app holds no reactor root" >&2; exit 1; }
[ -s "$here/tree.sha256" ] || { echo "solve: no manifest beside solve.sh" >&2; exit 1; }

# --- things this rung rewrites and adds ---------------------------------
#
# Driven from the manifest rather than from the directory, which on this rung
# is the difference between a solve that works and one that undoes the rung
# before it. Harbor's oracle agent uploads each step's `solution/` into one
# shared directory inside the environment (`harbor/agents/oracle.py` calls
# `upload_dir` into `env_paths.solution_dir`; on Docker that is
# `docker compose cp <dir>/. <service>:<target>`, which merges and never
# deletes). So by the time this rung runs, `$here/tree` is the union of steps
# 01 through 04, not step 04's tree alone -- it still holds step 01's
# `UrlModule.java` and `lifecycle/LifecycleManager.java`, step 02's
# `CommandModule.java`, `SshModule.java`, `ToySshModule.java` and
# `WEB-INF/web.xml`, and all six were retired by step 03. `tar -cf - .` over
# that directory would put every one of them back, and this rung would then
# have to re-state a removal list that grows with every rung after it.
#
# The manifest names exactly the sixteen paths step 04 owns, so extracting by
# name overlays this rung and nothing else. Where an earlier rung shipped the
# same path -- `pom.xml` is in all four trees -- the merged directory holds the
# last upload, which is this one, and the checksum pass below is what proves
# it. The removals still come after the extraction, so that the order is right
# whichever way the solution directory was assembled.
mani() { sed 's/^[0-9a-f]\{64\}  //' "$here/tree.sha256"; }
mani | tar -C "$here/tree" --no-recursion -T - -cf - | tar -C "$app" -xf -

# --- what this rung retires ---------------------------------------------
#
# `mini-git-server-war` in its entirety, because a reactor that builds an
# executable jar has no war module: `mini-git-server-app` in the overlay above
# is the same module under the name that now describes it, carrying the same
# `.gitignore`, the same four Eclipse preference files and the same
# `log4j.properties`, a `pom.xml` that packages a jar and repackages it, and
# four new classes under `com.google.gerrit.app` where `WebAppInitializer` used
# to be the single entry point.
#
# `WebAppInitializer` goes because its whole job was the servlet container's
# side of the deployment -- implementing `WebApplicationInitializer` so the
# container would find it, and being handed a live `ServletContext` to install
# into. The four contexts it created, their parenting, the sibling bean one
# borrows from another and the order they close in are not retired with it;
# `MiniGitServer` in the overlay is where they live now.
#
# `src/main/webapp` goes because a jar has no such thing. `WEB-INF/web.xml`
# was already dead at step 03 and is named here only so that this script is
# idempotent against a tree solved by any route. `WEB-INF/extra/jetty7/` is the
# deployment recipe for an external Jetty 7 -- a start script and two XML files
# configuring a server that is now inside the artifact. `favicon.ico` and
# `robots.txt` are the two files the deployable actually publishes, and they
# are not deleted at all: they moved to `src/main/resources/static/`, which is
# where a Boot application serves static content from and, unlike
# `src/main/webapp`, is somewhere a jar's classpath can reach.
#
# `UrlMapping` goes for the same reason as `WebAppInitializer`'s bootstrap
# half: it was the `ServletContext.addServlet`/`addFilter` call sequence, and
# with registration beans in `WebConfiguration` the container is told the same
# five things through the object graph instead. Nothing it decided changes --
# the names, the patterns, the filter order and the condition on the SSL
# filter all reappear in `WebConfiguration`.
#
# Unconditional, so that this is idempotent and so that a partially-solved tree
# reaches the same place as a freshly-handed-over one.
rm -f \
  "$app/mini-git-server-httpd/src/main/java/com/google/gerrit/httpd/UrlMapping.java" \
  "$app/mini-git-server-war/.gitignore" \
  "$app/mini-git-server-war/.settings/org.eclipse.core.resources.prefs" \
  "$app/mini-git-server-war/.settings/org.eclipse.core.runtime.prefs" \
  "$app/mini-git-server-war/.settings/org.eclipse.jdt.core.prefs" \
  "$app/mini-git-server-war/.settings/org.eclipse.jdt.ui.prefs" \
  "$app/mini-git-server-war/pom.xml" \
  "$app/mini-git-server-war/src/main/java/com/google/gerrit/httpd/WebAppInitializer.java" \
  "$app/mini-git-server-war/src/main/resources/log4j.properties" \
  "$app/mini-git-server-war/src/main/webapp/WEB-INF/web.xml" \
  "$app/mini-git-server-war/src/main/webapp/WEB-INF/extra/jetty7/gerrit-jetty.sh" \
  "$app/mini-git-server-war/src/main/webapp/WEB-INF/extra/jetty7/gerrit.xml" \
  "$app/mini-git-server-war/src/main/webapp/WEB-INF/extra/jetty7/jetty_sslproxy.xml" \
  "$app/mini-git-server-war/src/main/webapp/favicon.ico" \
  "$app/mini-git-server-war/src/main/webapp/robots.txt"

# The module directory itself, now that the fifteen files above have named
# everything in it. `-type d -empty` and not `rm -rf`, so that a tree holding a
# file here that this rung did not name keeps its directory alive and fails the
# assertion below, rather than losing the file silently. (`-delete` implies
# `-depth`, so the single pass clears the subtree from the leaves upward.) A
# `target/` left by an earlier build is the one thing that legitimately
# survives the named list, so it is swept first -- it is build output, and
# `build.sh` deletes every module's `target/` before it packages anyway.
rm -rf "$app/mini-git-server-war/target"
find "$app/mini-git-server-war" -mindepth 1 -type d -empty -delete 2>/dev/null || true
rmdir "$app/mini-git-server-war" 2>/dev/null || true

# --- assert every edit landed -------------------------------------------
#
# Byte-for-byte against the manifest, in $app and not in tree/, so that a
# checksum that passes is evidence about the workspace the verifier will grade
# and not about the copy that was read a line ago. It is also what catches a
# merged solution directory serving an earlier rung's `pom.xml` under a path
# this rung shares with it. The removals are asserted too: a `rm -f` that
# matched nothing is silent, and on this rung a surviving `WebAppInitializer`
# is a second bootstrap path in a tree that is supposed to have exactly one.
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

# Two separate assertions, because the module directory and the retired class
# names fail independently: a leftover `WebAppInitializer.java` copied into
# some other module would pass the first and not the second.
if [ -e "$app/mini-git-server-war" ]; then
  echo "solve: NOT REMOVED: mini-git-server-war still exists" >&2
  find "$app/mini-git-server-war" -type f -printf '  %p\n' >&2
  missing=$((missing + 1))
fi

left="$(find "$app" -path '*/target' -prune -o -type f \
          \( -name 'WebAppInitializer.java' -o -name 'UrlMapping.java' \
             -o -name 'web.xml' \) -print | sort)"
if [ -n "$left" ]; then
  echo "solve: NOT REMOVED:" >&2
  printf '  %s\n' $left >&2
  missing=$((missing + $(printf '%s\n' "$left" | wc -l)))
fi

# The three rungs below this one retired Guice, and a merged solution directory
# is the one thing that could quietly bring a piece of it back. Nothing here
# extracts those paths any more, so this is a tripwire rather than a fix: if it
# ever fires, the extraction above went back to copying the whole directory.
stale="$(find "$app" -path '*/target' -prune -o -type f \
           \( -name 'UrlModule.java' -o -name 'WebModule.java' \
              -o -name 'Lifecycle*.java' -o -name 'FactoryModule.java' \
              -o -name '*GlobalModule.java' -o -name 'GerritRequestModule.java' \
              -o -name 'GerritServerConfigModule.java' \
              -o -name 'CanonicalWebUrlModule.java' -o -name 'CommandModule.java' \
              -o -name 'CommandName.java' -o -name '*SshModule.java' \
              -o -name '*DefaultCommandModule.java' \) -print | sort)"
if [ -n "$stale" ]; then
  echo "solve: STALE (retired before this rung, back in the tree):" >&2
  printf '  %s\n' $stale >&2
  missing=$((missing + $(printf '%s\n' "$stale" | wc -l)))
fi

# The reactor has to agree that the module was renamed and not merely copied:
# a root pom still listing `mini-git-server-war` would fail the build outright,
# and one listing both would build the war again.
if grep -q '<module>mini-git-server-war</module>' "$app/pom.xml"; then
  echo "solve: root pom still lists mini-git-server-war" >&2
  missing=$((missing + 1))
fi

expected="$(wc -l < "$here/tree.sha256")"
echo "solve: $expected files in manifest, $missing did not land"
[ "$missing" -eq 0 ]

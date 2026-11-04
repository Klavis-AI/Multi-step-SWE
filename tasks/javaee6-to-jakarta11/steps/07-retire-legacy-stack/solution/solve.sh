#!/usr/bin/env bash
#
# Reference solution, step 07 -- the deployable stops carrying any 2003-2011
# artifact. Fourteen jars leave `BOOT-INF/lib` (antlr, antlr-runtime, args4j
# 2.0.12, commons-codec 1.4, commons-collections, commons-compress 1.1,
# commons-io, commons-lang, commons-net, ehcache-core, jsr305, oro,
# util-compress and velocity) and five arrive: args4j 2.37 in place of the
# 2009 pin, caffeine with its two annotation dependencies (jspecify and
# error_prone_annotations), and commons-codec 1.18.0 -- which nothing here
# asks for. It comes back under jgit 7, which is the whole point of asking
# this question about the graph and the source rather than about a filename.
# 54 archives before, 45 after.
#
# Seventeen files overlaid and two deleted:
#
#   * four poms lose the retired coordinates -- the root pom its managed
#     versions, the server pom nine direct dependencies, the httpd pom jsr305
#     and the sshd pom commons-codec -- and the server pom declares Caffeine
#     at the version Boot's BOM manages;
#   * the root pom moves args4j from 2.0.12 (2009) to 2.37. The four
#     annotations and the two types this tree uses are spelled the same in
#     both, but the upgrade is not invisible: from 2.0.16 args4j appends
#     `(default: false)` to every boolean option in its generated help, which
#     rewrites nine of the recorded observations. `CmdLineParser.MyParser`
#     passes `ParserProperties.defaults().withShowDefaults(false)` to put the
#     help text back. Nothing in this rung's gates asks for that line -- the
#     behaviour suite does, which is where a question about what the server
#     prints belongs;
#   * the cache package is ported from Ehcache 1.7.2 to Caffeine.
#     `ProxyEhcache` -- 393 lines that re-implemented Ehcache's `Ehcache`
#     interface over a volatile delegate so that a bean could be handed out
#     before the manager existed -- becomes `ProxyCache`, 90 lines over the
#     five operations this project's own `Cache` interface actually needs.
#     `SimpleCache` and `PopulatingCache` are rewritten over it,
#     `SelfPopulatingCache` becoming `Cache.get(key, mappingFunction)`;
#     `CachePool` builds each cache with `Caffeine.newBuilder()` instead of
#     assembling a `net.sf.ehcache.config.Configuration`;
#   * the disk store goes. Caffeine is in-memory only, and everything that
#     configured a disk store goes with it: `CacheModule.disk()`, the
#     `diskLimit` binding, the `cache.directory` read, and the
#     `net.sf.ehcache.skipUpdateCheck` system property. `EvictionPolicy` goes
#     for the same reason -- Caffeine's admission policy is not selectable, so
#     the knob had nothing left to set;
#   * `SshUtil` decodes with `java.util.Base64.getMimeDecoder()` rather than
#     `org.apache.commons.codec.binary.Base64`. Lenient, because the
#     commons-codec call it replaces ignores characters outside the alphabet
#     and an RFC 4716 key arrives folded across lines;
#   * `LocalDiskRepositoryManager` does its startup zip expansion with
#     `java.util.zip` and `java.nio.file` rather than `CompressUtil.unzip`,
#     `FileUtils.listFiles`, `FileUtils.deleteQuietly` and
#     `FilenameUtils.removeExtension`, and loses an unused
#     `org.apache.velocity.texen.util.FileUtil` import;
#   * `GerritGlobalConfiguration` loses `VelocityLifecycle` and the bean
#     method that published it -- the tree renders no template, so the only
#     thing Velocity did here was start up and configure its own logging;
#   * `logback.xml` loses the `net.sf.ehcache` and `velocity` categories,
#     which now name nothing in the deployable.
#
# Cumulative, like every rung after the first: /app arrives holding whatever
# step 06 left. Nothing is generated at solve time, so the oracle run and the
# tree in git are the same bytes; `tree.sha256` is what says so out loud.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="${1:-/app}"

[ -d "$app" ] || { echo "solve: no $app" >&2; exit 1; }
[ -f "$app/pom.xml" ] || { echo "solve: $app holds no reactor root" >&2; exit 1; }
[ -s "$here/tree.sha256" ] || { echo "solve: no manifest beside solve.sh" >&2; exit 1; }

# --- what this rung rewrites --------------------------------------------
#
# Driven from the manifest rather than from the directory, for the reason
# steps 04 to 06 record at length: Harbor's oracle agent uploads each step's
# `solution/` into one shared directory inside the environment, merging and
# never deleting, so by the time this rung runs `$here/tree` is the union of
# steps 01 through 07 and still holds files every later rung retired. The
# manifest names exactly the seventeen paths step 07 owns.
mani() { sed 's/^[0-9a-f]\{64\}  //' "$here/tree.sha256"; }
mani | tar -C "$here/tree" --no-recursion -T - -cf - | tar -C "$app" -xf -

# --- what this rung retires ---------------------------------------------
#
# Two files, and they have to go rather than be emptied. `ProxyEhcache`
# implements `net.sf.ehcache.Ehcache`, so it does not compile once the
# dependency leaves; `EvictionPolicy` is the argument type of a binding method
# that no longer exists.
rm -f "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/cache/ProxyEhcache.java"
rm -f "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/cache/EvictionPolicy.java"

# --- assert every edit landed -------------------------------------------
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

# The rung's own postcondition, asserted on the workspace rather than inferred
# from the manifest. A checksum pass covers the seventeen paths above; what it
# does not cover is whether some *other* file in the merged tree still names a
# retired library -- which is what a partially-applied overlay looks like.
#
# `org.apache.commons.codec` is in this list and `commons-codec` as a
# coordinate is not: jgit 7 brings 1.18.0 in transitively and nothing can or
# should stop it. What the rung asks is that this project's own source no
# longer calls it.
stale="$(grep -rlE 'net\.sf\.ehcache|org\.apache\.velocity|org\.apache\.commons\.(io|lang|net|codec|compress)|com\.madgag\.compress|javax\.annotation\.(Nullable|Nonnull|concurrent)' \
           --include='*.java' "$app" 2>/dev/null | sort || true)"
if [ -n "$stale" ]; then
  echo "solve: STILL NAMES a retired library:" >&2
  printf '  %s\n' $stale >&2
  missing=$((missing + $(printf '%s\n' "$stale" | wc -l)))
fi

# The coordinates, separately from the packages, because a pom that declares
# `ehcache-core` names no Java package at all. `commons-codec` is matched here
# only as a *declaration*: the transitive copy appears in no pom.
coord="$(grep -rlE '<artifactId>(antlr|bcpg-jdk15|commons-codec|commons-io|commons-lang|commons-net|ehcache-core|jsr305|velocity|util-compress|commons-compress)</artifactId>' \
           --include=pom.xml "$app" 2>/dev/null | sort || true)"
if [ -n "$coord" ]; then
  echo "solve: STILL DECLARES a retired coordinate:" >&2
  printf '  %s\n' $coord >&2
  missing=$((missing + $(printf '%s\n' "$coord" | wc -l)))
fi

# The replacement is present and not merely the old thing deleted.
grep -q '<artifactId>caffeine</artifactId>' "$app/mini-git-server-server/pom.xml" || {
  echo "solve: the server pom does not declare caffeine" >&2; missing=$((missing + 1)); }
grep -q 'com\.github\.benmanes\.caffeine' \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/cache/CachePool.java" || {
  echo "solve: CachePool does not build a Caffeine cache" >&2; missing=$((missing + 1)); }
grep -q 'java\.util\.Base64' \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/SshUtil.java" || {
  echo "solve: SshUtil does not decode with java.util.Base64" >&2; missing=$((missing + 1)); }
grep -q 'withShowDefaults(false)' \
  "$app/mini-git-server-util-cli/src/main/java/com/google/gerrit/util/cli/CmdLineParser.java" || {
  echo "solve: CmdLineParser lets args4j 2.37 rewrite the help text" >&2
  missing=$((missing + 1)); }

# The four levels that still steer code in this deployable, asserted by name
# so that a failure says which one did not land. The two this rung drops --
# `net.sf.ehcache` and `velocity` -- are asserted absent in the same loop's
# spirit, below.
for lg in 'level="DEBUG"' 'name="com.google.gerrit" level="INFO"' \
          'name="org.apache.sshd.common" level="WARN"' \
          'name="org.apache.sshd.server" level="INFO"' \
          'name="org.apache.sshd.common.keyprovider.FileKeyPairProvider" level="INFO"'; do
  grep -qF "$lg" "$app/mini-git-server-app/src/main/resources/logback.xml" || {
    echo "solve: logback.xml missing $lg" >&2; missing=$((missing + 1)); }
done
for lg in 'net.sf.ehcache' 'velocity'; do
  if grep -qF "$lg" "$app/mini-git-server-app/src/main/resources/logback.xml"; then
    echo "solve: logback.xml still declares $lg" >&2; missing=$((missing + 1))
  fi
done

# Tripwire, as in the rungs below: nothing here extracts a retired path, so if
# one reappears the extraction went back to copying the whole directory.
back="$(find "$app" -path '*/target' -prune -o -type f \
          \( -name 'UrlModule.java' -o -name 'WebModule.java' \
             -o -name 'web.xml' -o -name '*SshModule.java' \
             -o -name 'WebAppInitializer.java' -o -name 'log4j.properties' \
             -o -name 'ProxyEhcache.java' -o -name 'EvictionPolicy.java' \) \
          -print | sort)"
if [ -n "$back" ]; then
  echo "solve: STALE (retired at or below this rung, back in the tree):" >&2
  printf '  %s\n' $back >&2
  missing=$((missing + $(printf '%s\n' "$back" | wc -l)))
fi

expected="$(wc -l < "$here/tree.sha256")"
echo "solve: $expected files in manifest, $missing did not land"
[ "$missing" -eq 0 ]

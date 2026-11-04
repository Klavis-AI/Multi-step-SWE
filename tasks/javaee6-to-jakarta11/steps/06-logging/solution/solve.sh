#!/usr/bin/env bash
#
# Reference solution, step 06 -- log4j 1.2 is retired and logback takes its
# place. Eight files overlaid and one deleted:
#
#   * four poms lose the `log4j:log4j` coordinate and the root pom loses the
#     managed version and its four exclusions with it;
#   * the app pom loses `slf4j-log4j12` (a relocation to `slf4j-reload4j`, so
#     a second copy of the `org.apache.log4j` packages) and declares no
#     backend at all -- Boot's logging starter is the backend now;
#   * the httpd pom stops excluding that starter, which is where logback and
#     the slf4j API come from;
#   * the sshd pom declares `logback-classic`, because the audit appender is
#     built in Java and so compiles against the backend, and excludes
#     `jcl-over-slf4j` from *both* sshd artifacts;
#   * `SshLog` is ported appender-for-appender;
#   * `GerritGlobalConfiguration` stops naming a log4j Velocity chute;
#   * `log4j.properties` is deleted and `logback.xml` replaces it.
#
# The part of this rung that is not mechanical is the audit log. `SshLog`
# builds its own appender in code rather than declaring it in configuration,
# so there is no XML to translate: `DailyRollingFileAppender` becomes a
# `RollingFileAppender` plus a `TimeBasedRollingPolicy`, `Layout` becomes a
# `LayoutBase<ILoggingEvent>` behind a `LayoutWrappingEncoder`,
# `setThreshold(INFO)` becomes a `ThresholdFilter`, the per-record
# `event.setProperty` calls become one MDC map handed to the event at
# construction, and `ErrorHandler` -- which logback does not have -- becomes a
# started/not-started check plus a status listener. Two of those have a
# default that silently loses records and had to be set:
#
#   * logback's `AsyncAppender` discards events at or below INFO once its
#     queue is four-fifths full. Every audit record is INFO.
#     `setDiscardingThreshold(0)` turns that off.
#   * `%d{yyyy-MM-dd HH:mm:ss,SSS}` in a logback pattern does not mean what
#     it means in log4j: the comma separates the date pattern from a timezone
#     option, so `SSS` is read as a zone ID and the configuration throws
#     `ZoneRulesException: Unknown time-zone ID: SSS` before any appender
#     starts. The pattern has to be quoted.
#
# Cumulative, like every rung after the first: /app arrives holding whatever
# step 05 left. Nothing is generated at solve time, so the oracle run and the
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
# steps 04 and 05 record at length: Harbor's oracle agent uploads each step's
# `solution/` into one shared directory inside the environment, merging and
# never deleting, so by the time this rung runs `$here/tree` is the union of
# steps 01 through 06 and still holds files every later rung retired. The
# manifest names exactly the eight paths step 06 owns.
mani() { sed 's/^[0-9a-f]\{64\}  //' "$here/tree.sha256"; }
mani | tar -C "$here/tree" --no-recursion -T - -cf - | tar -C "$app" -xf -

# --- what this rung retires ---------------------------------------------
#
# One file, and it has to go rather than be emptied: `logging-levels-declared`
# reads the packaged configuration, and a `log4j.properties` sitting beside
# `logback.xml` in `BOOT-INF/classes` is the exact thing
# `no-log4j-config-packaged` exists to name -- the file an operator would open
# to change a level, which no longer steers anything.
rm -f "$app/mini-git-server-app/src/main/resources/log4j.properties"

# --- assert every edit landed -------------------------------------------
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

# The rung's own postcondition, asserted on the workspace rather than
# inferred from the manifest. A checksum pass covers the eight paths above;
# what it does not cover is whether some *other* file in the merged tree still
# names log4j -- which is what a partially-applied overlay looks like.
stale="$(grep -rl 'org\.apache\.log4j\|log4j\.properties\|SimpleLog4JLogSystem' \
           --include='*.java' --include='pom.xml' "$app" 2>/dev/null | sort || true)"
if [ -n "$stale" ]; then
  echo "solve: STILL NAMES log4j:" >&2
  printf '  %s\n' $stale >&2
  missing=$((missing + $(printf '%s\n' "$stale" | wc -l)))
fi

if [ -e "$app/mini-git-server-app/src/main/resources/log4j.properties" ]; then
  echo "solve: NOT REMOVED: log4j.properties is back" >&2
  missing=$((missing + 1))
fi

# The coordinate, separately from the package: a pom naming `log4j:log4j` or
# `slf4j-log4j12` does not name `org.apache.log4j` anywhere, so the grep above
# cannot see it. `log4j-to-slf4j` and `log4j-api` arrive with Boot's logging
# starter and are a route *away* from log4j, so the match is anchored.
coord="$(grep -rlE '<artifactId>(log4j|slf4j-log4j12|slf4j-reload4j|reload4j)</artifactId>' \
           --include=pom.xml "$app" 2>/dev/null | sort || true)"
if [ -n "$coord" ]; then
  echo "solve: STILL DECLARES a log4j coordinate:" >&2
  printf '  %s\n' $coord >&2
  missing=$((missing + $(printf '%s\n' "$coord" | wc -l)))
fi

# The six levels that still steer code in this deployable, asserted by name so
# that a failure says which one did not land rather than only that the gate
# failed. The other seven entries log4j.properties carried named libraries
# steps 01 and 02 retired and are deliberately not reproduced.
for lg in 'level="DEBUG"' 'name="com.google.gerrit" level="INFO"' \
          'name="org.apache.sshd.common" level="WARN"' \
          'name="org.apache.sshd.server" level="INFO"' \
          'name="org.apache.sshd.common.keyprovider.FileKeyPairProvider" level="INFO"' \
          'name="net.sf.ehcache" level="WARN"'; do
  grep -qF "$lg" "$app/mini-git-server-app/src/main/resources/logback.xml" || {
    echo "solve: logback.xml missing $lg" >&2; missing=$((missing + 1)); }
done

# Both sshd artifacts must carry the exclusion. Excluding from one of the two
# leaves the other branch open and `jcl-over-slf4j` still resolves; this is a
# count rather than a grep for that reason.
jcl="$( { grep -c '<artifactId>jcl-over-slf4j</artifactId>' \
            "$app/mini-git-server-sshd/pom.xml" || true; } )"
[ "$jcl" -eq 2 ] || {
  echo "solve: expected 2 jcl-over-slf4j exclusions in the sshd pom, found $jcl" >&2
  missing=$((missing + 1)); }

# Tripwire, as in the rungs below: nothing here extracts a retired path, so if
# one reappears the extraction went back to copying the whole directory.
back="$(find "$app" -path '*/target' -prune -o -type f \
          \( -name 'UrlModule.java' -o -name 'WebModule.java' \
             -o -name 'web.xml' -o -name '*SshModule.java' \
             -o -name 'WebAppInitializer.java' -o -name 'log4j.properties' \) \
          -print | sort)"
if [ -n "$back" ]; then
  echo "solve: STALE (retired at or below this rung, back in the tree):" >&2
  printf '  %s\n' $back >&2
  missing=$((missing + $(printf '%s\n' "$back" | wc -l)))
fi

expected="$(wc -l < "$here/tree.sha256")"
echo "solve: $expected files in manifest, $missing did not land"
[ "$missing" -eq 0 ]

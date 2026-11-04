#!/usr/bin/env bash
#
# Reference solution, step 09 -- the tree starts speaking the language it is
# already compiled with.
#
# Release 21 arrived at step 08, because a virtual thread is a 21 API and
# nothing below 21 compiles one. What did not arrive with it was any of the
# idiom: a tree can be compiled at 21 and still be written in the Java of 2009,
# and this one was. This rung spends the release.
#
#   * `java.util.Date`, `Calendar`, `TimeZone` and `SimpleDateFormat` leave the
#     four files that held them, for `Instant`, `ZoneId` and
#     `DateTimeFormatter`. Every wire format is unchanged and asserted by the
#     behaviour suite: `show-connections` still prints `HH:mm:ss` in its start
#     and idle columns, and the ssh audit log still writes
#     `yyyy-MM-dd HH:mm:ss,SSS` and an RFC 822 offset. The gain is not
#     shorter code. A `DateTimeFormatter` is immutable, so `SshLog.MyLayout`
#     loses the `Calendar` it shared between sessions, the lock that guarded
#     it, and the hand-rolled one-second format cache that lock existed for;
#     `AdminShowConnections` and `ShowQueue` stop allocating a formatter per
#     row and hold two static ones instead. `IdentifiedUser` hands JGit an
#     `Instant` and a `ZoneId`, which `PersonIdent` has taken since 6.x.
#
#   * `StringBuffer` leaves. Three of the seven uses were `SshLog`'s own
#     buffer, which is never shared; the other four are the value
#     `HttpServletRequest.getRequestURL()` returns, which is a fresh buffer the
#     caller owns -- so the two callers copy it into a `StringBuilder` and edit
#     that. Nothing in the tree now declares a synchronized buffer.
#
#   * Five anonymous classes become lambdas: the four `CommandRunnable`
#     thunks the ssh commands hand to `startThread`, and the `CommandFactory`
#     in `CommandFactoryProvider`. The six that remain are not functional
#     interfaces and stay: Spring's five-method `Scope`, the three-method
#     `ProjectCommandRunnable`, `X509TrustManager`, and three abstract
#     classes. Making the four thunks lambdas forced a second edit:
#     `BaseCommand` declared `startThread(Runnable)` beside
#     `startThread(CommandRunnable)`, and since both parameters are
#     no-argument void functional interfaces, every lambda argument matched
#     both and no call site compiled. The `Runnable` overload had no callers
#     and is deleted.
#
#   * Eighteen `instanceof` tests that were immediately followed by a cast to
#     the type just tested become pattern matches, and the casts go with them.
#     Seven `instanceof` tests remain; none of them casts.
#
#   * The args4j boundary is generified. `OptionHandlerFactory<T>` now returns
#     `OptionHandler<T>` and takes `Setter<T>`; `SocketAddressHandler` and
#     `SubcommandHandler` take `Setter<SocketAddress>` and `Setter<String>`;
#     `CmdLineParser.MyParser.isEnum` and `isPrimitive` take `Setter<?>`.
#     One raw signature survives and cannot be removed: args4j 2.37 declares
#     `protected OptionHandler createOptionHandler(OptionDef, Setter)` raw, so
#     an override that names a type argument is not an override. That method
#     keeps the only `@SuppressWarnings({"rawtypes", "unchecked"})` in the
#     tree.
#
#   * `@SuppressWarnings` falls from ten to five, and the five that stay are
#     the ones with a reason written beside them. Two of the five that go were
#     replaced by the thing they were hiding: `ProjectServlet` and
#     `SshInfoServlet` declare a `serialVersionUID` and mark their injected
#     fields `transient` rather than suppressing `serial`.
#
#   * And the reason all of the above is checkable: the reactor now compiles
#     with `-Xlint:all -Werror`. Reaching that took six more edits nobody
#     would have made otherwise -- `HostPlatform` drops the removal-marked
#     `AccessController.doPrivileged`, `IdGenerator` writes out four lossy
#     narrowing compound assignments, `NoShell` parses its URL with
#     `java.net.URI`, `SocketAddressHandler` stops calling a deprecated
#     `CmdLineException` constructor, and the two callers of the deprecated
#     `CurrentUser.isAdministrator` say so in one place each.
#
# Twenty-eight files overlaid, none created and none deleted. Nothing is
# generated at solve time, so the oracle run and the tree in git are the same
# bytes; `tree.sha256` is what says so out loud.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="${1:-/app}"

[ -d "$app" ] || { echo "solve: no $app" >&2; exit 1; }
[ -f "$app/pom.xml" ] || { echo "solve: $app holds no reactor root" >&2; exit 1; }
[ -s "$here/tree.sha256" ] || { echo "solve: no manifest beside solve.sh" >&2; exit 1; }

# --- what this rung rewrites --------------------------------------------
#
# Driven from the manifest rather than from the directory: Harbor's oracle
# agent uploads each step's `solution/` into one shared directory inside the
# environment, merging and never deleting, so by the time this rung runs
# `$here/tree` is the union of steps 01 through 09. Three of the paths below
# -- `pom.xml`, `WorkQueue.java`, `BaseCommand.java` -- are also step 08's,
# and the manifest is what makes this rung's copy the one that lands.
mani() { sed 's/^[0-9a-f]\{64\}  //' "$here/tree.sha256"; }
mani | tar -C "$here/tree" --no-recursion -T - -cf - | tar -C "$app" -xf -

# --- assert every edit landed -------------------------------------------
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

# The rung's own postcondition, asserted over the whole merged workspace
# rather than over the twenty-eight files above -- which is what a partially
# applied overlay, or a later rung's file left behind, looks like.
src() { find "$app" -path '*/target' -prune -o -name '*.java' -print; }

# Comments are not code, and two of this rung's own comments name the types it
# retired. Strip line and block comments before grepping, or the solution
# fails its own assertion.
grep_src() {   # grep_src <python-regex>; prints the files whose *code* matches
  local re="$1"; shift
  src | python3 -c '
import re, sys
pat = re.compile(sys.argv[1])
for path in sys.stdin.read().split():
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        continue
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    if pat.search(text):
        print(path)
' "$re"
}

legacy="$(grep_src '\b(java\.util\.(Date|Calendar|GregorianCalendar|TimeZone)|java\.text\.(DateFormat|SimpleDateFormat)|new +Date *\(|Calendar\.getInstance|new +SimpleDateFormat)\b' | sort)"
if [ -n "$legacy" ]; then
  echo "solve: STILL USES THE LEGACY DATE API:" >&2
  printf '  %s\n' $legacy >&2
  missing=$((missing + $(printf '%s\n' "$legacy" | wc -l)))
fi

buffers="$(grep_src '\bStringBuffer\b' | sort)"
if [ -n "$buffers" ]; then
  echo "solve: STILL DECLARES A StringBuffer:" >&2
  printf '  %s\n' $buffers >&2
  missing=$((missing + $(printf '%s\n' "$buffers" | wc -l)))
fi

# The four sites the dates moved to, by name, so a failure says which one a
# rewrite dropped rather than only that the count changed.
for pair in \
  "mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/SshLog.java:DateTimeFormatter" \
  "mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/commands/AdminShowConnections.java:DateTimeFormatter" \
  "mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/commands/ShowQueue.java:DateTimeFormatter" \
  "mini-git-server-server/src/main/java/com/google/gerrit/server/IdentifiedUser.java:Instant"; do
  f="${pair%%:*}"; want="${pair##*:}"
  grep -qF "$want" "$app/$f" || {
    echo "solve: $f names no $want" >&2; missing=$((missing + 1)); }
done

# The wire formats the behaviour suite reads, spelled out where they live.
grep -qF '"yyyy-MM-dd HH:mm:ss,SSS"' \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/SshLog.java" || {
  echo "solve: the audit log timestamp pattern changed shape" >&2
  missing=$((missing + 1)); }
grep -qF '"HH:mm:ss"' \
  "$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/commands/AdminShowConnections.java" || {
  echo "solve: show-connections no longer prints HH:mm:ss" >&2
  missing=$((missing + 1)); }

# `-Xlint:all -Werror`, in the parent pom, where every module inherits it.
python3 - "$app/pom.xml" <<'PY' || missing=$((missing + 1))
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
args = re.findall(r"<arg>\s*([^<]*?)\s*</arg>", text)
why = []
if not any(a == "-Xlint:all" or a.startswith("-Xlint:") and "all" in a for a in args):
    why.append("no -Xlint:all")
if "-Werror" not in args:
    why.append("no -Werror")
if why:
    sys.stderr.write("solve: the reactor root does not enforce the warning "
                     "set: %s\n" % "; ".join(why))
    sys.exit(1)
PY

# Exactly one raw-typed args4j override survives, and it is the one args4j
# forces. Anything else that suppresses `rawtypes` is a rewrite that gave up.
raw="$(grep_src 'rawtypes' | sort)"
for f in $raw; do
  case "$f" in
    */util/cli/CmdLineParser.java) ;;
    *) echo "solve: suppresses rawtypes: $f" >&2; missing=$((missing + 1)) ;;
  esac
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

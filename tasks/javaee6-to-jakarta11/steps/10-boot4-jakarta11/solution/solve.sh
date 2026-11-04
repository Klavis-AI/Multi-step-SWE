#!/usr/bin/env bash
#
# Reference solution, step 10 -- the platform rung, and the last one.
#
# Nine rungs got this tree from a 2009 Java EE 6 war on Java 5 to a Spring
# Boot 3 executable jar on Java 21, written in the Java the release allows.
# Everything it runs on is now one major version behind current: Boot 3.5,
# Spring Framework 6.2, Tomcat 10.1, JUnit 5. This rung moves all of it, and
# then writes the four source conventions the new platform is designed around
# but cannot install on the tree's behalf.
#
#   * `springBootVersion` 3.5.16 -> 4.1.1, and that one property moves
#     everything the platform manages: Spring Framework 6.2.19 -> 7.0.9,
#     tomcat-embed-* 10.1.55 -> 11.0.24 (Servlet 6.0 -> 6.1), and JSpecify
#     1.0.0 -> 1.0.1. The reactor resolves fourteen `org.springframework.boot`
#     coordinates and seven `org.springframework` ones, and there is no
#     artifact of either group left on the old line.
#
#   * The separate `junit-bom` import is deleted. Boot 4's
#     `spring-boot-dependencies` manages `org.junit.*` itself, so the reactor
#     no longer names a JUnit version at all -- and the version it stops
#     naming was 5.10.2, while the platform's is 6.0.3. Both junit-jupiter and
#     junit-platform resolve on the 6 line, which is the point: a reactor that
#     had kept its own bom would have been pinned to 5 by the very import that
#     was meant to keep JUnit consistent.
#
#   * `spring-boot-starter-web` -> `spring-boot-starter-webmvc`. Boot 4 broke
#     the aggregate starter into modules and deprecated it; the 4.1.1 pom of
#     `spring-boot-starter-web` says so in its own description. It still
#     publishes and still resolves, so nothing about the build or the 428
#     observations would have noticed the old name. The graph now holds
#     `spring-boot-starter-webmvc`, `spring-boot-webmvc`,
#     `spring-boot-http-converter`, `spring-boot-servlet`,
#     `spring-boot-web-server`, `spring-boot-jackson` and the Tomcat modules,
#     and no `spring-boot-starter-web`.
#
#   * `maven.compiler.parameters` is turned on. Constructor injection with no
#     annotation -- see below -- resolves arguments by name, and a build that
#     drops parameter names leaves the container matching by type and position.
#
#   * Nothing the platform manages keeps a version of its own. Nine
#     dependencies across four poms name a platform-managed coordinate --
#     `spring-boot-starter-webmvc`, `spring-context` twice, `spring-web`,
#     `junit-jupiter`, `jspecify` and the rest -- and not one of them carries a
#     `<version>`. The only version named is the BOM import's, which is where
#     it belongs. A pinned coordinate resolves and builds and is the one
#     artifact the next platform bump leaves behind.
#
# And the four conventions, which are source and not configuration:
#
#   * Nullness is declared, package by package. All eighteen packages of main
#     source get a `package-info.java` carrying JSpecify's `@NullMarked`, which
#     says every reference the package declares is non-null unless annotated
#     otherwise. Seventeen methods are annotated otherwise: they return null on
#     some path and now say `@Nullable` where they are declared. The eighteenth
#     was `BlindSSLSocketFactory`'s anonymous `X509TrustManager`, whose
#     `getAcceptedIssuers` returned null into a JSSE contract that asks for an
#     array; it returns an empty one. JSpecify is a direct compile dependency
#     of every module, unversioned, managed by the platform -- the same
#     vocabulary Spring Framework 7's own signatures carry, so a tool reading
#     this project's nullness and a tool reading the framework's read one
#     vocabulary and not two. No `javax.annotation`, `org.springframework.lang`
#     or JetBrains nullness annotation survives anywhere.
#
#   * Constructor injection stops being annotated. Thirty-one `@Autowired`
#     annotations come off sole constructors, one each in thirty-one types
#     spread over twenty-seven source files; a class
#     with exactly one constructor has been injected through it without an
#     annotation since Framework 4.3, and Boot 4 documents the annotation there
#     as redundant. Field and setter injection is a different thing and keeps
#     its annotations: the eight ssh command classes args4j instantiates, the
#     two `@Bean` factory methods in `CacheModule`, and
#     `HttpCanonicalWebUrlProvider`'s `@Autowired(required = false)` setter,
#     which is optional precisely because the core beans are built in a
#     context with no HTTP environment.
#
#   * Twelve `serialVersionUID` fields get `@Serial`. The annotation is what
#     makes a serialization field a checked declaration rather than a
#     convention the compiler cannot see: misspell the name or change the type
#     and `@Serial` is a compile error, where without it the field is simply
#     ignored and the class silently gets a computed UID.
#
# And one edit that is none of the above. `SitePathsTest` built its temporary
# directory with `Paths.get("target")`; it now uses `Path.of`. The two are the
# same call -- `Paths.get` has been documented as equivalent to `Path.of` since
# 11, which is where the first rung put this reactor -- and the reason it moves
# here is that the platform's own Boot 4 upgrade recipe rewrites it. §1.8 asks
# for a tree that recipe has nothing left to say about, and a tree carrying one
# rewritable call is a tree it still has something to say about. It is one
# line, in a test, and it changes nothing the 428 observations can see.
#
# Sixty-six files: forty-eight rewritten and eighteen newly created, none
# deleted. Nothing is generated at solve time, so the oracle run and the tree
# in git are the same bytes; `tree.sha256` is what says so out loud.
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
# `$here/tree` is the union of steps 01 through 10. Several of the paths below
# -- `pom.xml`, `WorkQueue.java`, `BaseCommand.java`, `SshLog.java` among them
# -- are also an earlier rung's, and the manifest is what makes this rung's
# copy the one that lands.
mani() { sed 's/^[0-9a-f]\{64\}  //' "$here/tree.sha256"; }
mani | tar -C "$here/tree" --no-recursion -T - -cf - | tar -C "$app" -xf -

# --- assert every edit landed -------------------------------------------
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

# The rung's own postconditions, asserted over the whole merged workspace
# rather than over the sixty-six files above -- which is what a partially
# applied overlay, or an earlier rung's file left on top of this one, looks
# like.
src() { find "$app" -path '*/target' -prune -o -name '*.java' -print; }
poms() { find "$app" -path '*/target' -prune -o -name 'pom.xml' -print; }

# Comments are not code, and this rung's own comments name the coordinate it
# retired and the annotations it removed. Strip line and block comments -- and,
# in the poms, XML comments -- before matching, or the solution fails its own
# assertions.
decomment() {   # decomment <file-list-on-stdin> <python-regex>; prints matching files
  local re="$1"
  python3 -c '
import re, sys
pat = re.compile(sys.argv[1])
for path in sys.stdin.read().split():
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        continue
    text = re.sub(r"<!--.*?-->", " ", text, flags=re.S)
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    if pat.search(text):
        print(path)
' "$re"
}

fail() { echo "solve: $1" >&2; missing=$((missing + 1)); }

# --- the platform -------------------------------------------------------
grep -qE '<springBootVersion>4\.[0-9]' "$app/pom.xml" \
  || fail "the reactor root does not name Spring Boot 4"

bom="$(poms | decomment 'junit-bom' || true)"
[ -z "$bom" ] || { echo "solve: still imports junit-bom:" >&2
                   printf '  %s\n' $bom >&2
                   missing=$((missing + $(printf '%s\n' "$bom" | wc -l))); }

agg="$(poms | decomment '<artifactId>\s*spring-boot-starter-web\s*</artifactId>' || true)"
[ -z "$agg" ] || { echo "solve: still depends on the retired aggregate starter:" >&2
                   printf '  %s\n' $agg >&2
                   missing=$((missing + $(printf '%s\n' "$agg" | wc -l))); }

grep -qE '<artifactId>\s*spring-boot-starter-webmvc\s*</artifactId>' \
  "$app/mini-git-server-httpd/pom.xml" \
  || fail "mini-git-server-httpd asks for no modular web starter"

grep -qE '<artifactId>\s*jspecify\s*</artifactId>' "$app/pom.xml" \
  || fail "the reactor root declares no jspecify dependency"

# The one call the upgrade recipe would still rewrite. Asserted over the whole
# workspace and not just the file this rung edits: another rung's copy landing
# on top would put it back, and the recipe would have something to say again.
paths="$(src | decomment '\bPaths\s*\.\s*get\s*\(|java\.nio\.file\.Paths' || true)"
[ -z "$paths" ] || { echo "solve: java.nio.file.Paths survives:" >&2
                     printf '  %s\n' $paths >&2
                     missing=$((missing + $(printf '%s\n' "$paths" | wc -l))); }

grep -qE '<maven\.compiler\.parameters>\s*true\s*</maven\.compiler\.parameters>' \
  "$app/pom.xml" || fail "the reactor root does not keep parameter names"

# Every dependency on something the platform manages is declared without a
# version of its own, `<dependencyManagement>` excepted -- importing a BOM is
# where a version is supposed to be named. Written here rather than as a grep
# because the whole question is which block the element sits in, and a line
# regex cannot see that.
python3 - "$app" <<'PY' || missing=$((missing + 1))
import glob, os, sys, xml.etree.ElementTree as ET
app = sys.argv[1]
MANAGED = ("org.springframework", "org.junit", "org.jspecify", "ch.qos.logback",
           "org.slf4j", "org.apache.tomcat.embed", "com.fasterxml.jackson",
           "jakarta")
local = lambda t: t.rsplit("}", 1)[-1]
pinned, clean = [], []
for pom in sorted(glob.glob(app + "/pom.xml") + glob.glob(app + "/*/pom.xml")):
    stack = [(ET.parse(pom).getroot(), False)]
    while stack:
        el, mgmt = stack.pop()
        for kid in el:
            tag = local(kid.tag)
            if tag == "dependencyManagement":
                stack.append((kid, True))
            elif tag != "dependency":
                stack.append((kid, mgmt))
            elif not mgmt:
                f = {local(c.tag): (c.text or "").strip() for c in kid}
                g = f.get("groupId", "")
                if not any(g == m or g.startswith(m + ".") for m in MANAGED):
                    continue
                where = "%s %s:%s" % (os.path.relpath(pom, app), g,
                                      f.get("artifactId", ""))
                (pinned if f.get("version") else clean).append(where)
if pinned:
    sys.stderr.write("solve: platform-managed dependencies pin a version:\n")
    for x in sorted(pinned):
        sys.stderr.write("  %s\n" % x)
    raise SystemExit(1)
if len(clean) < 4:
    sys.stderr.write("solve: only %d platform-managed dependenc(y/ies) "
                     "declared\n" % len(clean))
    raise SystemExit(1)
print("solve: %d platform-managed dependencies, none pinned" % len(clean))
PY

# --- nullness -----------------------------------------------------------
#
# One package-info per package of main source that declares a type, each
# carrying @NullMarked. Named by package rather than counted, so a failure
# says which package was left undeclared.
python3 - "$app" <<'PY' || missing=$((missing + 1))
import os, re, sys
app = sys.argv[1]
pkgs = {}
for root, dirs, files in os.walk(app):
    dirs[:] = [d for d in dirs if d != "target"]
    if "/src/main/java/" not in root.replace("\\", "/") + "/":
        continue
    java = [f for f in files if f.endswith(".java")]
    if not java:
        continue
    types = [f for f in java if f != "package-info.java"]
    if not types:
        continue
    pkgs[root] = "package-info.java" in java
bare = sorted(p for p, has in pkgs.items() if not has)
if len(pkgs) == 0:
    sys.stderr.write("solve: no main-source packages found at all\n")
    sys.exit(1)
if bare:
    sys.stderr.write("solve: %d package(s) of main source declare no nullness:\n"
                     % len(bare))
    for p in bare:
        sys.stderr.write("  %s\n" % p[len(app) + 1:])
    sys.exit(1)
unmarked = []
for p in pkgs:
    text = open(os.path.join(p, "package-info.java"), encoding="utf-8").read()
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    if "@NullMarked" not in text:
        unmarked.append(p)
if unmarked:
    sys.stderr.write("solve: %d package-info.java carry no @NullMarked:\n"
                     % len(unmarked))
    for p in sorted(unmarked):
        sys.stderr.write("  %s\n" % p[len(app) + 1:])
    sys.exit(1)
sys.stderr.write("solve: %d main-source package(s), all @NullMarked\n" % len(pkgs))
PY

foreign="$(src | decomment 'import +(javax\.annotation\.(Nullable|Nonnull|CheckForNull|ParametersAreNonnullByDefault)|org\.springframework\.lang\.|org\.jetbrains\.annotations\.|edu\.umd\.cs\.findbugs\.annotations\.|org\.checkerframework\.)' || true)"
[ -z "$foreign" ] || { echo "solve: IMPORTS A SECOND NULLNESS VOCABULARY:" >&2
                       printf '  %s\n' $foreign >&2
                       missing=$((missing + $(printf '%s\n' "$foreign" | wc -l))); }

# The one null return this rung removed rather than annotated, by name: a JSSE
# trust manager that hands back an array, not null.
grep -qF 'new X509Certificate[0]' \
  "$app/mini-git-server-util-ssl/src/main/java/com/google/gerrit/util/ssl/BlindSSLSocketFactory.java" \
  || fail "BlindSSLSocketFactory still returns null for its accepted issuers"

# --- injection and serialization ----------------------------------------
#
# Two textual assertions over main source, both of which a rewrite that gave
# up halfway would fail. Neither replaces the verifier's own AST gates; they
# are here so the oracle run fails loudly at solve time rather than quietly at
# score time.
python3 - "$app" <<'PY' || missing=$((missing + 1))
import os, re, sys
app = sys.argv[1]
ANN = re.compile(r"^\s*@[A-Za-z_$][\w$.]*(\s*\([^)]*\))?\s*$")
bad_ctor, bad_serial = [], []
for root, dirs, files in os.walk(app):
    dirs[:] = [d for d in dirs if d != "target"]
    if "/src/main/java/" not in root.replace("\\", "/") + "/":
        continue
    for f in files:
        if not f.endswith(".java") or f == "package-info.java":
            continue
        path = os.path.join(root, f)
        text = open(path, encoding="utf-8", errors="replace").read()
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        text = re.sub(r"//[^\n]*", " ", text)
        lines = text.split("\n")
        # An @Autowired whose next declaration is a constructor. Constructors
        # are matched by the file's own type name, which is what makes this a
        # constructor test and not a method test: a method may not share it.
        ctor = re.compile(r"^\s*(?:(?:public|protected|private|final)\s+)*"
                          + re.escape(f[:-5]) + r"\s*\(")
        for i, line in enumerate(lines):
            if "@Autowired" not in line:
                continue
            j = i + 1
            while j < len(lines) and (not lines[j].strip() or ANN.match(lines[j])):
                j += 1
            if j < len(lines) and ctor.match(lines[j]):
                bad_ctor.append("%s:%d" % (path[len(app) + 1:], i + 1))
        # A serialVersionUID whose preceding declaration is not @Serial.
        for i, line in enumerate(lines):
            if "serialVersionUID" not in line or "=" not in line:
                continue
            j = i - 1
            while j >= 0 and not lines[j].strip():
                j -= 1
            if j < 0 or "@Serial" not in lines[j]:
                bad_serial.append("%s:%d" % (path[len(app) + 1:], i + 1))
rc = 0
if bad_ctor:
    sys.stderr.write("solve: %d constructor(s) still annotated @Autowired:\n"
                     % len(bad_ctor))
    for s in bad_ctor:
        sys.stderr.write("  %s\n" % s)
    rc = 1
if bad_serial:
    sys.stderr.write("solve: %d serialVersionUID field(s) are not @Serial:\n"
                     % len(bad_serial))
    for s in bad_serial:
        sys.stderr.write("  %s\n" % s)
    rc = 1
sys.exit(rc)
PY

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

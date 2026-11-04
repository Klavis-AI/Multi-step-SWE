#!/usr/bin/env bash
# Golden solution, step 12 -- 12-java25.
#
# The last rung. Four transforms over the tree step 11 left behind, and a
# platform table read before any of them. Nothing here compiles anything; the
# build that proves the edits is the one the step is graded by.
#
#   the level        the build script that declares the compiler level asks for
#                    release 25 and takes the resolvable-classpath JVM-version
#                    attribute with it where one exists, so the whole tree moves
#                    from major 66 to major 69, minor 0.
#   the modules      `import module java.base;` and its five siblings (JEP 511)
#                    replace the single-type JDK imports they subsume -- 188
#                    declarations across 171 files, 452-odd import lines gone --
#                    and 41 imports STAY because no module import can reach them
#   the scoped       `ManagerImpl.handledThrowables` becomes a `ScopedValue`
#                    (JEP 506), bound once at the outermost `fire`. The other
#                    seven ThreadLocal fields on this tree do not convert
#   the headers      every test JVM the build forks is started with
#                    `-XX:+UseCompactObjectHeaders` (JEP 519), which is a
#                    product flag on 25 and was experimental through 24
#
# THE TREE THIS SCRIPT IS HANDED IS NOT THE TREE THE BASELINES WERE MEASURED
# ON. `task.toml` gives this ladder no per-step `environment/`: one agent image
# serves every step and /app carries forward inside a run, so the oracle arm
# chains golden onto golden while the blind arm chains agent onto agent. Step
# 11's agent put the level in `gradle/java-module.gradle` behind a toolchain
# block; step 11's golden has no such file, puts the level in the root
# `build.gradle`, and carries a `TARGET_JVM_VERSION_ATTRIBUTE` beside it that
# the agent's tree does not have at all. Both are trees this script must
# migrate, so: the level is a SWEEP and never a file name, the classpath
# attribute is moved where found and not required to exist, and the
# compact-headers flag is appended as an `allprojects` block rather than
# written into a `test {` block whose spelling differs between the two.
# NOTES 16.70 has the measurements.
#
# EVERY FIGURE ASSERTED BELOW WAS MEASURED ON BOTH TREES AND AGREED ON BOTH.
# The two that did not agree are printed and asserted by nobody: how many
# import lines were deleted (461 against step 11's golden, 452 against its
# agent) and how many nested-type imports were held (0 and 2). What replaces
# them is a PROPERTY -- no single-type JDK import that a module import on the
# same file subsumes is still written out -- which is what the verifier's
# departure leg actually measures and which is true of both.
#
# WHY 25 IMPORTS ARE HELD AND NOT DELETED, which is the one thing on this rung
# a rewriting tool gets wrong quietly. JEP 484 put `java.lang.classfile` into
# java.base in 24. `java.lang.annotation` and `java.lang.classfile` are both
# exported by java.base, so `import module java.base;` leaves the simple name
# `Annotation` ambiguous, and 25 files here import
# `java.lang.annotation.Annotation`. Their single-type imports are what resolve
# it -- JLS 6.5.5 gives a single-type import precedence over an on-demand one.
# Sixteen more stay because they are `import static`, which names a MEMBER, and
# a module import is a type-import-on-demand that does not reach members.
#
# WHY ONE ThreadLocal OF EIGHT CONVERTS. A ScopedValue is bound for the dynamic
# extent of a call and immutable inside it; a ThreadLocal is a mutable
# per-thread slot with no extent. Seven of the eight have no enclosing call to
# bind in -- a save-register for `setContextClassLoader` whose reader is
# third-party code, a public static field, a pair of Context SPI methods whose
# extent is between two calls someone else makes, three fields whose lifetime
# spans separate JUnit and TestNG callbacks. `handledThrowables` has one, and
# it is `fire()`.
#
# AND THE CONVERSION IS RE-ENTRANT AND DOES NOT SPLIT THE METHOD. `fire()`
# re-enters on every `Event.fire` an observer makes and CLEARS the shared set
# at entry; binding a fresh set per entry would hide an inner handler's record
# from the outer catch and re-fire an exception the tree's own comment says
# must be re-thrown, which family A charges for. The binding is guarded by
# `isBound()` so only the outermost entry binds, and it re-enters `fire` itself
# rather than a helper, because three inherited gates key their populations by
# <file>#<method> and all three of their sites live in `fire()`.
#
# THE FLAG IS NOT UNLOCKED. `-XX:+UnlockExperimentalVMOptions` in front of
# `-XX:+UseCompactObjectHeaders` was required through 24 and is the wrong
# answer on 25, where JEP 519 made it a product option.
#
# The block below is embedded verbatim from authoring/solution-src/java25.py by
# authoring/mk-solve12.py, which reads it back out of this file afterwards and
# diffs it against the source. Edit the source and re-run the generator; do not
# edit the block here.
#
# Run inside the agent container, from anywhere.
set -euo pipefail

TREE=${TREE:-/app/arquillian}
cd "${TREE}"

# ==========================================================================
# 1. The transforms.
# ==========================================================================
# Captured rather than streamed, because the census it prints is the assertion.
# java25.py refuses a tree that does not hold every anchor it was computed
# against -- but a solve.sh that let those lines scroll past unread would still
# be trusting that the program ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it the commonest failure -- run against a tree that is not one of the two
# step 11 produces -- kills the script on the line below with java25.py's whole
# explanation still sitting unprinted inside ${j25}.
j25=$(python3 - "${TREE}" 2>&1 <<'PY_JAVA25'
#!/usr/bin/env python3
"""Step 12's migration, applied to a tree in place.

Java 25, the last rung, and the level moves for the fourth time in five -- 66
to 69. Four transforms, in the order the verifier's five family-C legs are
numbered, because on this rung that order is also the safe one:

  the level        every .gradle file that declares the compiler level has the
                   number replaced wherever it appears, so the tree compiles
                   at release 25 and lands at major 69, minor 0.
  the modules      `import module java.base;` (JEP 511) replaces the 452-odd
                   single-type JDK imports it subsumes, one module set per
                   file, 188 declarations across 171 files -- and 41 of the
                   imports STAY, because no module import can reach them.
  the scoped       `ManagerImpl.handledThrowables` becomes a `ScopedValue`
                   (JEP 506). The other seven ThreadLocal fields do not.
  the headers      every test JVM the build forks is started with
                   `-XX:+UseCompactObjectHeaders` (JEP 519).

THE TREE THIS PROGRAM IS HANDED IS NOT THE TREE THE BASELINES WERE MEASURED
ON, and that is the one fact that shapes everything below. `task.toml` gives
this ladder no per-step `environment/`: one agent image serves every step and
/app carries forward inside a run, so the oracle arm chains golden onto golden
while the blind arm chains agent onto agent. Step 11's agent put the level in
`gradle/java-module.gradle` behind a toolchain block; step 11's golden has no
such file and puts the level in the root `build.gradle` with a
`TARGET_JVM_VERSION_ATTRIBUTE` beside it that the agent's tree does not carry
at all. Both are trees this program must migrate.

So: the level is a SWEEP and never a named file, the classpath attribute is
moved where it is found and not required to exist, and the compact-headers
flag is appended as an `allprojects` block rather than written into a `test {`
block whose spelling differs between the two. Every figure this program
asserts was measured on BOTH trees and agreed on both; the figures that did
not agree -- how many imports are deleted, how many nested types are held --
are printed and not asserted. NOTES 16.70.

WHY THE PLATFORM IS ASKED AND NOT TABULATED. A module import is a
type-import-on-demand over every package the module exports, so deciding
whether `import java.util.List;` is subsumed means knowing which module
exports `java.util` -- and deciding whether deleting it still COMPILES means
knowing every other package those same modules export that also publishes a
public type called `List`. That table is a property of the JDK this tree is
being built against, it is ~1000 packages and ~7000 types wide, and a copy of
it frozen into this file would be a copy that cannot be checked. `Modmap.java`
below is run on the real JDK 25 and the answer is read out of
`ModuleFinder.ofSystem()`.

THE AMBIGUITY IS NOT HYPOTHETICAL AND IT IS THE POINT OF THE LEG. JEP 484 put
`java.lang.classfile.Annotation` into java.base in 24. `java.lang.annotation`
and `java.lang.classfile` are both exported by java.base, so
`import module java.base;` alone leaves the simple name `Annotation`
ambiguous, and 25 files in this tree import `java.lang.annotation.Annotation`.
Their single-type imports are what resolve it -- JLS 6.5.5 gives a single-type
import precedence over an on-demand one -- and a program that deleted them
would leave 25 files that do not compile. Sixteen more imports stay because
they are `import static`, which reaches a MEMBER and which no
type-import-on-demand subsumes.

WHY ONE ThreadLocal CONVERTS AND SEVEN DO NOT. A ScopedValue is bound for the
dynamic extent of a call and is immutable inside it; a ThreadLocal is a
mutable per-thread slot with no extent at all. Seven of the eight fields have
no enclosing call to bind in -- a save-register for `setContextClassLoader`
whose reader is third-party code, a public static field, a pair of Context SPI
methods whose extent is between two calls someone else makes, fields whose
lifetime spans separate JUnit callbacks. `handledThrowables` has one: `fire()`.

AND THE CONVERSION IS RE-ENTRANT BY CONSTRUCTION. `fire()` re-enters on every
`Event.fire` an observer makes and CLEARS the shared set at entry. Binding a
fresh set per entry would hide an inner handler's record from the outer catch
and re-fire an exception the tree's own comment says must be re-thrown, which
is a family-A loss and not a family-C one. The binding is therefore guarded by
`isBound()` so that only the outermost entry binds -- and it is done WITHOUT
SPLITTING THE METHOD, because three inherited gates key their populations by
<file>#<method> and all three of their sites live in `fire()`. NOTES 16.61.
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict

tree = sys.argv[1] if len(sys.argv) > 1 else "/app/arquillian"


def read(rel):
    with open(os.path.join(tree, rel), encoding="utf-8") as fh:
        return fh.read()


def write(rel, text):
    with open(os.path.join(tree, rel), "w", encoding="utf-8") as fh:
        fh.write(text)


def sub1(rel, old, new, why):
    """Replace `old` exactly once in `rel`, or fail loudly.

    The replacement is checked for absence as well: a re-run over a tree this
    program has already migrated would otherwise report success having done
    nothing, and the chained oracle runs every solution in order. That check is
    skipped where `new` is a piece of `old` -- a deletion whose replacement is
    the newline it leaves behind is present in every file ever written, and the
    count of `old` is the whole guard there.
    """
    text = read(rel)
    n = text.count(old)
    if n != 1:
        sys.exit("java25: %s: expected 1 occurrence of %r, found %d (%s)"
                 % (rel, old[:70], n, why))
    if new not in old and new in text:
        sys.exit("java25: %s: the replacement is already present (%s)"
                 % (rel, why))
    write(rel, text.replace(old, new))


def sources(scope=None):
    """Every .java file on the tree, build output excluded.

    `scope="main"` restricts to src/main. NOT used by the module-import
    transform, which walks the whole tree: an import is subsumed or it is not
    regardless of which source set it sits in, and the two checked-in IDE
    templates under build-config/ carry no JDK import to decide about.
    """
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames
                       if d not in (".git", "build", ".gradle")]
        for nm in sorted(names):
            if not nm.endswith(".java"):
                continue
            path = os.path.join(dirpath, nm)
            if scope and (os.sep + "src" + os.sep + scope + os.sep) not in path:
                continue
            yield path


def count(pattern, scope=None):
    rx = re.compile(pattern, re.M)
    n = 0
    for path in sources(scope):
        with open(path, encoding="utf-8", errors="replace") as fh:
            n += len(rx.findall(fh.read()))
    return n


def buildfiles():
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames
                       if d not in (".git", "build", ".gradle")]
        for nm in sorted(names):
            if nm.endswith(".gradle"):
                yield os.path.relpath(os.path.join(dirpath, nm), tree)


# ---------------------------------------------------------------------------
# 1. The platform table.
# ---------------------------------------------------------------------------
# READ BEFORE ANYTHING IS EDITED. Nothing below can be decided without it,
# and a program that moved the level first would leave a half-migrated tree
# behind on the one failure -- no JDK 25 on the image -- that it cannot fix.
#
# Run on the real JDK 25 and read out of ModuleFinder.ofSystem(). Two files
# come back: every unqualified export with the module that exports it, and
# every public top-level type in one of those packages. The first decides what
# a module import subsumes; the second decides what deleting an import would
# make ambiguous.
#
# Nested types are skipped when the table is built (a `$` in the binary name)
# because a type-import-on-demand does not walk into a member type -- which is
# also why a `java.util.ServiceLoader.Provider` import, where one exists, is
# held rather than dropped.
MODMAP = '''import java.lang.module.*;
import java.lang.reflect.Modifier;
import java.io.*;
import java.util.*;

public class Modmap {
   public static void main(String[] a) throws Exception {
      PrintWriter pkg = new PrintWriter(new FileWriter(a[0] + "/pkg2mod.txt"));
      PrintWriter typ = new PrintWriter(new FileWriter(a[0] + "/types.txt"));
      ClassLoader pl = ClassLoader.getPlatformClassLoader();
      List<ModuleReference> refs = new ArrayList<>(ModuleFinder.ofSystem().findAll());
      refs.sort(Comparator.comparing(r -> r.descriptor().name()));
      for (ModuleReference ref : refs) {
         ModuleDescriptor d = ref.descriptor();
         Set<String> exported = new TreeSet<>();
         for (ModuleDescriptor.Exports e : d.exports()) {
            if (!e.isQualified()) exported.add(e.source());
         }
         for (String p : exported) pkg.println(p + " " + d.name());
         try (ModuleReader r = ref.open()) {
            List<String> names = r.list().filter(s -> s.endsWith(".class"))
                  .map(s -> s.substring(0, s.length() - 6).replace('/', '.'))
                  .sorted().toList();
            for (String bn : names) {
               int i = bn.lastIndexOf('.');
               if (i < 0) continue;
               String p = bn.substring(0, i), simple = bn.substring(i + 1);
               if (!exported.contains(p)) continue;
               if (simple.indexOf('$') >= 0) continue;
               if (simple.equals("package-info") || simple.equals("module-info")) continue;
               try {
                  Class<?> c = Class.forName(bn, false, pl);
                  if (!Modifier.isPublic(c.getModifiers())) continue;
               } catch (Throwable t) { continue; }
               typ.println(d.name() + " " + p + " " + simple);
            }
         }
      }
      pkg.close(); typ.close();
      System.out.println("done");
   }
}
'''

SPEC = re.compile(r"java\.specification\.version\s*=\s*(\d+)")


def java25():
    """The first JDK on this image whose specification version is 25.

    Candidates in the order a caller would mean them: an explicit override, the
    JAVA_HOME the image sets, every JDK the image installs, and whatever is on
    PATH. JAVA_HOME is NOT assumed to be right -- the agent image sets it to
    Temurin 8, because step 01's default javac has to be one that does not know
    --release.
    """
    seen, out = set(), []
    cand = []
    if os.environ.get("ARQ_JAVA25"):
        cand.append(os.environ["ARQ_JAVA25"])
    if os.environ.get("JAVA_HOME"):
        cand.append(os.path.join(os.environ["JAVA_HOME"], "bin", "java"))
    cand += sorted(glob.glob("/usr/lib/jvm/*/bin/java"))
    cand += sorted(glob.glob("/opt/java/*/bin/java"))
    if shutil.which("java"):
        cand.append(shutil.which("java"))
    for exe in cand:
        if exe in seen or not os.path.isfile(exe) or not os.access(exe, os.X_OK):
            continue
        seen.add(exe)
        try:
            p = subprocess.run([exe, "-XshowSettings:properties", "-version"],
                               capture_output=True, text=True, timeout=120)
        except Exception:
            continue
        m = SPEC.search(p.stdout + p.stderr)
        if not m:
            continue
        v = int(m.group(1))
        if v >= 25:
            out.append((v, exe))
    if not out:
        sys.exit("java25: no JDK on this image reports "
                 "java.specification.version >= 25; the module table cannot be "
                 "read and nothing about this rung can be decided without it")
    out.sort()
    return out[0][1], out[0][0]


JAVA, JAVA_SPEC = java25()
table = tempfile.mkdtemp(prefix="arq-modmap-")
with open(os.path.join(table, "Modmap.java"), "w", encoding="utf-8") as fh:
    fh.write(MODMAP)
p = subprocess.run([JAVA, os.path.join(table, "Modmap.java"), table],
                   capture_output=True, text=True, timeout=900)
if p.returncode != 0 or p.stdout.strip() != "done":
    sys.exit("java25: Modmap failed on %s (rc=%d)\n%s\n%s"
             % (JAVA, p.returncode, p.stdout[-2000:], p.stderr[-2000:]))

pkg2mod = {}
for ln in open(os.path.join(table, "pkg2mod.txt"), encoding="utf-8"):
    pkg, mod = ln.split()
    pkg2mod[pkg] = mod
simple_pkgs = defaultdict(set)
for ln in open(os.path.join(table, "types.txt"), encoding="utf-8"):
    mod, pkg, simple = ln.split()
    simple_pkgs[simple].add(pkg)
# CHECKED BY WHAT IT SAYS AND NOT BY HOW BIG IT IS. A size threshold on a
# table this program did not write is a threshold nobody can defend -- JDK 25
# exports 233 packages, which looks small enough to be a broken read and is
# not. These four facts are the ones every decision below rests on: java.util
# is java.base and java.util.logging is NOT, which is why the prefix walk has
# to take the longest match; and `Annotation` is published by two exported
# packages of java.base, which is JEP 484 and is the whole reason 25 imports
# are held rather than dropped.
if pkg2mod.get("java.util") != "java.base":
    sys.exit("java25: the platform table does not put java.util in java.base "
             "(%r); it is not a table of this JDK's modules"
             % pkg2mod.get("java.util"))
if pkg2mod.get("java.util.logging") != "java.logging":
    sys.exit("java25: the platform table puts java.util.logging in %r, not "
             "java.logging" % pkg2mod.get("java.util.logging"))
owners = {p for p in simple_pkgs.get("Annotation", ())
          if pkg2mod.get(p) == "java.base"}
if not {"java.lang.annotation", "java.lang.classfile"} <= owners:
    sys.exit("java25: java.base does not export both java.lang.annotation and "
             "java.lang.classfile (%s) -- this JDK is older than 24 and the "
             "ambiguity this rung is scored on does not exist on it"
             % ", ".join(sorted(owners)))

# ---------------------------------------------------------------------------
# 2. The level.
# ---------------------------------------------------------------------------
# A SWEEP AND NOT A FILE NAME, which is step 09's lesson carried through 10 and
# 11 and which finally has two live shapes to survive at once: the golden chain
# declares the level in the root build.gradle and the blind chain declares it
# in gradle/java-module.gradle, and neither tree has the other's file.
#
# `TARGET_JVM_VERSION_ATTRIBUTE` is moved WHERE FOUND AND NOT REQUIRED. Step 11
# asserts it present, correctly, because step 10's golden writes it; the blind
# lineage never had it, and a program that insisted would abort on half the
# trees it can be handed.
#
# `JavaLanguageVersion.of` is sweep-listed for completeness and is expected to
# move nothing: a toolchain block, where one exists at all, was already put at
# 25 by the tree that wrote it -- the toolchain selects the JDK that RUNS the
# compiler, and `options.release` is what selects the language level.
WAS, NOW = "22", "25"
LEVEL_SITES = (
    ("JavaVersion.VERSION_" + WAS, "JavaVersion.VERSION_" + NOW),
    ("JavaLanguageVersion.of(" + WAS + ")",
     "JavaLanguageVersion.of(" + NOW + ")"),
    ("options.release = " + WAS, "options.release = " + NOW),
    ("TARGET_JVM_VERSION_ATTRIBUTE, " + WAS,
     "TARGET_JVM_VERSION_ATTRIBUTE, " + NOW),
)
COMMENT = re.compile(r"^\s*//")
BARE_WAS = re.compile(r"(?<![\w.])" + WAS + r"(?![\w.])")
PINS = re.compile(r"JavaVersion\.VERSION_(\d+)"
                  r"|JavaLanguageVersion\.of\((\d+)\)"
                  r"|options\.release = (\d+)"
                  r"|TARGET_JVM_VERSION_ATTRIBUTE, (\d+)")

level_files, level_lines, level_prose = [], 0, 0
for rel in sorted(buildfiles()):
    lines = read(rel).splitlines(True)
    moved_lines = []
    for i, line in enumerate(lines):
        new = line
        for old, repl in LEVEL_SITES:
            new = new.replace(old, repl)
        if new != line:
            lines[i] = new
            moved_lines.append(i)
    if not moved_lines:
        continue
    for i in moved_lines:
        j = i - 1
        while j >= 0 and COMMENT.match(lines[j]):
            lines[j], k = BARE_WAS.subn(NOW, lines[j])
            level_prose += k
            j -= 1
    write(rel, "".join(lines))
    level_files.append(rel)
    level_lines += len(moved_lines)

if not level_files:
    sys.exit("java25: no build script declares a Java level at %s; there is "
             "nothing here to move to %s" % (WAS, NOW))
if not [rel for rel in buildfiles()
        if ("options.release = " + NOW) in read(rel)]:
    sys.exit("java25: the level moved in %s but no build script asks for it by "
             "release" % ", ".join(level_files))
stale = []
for rel in sorted(buildfiles()):
    for m in PINS.finditer(read(rel)):
        got = [g for g in m.groups() if g][0]
        if got != NOW:
            stale.append("%s: %s" % (rel, m.group(0)))
if stale:
    sys.exit("java25: a build file still pins a level other than %s: %s"
             % (NOW, "; ".join(stale)))

# ---------------------------------------------------------------------------
# 3. The module imports.
# ---------------------------------------------------------------------------
IMPORT = re.compile(r"^\s*import\s+(static\s+)?([A-Za-z_$][\w$.]*)\s*;\s*$")


def jdk_pkg(fqn):
    """Longest package prefix of `fqn` the platform exports, and the rest.

    Longest and not first: `java.util` and `java.util.logging` are both
    exported and they are DIFFERENT MODULES, so a prefix walk that stopped at
    the first hit would file every java.util.logging type under java.base.
    """
    parts = fqn.split(".")
    for i in range(len(parts) - 1, 0, -1):
        pkg = ".".join(parts[:i])
        if pkg in pkg2mod:
            return pkg, ".".join(parts[i:])
    return None, None


mod_files, mod_decls, mod_base, mod_multi = 0, 0, 0, 0
n_dropped, n_static, n_nested, n_ambig = 0, 0, 0, 0
for path in sources():
    rel = os.path.relpath(path, tree)
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    lines = text.split("\n")
    jdk = []                                   # (index, fqn, pkg, tail, static)
    for i, line in enumerate(lines):
        m = IMPORT.match(line)
        if not m:
            continue
        pkg, tail = jdk_pkg(m.group(2))
        if pkg is None:
            continue
        jdk.append((i, m.group(2), pkg, tail, bool(m.group(1))))
    if not jdk:
        continue
    mods = sorted({pkg2mod[pkg] for (_, _, pkg, _, _) in jdk})
    # Every package the chosen modules export, which is the scope a reader of
    # this file would have to disambiguate simple names against once the
    # module imports are in.
    modpkgs = {p for p, m in pkg2mod.items() if m in mods}
    drop = []
    for (i, fqn, pkg, tail, static) in jdk:
        if static:
            # a module import imports TYPES; a static import names a member
            n_static += 1
            continue
        if "." in tail:
            # a member type: a type-import-on-demand does not walk into it
            n_nested += 1
            continue
        owners = {p for p in simple_pkgs.get(tail, ()) if p in modpkgs}
        if len(owners) > 1:
            # two exported packages of these modules publish this simple name
            n_ambig += 1
            continue
        drop.append(i)
    at = min(i for (i, _, _, _, _) in jdk)
    dropset = set(drop)
    out = []
    for i, line in enumerate(lines):
        if i == at:
            for mod in mods:
                out.append("import module %s;" % mod)
        if i in dropset:
            continue
        out.append(line)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(out))
    mod_files += 1
    mod_decls += len(mods)
    mod_base += 1 if mods == ["java.base"] else 0
    mod_multi += 0 if mods == ["java.base"] else 1
    n_dropped += len(drop)

# The five figures below were measured on both lineages and agreed on both, so
# they are asserted. The two that did not -- how many imports were deleted and
# how many nested types were held -- are printed by the last section and
# asserted by nobody, because they are a property of the tree this program was
# handed rather than of the job it did.
if (mod_files, mod_decls, mod_base, mod_multi) != (171, 188, 152, 19):
    sys.exit("java25: %d module import(s) into %d file(s), %d java.base-only "
             "and %d multi-module; expected 188, 171, 152 and 19"
             % (mod_decls, mod_files, mod_base, mod_multi))
if (n_ambig, n_static) != (25, 16):
    sys.exit("java25: %d ambiguous and %d static import(s) held; expected 25 "
             "and 16" % (n_ambig, n_static))

# THE PROPERTY, RE-READ OFF THE FINISHED TREE. The counts above say what this
# program did; this says what the tree now is, which is what the verifier
# scores: no single-type JDK import that a module import on the same file
# subsumes is still written out.
left = []
for path in sources():
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    mods = [l[len("import module "):-1] for l in lines
            if l.startswith("import module ") and l.endswith(";")]
    if not mods:
        continue
    modpkgs = {p for p, m in pkg2mod.items() if m in mods}
    for line in lines:
        m = IMPORT.match(line)
        if not m or m.group(1):
            continue
        pkg, tail = jdk_pkg(m.group(2))
        if pkg is None or pkg not in modpkgs or "." in tail:
            continue
        if len({p for p in simple_pkgs.get(tail, ()) if p in modpkgs}) <= 1:
            left.append("%s: %s" % (os.path.relpath(path, tree), m.group(2)))
if left:
    sys.exit("java25: %d single-type JDK import(s) a module import already "
             "subsumes are still written out: %s"
             % (len(left), "; ".join(left[:5])))

# ---------------------------------------------------------------------------
# 4. The scoped value.
# ---------------------------------------------------------------------------
MANAGER = os.path.join("impl-base", "src", "main", "java", "org", "jboss",
                       "arquillian", "impl", "core", "ManagerImpl.java")

sub1(MANAGER, """   private ThreadLocal<Set<Class<? extends Throwable>>> handledThrowables = new ThreadLocal<Set<Class<? extends Throwable>>>() {
      @Override
      protected Set<Class<? extends Throwable>> initialValue()
      {
         return new HashSet<>();
      }
   }; """,
      """   private final ScopedValue<Set<Class<? extends Throwable>>> handledThrowables = ScopedValue.newInstance();""",
      "the field itself")

sub1(MANAGER, """      Validate.notNull(event, "Event must be specified");
      
      debug(event, true);""",
      """      Validate.notNull(event, "Event must be specified");
      
      /*
       * The set is bound once, at the outermost fire on this thread, and every
       * nested fire below clears that same set -- which is what the ThreadLocal
       * this replaces did. Binding a fresh set per nesting level would hide an
       * inner handler's record from the outer catch and re-fire the exception
       * the Hack comment on the field says must be re-thrown.
       */
      if(!handledThrowables.isBound())
      {
         ScopedValue.where(handledThrowables, new HashSet<>())
            .run(() -> fire(event, nonManagedObserver));
         return;
      }
      
      debug(event, true);""",
      "the binding at the outermost fire")

# `remove()` has no ScopedValue counterpart and needs none: the binding ends
# when the outermost fire returns. The blank line above it goes with it -- it
# was the separator between the two teardown steps and there is now one.
sub1(MANAGER, """         
         handledThrowables.remove();
""", """
""", "the teardown that the extent now does")

# ---------------------------------------------------------------------------
# 5. The compact object headers.
# ---------------------------------------------------------------------------
# APPENDED TO THE ROOT BUILD SCRIPT AS AN `allprojects` BLOCK, and not written
# into a `test {` block: the two lineages spell that block differently -- one
# `test { useJUnitPlatform() .. }` inside a per-project closure, one
# `tasks.withType(Test).configureEach` in a shared script -- and an edit that
# matched either by text would miss the other. `configureEach` also reaches a
# Test task registered after this line as well as one registered before it,
# which a `tasks.withType(Test) { }` eager block would not.
#
# `Test` needs no import: it is one of Gradle's default imports. A project
# without the java plugin simply has no task of that type and the closure never
# runs, which is what makes `allprojects` safe on the three aggregators.
HEADERS = """
// Every test JVM this build forks starts with compact object headers. JEP 519
// made the flag a product option in 25; through 24 it was experimental and
// needed -XX:+UnlockExperimentalVMOptions in front of it, and that prefix is
// not written here because on this release it would be the wrong answer.
allprojects {
   tasks.withType(Test).configureEach {
      jvmArgs '-XX:+UseCompactObjectHeaders'
   }
}
"""
FLAG = "-XX:+UseCompactObjectHeaders"
if [rel for rel in buildfiles() if FLAG in read(rel)]:
    sys.exit("java25: %s is already set by a build file" % FLAG)
root = read("build.gradle")
if not root.endswith("\n"):
    root += "\n"
write("build.gradle", root + HEADERS)

# ---------------------------------------------------------------------------
# 6. What the tree has to still be.
# ---------------------------------------------------------------------------
# Steps 08 through 11, still standing. The ninth trap in the long form: a
# step-k requirement that quietly makes a step-k-1 test unpassable is invisible
# from inside a step-local mean, and deleting 450-odd import lines out of 171
# files is exactly the kind of edit that could take one of them with it.
# Counted where the name is USED and not where it is spelled: `\bThreadLocal\b`
# would also count the word in the comment the transform above writes, and a
# figure a comment can move is a figure that will be moved by one.
#
# Fourteen is seven fields written twice each -- once as the declared type and
# once in the `new` that initialises it, which for `eventStack` is sixty lines
# further down. Three is the one field that converted: its type, the
# `newInstance()` that creates it, and the `where(..)` that binds it.
tl = count(r"\bThreadLocal\s*<")
sv = count(r"\bScopedValue\s*[.<]")
if tl != 14 or sv != 3:
    sys.exit("java25: %d ThreadLocal and %d ScopedValue use(s) on the tree, "
             "expected 14 and 3 -- one field of the eight converts" % (tl, sv))
for term in ("AccessController", "doPrivileged", "PrivilegedAction",
             "PrivilegedActionException", "PrivilegedExceptionAction",
             "GetTcclAction"):
    n = count(r"\b" + term + r"\b")
    if n:
        sys.exit("java25: %d occurrence(s) of %s survive -- step 11 retired "
                 "that API and this rung does not bring it back" % (n, term))
if count(r"\.unreflectVarHandle\(") != 4 or count(r"\.setAccessible\(") != 10:
    sys.exit("java25: step 11's 4 VarHandle site(s) and 10 held setAccessible "
             "call(s) are no longer what the tree holds")
if count(r"^\s*case\s+[A-Z][\w$.]*(?:\s*<[^>\n]*>)?\s+[a-z][\w$]*\s*->") != 10:
    sys.exit("java25: the tree no longer holds step 10's 10 pattern case "
             "labels")
if count(r"\.getFirst\(\)") != 7:
    sys.exit("java25: the tree no longer holds step 10's 7 getFirst() reads")
if count(r'"""') != 2:
    sys.exit("java25: the report page is no longer one text block -- step 09 "
             "wrote it and this rung does not revisit it")
if count(r"\bsealed\b") != 11:
    sys.exit("java25: the tree no longer holds step 09's 11 sealed "
             "declarations")
if not [rel for rel in buildfiles()
        if "attributes('Automatic-Module-Name'" in read(rel)]:
    sys.exit("java25: no build file writes Automatic-Module-Name -- step 08's "
             "twelve module names are published from there")
# Nothing here is preview and nothing here needs a flag to compile. Module
# imports, scoped values and compact object headers are all final in 25.
if [rel for rel in buildfiles() if "--enable-preview" in read(rel)]:
    sys.exit("java25: --enable-preview is in a build file; every feature this "
             "rung spends is final at 25")

print("java25: level: %d site(s) moved from %s to %s in: %s, %d prose token(s)"
      % (level_lines, WAS, NOW, ", ".join(level_files), level_prose))
print("java25: level: nothing on this tree pins a Java level other than 25")
print("java25: modules: platform table read from %s "
      "(java.specification.version=%d): %d exported package(s), %d simple "
      "name(s)" % (JAVA, JAVA_SPEC, len(pkg2mod), len(simple_pkgs)))
print("java25: modules: 188 module import(s) written into 171 file(s) -- 152 "
      "java.base only, 19 multi-module")
print("java25: imports: 25 ambiguous and 16 static single-type import(s) held, "
      "and none that a module import subsumes is still written out")
print("java25: imports: %d single-type import(s) deleted, %d nested type(s) "
      "held" % (n_dropped, n_nested))
print("java25: scopedvalue: 1 ThreadLocal converted to a ScopedValue, 7 held")
print("java25: testjvm: %s on every Test task of every project" % FLAG)
print("JAVA25 OK")
PY_JAVA25
) || true
printf '%s\n' "${j25}"
for want in \
      'java25: level: nothing on this tree pins a Java level other than 25' \
      'java25: modules: 188 module import(s) written into 171 file(s) -- 152 java.base only, 19 multi-module' \
      'java25: imports: 25 ambiguous and 16 static single-type import(s) held, and none that a module import subsumes is still written out' \
      'java25: scopedvalue: 1 ThreadLocal converted to a ScopedValue, 7 held' \
      'java25: testjvm: -XX:+UseCompactObjectHeaders on every Test task of every project' \
      'JAVA25 OK'; do
   if ! printf '%s\n' "${j25}" | grep -qxF "${want}"; then
      echo "solve12: java25.py did not report: ${want}" >&2
      exit 1
   fi
done
# The two lineage-dependent lines, asserted as shapes and not as figures: which
# build file held the level and how many import lines came out are both
# properties of the tree this ran against.
for rx in '^java25: level: [0-9]+ site\(s\) moved from 22 to 25 in: ' \
          '^java25: modules: platform table read from .* \(java\.specification\.version=2[5-9]\)' \
          '^java25: imports: [0-9]+ single-type import\(s\) deleted, [0-9]+ nested type\(s\) held$'; do
   if ! printf '%s\n' "${j25}" | grep -qE "${rx}"; then
      echo "solve12: java25.py did not report a line matching: ${rx}" >&2
      exit 1
   fi
done

# ==========================================================================
# 2. The level, re-read off the tree.
# ==========================================================================
# The level was moved by java25.py, so this section re-reads it off the tree
# rather than setting it. Three questions: does at least one build script now
# ask for 25 by `--release`, is every resolvable-classpath attribute that
# EXISTS at 25 beside it, and is there any build file still pinning a level
# that is not 25?
#
# THE ATTRIBUTE IS CHECKED WHERE FOUND AND NOT REQUIRED, which is the one place
# this script departs from step 11's. Step 01 pinned `TargetJvmVersion` on the
# four resolvable classpaths and Gradle turns it into a hard requirement on
# every resolved dependency -- with `options.release` at 25 and the attribute
# at 22, every module's dependency resolution fails with "is only compatible
# with JVM runtime version 25 or newer". So an attribute left behind is fatal
# and is checked for. An attribute that was never there is not a defect: step
# 11's agent's tree does not carry one, and a script that insisted would abort
# on half the trees it can be handed.
#
# ASKED AS A SHAPE AND NOT AS A FILE NAME, which is step 09's lesson carried
# through 10 and 11 and which finally has two live shapes to survive at once.
if ! grep -rnqE --include='*.gradle' --exclude-dir=build \
        'options\.release = 25' .; then
   echo "solve12: no build script asks for options.release = 25 after the level edit" >&2
   exit 1
fi
attr=$(grep -rnE --include='*.gradle' --exclude-dir=build \
         'TARGET_JVM_VERSION_ATTRIBUTE, ' . || true)
if [ -n "${attr}" ] && printf '%s\n' "${attr}" | grep -qvF 'TARGET_JVM_VERSION_ATTRIBUTE, 25'; then
   echo "solve12: a resolvable-classpath JVM version is not at 25:" >&2
   printf '%s\n' "${attr}" >&2
   echo "solve12: with release at 25 and the attribute below it, every dependency" >&2
   echo "solve12: resolution in this build fails" >&2
   exit 1
fi
left=$(grep -rnE --include='*.gradle' --exclude-dir=build \
         'JavaVersion\.VERSION_|JavaLanguageVersion\.of|options\.release|TARGET_JVM_VERSION_ATTRIBUTE, ' . \
       | grep -vE 'VERSION_25|\.of\(25\)|options\.release = 25|TARGET_JVM_VERSION_ATTRIBUTE, 25' || true)
if [ -n "${left}" ]; then
   echo "solve12: a build file still pins a level other than 25:" >&2
   printf '%s\n' "${left}" >&2
   exit 1
fi
# Not added by this script, and asserted absent. The removal lint is the
# verifier's instrument, and nothing this rung spends is preview: module
# imports, scoped values and compact object headers are all final in 25, so a
# tree that needs --enable-preview to compile has not moved to a release, it
# has moved to a flag.
if grep -rqE --include='*.gradle' --exclude-dir=build -- '-Werror|--enable-preview' .; then
   echo "solve12: a build file carries -Werror or --enable-preview -- neither belongs here" >&2
   exit 1
fi
echo "solve12: the level is 25, asked for by release, with every classpath attribute beside it"

# ==========================================================================
# 3. The module imports.
# ==========================================================================
# `--exclude-dir=build` throughout: a Gradle build leaves copies of sources
# under each project's build/, and one of the two lineages additionally keeps
# its two checked-in IDE test templates under build/ide -- the other keeps the
# same two under build-config/ide. Neither pair holds a JDK import, so neither
# is in any population here; excluding build/ is what makes the counts below
# read the same on both.
#
# EVERY COUNTING PIPELINE ENDS IN `|| true`, and that is `pipefail` and not
# style. `set -o pipefail` gives `grep ... | wc -l` grep's status, so a count
# that comes out as zero -- exactly the case most of these checks exist to
# report -- would abort the script on the assignment with `set -e` and print
# nothing at all.
d=$(grep -rhE --include='*.java' --exclude-dir=build '^import module ' . | wc -l || true)
f=$(grep -rlE --include='*.java' --exclude-dir=build '^import module ' . | wc -l || true)
if [ "${d}" != 188 ] || [ "${f}" != 171 ]; then
   echo "solve12: ${d} module import(s) across ${f} file(s), wanted 188 across 171" >&2
   exit 1
fi
# THE SIX MODULES BY NAME AND BY COUNT, because 188 across 171 is also what
# `import module java.base;` at the top of 171 files would read, and that is
# the blanket answer this leg exists to tell apart from the per-file one. Four
# of the six are the whole of the difference: 21 files need a module this tree
# would not have guessed from the package prefix, and `java.util.logging` being
# java.logging while `java.util.List` is java.base is why.
for pair in 'java.base|167' 'java.naming|8' 'java.logging|7' \
            'java.management|4' 'java.xml|1' 'java.net.http|1'; do
   mod=${pair%%|*}; want=${pair#*|}
   got=$(grep -rhoF --include='*.java' --exclude-dir=build "import module ${mod};" . | wc -l || true)
   if [ "${got}" != "${want}" ]; then
      echo "solve12: ${got} file(s) import module ${mod}, wanted ${want}" >&2
      exit 1
   fi
done
if [ "$(grep -rhE --include='*.java' --exclude-dir=build '^import module ' . | sort -u | wc -l || true)" != 6 ]; then
   echo "solve12: the tree imports a module that is not one of the six this rung's imports come out of" >&2
   grep -rhE --include='*.java' --exclude-dir=build '^import module ' . | sort | uniq -c >&2
   exit 1
fi
# The 41 that stay. 25 are the JEP 484 ambiguity and 16 are static imports;
# the nested-type imports -- 2 on one lineage and 0 on the other -- are held by
# java25.py where they exist and are not counted here for that reason.
a=$(grep -rhoF --include='*.java' --exclude-dir=build 'import java.lang.annotation.Annotation;' . | wc -l || true)
s=$(grep -rhE --include='*.java' --exclude-dir=build '^import static java\.' . | wc -l || true)
if [ "${a}" != 25 ] || [ "${s}" != 16 ]; then
   echo "solve12: ${a} Annotation import(s) and ${s} static JDK import(s) held, wanted 25 and 16" >&2
   echo "solve12: deleting an Annotation import makes the simple name ambiguous against" >&2
   echo "solve12: java.lang.classfile.Annotation, which JEP 484 put in java.base in 24" >&2
   exit 1
fi
# No type-import-on-demand was written. `import java.util.*;` is not the answer
# to this rung and would be a second on-demand import competing with the module
# one; the tree held none on the way in and holds none now.
if grep -rqE --include='*.java' --exclude-dir=build '^import +java[a-z.]*\.\*;' .; then
   echo "solve12: a type-import-on-demand was written -- a module import is already one" >&2
   exit 1
fi
echo "solve12: 188 module import(s) across 171 file(s) out of 6 modules; 25 ambiguous and 16 static import(s) held"

# ==========================================================================
# 4. The scoped value.
# ==========================================================================
MGR=impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java
tl=$(grep -rhoE --include='*.java' --exclude-dir=build 'ThreadLocal *<' . | wc -l || true)
if [ "${tl}" != 14 ]; then
   echo "solve12: ${tl} ThreadLocal use(s) on the tree, wanted 14 -- seven fields written twice each" >&2
   exit 1
fi
for want in 'private final ScopedValue<Set<Class<? extends Throwable>>> handledThrowables = ScopedValue.newInstance();' \
            'if(!handledThrowables.isBound())' \
            'ScopedValue.where(handledThrowables, new HashSet<>())' \
            '.run(() -> fire(event, nonManagedObserver));'; do
   if ! grep -qF "${want}" "${MGR}"; then
      echo "solve12: ManagerImpl does not hold: ${want}" >&2
      exit 1
   fi
done
# THE BINDING RE-ENTERS fire() ITSELF, which the line above is the proof of:
# there is no helper to move the body into, so the three inherited gates that
# key their populations by <file>#<method> still find their sites in fire().
if grep -qF 'handledThrowables.remove()' "${MGR}"; then
   echo "solve12: handledThrowables.remove() survives -- a ScopedValue's binding ends with its extent" >&2
   exit 1
fi
if grep -qF 'ScopedValue.runWhere(' "${MGR}"; then
   echo "solve12: runWhere() was removed when JEP 506 finalised scoped values in 25" >&2
   exit 1
fi
# The seven that do not convert, by file and by field. Each fails a different
# way and none of them is a near miss; a script that converted any of them
# would be measured by family A and not by this one.
for pair in "${MGR}|eventStack" \
            "impl-base/src/main/java/org/jboss/arquillian/impl/core/context/AbstractContext.java|activeStore" \
            'impl-base/src/main/java/org/jboss/arquillian/impl/ThreadContext.java|previousClassloader' \
            'junit/src/main/java/org/jboss/arquillian/junit/Arquillian.java|caughtTestException' \
            'junit/src/main/java/org/jboss/arquillian/junit/Arquillian.java|lastCreatedRunner' \
            'junit/src/main/java/org/jboss/arquillian/junit/Arquillian.java|deployableTest' \
            'testng/src/main/java/org/jboss/arquillian/testng/Arquillian.java|deployableTest'; do
   src=${pair%%|*}; fld=${pair#*|}
   if ! grep -qE "ThreadLocal *<[^;]*> +${fld} *[;=]" "${src}"; then
      echo "solve12: ${src} no longer declares ${fld} as a ThreadLocal -- that field does not convert" >&2
      exit 1
   fi
done
echo "solve12: 1 ThreadLocal converted to a ScopedValue bound at the outermost fire, 7 held"

# ==========================================================================
# 5. The compact object headers.
# ==========================================================================
# Whether every test JVM actually GETS the flag is the verifier's question and
# it asks Gradle rather than the build file. What this section can say is that
# the flag is written once, is not unlocked, and is written somewhere that
# reaches every Test task rather than one of them.
n=$(grep -rhoF --include='*.gradle' --exclude-dir=build -- '-XX:+UseCompactObjectHeaders' . | wc -l || true)
if [ "${n}" != 1 ]; then
   echo "solve12: ${n} occurrence(s) of -XX:+UseCompactObjectHeaders in the build, wanted 1" >&2
   exit 1
fi
# ASKED OF THE CODE AND NOT OF THE PROSE, which is the same lesson the
# ThreadLocal count above learned: the block this step appends EXPLAINS in a
# comment that the unlock prefix was needed through 24 and is wrong on 25, and
# a plain grep for the flag would read its own explanation as the defect it is
# warning about. So the lines that mention it are fetched and the comment ones
# dropped, and what is left has to be empty.
if grep -rhF --include='*.gradle' --exclude-dir=build -- '-XX:+UnlockExperimentalVMOptions' . \
     | grep -qvE '^[[:space:]]*//'; then
   echo "solve12: the flag was unlocked in code -- JEP 519 made it a product option in 25" >&2
   echo "solve12: and an unknown +Unlock... on a JVM that does not need it is not free" >&2
   exit 1
fi
if ! grep -rqF --include='*.gradle' --exclude-dir=build 'tasks.withType(Test)' .; then
   echo "solve12: no build file configures Test tasks by type -- a flag written into one" >&2
   echo "solve12: named test block reaches one project's one Test task" >&2
   exit 1
fi
echo "solve12: -XX:+UseCompactObjectHeaders written once, not unlocked, on every Test task by type"

# ==========================================================================
# 6. The things this step deliberately does not do.
# ==========================================================================
# Asserted, not merely described. Each is an edit that looks like finishing the
# job and is not one, and a guard is cheaper than the argument.
n=$(grep -rhoF --include='*.java' --exclude-dir=build '@SuppressWarnings' . | wc -l || true)
if [ "${n}" != 44 ]; then
   echo "solve12: ${n} @SuppressWarnings on the tree, wanted 44" >&2
   exit 1
fi
r=$(grep -rlE --include='*.java' --exclude-dir=build '^public( [a-z-]+)* record ' . | wc -l || true)
if [ "${r}" != 0 ]; then
   echo "solve12: ${r} type(s) became records -- that is a published API change" >&2
   exit 1
fi
mi=$(find . -name 'module-info.java' -not -path '*/build/*' || true)
if [ -n "${mi}" ]; then
   echo "solve12: a module-info.java was written: ${mi}" >&2
   echo "solve12: step 08 claims names only, and a module IMPORT is not a module DECLARATION" >&2
   exit 1
fi
# Steps 08 through 11, still standing. The ninth trap in the long form: a
# step-k requirement that quietly makes a step-k-1 test unpassable is invisible
# from inside a step-local mean, and deleting 450-odd import lines out of 171
# files is exactly the kind of edit that could take one of them with it.
for term in AccessController doPrivileged PrivilegedAction PrivilegedActionException \
            PrivilegedExceptionAction GetTcclAction; do
   n=$(grep -rhowE --include='*.java' --exclude-dir=build "${term}" . | wc -l || true)
   if [ "${n}" != 0 ]; then
      echo "solve12: ${n} occurrence(s) of ${term} survive -- step 11 retired that API" >&2
      exit 1
   fi
done
h=$(grep -rhoF --include='*.java' --exclude-dir=build '.unreflectVarHandle(' . | wc -l || true)
s=$(grep -rhoF --include='*.java' --exclude-dir=build '.setAccessible(' . | wc -l || true)
if [ "${h}" != 4 ] || [ "${s}" != 10 ]; then
   echo "solve12: ${h} VarHandle site(s) and ${s} setAccessible call(s), wanted 4 and 10" >&2
   echo "solve12: step 11 converted four and held ten, and five transcripts observe the ten" >&2
   exit 1
fi
CATCH_UNNAMED='catch *\( *(final +)?[A-Za-z_$][A-Za-z0-9_$.]*( *\| *[A-Za-z_$][A-Za-z0-9_$.]*)* +_ *\)'
u=$(grep -rhoE --include='*.java' --exclude-dir=build "${CATCH_UNNAMED}" \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
t=$(grep -rhoE --include='*.java' --exclude-dir=build 'catch *\(' \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
if [ "${u}" != 28 ] || [ "${t}" != 80 ]; then
   echo "solve12: ${u} of ${t} catch parameter(s) unnamed, wanted 28 of 80 -- step 11 wrote those" >&2
   exit 1
fi
PATCASE='^ *case [A-Za-z_$][A-Za-z0-9_$.]*( *<[^>]*>)? +[a-z][A-Za-z0-9_$]* *->'
p=$(grep -rhoE --include='*.java' --exclude-dir=build "${PATCASE}" . | wc -l || true)
g=$(grep -rhoF --include='*.java' --exclude-dir=build '.getFirst()' \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
if [ "${p}" != 10 ] || [ "${g}" != 7 ]; then
   echo "solve12: ${p} pattern case label(s) and ${g} getFirst() read(s), wanted 10 and 7" >&2
   echo "solve12: step 10 wrote those and this rung does not revisit them" >&2
   exit 1
fi
sl=$(grep -rhoE --include='*.java' --exclude-dir=build '^public [a-z -]*sealed (class|interface) ' . | wc -l || true)
tb=$(grep -rlF --include='*.java' --exclude-dir=build '"""' . | wc -l || true)
if [ "${sl}" != 11 ] || [ "${tb}" != 1 ]; then
   echo "solve12: ${sl} sealed declaration(s) and ${tb} text-block file(s), wanted 11 and 1" >&2
   echo "solve12: step 09 wrote those and this rung does not revisit them" >&2
   exit 1
fi
# Step 08's provider lookup, which is the one inherited site the module-import
# transform could plausibly have broken: `java.util.ServiceLoader.Provider` is
# a MEMBER type, no module import reaches it, and a rewrite that deleted its
# import would take this line's compile with it. Asked as `at least one`
# because step 11's two lineages write the name differently -- one imports it,
# the other writes it out -- and both are still naming providers without
# loading them, which is what step 08 asked for.
pv=$(grep -c 'Provider::type' \
      impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java || true)
if [ "${pv}" -lt 1 ]; then
   echo "solve12: impl-base no longer names providers without loading them" >&2
   echo "solve12: step 08 moved that lookup onto java.util.ServiceLoader" >&2
   exit 1
fi
amn=$(grep -rlF --include='*.gradle' "attributes('Automatic-Module-Name'" gradle | wc -l || true)
if [ "${amn}" -lt 1 ]; then
   echo "solve12: no build file under gradle/ writes Automatic-Module-Name" >&2
   echo "solve12: step 08's twelve module names are published from there" >&2
   exit 1
fi
echo "solve12: 44 @SuppressWarnings, no records, no module-info, and steps 08 through 11 undisturbed"

echo "solve: step 12 complete"

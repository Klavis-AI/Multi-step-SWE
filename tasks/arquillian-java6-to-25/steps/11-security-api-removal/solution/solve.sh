#!/usr/bin/env bash
# Golden solution, step 11 -- 11-security-api-removal.
#
# Four transforms over the tree step 10 left behind, and ten decisions not to
# make a fifth. Nothing here compiles anything; the build that proves the edits
# is the one the step is graded by.
#
#   the level        the build script that declares the compiler level asks for
#                    release 22 and moves the resolvable-classpath JVM-version
#                    attribute with it, so the whole tree moves from major 65
#                    to major 66 in two lines.
#   the privileged   twenty `AccessController.doPrivileged(..)` sites across the
#                    seven SecurityActions classes lose the wrapper, the seven
#                    `GetTcclAction` enums that existed only to be handed to
#                    one of them are deleted, and the seven
#                    `PrivilegedActionException` unwrap ladders collapse to the
#                    call they were unwrapping
#   the unnamed      every catch parameter that is never read in its own block
#                    is renamed to `_` -- twenty-eight of them on the tree the
#                    first transform leaves
#   the handles      four private-field reflection sites stop calling
#                    `setAccessible(true)` and go through
#                    `MethodHandles.privateLookupIn(..).unreflectVarHandle(..)`
#
# WHY THE LEVEL MOVES AGAIN, AND WHY TO 22 AND NOT 25. `_` as a declaration is
# a compile error before release 22 and a keyword from 22 on -- the unnamed
# transform does not compile at 21 by construction. Nothing above 22 that this
# tree can spend is final: stream gatherers are preview in 23, the class-file
# API is preview through 23 and there is no bytecode tooling here to point it
# at, and there is no JNI for the FFM API to replace (NOTES 16.1).
#
# THE SECOND MUST-FLIP IS A BUILD FLAG. `AccessController.doPrivileged` and
# the three types around it are deprecated for removal (JEP 411) and the
# security manager they served is permanently disabled from JDK 24 (JEP 486).
# The tree this script is handed FAILS `-Xlint:removal -Werror` with twenty
# removal warnings; the tree it leaves PASSES with none. That flag is the
# verifier's, not the build's: nothing here adds it to build.gradle.
#
# THE TEN REFLECTION SITES THAT STAY. Fourteen `setAccessible(true)` calls
# stand on the tree and four convert. Six of the ten that stay are inside the
# SecurityActions annotation scanners, which hand `Field` and `Method` objects
# back to their callers -- there is no access here to route through a handle.
# The other four are three `Constructor.newInstance` calls and one
# `Method.invoke`, and converting any of them changes what a caller sees:
# reflection reports inaccessibility as `IllegalAccessException` and wraps a
# member's own exception in `InvocationTargetException`, and a method handle
# does neither. FOUR RECORDED TRANSCRIPTS observe that reflective outcome at
# THIS TREE's sites, and a candidate who converts them moves all four -- they
# are baseline/vhids.txt. A fifth recording carries the same exception out of
# `java.util.ServiceLoader` inside java.base, with no site here on its path;
# it scales B-15.3 and counting it here would price one regression on two
# legs (NOTES 16.8).
#
# THE SEVEN CATCH BLOCKS THAT ARE NOT RENAMED BUT DELETED. Thirty-five catch
# parameters go unread on the tree this script is handed. Seven of them are
# the `catch (ClassCastException cce)` inside the unwrap ladders the privileged
# transform removes whole, so twenty-eight is what is left to rename -- and the
# order of the two transforms is not free. The count on this rung is a
# PROPERTY of the finished tree (no catch parameter that is never read keeps a
# name), not a list of sites, for exactly that reason.
#
# The block below is embedded verbatim from authoring/solution-src/java22.py by
# authoring/mk-solve11.py, which reads it back out of this file afterwards and
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
# java22.py refuses a tree that does not hold every anchor it was computed
# against -- but a solve.sh that let those lines scroll past unread would still
# be trusting that the program ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it the commonest failure -- run against a tree that is not the one step 10
# produces -- kills the script on the line below with java22.py's whole
# explanation still sitting unprinted inside ${j22}.
j22=$(python3 - "${TREE}" 2>&1 <<'PY_JAVA22'
#!/usr/bin/env python3
"""Step 11's migration, applied to a tree in place.

Java 22, and the level moves for the third rung in four -- 65 to 66. Three
transforms, and unlike every rung below this one the first of them is a
DELETION rather than a rewrite:

  the level        every .gradle file that declares the compiler level has the
                   number replaced wherever it appears, so the tree compiles at
                   release 22 and lands at major 66.
  the privileged   twenty `AccessController.doPrivileged(..)` call sites across
                   the seven SecurityActions classes lose the privileged
                   wrapper, the seven `GetTcclAction` singleton enums that
                   existed only to be passed to one of them are deleted, and
                   the `PrivilegedActionException` unwrap ladder each
                   `getConstructor` was built on collapses to the call it was
                   unwrapping.
  the unnamed      every catch parameter whose name is never read in its own
                   block is renamed to `_` (JEP 456).
  the handles      four private-field reflection sites stop calling
                   `setAccessible(true)` and route through
                   `MethodHandles.privateLookupIn(..).unreflectVarHandle(..)`.

WHY THE LEVEL HAS TO MOVE, AND WHY IT IS 22 AND NOT 23, 24 OR 25. `_` as a
declaration name is a compile error before release 22 and a keyword from 22 on;
this program's second transform does not compile at 21 by construction, which
is what makes the level a must-flip rather than a preference. It is not higher
than 22 because nothing above 22 that this tree can spend is final: stream
gatherers are preview in 23, the class-file API is preview through 23 and there
is no ASM-based tooling here to point it at, and there is no JNI in this tree
for the FFM API to replace. NOTES 16.1 carries the measurement for each.

WHY THE PRIVILEGED API GOES. `AccessController.doPrivileged` and the three
types around it are deprecated for removal (JEP 411), the security manager is
permanently disabled from JDK 24 (JEP 486), and `-Xlint:removal -Werror` is
therefore the second must-flip of this rung: the tree this program is handed
FAILS that build with twenty removal warnings and the tree it leaves PASSES it
with none. The twenty are not twenty copies of one edit -- they are three
shapes, and the three need three different things done to them:

  SEVEN are `AccessController.doPrivileged(GetTcclAction.INSTANCE)`, where the
  action is a private enum singleton whose `run()` body is one line:
  `Thread.currentThread().getContextClassLoader()`. The call site becomes that
  line and the enum goes with it. This is the shape that changes the ARTIFACT
  as well as the source -- `SecurityActions$GetTcclAction.class` stops being
  emitted, and the servlet protocol archive whose file tree thirty-six recorded
  transcripts enumerate is one entry shorter for it (NOTES 16.7).

  SEVEN are `getConstructor`, where the privileged call is inside a `try` whose
  `catch (PrivilegedActionException pae)` exists only to unwrap what the
  privileged call wrapped. Remove the wrapper and there is nothing to unwrap:
  `clazz.getConstructor(argumentTypes)` throws `NoSuchMethodException` directly
  and the method already declares it. The whole ladder -- the rethrow, the
  `(RuntimeException) t` cast and the `ClassCastException` it guards against --
  is dead code once the wrapper is gone, and leaving it in place would be a
  `catch` of an exception the `try` can no longer throw.

  SIX are an inline `(PrivilegedAction<List<X>>) () -> { .. }` lambda whose
  body is the annotation scan itself. The body moves out to where the call was
  and the lambda's trailing `return foundX;` becomes the declaration's
  initialiser. THIS IS THE SHAPE THAT DEFEATS A CODEMOD: the three variants of
  the scanner across the seven classes are not textually identical, so a rule
  that matched one of them leaves eleven of the twenty standing. NOTES 16.3
  carries the md5s.

THE OPERATION IS STRUCTURAL AND NOT A TEMPLATE MATCH. Brace matching finds the
end of each lambda body and each enum declaration, because the seven classes
carry three variants of the scanner and two signatures of `getConstructor`.

WHY ONLY THE CATCH PARAMETERS ARE UNNAMED, of JEP 456's five shapes. The other
four were measured on this tree and are empty. There is no unused enhanced-for
variable: the one the first scan reported was a regex artifact, and `field` is
read on the line below. There is no try-with-resources whose resource is never
touched. There is no lambda parameter that goes unread -- the six the first
scan reported were `case CLIENT ->` and `default ->` switch arrow labels that
steps 09 and 10 wrote, not lambdas at all. Both false positives were found by
reading the sites rather than by trusting the count (NOTES 16.8).

THE CATCH RENAMES ARE ADJUDICATED BY javac AND NOT BY THIS PROGRAM'S REGEX. A
text scan cannot see a name read inside a nested class body and cannot see
shadowing, so it over-reports. It does not matter: reading `_` is itself a
compile error from release 22 ("as of release 22, '_' is a keyword and may not
be used as a reference"), so a parameter this program renames that WAS in fact
read fails the build at the exact line that read it. The scan proposes and the
compiler disposes -- which is the only way to be sure on a tree where the
transform above has already deleted seven catch blocks and moved every line
number below them. On this tree the compiler accepted all of them and no site
had to be held back.

THE POPULATION IS A PROPERTY AND NOT A SITE LIST for the same reason. Thirty-
five catch parameters go unread on the tree this program is handed; seven of
them are inside the `PrivilegedActionException` ladders the transform above
deletes, so twenty-eight is what is left to rename -- and twenty-eight is a
consequence of the first transform rather than an independent fact. What this
rung asks for is `no catch parameter that is never read keeps a name`, which is
true of both trees and is what the verifier scores.

WHY FOUR REFLECTION SITES AND NOT FOURTEEN. Fourteen `setAccessible(true)`
calls stand on this tree and ten of them stay, for two measured reasons:

  SIX are inside the SecurityActions annotation scanners -- four `Field` and
  two `Method` -- which HAND those objects back to their callers. There is no
  access to route through a handle here, because this method does not do the
  access.

  FOUR change observable behaviour. Three are `Constructor.newInstance` and
  one is `Method.invoke`; both wrap whatever the member throws in
  `InvocationTargetException` and report inaccessibility as
  `IllegalAccessException`, and `MethodHandle.invoke` wraps nothing.
  FOUR RECORDED TRANSCRIPTS observe exactly that outcome at these sites --
  domain.container-create-config-no-public-ctor,
  domain.container-create-config-hidden-class,
  domain.protodef-create-configuration-no-public-ctor and
  webrunner.security-new-instance-non-public-class all record
  `java.lang.IllegalAccessException`, and they are baseline/vhids.txt.
  svc.sl-package-private-class records that exception too, but java.base's
  `ServiceLoader$ProviderImpl` raises it about a probe provider class and no
  site in varhandle.txt is on its path -- it is B-15.3's, and a list that took
  all five would charge one regression to two legs.

The remaining four are plain private-field reads and writes whose only failure
modes are argument and access errors that no transcript records, and converting
them was measured to move no transcript at all (NOTES 16.8).

THE ORDER OF THE THREE TRANSFORMS IS NOT FREE. The privileged transform runs
first because it deletes seven catch blocks the unnamed transform would
otherwise rename; the handle transform runs last because it rewrites two lines
inside a method whose catch parameter the unnamed transform may have touched.
"""
import os
import re
import sys

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
    nothing, and the chained oracle runs every solution in order.
    """
    text = read(rel)
    n = text.count(old)
    if n != 1:
        sys.exit("java22: %s: expected 1 occurrence of %r, found %d (%s)"
                 % (rel, old[:70], n, why))
    if new in text:
        sys.exit("java22: %s: the replacement is already present (%s)"
                 % (rel, why))
    write(rel, text.replace(old, new))


def sources(scope=None):
    """Every .java file on the tree, build output excluded.

    `scope="main"` restricts to src/main, which is where every population on
    this rung lives and where rule 6 says the repo's own tests do not.
    """
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build")]
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


def match_fwd(txt, i, open_c, close_c):
    """`i` indexes an `open_c`; return the index of its matching `close_c`.

    String literals are skipped, because a brace inside one is not a brace.
    """
    d = 0
    while i < len(txt):
        c = txt[i]
        if c == open_c:
            d += 1
        elif c == close_c:
            d -= 1
            if d == 0:
                return i
        elif c == '"':
            i += 1
            while i < len(txt) and txt[i] != '"':
                if txt[i] == "\\":
                    i += 1
                i += 1
        i += 1
    sys.exit("java22: unbalanced %s in a block this program has to bound" % open_c)


# The populations the census reads. Each is a spelling that exists on the way
# in, so every figure below is a delta against a measured number rather than a
# constant this program carries.
ACCESSCONTROLLER = r"\bAccessController\b"
DOPRIVILEGED = r"\bdoPrivileged\b"
PRIVACTION = r"\bPrivilegedAction\b"
PRIVACTIONEXC = r"\bPrivilegedActionException\b"
PRIVEXCACTION = r"\bPrivilegedExceptionAction\b"
GETTCCLENUM = r"\benum GetTcclAction\b"
SETACCESSIBLE = r"\bsetAccessible\b"
MHANDLES = r"\bMethodHandles\b"
UNREFLECT = r"\bunreflectVarHandle\b"

CATCH = re.compile(r"(catch\s*\(\s*(?:final\s+)?[\w.]+(?:\s*\|\s*[\w.]+)*\s+)"
                   r"(\w+)(\s*\)\s*\{)")


def catch_sites(path):
    """(name, body, start) for every catch clause in `path`.

    The body is bounded by brace matching rather than by a regex, so a catch
    whose block contains an anonymous class or a lambda is bounded correctly.
    """
    with open(path, encoding="utf-8") as fh:
        txt = fh.read()
    out = []
    for m in CATCH.finditer(txt):
        brace = m.end() - 1
        out.append((m.group(2), txt[brace + 1:match_fwd(txt, brace, "{", "}")],
                    m.start(2), m.end(2)))
    return txt, out


def catch_census():
    """(unnamed, named_read, named_unread) over the main sources."""
    unnamed = read_ = unread = 0
    for path in sources("main"):
        _, sites = catch_sites(path)
        for name, body, _s, _e in sites:
            if name == "_":
                unnamed += 1
            elif re.search(r"\b%s\b" % re.escape(name), body):
                read_ += 1
            else:
                unread += 1
    return unnamed, read_, unread


before_ac = count(ACCESSCONTROLLER)
before_dp = count(DOPRIVILEGED)
before_pa = count(PRIVACTION)
before_pae = count(PRIVACTIONEXC)
before_pea = count(PRIVEXCACTION)
before_enum = count(GETTCCLENUM)
before_sa = count(SETACCESSIBLE)
before_mh = count(MHANDLES)
before_catch = catch_census()

# ---------------------------------------------------------------------------
# 1. The level.
# ---------------------------------------------------------------------------
# A SWEEP OF THE BUILD SCRIPTS AND NOT ONE NAMED FILE, which is step 09's
# lesson carried forward through step 10: more than one finished predecessor
# reaches this program and they do not all put the level in the same file.
#
# AND THE LEVEL IS TWO NUMBERS IN THIS BUILD, NOT ONE. Step 01 pinned an
# explicit `TargetJvmVersion` attribute on the four resolvable classpaths so
# that Gradle's variant resolution would stop asking for a JVM version the
# build no longer emits. Move `options.release` alone and every dependency
# resolution fails with "is only compatible with JVM runtime version 22 or
# newer" -- measured, in four arms, before this line was written (NOTES 16.5).
WAS, NOW = "21", "22"
LEVEL_SITES = (
    ("JavaVersion.VERSION_" + WAS, "JavaVersion.VERSION_" + NOW),
    ("options.release = " + WAS, "options.release = " + NOW),
    ("TARGET_JVM_VERSION_ATTRIBUTE, " + WAS,
     "TARGET_JVM_VERSION_ATTRIBUTE, " + NOW),
)
COMMENT = re.compile(r"^\s*//")
BARE_WAS = re.compile(r"(?<![\w.])" + WAS + r"(?![\w.])")


def buildfiles():
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames
                       if d not in (".git", "build", ".gradle")]
        for nm in sorted(names):
            if nm.endswith(".gradle"):
                yield os.path.relpath(os.path.join(dirpath, nm), tree)


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
    sys.exit("java22: no build script declares a Java level at %s; there is "
             "nothing here to move to %s" % (WAS, NOW))
if not [rel for rel in buildfiles()
        if ("options.release = " + NOW) in read(rel)]:
    sys.exit("java22: the level moved in %s but no build script asks for it by "
             "release" % ", ".join(level_files))

# ---------------------------------------------------------------------------
# 2. The privileged-action API.
# ---------------------------------------------------------------------------
TERMS = ("AccessController", "PrivilegedAction", "PrivilegedActionException",
         "PrivilegedExceptionAction")

TCCL_CALL = "AccessController.doPrivileged(GetTcclAction.INSTANCE)"
TCCL_PLAIN = "Thread.currentThread().getContextClassLoader()"

GETCTOR_RE = re.compile(
    r"(?P<head>   static (?:<T> )?Constructor<[^>]*(?:>[^>]*)?> getConstructor\("
    r".*?\n         throws NoSuchMethodException\n   \{\n)"
    r"      try\n      \{\n"
    r"         return AccessController\.doPrivileged\(\n"
    r"            \(PrivilegedExceptionAction<(?P<ret>.*?)>\) \(\) -> clazz"
    r"\.getConstructor\(argumentTypes\)\);\n"
    r"      \}\n"
    r"      // Unwrap\n"
    r"      catch \(final PrivilegedActionException pae\)\n"
    r"      \{\n"
    r"(?:.*?\n)*?"
    r"      \}\n"
    r"   \}\n", re.S)

BANNER = re.compile(r"\n   //-+\|\|\n   // Inner Classes -+\|\|\n   //-+\|\|\n")


def drop_gettccl_enum(txt):
    """Delete the `GetTcclAction` singleton and the javadoc above it."""
    m = re.search(r"\n   /\*\*\n    \* Single instance to get the TCCL\n    \*/\n"
                  r"   private enum GetTcclAction implements "
                  r"PrivilegedAction<ClassLoader> \{", txt)
    if not m:
        m = re.search(r"\n   private enum GetTcclAction implements "
                      r"PrivilegedAction<ClassLoader> \{", txt)
        if not m:
            return txt, 0
    brace = txt.index("{", m.end() - 1)
    end = match_fwd(txt, brace, "{", "}")
    j = end + 1
    while j < len(txt) and txt[j] == "\n":
        j += 1
    return txt[:m.start()] + "\n" + txt[j:], 1


def fix_inline_lambda(txt):
    """`List<X> v = doPrivileged((PrivilegedAction<List<X>>) () -> { B });`

    becomes B, inlined where the declaration was, with the lambda's trailing
    `return foundX;` as the declaration's initialiser -- or as the method's own
    `return` where the next line returned the alias verbatim.
    """
    n = 0
    while True:
        m = re.search(r"(?P<ind>   +)(?P<decl>List<(?P<ty>\w+)> (?P<var>\w+) = )"
                      r"AccessController\.doPrivileged\(\(PrivilegedAction"
                      r"<List<(?P=ty)>>\) \(\) ->\n(?P=ind)\{\n", txt)
        if not m:
            return txt, n
        brace = txt.index("{", m.end() - 2)
        end = match_fwd(txt, brace, "{", "}")
        body = txt[brace + 1:end]
        tail = txt[end:]
        mm = re.match(r"\}\);\n", tail)
        if not mm:
            sys.exit("java22: unexpected lambda tail: %r" % tail[:40])
        ind = m.group("ind")
        # the body is indented one level deeper than the statement was; the
        # statement's own level is what it has to come back to
        body_txt = "\n".join(l[3:] if l.startswith(ind + "   ") else l
                             for l in body.split("\n")).strip("\n")
        rm = re.search(r"\n?%sreturn (\w+);\s*$" % re.escape(ind), body_txt)
        if not rm:
            sys.exit("java22: no trailing return in a privileged lambda body")
        produced = rm.group(1)
        body_txt = body_txt[:rm.start()]
        rest = txt[end + len(mm.group(0)):]
        rv = re.match(r"%sreturn %s;\n" % (re.escape(ind),
                                           re.escape(m.group("var"))), rest)
        if rv:
            # the alias existed only to be returned on the next line
            new = body_txt + "\n" + ind + "return " + produced + ";\n"
            txt = txt[:m.start()] + new + rest[rv.end():]
        else:
            new = body_txt + "\n" + ind + m.group("decl") + produced + ";\n"
            txt = txt[:m.start()] + new + rest
        n += 1


priv_files, n_tccl, n_ctor, n_lam, n_enum = [], 0, 0, 0, 0
for path in sorted(sources("main")):
    if os.path.basename(path) != "SecurityActions.java":
        continue
    with open(path, encoding="utf-8") as fh:
        txt = fh.read()
    if "AccessController" not in txt:
        continue
    rel = os.path.relpath(path, tree)
    orig = txt
    k = txt.count(TCCL_CALL)
    txt = txt.replace(TCCL_CALL, TCCL_PLAIN)
    n_tccl += k
    txt, kc = GETCTOR_RE.subn(
        lambda m: m.group("head")
        + "      return clazz.getConstructor(argumentTypes);\n   }\n", txt)
    n_ctor += kc
    txt, kl = fix_inline_lambda(txt)
    n_lam += kl
    txt, ke = drop_gettccl_enum(txt)
    n_enum += ke
    if ke:
        # the banner announced a section that no longer has anything in it
        txt = BANNER.sub("\n", txt)
    txt = re.sub(r"\n[ \t]+\n\n\}\n*$", "\n}\n", txt)
    for t in TERMS:
        txt = re.sub(r"^import java\.security\.%s;\n" % t, "", txt, flags=re.M)
    left = [t for t in TERMS if re.search(r"\b%s\b" % t, txt)]
    if left:
        sys.exit("java22: %s still names %s after the privileged transform"
                 % (rel, ", ".join(left)))
    if txt == orig:
        sys.exit("java22: %s holds AccessController and nothing matched" % rel)
    write(rel, txt)
    priv_files.append(rel)

if len(priv_files) != 7:
    sys.exit("java22: %d SecurityActions class(es) hold the privileged API, "
             "expected 7" % len(priv_files))

# ---------------------------------------------------------------------------
# 3. The unnamed catch parameters.
# ---------------------------------------------------------------------------
# Proposed here, adjudicated by javac. See the header: reading `_` does not
# compile from release 22, so a rename this scan gets wrong is a build failure
# at the line that read it rather than a silent behaviour change.
unnamed_sites = []
for path in sorted(sources("main")):
    txt, sites = catch_sites(path)
    rel = os.path.relpath(path, tree)
    out, pos, changed = [], 0, False
    for name, body, s, e in sites:
        if name == "_" or re.search(r"\b%s\b" % re.escape(name), body):
            continue
        out.append(txt[pos:s])
        out.append("_")
        pos = e
        changed = True
        unnamed_sites.append((rel, txt[:s].count("\n") + 1, name))
    if changed:
        out.append(txt[pos:])
        write(rel, "".join(out))

# ---------------------------------------------------------------------------
# 4. The four private-field reflection sites.
# ---------------------------------------------------------------------------
IMPLB = "impl-base/src/main/java/org/jboss/arquillian/impl/"
LOOKUP = ("MethodHandles.privateLookupIn(field.getDeclaringClass(), "
          "MethodHandles.lookup()).unreflectVarHandle(field)")

sub1(IMPLB + "core/EventPointImpl.java",
     "         field.setAccessible(true);\n"
     "         field.set(target, value);\n",
     "         %s.set(target, value);\n" % LOOKUP,
     "the event point's field write")

# The asymmetry with EventPointImpl -- this catch wraps e.getCause() rather
# than e, and getCause() is null for an argument failure both before and after
# -- is the tree's and is preserved exactly as it was found.
sub1(IMPLB + "core/InjectionPointImpl.java",
     "         field.setAccessible(true);\n"
     "         field.set(target, value);\n",
     "         %s.set(target, value);\n" % LOOKUP,
     "the injection point's field write")

# The original's indentation is ragged here. It is left ragged: this rung is
# not a reformatting rung and a whitespace-only diff on a line nobody asked
# about is noise in every review below it.
sub1(IMPLB + "enricher/resource/ArquillianResourceTestEnricher.java",
     "            field.setAccessible(true);\n"
     "               field.set(testCase, value);\n",
     "            %s.set(testCase, value);\n" % LOOKUP,
     "the resource enricher's field write")

# The only one of the four that READS. varHandle.get returns Object and the
# cast that was already there still does the narrowing.
sub1(IMPLB + "client/deployment/tool/ToolingDeploymentFormatter.java",
     "         Field field = obj.getClass().getDeclaredField(fieldName);\n"
     "         field.setAccessible(true);\n"
     "         \n"
     "         return (T)field.get(obj);\n",
     "         Field field = obj.getClass().getDeclaredField(fieldName);\n"
     "         \n"
     "         return (T)%s.get(obj);\n" % LOOKUP,
     "the tooling formatter's field read")

HANDLE_FILES = (
    IMPLB + "core/EventPointImpl.java",
    IMPLB + "core/InjectionPointImpl.java",
    IMPLB + "enricher/resource/ArquillianResourceTestEnricher.java",
    IMPLB + "client/deployment/tool/ToolingDeploymentFormatter.java",
)
for rel in HANDLE_FILES:
    txt = read(rel)
    if "import java.lang.invoke.MethodHandles;" in txt:
        continue
    ms = list(re.finditer(r"^import [\w.]+;\n", txt, re.M))
    if not ms:
        sys.exit("java22: %s has no import block to add MethodHandles to" % rel)
    anchor = ms[-1]
    for m in ms:
        if m.group(0).startswith("import java."):
            anchor = m
    write(rel, txt[:anchor.end()] + "import java.lang.invoke.MethodHandles;\n"
          + txt[anchor.end():])

# ---------------------------------------------------------------------------
# The census.
# ---------------------------------------------------------------------------
after_ac = count(ACCESSCONTROLLER)
after_dp = count(DOPRIVILEGED)
after_pa = count(PRIVACTION)
after_pae = count(PRIVACTIONEXC)
after_pea = count(PRIVEXCACTION)
after_enum = count(GETTCCLENUM)
after_sa = count(SETACCESSIBLE)
after_mh = count(MHANDLES)
after_unreflect = count(UNREFLECT)
after_catch = catch_census()


def gone(name, before, after):
    if after != 0:
        sys.exit("java22: %d %s left on the tree; this rung leaves none"
                 % (after, name))
    if before == 0:
        sys.exit("java22: %s was already absent before the transform ran -- "
                 "this is not the tree step 10 produces" % name)


gone("AccessController reference(s)", before_ac, after_ac)
gone("doPrivileged call(s)", before_dp, after_dp)
gone("PrivilegedAction reference(s)", before_pa, after_pa)
gone("PrivilegedActionException reference(s)", before_pae, after_pae)
gone("PrivilegedExceptionAction reference(s)", before_pea, after_pea)
gone("GetTcclAction enum(s)", before_enum, after_enum)

if (before_ac, before_dp, before_pa, before_pae, before_pea, before_enum) \
        != (27, 20, 20, 14, 14, 7):
    sys.exit("java22: the privileged population on the way in was %s, expected "
             "(27, 20, 20, 14, 14, 7)"
             % str((before_ac, before_dp, before_pa, before_pae, before_pea,
                    before_enum)))
if (n_tccl, n_ctor, n_lam, n_enum) != (7, 7, 6, 7):
    sys.exit("java22: converted %d TCCL read(s), %d getConstructor ladder(s), "
             "%d inline scan(s) and deleted %d enum(s); expected 7, 7, 6, 7"
             % (n_tccl, n_ctor, n_lam, n_enum))

# The unnamed half, both directions. The departure is an ABSOLUTE and not a
# delta: what this rung asks for is a property of the finished tree, and a
# count that merely fell would be satisfied by deleting a catch block.
if after_catch[2] != 0:
    sys.exit("java22: %d catch parameter(s) still carry a name nothing reads"
             % after_catch[2])
if after_catch[0] != len(unnamed_sites) or len(unnamed_sites) != 28:
    sys.exit("java22: %d catch parameter(s) renamed to `_` and %d found on the "
             "tree, expected 28 of each" % (len(unnamed_sites), after_catch[0]))
# The hold half. Seven of the thirty-five went with the ladders the privileged
# transform deleted, and every parameter that IS read kept its name -- seven
# fewer of those too, for the same reason and in the same places.
if before_catch != (0, 59, 35):
    sys.exit("java22: the catch population on the way in was %s, expected "
             "(0 unnamed, 59 read, 35 unread)" % str(before_catch))
if after_catch[1] != 52:
    sys.exit("java22: %d catch parameter(s) are read, expected 52 -- the 59 "
             "that were, less the 7 PrivilegedActionException unwraps the "
             "privileged transform deleted" % after_catch[1])

if before_sa != 14 or after_sa != 10:
    sys.exit("java22: setAccessible went %d -> %d, expected 14 -> 10"
             % (before_sa, after_sa))
if before_mh != 0 or after_unreflect != 4:
    sys.exit("java22: %d unreflectVarHandle site(s) on a tree that started with "
             "%d MethodHandles reference(s), expected 4 and 0"
             % (after_unreflect, before_mh))

# Steps 08, 09 and 10, still standing. The ninth trap in the long form: a
# step-k requirement that quietly makes a step-k-1 test unpassable is invisible
# from inside a step-local mean, and deleting a privileged wrapper out of seven
# classes is exactly the kind of edit that could take a pattern switch with it.
if count(r"^\s*case\s+[A-Z][\w$.]*(?:\s*<[^>\n]*>)?\s+[a-z][\w$]*\s*->") != 10:
    sys.exit("java22: the tree no longer holds step 10's 10 pattern case "
             "labels")
if count(r"\.getFirst\(\)") != 7:
    sys.exit("java22: the tree no longer holds step 10's 7 getFirst() reads")
if count(r'"""') != 2:
    sys.exit("java22: the report page is no longer one text block -- step 09 "
             "wrote it and this rung does not revisit it")
if count(r"\bsealed\b") != 11:
    sys.exit("java22: the tree no longer holds step 09's 11 sealed "
             "declarations")
if not [rel for rel in buildfiles()
        if "attributes('Automatic-Module-Name'" in read(rel)]:
    sys.exit("java22: no build file writes Automatic-Module-Name -- step 08's "
             "twelve module names are published from there")

print("java22: level: release and the resolvable-classpath attribute at 22 in: "
      "%s, %d line(s), %d prose token(s)"
      % (", ".join(level_files), level_lines, level_prose))
print("java22: privileged: %d call site(s) retired across %d SecurityActions "
      "class(es) -- %d TCCL read(s), %d getConstructor ladder(s), %d inline "
      "scan(s) -- and %d GetTcclAction enum(s) deleted"
      % (n_tccl + n_ctor + n_lam, len(priv_files), n_tccl, n_ctor, n_lam,
         n_enum))
print("java22: unnamed: %d catch parameter(s) renamed to `_`, %d that are read "
      "left named, none left named and unread"
      % (len(unnamed_sites), after_catch[1]))
print("java22: handles: %d private-field site(s) routed through a VarHandle, "
      "%d setAccessible call(s) held" % (after_unreflect, after_sa))
print("JAVA22 OK")
PY_JAVA22
) || true
printf '%s\n' "${j22}"
for want in \
      'java22: privileged: 20 call site(s) retired across 7 SecurityActions class(es) -- 7 TCCL read(s), 7 getConstructor ladder(s), 6 inline scan(s) -- and 7 GetTcclAction enum(s) deleted' \
      'java22: unnamed: 28 catch parameter(s) renamed to `_`, 52 that are read left named, none left named and unread' \
      'java22: handles: 4 private-field site(s) routed through a VarHandle, 10 setAccessible call(s) held' \
      'JAVA22 OK'; do
   if ! printf '%s\n' "${j22}" | grep -qxF "${want}"; then
      echo "solve11: java22.py did not report: ${want}" >&2
      exit 1
   fi
done
# The level print line includes the file names, which vary by tree shape.
if ! printf '%s\n' "${j22}" | grep -qE '^java22: level: release and the resolvable-classpath attribute at 22 in:'; then
   echo "solve11: java22.py did not report the level move" >&2
   exit 1
fi

# ==========================================================================
# 2. The level, re-read off the tree.
# ==========================================================================
# The level was moved by java22.py, so this section re-reads it off the tree
# rather than setting it. Three questions: does at least one build script now
# ask for 22 by `--release`, is the resolvable-classpath attribute at 22 beside
# it, and is there any build file still pinning a level that is not 22?
#
# THE ATTRIBUTE IS NOT OPTIONAL. Step 01 pinned `TargetJvmVersion` on the four
# resolvable classpaths, and Gradle turns it into a hard requirement on every
# resolved dependency: with `options.release` at 22 and the attribute at 21,
# every module's dependency resolution fails with "is only compatible with JVM
# runtime version 22 or newer" -- measured in four arms before the golden was
# written (NOTES 16.5).
#
# ASKED AS A SHAPE AND NOT AS A FILE NAME, which is step 09's lesson carried
# through step 10: the finished predecessors put the level in different files,
# and an assertion that named one of them crashed on the tree it was not
# written against.
if ! grep -rnqE --include='*.gradle' --exclude-dir=build \
        'options\.release = 22' .; then
   echo "solve11: no build script asks for options.release = 22 after the level edit" >&2
   exit 1
fi
if ! grep -rnqE --include='*.gradle' --exclude-dir=build \
        'TARGET_JVM_VERSION_ATTRIBUTE, 22' .; then
   echo "solve11: no build script sets the resolvable-classpath JVM version to 22" >&2
   echo "solve11: with release at 22 and the attribute at 21, every dependency" >&2
   echo "solve11: resolution in this build fails" >&2
   exit 1
fi
left=$(grep -rnE --include='*.gradle' --exclude-dir=build \
         'JavaVersion\.VERSION_|JavaLanguageVersion\.of|options\.release|TARGET_JVM_VERSION_ATTRIBUTE, ' . \
       | grep -vE 'VERSION_22|\.of\(22\)|options\.release = 22|TARGET_JVM_VERSION_ATTRIBUTE, 22' || true)
if [ -n "${left}" ]; then
   echo "solve11: a build file still pins a level other than 22:" >&2
   printf '%s\n' "${left}" >&2
   exit 1
fi
# Not added by this script, and asserted absent: the removal lint is the
# verifier's instrument. A `-Werror` left in the build would turn the first
# unrelated warning on the next rung into a build failure.
if grep -rqE --include='*.gradle' --exclude-dir=build -- '-Werror' .; then
   echo "solve11: a build file carries -Werror -- that flag is the verifier's, not the build's" >&2
   exit 1
fi
echo "solve11: the level is 22, asked for by release, with the classpath attribute beside it"

# ==========================================================================
# 3. The privileged-action API.
# ==========================================================================
# `--exclude-dir=build` throughout: a Gradle build leaves copies of sources
# under each project's build/, and this tree additionally keeps two checked-in
# IDE test templates under build/ide that belong to no source set.
#
# EVERY COUNTING PIPELINE ENDS IN `|| true`, and that is `pipefail` and not
# style. `set -o pipefail` gives `grep ... | wc -l` grep's status, so a count
# that comes out as zero -- exactly the case most of these checks exist to
# report -- would abort the script on the assignment with `set -e` and print
# nothing at all.
#
# ABSOLUTES, ALL FIVE. After this rung the tree names none of the four
# java.security types and makes no privileged call, in the main sources or
# anywhere else: the rung retires an API, and an API is retired from a tree
# and not from a source set.
for term in AccessController doPrivileged PrivilegedAction PrivilegedActionException \
            PrivilegedExceptionAction GetTcclAction; do
   n=$(grep -rhowE --include='*.java' --exclude-dir=build "${term}" . | wc -l || true)
   if [ "${n}" != 0 ]; then
      echo "solve11: ${n} occurrence(s) of ${term} survive -- this rung leaves none" >&2
      exit 1
   fi
done
if grep -rqE --include='*.java' --exclude-dir=build '^import java\.security\.' .; then
   echo "solve11: a java.security import survives" >&2
   exit 1
fi
# The classes the API was retired FROM are still there and still do their
# jobs. Seven SecurityActions, seven `getConstructor` declarations (the ladder
# collapsed, the method did not), and the TCCL read is still done twelve times
# -- the same twelve as before, because the seven enum bodies that held it are
# gone and the seven call sites that invoked them hold it now.
sa=$(find . -name SecurityActions.java -not -path '*/build/*' | wc -l || true)
if [ "${sa}" != 7 ]; then
   echo "solve11: ${sa} SecurityActions.java on the tree, wanted 7" >&2
   echo "solve11: the rung retires the API these classes wrap, not the classes" >&2
   exit 1
fi
gc=$(grep -rhoE --include='*.java' --exclude-dir=build 'Constructor<[^>]*>+ getConstructor\(' . | wc -l || true)
if [ "${gc}" != 7 ]; then
   echo "solve11: ${gc} getConstructor declaration(s), wanted 7 -- the ladder collapses, the method stays" >&2
   exit 1
fi
tc=$(grep -rhoF --include='*.java' --exclude-dir=build 'Thread.currentThread().getContextClassLoader()' . | wc -l || true)
if [ "${tc}" != 12 ]; then
   echo "solve11: ${tc} context-classloader read(s), wanted 12 -- the read moves out of the enum, it does not go" >&2
   exit 1
fi
# The inline scans survived the move out of their lambdas: every scanner still
# walks the class hierarchy. The `getSuperclass()` loop is the body that moved.
gs=$(grep -rhoF --include='*.java' --exclude-dir=build 'getSuperclass()' . | wc -l || true)
if [ "${gs}" -lt 6 ]; then
   echo "solve11: ${gs} getSuperclass() walk(s) on the tree, wanted at least 6 -- the scan bodies moved, they did not go" >&2
   exit 1
fi
echo "solve11: the privileged API is gone from the tree; 7 SecurityActions, 7 getConstructor, 12 TCCL reads stand"

# ==========================================================================
# 4. The unnamed catch parameters.
# ==========================================================================
# THE MAIN SOURCES AND NOT THE TREE. The repo's own tests hold eleven catch
# clauses of their own, and rule 6 leaves those alone.
#
# Coarse counts here; the property itself -- no parameter left named and
# unread -- is java22.py's third report line, already checked above. What this
# section adds is the hold: a rename is a rename and not a deletion, so the
# catch population is exactly what the privileged transform left, 80, and the
# 52 that are read kept their names.
CATCH_UNNAMED='catch *\( *(final +)?[A-Za-z_$][A-Za-z0-9_$.]*( *\| *[A-Za-z_$][A-Za-z0-9_$.]*)* +_ *\)'
u=$(grep -rhoE --include='*.java' --exclude-dir=build "${CATCH_UNNAMED}" \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
t=$(grep -rhoE --include='*.java' --exclude-dir=build 'catch *\(' \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
if [ "${u}" != 28 ] || [ "${t}" != 80 ]; then
   echo "solve11: ${u} of ${t} catch parameter(s) in the main sources are unnamed, wanted 28 of 80" >&2
   echo "solve11: 94 on the way in, less the 7 unwrap ladders the privileged transform deleted" >&2
   exit 1
fi
# `_` is a declaration only. A read of it does not compile at 22 and this
# script does not compile anything, so the one thing it CAN check is that no
# `_` appears anywhere a catch parameter would be read from.
if grep -rqE --include='*.java' --exclude-dir=build '[^A-Za-z0-9_$]_\.(getMessage|getCause|printStackTrace)' .; then
   echo "solve11: an unnamed catch parameter is read -- that is a compile error at 22" >&2
   exit 1
fi
echo "solve11: 28 catch parameter(s) unnamed, 52 that are read left named, 80 catch clauses held"

# ==========================================================================
# 5. The handles.
# ==========================================================================
h=$(grep -rhoF --include='*.java' --exclude-dir=build '.unreflectVarHandle(' . | wc -l || true)
p=$(grep -rhoF --include='*.java' --exclude-dir=build 'MethodHandles.privateLookupIn(' . | wc -l || true)
s=$(grep -rhoF --include='*.java' --exclude-dir=build '.setAccessible(' . | wc -l || true)
if [ "${h}" != 4 ] || [ "${p}" != 4 ] || [ "${s}" != 10 ]; then
   echo "solve11: ${h} unreflectVarHandle, ${p} privateLookupIn, ${s} setAccessible on the tree, wanted 4, 4, 10" >&2
   exit 1
fi
# The four that convert, by name: each has no setAccessible left and one
# handle. And `privateLookupIn` rather than `lookup()` alone, because the
# field is private to a class in another package and a plain lookup has no
# access to it.
for src in impl-base/src/main/java/org/jboss/arquillian/impl/core/EventPointImpl.java \
           impl-base/src/main/java/org/jboss/arquillian/impl/core/InjectionPointImpl.java \
           impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/ArquillianResourceTestEnricher.java \
           impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/tool/ToolingDeploymentFormatter.java; do
   if grep -qF '.setAccessible(' "${src}"; then
      echo "solve11: ${src} still calls setAccessible -- that is one of the four that convert" >&2
      exit 1
   fi
   k=$(grep -oF '.unreflectVarHandle(' "${src}" | wc -l || true)
   if [ "${k}" != 1 ]; then
      echo "solve11: ${src} holds ${k} VarHandle site(s), wanted 1" >&2
      exit 1
   fi
   if ! grep -qF 'import java.lang.invoke.MethodHandles;' "${src}"; then
      echo "solve11: ${src} does not import MethodHandles" >&2
      exit 1
   fi
done
# The ten that stay, by file and by count. Nothing else in this image can see
# this mistake: a constructor routed through `unreflectConstructor` compiles,
# and the five transcripts that would move are the verifier's to replay.
for pair in 'impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java|1' \
            'impl-base/src/main/java/org/jboss/arquillian/impl/core/Reflections.java|1' \
            'spi/src/main/java/org/jboss/arquillian/spi/util/ServiceLoader.java|1' \
            'impl-base/src/main/java/org/jboss/arquillian/impl/MapObject.java|1' \
            'impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/SecurityActions.java|1' \
            'testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/SecurityActions.java|1' \
            'testenrichers/ejb/src/main/java/org/jboss/arquillian/testenricher/ejb/SecurityActions.java|2' \
            'testenrichers/resource/src/main/java/org/jboss/arquillian/testenricher/resource/SecurityActions.java|2'; do
   src=${pair%%|*}; want=${pair#*|}
   got=$(grep -oF '.setAccessible(' "${src}" | wc -l || true)
   if [ "${got}" != "${want}" ]; then
      echo "solve11: ${src} calls setAccessible ${got} time(s), wanted ${want} -- that site stays" >&2
      exit 1
   fi
done
if grep -rqF --include='*.java' --exclude-dir=build 'unreflectConstructor(' .; then
   echo "solve11: a constructor was routed through a method handle -- five transcripts observe the reflective outcome" >&2
   exit 1
fi
echo "solve11: 4 private-field site(s) through a VarHandle, 10 reflection site(s) held where measured"

# ==========================================================================
# 6. The things this step deliberately does not do.
# ==========================================================================
# Asserted, not merely described. Each is an edit that looks like finishing the
# job and is not one, and a guard is cheaper than the argument.
n=$(grep -rhoF --include='*.java' --exclude-dir=build '@SuppressWarnings' . | wc -l || true)
if [ "${n}" != 44 ]; then
   echo "solve11: ${n} @SuppressWarnings on the tree, wanted 44" >&2
   echo "solve11: a removal warning is retired by retiring the call, not by suppressing it" >&2
   exit 1
fi
if grep -rqE --include='*.java' --exclude-dir=build '@SuppressWarnings\(.*"removal"' .; then
   echo "solve11: a \"removal\" suppression was written -- that hides the warning and keeps the call" >&2
   exit 1
fi
r=$(grep -rlE --include='*.java' --exclude-dir=build '^public( [a-z-]+)* record ' . | wc -l || true)
if [ "${r}" != 0 ]; then
   echo "solve11: ${r} type(s) became records -- that is a published API change" >&2
   exit 1
fi
mi=$(find . -name 'module-info.java' -not -path '*/build/*' || true)
if [ -n "${mi}" ]; then
   echo "solve11: a module-info.java was written: ${mi}" >&2
   echo "solve11: step 08 claims names only, and this rung does not revisit that" >&2
   exit 1
fi
# No preview feature. Stream gatherers and the class-file API are preview at
# 22 and 23, and a tree that needs --enable-preview to compile has not moved
# to a release, it has moved to a flag.
if grep -rqE --include='*.gradle' --exclude-dir=build -- '--enable-preview' .; then
   echo "solve11: --enable-preview in a build file -- nothing on this rung is preview" >&2
   exit 1
fi
if grep -rqE --include='*.java' --exclude-dir=build '\.gather\(|java\.util\.stream\.Gatherer|java\.lang\.classfile' .; then
   echo "solve11: a preview API is used -- gatherers and the class-file API are not final at 22" >&2
   exit 1
fi
# Steps 08, 09 and 10, still standing. The ninth trap in the long form: a
# step-k requirement that quietly makes a step-k-1 test unpassable is invisible
# from inside a step-local mean, and deleting a wrapper out of seven classes is
# exactly the kind of edit that could take a pattern switch with it.
PATCASE='^ *case [A-Za-z_$][A-Za-z0-9_$.]*( *<[^>]*>)? +[a-z][A-Za-z0-9_$]* *->'
p=$(grep -rhoE --include='*.java' --exclude-dir=build "${PATCASE}" . | wc -l || true)
g=$(grep -rhoF --include='*.java' --exclude-dir=build '.getFirst()' \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
if [ "${p}" != 10 ] || [ "${g}" != 7 ]; then
   echo "solve11: ${p} pattern case label(s) and ${g} getFirst() read(s), wanted 10 and 7" >&2
   echo "solve11: step 10 wrote those and this rung does not revisit them" >&2
   exit 1
fi
s=$(grep -rhoE --include='*.java' --exclude-dir=build '^public [a-z -]*sealed (class|interface) ' . | wc -l || true)
tb=$(grep -rlF --include='*.java' --exclude-dir=build '"""' . | wc -l || true)
if [ "${s}" != 11 ] || [ "${tb}" != 1 ]; then
   echo "solve11: ${s} sealed declaration(s) and ${tb} text-block file(s), wanted 11 and 1" >&2
   echo "solve11: step 09 wrote those and this rung does not revisit them" >&2
   exit 1
fi
if ! grep -qF 'Provider::type' \
      impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java; then
   echo "solve11: impl-base no longer names providers without loading them" >&2
   echo "solve11: step 08 moved that lookup onto java.util.ServiceLoader" >&2
   exit 1
fi
amn=$(grep -rlF --include='*.gradle' "attributes('Automatic-Module-Name'" gradle | wc -l || true)
if [ "${amn}" -lt 1 ]; then
   echo "solve11: no build file under gradle/ writes Automatic-Module-Name" >&2
   echo "solve11: step 08's twelve module names are published from there" >&2
   exit 1
fi
echo "solve11: 44 @SuppressWarnings, no records, no preview, and steps 08, 09 and 10 undisturbed"

echo "solve: step 11 complete"

#!/usr/bin/env bash
# Golden solution, step 09 -- 09-java17.
#
# Five transforms over the tree step 08 left behind, and four decisions not to
# make a sixth. Nothing here compiles anything; the build that proves the edits
# is the one the step is graded by.
#
#   the level        gradle/java-module.gradle asks for release 17 and declares
#                    source and target compatibility to match. Every module
#                    applies that one file, so the whole tree moves from major
#                    55 to major 61 in three lines.
#   the patterns     twenty-one `instanceof` tests in eighteen methods bind a
#                    pattern variable and drop the cast that followed
#   the document     ServletTestRunner#doGet writes its report page as one text
#                    block instead of sixteen newline-terminated literals
#   the hierarchies  eleven event roots declare `sealed ... permits`, and the
#                    twenty-nine leaves under them become `final`
#   the switches     two colon-form statement switches become arrow form
#
# WHY THE LEVEL MOVES HERE AND MOVED NOWHERE SINCE STEP 07. Java 17 is the
# first release since 11 that gives this library anything it can spend: 16
# finalised pattern matching for `instanceof`, 15 finalised text blocks, 17
# finalised sealed types, and 14 finalised the arrow switch. Steps 08's work --
# module names and the service lookup -- needed no level at all. The class-file
# major is therefore this rung's must-flip and the only one on the ladder that
# is not also a library or an API question.
#
# THE SEVEN `instanceof` TESTS THAT STAY PLAIN. Twenty-eight sites, seven of
# which never cast the thing they tested. Two are in main code:
# `ManagerImpl#fire` asks whether a caught exception is already an
# `InvocationException` before it wraps one, and
# `DeploymentExceptionHandler#transform` asks the same of a
# `DeploymentException`. The other five are assertions in tests, asking whether
# what was thrown is the class the test expected. A pattern variable at any of
# them is a variable nothing reads, and writing one is a different edit from
# this one -- it makes five files noisier and buys nothing. A sweep that bound
# every `instanceof` in the tree would look like more of this rung's work and
# is less of it.
#
# THE SEVEN NEWLINE LITERALS IN ToolingDeploymentFormatter. `format` and
# `formatNode` build an XML fragment by appending one- and two-character pieces
# around `archive.getName()` and `node.getPath().get()`. There is no run of
# adjacent constants to lift, and a text block wrapped around interleaved
# computed values is a different string. The sixteen in ServletTestRunner are
# the opposite case -- sixteen constant writes in a row spelling one document --
# and that is the whole difference between the two.
#
# THE ELEVEN TYPES THIS LIBRARY IS EXTENDED THROUGH. `ResourceProvider`, the
# context and descriptor bases, `Typed`, `ExecutionEvent`, `LifecycleEvent`,
# the tests' own service doubles. Nothing in this tree subclasses any of them
# from outside, so sealing them would compile and every recorded behaviour
# would replay identically -- which is exactly why the decision has to be made
# on purpose rather than discovered. A test framework's extension points are
# the part of it other people implement, and a `permits` clause is the
# declaration that nobody else may. Two of the eleven are event roots javac
# will not seal here in any case: `ExecutionEvent` and `LifecycleEvent` carry
# step 04's @FunctionalInterface, and a sealed interface is not functional --
# javac rejects the combination outright, so sealing them does not compile.
#
# THE FORTY-FOUR @SuppressWarnings. They are what the tree needs, not what it
# can be talked out of needing, and nothing on this rung adds a forty-fifth: a
# pattern variable removes a cast rather than a warning, and a sealed hierarchy
# raises none.
#
# WHY THE LEAVES ARE `final` AND NOT `non-sealed`. The compiler requires every
# permitted subtype to be one of `final`, `sealed` or `non-sealed`, and will
# take either here. `final` is the honest one: nothing in this tree extends any
# of the twenty-nine, and a `non-sealed` leaf would be re-opening a hierarchy
# the rung above it just closed. Six of the thirty-five permitted names are
# roots themselves -- the vocabulary nests, so a container control event
# permits a deployment event, which permits two of its own -- and those six are
# `sealed` rather than `final` for the same reason.
#
# The block below is embedded verbatim from authoring/solution-src/java17.py by
# authoring/mk-solve09.py, which reads it back out of this file afterwards and
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
# java17.py refuses a tree that does not hold every anchor it was computed
# against -- but a solve.sh that let those lines scroll past unread would still
# be trusting that the program ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it the commonest failure -- run against a tree that is not the one step 08
# produces -- kills the script on the line below with java17.py's whole
# explanation still sitting unprinted inside ${j17}.
j17=$(python3 - "${TREE}" 2>&1 <<'PY_JAVA17'
#!/usr/bin/env python3
"""Step 09's migration, applied to a tree in place.

Java 17, and the first rung since step 07 whose class-file level moves. Five
transforms, one per thing the era gave this library that it can actually use:

  the level        the build scripts that declare the compiler level are updated to ask
                   for release 17; every .gradle file that carries the Java-11 level
                   has the number replaced wherever it appears.
                   source and target compatibility to match. Every module
                   compiles through that one file, so the whole tree moves from
                   major 55 to major 61 in three lines.
  the patterns     twenty-one `instanceof` tests in eighteen methods bind a
                   pattern variable, and the cast that used to follow on the
                   next line -- or inside the expression -- goes with it.
  the document     ServletTestRunner#doGet writes its report page as one text
                   block instead of sixteen newline-terminated literals, and
                   the bytes it serves do not change.
  the hierarchies  eleven event roots declare `sealed ... permits`, naming the
                   thirty-five subtypes between them, and the twenty-nine
                   leaves under those roots become `final`.
  the switches     two colon-form statement switches become arrow form and
                   lose their `break`s.

AND FOUR THINGS THAT LOOK LIKE THEY BELONG HERE AND DELIBERATELY DO NOT MOVE.

THE SEVEN `instanceof` TESTS WITH NOTHING TO BIND. Twenty-eight sites, and
seven of them never cast the thing they tested. Two are in main code:
`ManagerImpl#fire` asks whether a caught exception is already an
`InvocationException` before it wraps one, and
`DeploymentExceptionHandler#transform` asks the same of a `DeploymentException`
before it transforms one. The other five are assertions in tests, asking
whether what was thrown is the class the test expected. A pattern variable at
any of them is a variable nothing reads. Converting them is not a smaller
version of this rung's work; it is a different edit, and one that makes five
files noisier for nothing.

THE SEVEN NEWLINE LITERALS IN ToolingDeploymentFormatter. `format` and
`formatNode` build an XML fragment by appending one- and two-character pieces
around `archive.getName()` and `node.getPath().get()`. There is no run of
adjacent constants to lift out, and a text block wrapped around interleaved
computed values is a different string. The sixteen in ServletTestRunner are the
opposite case and that is why they move: sixteen constant writes in a row that
spell one document.

THE ELEVEN TYPES THIS LIBRARY IS EXTENDED THROUGH. `ResourceProvider`, the
context and descriptor bases, the tests' own service doubles: nothing in this
tree subclasses them from outside, so sealing them would compile and every
recorded behaviour would replay identically. They stay open because a test
framework's extension points are the part of it other people implement, and a
`permits` clause is the declaration that nobody else may.

THE FORTY-FOUR @SuppressWarnings. They are what the tree needs, not what it can
be talked out of needing, and nothing here adds a forty-fifth: a pattern
variable removes a cast rather than a warning, and a sealed hierarchy raises
none.

EVERY EDIT IS ASSERTED, for the reason step 08's script gives: this runs on more
than one tree -- the chained golden, the blind snapshot, the calibration arms --
and a transform that quietly matched nothing produces a tree that builds, passes
family A, and scores zero on the leg it was meant to demonstrate.

WHAT IS COUNTED AND WHAT IS NOT. The census asserts DELTAS where the absolute
is a property of the input tree (bound patterns, `final` leaves) and EQUALITY
where the whole point is that a number did not move (the twenty-eight
`instanceof` sites, the seven held newline literals). The one absolute it does
assert is the text block's value, and it asserts it against the sixteen
literals it just removed rather than against a constant: the page is whatever
the input tree was serving, and this script's claim is only that it is still
serving it.
"""

import os
import re
import sys

tree = sys.argv[1] if len(sys.argv) > 1 else "/app/arquillian"

TQ = '"""'                      # the Java text block delimiter, three of them


def read(rel):
    with open(os.path.join(tree, rel), encoding="utf-8") as fh:
        return fh.read()


def write(rel, text):
    with open(os.path.join(tree, rel), "w", encoding="utf-8") as fh:
        fh.write(text)


def sub1(rel, old, new, count=1):
    """Replace `old` exactly `count` times in `rel`, or fail loudly."""
    text = read(rel)
    n = text.count(old)
    if n != count:
        sys.exit("java17: %s: expected %d occurrence(s) of %r, found %d"
                 % (rel, count, old[:70], n))
    write(rel, text.replace(old, new))


def sub_any(rel, *pairs):
    """Apply the first (old, new) whose `old` occurs exactly once in `rel`.

    ONE SITE, MORE THAN ONE SPELLING. Two different finished step 08s reach
    this program and a handful of methods are not spelled the same way in
    both: step 06's Optional work left ExceptionProxy as a `.map` over a
    ternary in the tree this rung's baseline was measured on, and as an early
    `if` over the same test in the chained golden. Both are the one
    convertible site in `constructExceptionForClass`, which is how the
    verifier keys the bucket -- by `file#method`, never by line and never by
    shape -- so the edit this rung owes is the same either way. Refusing the
    tree whose spelling happened not to be the baseline's would be an
    assertion about the wrong thing.

    Exactly one alternative has to match. Zero means neither step 08 produced
    this tree and the caller should hear about it; two would mean the
    alternatives overlap, which makes the edit that follows ambiguous.
    """
    text = read(rel)
    hits = [(o, n) for o, n in pairs if text.count(o) == 1]
    if len(hits) != 1:
        sys.exit("java17: %s: %d of %d known spelling(s) match, expected "
                 "exactly 1" % (rel, len(hits), len(pairs)))
    old, new = hits[0]
    write(rel, text.replace(old, new))


def sources():
    """Every .java file on the tree, build output excluded."""
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build")]
        for nm in sorted(names):
            if nm.endswith(".java"):
                yield os.path.join(dirpath, nm)


def count(pattern):
    """Occurrences of `pattern` across the tree's Java sources.

    MULTILINE always, because two of the four census patterns anchor on `^` to
    mean "a top-level declaration" rather than "a word somewhere in the file":
    `public final class` at column zero is a type, and the same words indented
    are a nested one or a line of prose.
    """
    rx = re.compile(pattern, re.M)
    n = 0
    for path in sources():
        with open(path, encoding="utf-8", errors="replace") as fh:
            n += len(rx.findall(fh.read()))
    return n


# A type name, an optional type argument list, and a lowercase-initial
# identifier: `instanceof Class<?> raw` matches and `instanceof ArchiveAsset )`
# does not. The seven sites that must stay plain have no identifier after the
# type, so this regex is exactly the bound-pattern population C-2 counts.
BOUND = (r"instanceof\s+(?:[A-Za-z_$][\w$.]*)(?:\s*<[^>()\n]*>)?"
         r"\s+([a-z][\w$]*)\b")
ANY_INSTANCEOF = r"\binstanceof\b"

before_any = count(ANY_INSTANCEOF)
before_bound = count(BOUND)
before_final = count(r"^public final (?:class|interface)\b")
before_sealed = count(r"\bsealed\b")

# ---------------------------------------------------------------------------
# 1. The level.
# ---------------------------------------------------------------------------
# Asking by `--release` rather than by `-target` is step 07's decision carried
# forward: it caps the platform API at the same version as the bytecode, so a
# call to something a later JDK added is a compile error here instead of a
# NoSuchMethodError on a Java 17 runtime. What moves at this rung is the
# number, wherever the build happens to say it.
#
# A SWEEP OF THE BUILD SCRIPTS AND NOT ONE NAMED FILE, because more than one
# finished step 08 reaches this program. The blind snapshot this rung's
# baseline was measured on declares the level in `gradle/java-module.gradle`,
# as `sourceCompatibility`, `targetCompatibility` and `options.release`; the
# chained golden never grew that file and declares it in `build.gradle`, as
# `options.release` and the TargetJvmVersion attribute step 07 put on the four
# resolvable classpaths. Both are a build that compiles the whole tree at one
# level. A program that edited one named file would crash on the other tree,
# and the crash would be about this program rather than about the tree.
#
# THE PROSE ABOVE A MOVED LINE MOVES WITH IT. A comment that says "Java 11" a
# line above `options.release = 17` is worse than no comment at all: the next
# reader has to work out which of the two is the build. The carry is
# deliberately narrow -- the contiguous run of `//` lines immediately above a
# line that actually changed, bare `11` tokens only -- so that `--release`,
# `-source`, `major version 54` and a dependency version elsewhere in the same
# file are all left exactly as they are.
WAS, NOW = "11", "17"
LEVEL_SITES = (
    ("JavaVersion.VERSION_" + WAS, "JavaVersion.VERSION_" + NOW),
    ("options.release = " + WAS, "options.release = " + NOW),
    ("TARGET_JVM_VERSION_ATTRIBUTE, " + WAS,
     "TARGET_JVM_VERSION_ATTRIBUTE, " + NOW),
)
COMMENT = re.compile(r"^\s*//")
BARE_WAS = re.compile(r"(?<![\w.])" + WAS + r"(?![\w.])")


def buildfiles():
    """Every .gradle file on the tree, build output and caches excluded."""
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames
                       if d not in (".git", "build", ".gradle")]
        for nm in sorted(names):
            if nm.endswith(".gradle"):
                yield os.path.relpath(os.path.join(dirpath, nm), tree)


level_files, level_lines, level_prose = [], 0, 0
for rel in sorted(buildfiles()):
    lines = read(rel).splitlines(True)
    moved = []
    for i, line in enumerate(lines):
        new = line
        for old, repl in LEVEL_SITES:
            new = new.replace(old, repl)
        if new != line:
            lines[i] = new
            moved.append(i)
    if not moved:
        continue
    for i in moved:
        j = i - 1
        while j >= 0 and COMMENT.match(lines[j]):
            lines[j], k = BARE_WAS.subn(NOW, lines[j])
            level_prose += k
            j -= 1
    write(rel, "".join(lines))
    level_files.append(rel)
    level_lines += len(moved)

if not level_files:
    sys.exit("java17: no build script declares a Java level at %s; there is "
             "nothing here to move to %s" % (WAS, NOW))
if not [rel for rel in buildfiles()
        if ("options.release = " + NOW) in read(rel)]:
    sys.exit("java17: the level moved in %s but no build script asks for it by "
             "release" % ", ".join(level_files))

# ---------------------------------------------------------------------------
# 2. The patterns.
# ---------------------------------------------------------------------------
# Seven of the eighteen methods are the same three lines in seven different
# packages: every SecurityActions in the tree unwraps a PrivilegedActionException
# the same way, and the seven files are copies of one another rather than seven
# separate decisions. They are done as a loop for that reason; the remaining
# eleven are one each and are spelled out.
SECURITYACTIONS = [
    "impl-base/src/main/java/org/jboss/arquillian/impl/SecurityActions.java",
    "impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/"
    "SecurityActions.java",
    "protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/"
    "runner/SecurityActions.java",
    "spi/src/main/java/org/jboss/arquillian/spi/util/SecurityActions.java",
    "testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/"
    "SecurityActions.java",
    "testenrichers/ejb/src/main/java/org/jboss/arquillian/testenricher/ejb/"
    "SecurityActions.java",
    "testenrichers/resource/src/main/java/org/jboss/arquillian/testenricher/"
    "resource/SecurityActions.java",
]

for rel in SECURITYACTIONS:
    sub1(rel,
         "         if (t instanceof NoSuchMethodException)\n"
         "         {\n"
         "            throw (NoSuchMethodException) t;\n",
         "         if (t instanceof NoSuchMethodException nsme)\n"
         "         {\n"
         "            throw nsme;\n")

IMPLB = "impl-base/src/main/java/org/jboss/arquillian/impl/"
SERVLET = "protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/"
SPI = "spi/src/main/java/org/jboss/arquillian/spi/"

# (file, old, new). Each is one method's worth. The names are the ones the
# variable already had where the cast was assigned to a local -- `first`,
# `warArchiveAsset`, `annoation` and its original spelling included -- and
# descriptive where the cast was inline.
PATTERNS = [
    # InjectionPointImpl#getType: the cast on the next line was the whole body
    # of the branch's first statement.
    (IMPLB + "core/InjectionPointImpl.java",
     "      if(type.getActualTypeArguments()[0] instanceof ParameterizedType)\n"
     "      {\n"
     "         ParameterizedType first = (ParameterizedType)"
     "type.getActualTypeArguments()[0];\n",
     "      if(type.getActualTypeArguments()[0] instanceof ParameterizedType first)\n"
     "      {\n"),
    # ObserverImpl#invoke: two sites, one nested inside the other's branch.
    (IMPLB + "core/ObserverImpl.java",
     "         if(e instanceof InvocationTargetException)\n"
     "         {\n"
     "            Throwable cause = ((InvocationTargetException)e)"
     ".getTargetException();\n",
     "         if(e instanceof InvocationTargetException ite)\n"
     "         {\n"
     "            Throwable cause = ite.getTargetException();\n"),
    (IMPLB + "core/ObserverImpl.java",
     "            if(cause instanceof InvocationException)\n"
     "            {\n"
     "               throw (InvocationException)cause;\n",
     "            if(cause instanceof InvocationException wrapped)\n"
     "            {\n"
     "               throw wrapped;\n"),
    # Reflections#isType and #getType: two sites each, and the two methods ask
    # the same two questions of the same parameter, so the names are shared.
    (IMPLB + "core/Reflections.java",
     "      if(type instanceof Class<?>)\n"
     "      {\n"
     "         return ((Class<?>)type) == clazz;\n"
     "      }\n"
     "      else if(type instanceof ParameterizedType)\n"
     "      {\n"
     "         return ((ParameterizedType)type).getRawType() == clazz;\n",
     "      if(type instanceof Class<?> raw)\n"
     "      {\n"
     "         return raw == clazz;\n"
     "      }\n"
     "      else if(type instanceof ParameterizedType parameterized)\n"
     "      {\n"
     "         return parameterized.getRawType() == clazz;\n"),
    (IMPLB + "core/Reflections.java",
     "      if(type instanceof Class<?>)\n"
     "      {\n"
     "         return (Class<?>)type;\n"
     "      }\n"
     "      else if(type instanceof ParameterizedType)\n"
     "      {\n"
     "         return getType(((ParameterizedType)type).getActualTypeArguments()[0]);\n",
     "      if(type instanceof Class<?> raw)\n"
     "      {\n"
     "         return raw;\n"
     "      }\n"
     "      else if(type instanceof ParameterizedType parameterized)\n"
     "      {\n"
     "         return getType(parameterized.getActualTypeArguments()[0]);\n"),
    # AbstractManagerTestBase#register, in the test sources: the same edit is
    # worth making there, because the tests are compiled at this level too.
    (IMPLB + "../../../../../../test/java/org/jboss/arquillian/impl/"
     "AbstractManagerTestBase.java",
     "         if(event instanceof Throwable)\n",
     "         if(event instanceof Throwable thrown)\n"),
    (IMPLB + "../../../../../../test/java/org/jboss/arquillian/impl/"
     "AbstractManagerTestBase.java",
     "            UncheckedThrow.throwUnchecked((Throwable)event);\n",
     "            UncheckedThrow.throwUnchecked(thrown);\n"),
    # ServletMethodExecutor#execute: a NEGATED test, and the pattern variable is
    # in scope after the `if` because the branch always throws.
    (SERVLET + "ServletMethodExecutor.java",
     "            if (!(o instanceof TestResult))\n",
     "            if (!(o instanceof TestResult result))\n"),
    (SERVLET + "ServletMethodExecutor.java",
     "            return (TestResult) o;\n",
     "            return result;\n"),
    # The two ServletProtocolDeploymentPackager copies, one per servlet version.
    (SERVLET + "v_2_5/ServletProtocolDeploymentPackager.java",
     "         if (warAsset instanceof ArchiveAsset)\n"
     "         {\n"
     "            ArchiveAsset warArchiveAsset = (ArchiveAsset) warAsset;\n",
     "         if (warAsset instanceof ArchiveAsset warArchiveAsset)\n"
     "         {\n"),
    (SERVLET + "v_3/ServletProtocolDeploymentPackager.java",
     "         if (warAsset instanceof ArchiveAsset)\n"
     "         {\n"
     "            ArchiveAsset warArchiveAsset = (ArchiveAsset) warAsset;\n",
     "         if (warAsset instanceof ArchiveAsset warArchiveAsset)\n"
     "         {\n"),
    # ExceptionProxy#constructExceptionForClass, and the one site on this
    # list that is spelled two ways. Step 06's Optional work left it as a
    # `.map` over a ternary in the tree this rung's baseline was measured on,
    # and as an early `if` over the same test in the chained golden. The file
    # is indented with tabs in both and the replacements keep them, trailing
    # whitespace included -- the second spelling really does carry three tabs
    # after its brace, and an edit that tidied them would be a second change
    # smuggled in beside this one.
    (SPI + "ExceptionProxy.java", [
        ("\t\t\t\t.map(object -> object instanceof Throwable\n"
         "\t\t\t\t\t\t? (Throwable) object\n",
         "\t\t\t\t.map(object -> object instanceof Throwable throwable\n"
         "\t\t\t\t\t\t? throwable\n"),
        ("\t\tif (!(object instanceof Throwable)) {\t\t\t\n"
         "\t\t\treturn createProxyException(\"Proxy references non-Throwable type\");\n"
         "\t\t}\n"
         "\t\treturn (Throwable) object;\n",
         "\t\tif (!(object instanceof Throwable throwable)) {\t\t\t\n"
         "\t\t\treturn createProxyException(\"Proxy references non-Throwable type\");\n"
         "\t\t}\n"
         "\t\treturn throwable;\n"),
    ]),
    # BeansXMLProtocolProcessor#containsBeansXML: the cast was inline and
    # repeated the getter it tested.
    ("testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/"
     "BeansXMLProtocolProcessor.java",
     "            if(node.getAsset() instanceof ArchiveAsset )\n"
     "            {\n"
     "               boolean containsBeansXML = containsBeansXML(\n"
     "                     ((ArchiveAsset)node.getAsset()).getArchive());\n",
     "            if(node.getAsset() instanceof ArchiveAsset nestedArchive)\n"
     "            {\n"
     "               boolean containsBeansXML = containsBeansXML(\n"
     "                     nestedArchive.getArchive());\n"),
    # TestDataProviderTransformer#transform: the original misspelling of the
    # local stays. Renaming it would be a second edit hiding inside this one.
    ("testng/src/main/java/org/jboss/arquillian/testng/"
     "TestDataProviderTransformer.java",
     "         if (testAnnotation instanceof TestAnnotation)\n"
     "         {\n"
     "            TestAnnotation annoation = (TestAnnotation) testAnnotation;\n",
     "         if (testAnnotation instanceof TestAnnotation annoation)\n"
     "         {\n"),
]

# A three-element entry is one spelling; a two-element one carries a list of
# alternatives, exactly one of which must match the tree in hand.
for entry in PATTERNS:
    rel = os.path.normpath(entry[0])
    if len(entry) == 3:
        sub1(rel, entry[1], entry[2])
    else:
        sub_any(rel, *entry[1])

# ---------------------------------------------------------------------------
# 3. The document.
# ---------------------------------------------------------------------------
# THE PAGE IS READ OUT OF THE TREE AND NOT TYPED IN HERE. Sixteen adjacent
# constant writes are found, their literals decoded and concatenated, and the
# text block is built from that concatenation -- so the block cannot disagree
# with the page the servlet was serving, whatever the input tree happens to
# hold. The value is then read back under the incidental-whitespace rules and
# compared with the concatenation, which is the only check that catches an
# indentation mistake: a text block indented one space wrong compiles, runs,
# and serves a different document.
TBFILE = (SERVLET + "runner/ServletTestRunner.java")
WRITE_RX = re.compile(r'^(\s*)writer\.write\("((?:[^"\\]|\\.)*)"\);\s*$')
ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'"}


def decode(lit):
    """The value of a Java string literal body. No unicode escapes here."""
    out, i = [], 0
    while i < len(lit):
        c = lit[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        e = lit[i + 1]
        if e not in ESCAPES:
            sys.exit("java17: unhandled escape \\%s in %r" % (e, lit))
        out.append(ESCAPES[e])
        i += 2
    return "".join(out)


def block_value(text):
    """The value of the one text block in `text`, per the JLS rules.

    Minimal indentation over the non-blank content lines AND the closing
    delimiter's line; that many characters off the front of each; trailing
    whitespace off the end of each; and a trailing newline because the closing
    delimiter is on a line of its own.
    """
    open_at = text.index(TQ)
    close_at = text.index(TQ, open_at + 3)
    if text.count(TQ) != 2:
        sys.exit("java17: %s holds %d text block delimiters, expected 2"
                 % (TBFILE, text.count(TQ)))
    body = text[text.index("\n", open_at) + 1:close_at]
    lines = body.split("\n")
    closing = lines[-1]                       # the run of spaces before the TQ
    strip = min([len(l) - len(l.lstrip()) for l in lines[:-1] if l.strip()]
                + [len(closing)])
    return "".join(l[strip:].rstrip() + "\n" for l in lines[:-1])


text = read(TBFILE)
lines = text.split("\n")
first = last = None
lits = []
for i, line in enumerate(lines):
    m = WRITE_RX.match(line)
    if m and decode(m.group(2)) == "<html>\n":
        first = i
    if m and decode(m.group(2)) == "</body>\n":
        last = i
if first is None or last is None or last <= first:
    sys.exit("java17: %s: no <html>..</body> run of writer.write() calls" % TBFILE)
for line in lines[first:last + 1]:
    m = WRITE_RX.match(line)
    if m:
        lits.append(decode(m.group(2)))
    elif line.strip():
        sys.exit("java17: %s: %r sits inside the document run" % (TBFILE, line))
if len(lits) != 16:
    sys.exit("java17: %s: %d constant writes in the run, expected 16"
             % (TBFILE, len(lits)))

page = "".join(lits)
if not page.endswith("\n"):
    sys.exit("java17: the page does not end in a newline; a text block would add one")

indent = WRITE_RX.match(lines[first]).group(1)
inner = indent + "   "
blk = ([indent + 'writer.write(' + TQ]
       + [inner + l for l in page.split("\n")[:-1]]
       + [inner + TQ + ");"])
write(TBFILE, "\n".join(lines[:first] + blk + lines[last + 1:]))

got = block_value(read(TBFILE))
if got != page:
    sys.exit("java17: the text block's value is not the page: %r != %r"
             % (got[:60], page[:60]))

# ---------------------------------------------------------------------------
# 4. The hierarchies.
# ---------------------------------------------------------------------------
# Eleven roots and the twenty-nine leaves under them. The permits clauses name
# thirty-five subtypes, six of which are roots themselves -- the vocabulary
# nests, so a container control event permits a deployment event, which permits
# two of its own -- and the eleven hierarchies therefore close over forty
# distinct types rather than forty-six.
#
# The leaves are marked `final`. `non-sealed` would compile just as well and
# neither modifier changes a published signature as this rung measures it; what
# the compiler will not accept is a permitted subtype that is none of the three.
# `final` is the honest one here: nothing in this tree extends any of the
# twenty-nine, and nothing outside it is meant to.
EVENT = IMPLB + "client/container/event/"
CONTAINER = SPI + "event/container/"
ENRICH = SPI + "event/enrichment/"
SUITE = SPI + "event/suite/"

# (file, the declaration as it stands, the permits clause that follows it).
# The wrapping is the tree's own: a continuation at three spaces, and a second
# line at eight where the names do not fit in one.
ROOTS = [
    (EVENT + "ContainerControlEvent.java",
     "public abstract class ContainerControlEvent implements Event",
     "   permits DeploymentEvent, SetupContainer, StartContainer, StopContainer"),
    (EVENT + "ContainerMultiControlEvent.java",
     "public interface ContainerMultiControlEvent",
     "   permits DeployManagedDeployments, SetupContainers, StartManagedContainers,\n"
     "        StopManagedContainers, UnDeployManagedDeployments"),
    (EVENT + "DeploymentEvent.java",
     "public abstract class DeploymentEvent extends ContainerControlEvent",
     "   permits DeployDeployment, UnDeployDeployment"),
    (CONTAINER + "ContainerEvent.java",
     "public class ContainerEvent implements Event",
     "   permits AfterSetup, AfterStart, AfterStop, BeforeSetup, BeforeStart, BeforeStop,\n"
     "        DeployerEvent"),
    (CONTAINER + "DeployerEvent.java",
     "public class DeployerEvent extends ContainerEvent",
     "   permits AfterDeploy, AfterUnDeploy, BeforeDeploy, BeforeUnDeploy"),
    (ENRICH + "EnrichmentEvent.java",
     "public class EnrichmentEvent implements Event",
     "   permits AfterEnrichment, BeforeEnrichment"),
    (SUITE + "ClassEvent.java",
     "public class ClassEvent extends SuiteEvent",
     "   permits ClassLifecycleEvent, TestEvent"),
    (SUITE + "ClassLifecycleEvent.java",
     "public class ClassLifecycleEvent extends ClassEvent implements LifecycleEvent",
     "   permits AfterClass, BeforeClass"),
    (SUITE + "SuiteEvent.java",
     "public class SuiteEvent implements Event",
     "   permits AfterSuite, BeforeSuite, ClassEvent"),
    (SUITE + "TestEvent.java",
     "public class TestEvent extends ClassEvent",
     "   permits Test, TestLifecycleEvent"),
    (SUITE + "TestLifecycleEvent.java",
     "public class TestLifecycleEvent extends TestEvent implements LifecycleEvent",
     "   permits After, Before"),
]

permitted = []
for rel, decl, permits in ROOTS:
    if " class " in decl:
        sealed = decl.replace(" class ", " sealed class ", 1)
    elif " interface " in decl:
        sealed = decl.replace(" interface ", " sealed interface ", 1)
    else:
        sys.exit("java17: %s: %r declares neither a class nor an interface"
                 % (rel, decl))
    sub1(rel, decl + "\n", sealed + "\n" + permits + "\n")
    permitted += [n.strip() for n in
                  permits.replace("\n", " ").split("permits", 1)[1].split(",")]

LEAVES = [
    (EVENT, "DeployDeployment", "extends DeploymentEvent"),
    (EVENT, "DeployManagedDeployments", "implements ContainerMultiControlEvent"),
    (EVENT, "SetupContainer", "extends ContainerControlEvent"),
    (EVENT, "SetupContainers", "implements ContainerMultiControlEvent"),
    (EVENT, "StartContainer", "extends ContainerControlEvent"),
    (EVENT, "StartManagedContainers", "implements ContainerMultiControlEvent"),
    (EVENT, "StopContainer", "extends ContainerControlEvent"),
    (EVENT, "StopManagedContainers", "implements ContainerMultiControlEvent"),
    (EVENT, "UnDeployDeployment", "extends DeploymentEvent"),
    (EVENT, "UnDeployManagedDeployments", "implements ContainerMultiControlEvent"),
    (CONTAINER, "AfterDeploy", "extends DeployerEvent"),
    (CONTAINER, "AfterSetup", "extends ContainerEvent"),
    (CONTAINER, "AfterStart", "extends ContainerEvent"),
    (CONTAINER, "AfterStop", "extends ContainerEvent"),
    (CONTAINER, "AfterUnDeploy", "extends DeployerEvent"),
    (CONTAINER, "BeforeDeploy", "extends DeployerEvent"),
    (CONTAINER, "BeforeSetup", "extends ContainerEvent"),
    (CONTAINER, "BeforeStart", "extends ContainerEvent"),
    (CONTAINER, "BeforeStop", "extends ContainerEvent"),
    (CONTAINER, "BeforeUnDeploy", "extends DeployerEvent"),
    (ENRICH, "AfterEnrichment", "extends EnrichmentEvent"),
    (ENRICH, "BeforeEnrichment", "extends EnrichmentEvent"),
    (SUITE, "After", "extends TestLifecycleEvent"),
    (SUITE, "AfterClass", "extends ClassLifecycleEvent"),
    (SUITE, "AfterSuite", "extends SuiteEvent"),
    (SUITE, "Before", "extends TestLifecycleEvent"),
    (SUITE, "BeforeClass", "extends ClassLifecycleEvent"),
    (SUITE, "BeforeSuite", "extends SuiteEvent"),
    (SUITE, "Test", "extends TestEvent"),
]

for pkg, name, extends in LEAVES:
    sub1(pkg + name + ".java",
         "public class %s %s\n" % (name, extends),
         "public final class %s %s\n" % (name, extends))

# The two lists are cross-checked against each other rather than against a
# constant: every leaf is named by some root's permits clause, and the names in
# those clauses that are not leaves are the six roots that nest.
leafnames = set(n for _, n, _ in LEAVES)
rootnames = set(os.path.basename(r)[:-5] for r, _, _ in ROOTS)
if len(permitted) != 35 or len(set(permitted)) != 35:
    sys.exit("java17: the permits clauses name %d subtypes (%d distinct), expected 35"
             % (len(permitted), len(set(permitted))))
if set(permitted) - rootnames != leafnames:
    sys.exit("java17: the permitted names that are not roots are not the leaves")

# ---------------------------------------------------------------------------
# 5. The switches.
# ---------------------------------------------------------------------------
# Both are two-case colon-form STATEMENT switches whose every case ends in a
# `break`, so arrow form does exactly what they did. Neither gains a `default`:
# exhaustiveness is a switch EXPRESSION rule, and these two stay statements.
sub1(IMPLB + "DeployableTestBuilder.java",
     "            case CLIENT :\n"
     "               builder.extensions(profile.getClientProfile()"
     ".toArray(Class<?>[]::new));\n"
     "               break;\n"
     "            case CONTAINER :\n"
     "               builder.extensions(profile.getContainerProfile()"
     ".toArray(Class<?>[]::new));\n"
     "               break;\n",
     "            case CLIENT -> builder.extensions(profile.getClientProfile()"
     ".toArray(Class<?>[]::new));\n"
     "            case CONTAINER -> builder.extensions(profile.getContainerProfile()"
     ".toArray(Class<?>[]::new));\n")

DSLTEST = os.path.normpath(
    IMPLB + "../../../../../../test/java/org/jboss/arquillian/impl/"
    "DynamicServiceLoaderTestCase.java")
sub1(DSLTEST,
     "            case 0:\n"
     "               Assertions.assertEquals(service.getClass(), ServiceImpl1.class);\n"
     "               break;\n"
     "            case 1:\n"
     "               Assertions.assertEquals(service.getClass(), ServiceImpl2.class);\n"
     "               break;\n",
     "            case 0 -> Assertions.assertEquals(service.getClass(), "
     "ServiceImpl1.class);\n"
     "            case 1 -> Assertions.assertEquals(service.getClass(), "
     "ServiceImpl2.class);\n")

# ---------------------------------------------------------------------------
# 6. The census.
# ---------------------------------------------------------------------------
after_any = count(ANY_INSTANCEOF)
after_bound = count(BOUND)
after_final = count(r"^public final (?:class|interface)\b")
after_sealed = count(r"\bsealed\b")

if after_any != before_any:
    sys.exit("java17: instanceof sites went %d -> %d; this rung converts them "
             "and deletes none" % (before_any, after_any))
if after_bound - before_bound != 21:
    sys.exit("java17: bound patterns went %d -> %d, expected a rise of exactly 21"
             % (before_bound, after_bound))
if after_final - before_final != 29:
    sys.exit("java17: final types went %d -> %d, expected a rise of exactly 29"
             % (before_final, after_final))
if after_sealed - before_sealed != 11:
    sys.exit("java17: sealed declarations went %d -> %d, expected a rise of "
             "exactly 11" % (before_sealed, after_sealed))

# The two XML builders are the held half of C-3 and this is the assertion that
# they were not swept up with the page: seven newline-bearing literals, in the
# two methods the baseline names, after the document moved.
FORMATTER = (IMPLB + "client/deployment/tool/ToolingDeploymentFormatter.java")
held = len(re.findall(r'"(?:[^"\\]|\\.)*\\n(?:[^"\\]|\\.)*"', read(FORMATTER)))
if held != 7:
    sys.exit("java17: %s holds %d newline literal(s), expected 7"
             % (FORMATTER, held))
if TQ in read(FORMATTER):
    sys.exit("java17: %s gained a text block; its fragments are not one document"
             % FORMATTER)

arrows = 0
for rel in (IMPLB + "DeployableTestBuilder.java", DSLTEST):
    src = read(rel)
    arrows += len(re.findall(r"^\s*case .* -> ", src, re.M))
    left = re.findall(r"^\s*case [^\n]*:\s*$", src, re.M)
    if left:
        sys.exit("java17: %s still holds a colon-form case: %r" % (rel, left[0]))
if arrows != 4:
    sys.exit("java17: %d arrow-form case label(s) in the two files, expected 4"
             % arrows)

sup = count(r"@SuppressWarnings")
print("java17: level: source, target and release at 17 in: %s, %d line(s), %d prose token(s)"
      % (", ".join(level_files), level_lines, level_prose))
print("java17: patterns: %d of %d instanceof site(s) bind a variable, %d left plain"
      % (after_bound, after_any, after_any - after_bound))
print("java17: document: 1 text block in ServletTestRunner, 16 literal(s) gone; "
      "%d newline literal(s) held in the two XML builders" % held)
print("java17: hierarchies: %d sealed root(s), %d permitted name(s), %d final leaf/leaves"
      % (len(ROOTS), len(permitted), len(LEAVES)))
print("java17: switches: %d arrow-form case label(s), 0 colon-form left" % arrows)
print("java17: kept: %d @SuppressWarnings, the seven plain instanceof tests, "
      "the eleven open extension points" % sup)
print("JAVA17 OK")
PY_JAVA17
) || true
printf '%s\n' "${j17}"
for want in \
      'java17: patterns: 21 of 28 instanceof site(s) bind a variable, 7 left plain' \
      'java17: hierarchies: 11 sealed root(s), 35 permitted name(s), 29 final leaf/leaves' \
      'java17: switches: 4 arrow-form case label(s), 0 colon-form left' \
      'JAVA17 OK'; do
   if ! printf '%s\n' "${j17}" | grep -qxF "${want}"; then
      echo "solve09: java17.py did not report: ${want}" >&2
      exit 1
   fi
done
# The level print line includes the file names which vary by tree shape.
if ! printf '%s\n' "${j17}" | grep -qE '^java17: level: source, target and release at 17 in:'; then
   echo "solve09: java17.py did not report the level move" >&2
   exit 1
fi
# The document line is matched as a shape in its trailing half. The sixteen and
# the seven are frozen populations and are spelled out; the text block count is
# java17.py's own reading of the file it just wrote, and a literal match on the
# whole line would be asserting the same fact twice in two spellings.
if ! printf '%s\n' "${j17}" | grep -qE '^java17: document: 1 text block in ServletTestRunner, 16 literal\(s\) gone; 7 newline literal\(s\) held in the two XML builders$'; then
   echo "solve09: java17.py did not report the document move" >&2
   exit 1
fi

# ==========================================================================
# 2. The level, re-read off the tree.
# ==========================================================================
# The level was moved by java17.py, so this section re-reads it off the tree
# rather than setting it. Two questions, and the second is the one that matters:
# does at least one build script now ask for 17 by `--release`, and is there any
# other build file still pinning a level that is not 17?
#
# ASKED AS A SHAPE AND NOT AS A FILE NAME. An earlier draft named
# `gradle/java-module.gradle` and asserted three settings in it; the two
# finished step 08s put the level in different files, and the assertion crashed
# on the tree it was not written against (14.12). Both shapes do leave
# `options.release = 17` in exactly one build file, but which one is step 08's
# business and not this rung's.
#
# `--release` rather than `-target` alone is step 07's decision carried forward:
# it caps the platform API at the same version as the bytecode, so a call to
# something a later JDK added is a compile error here instead of a
# NoSuchMethodError on a Java 17 runtime. The sweep below catches a subproject
# that overrode the shared file and stayed at 11.
if ! grep -rnqE --include='*.gradle' --exclude-dir=build \
        'options\.release = 17' .; then
   echo "solve09: no build script asks for options.release = 17 after the level edit" >&2
   exit 1
fi
left=$(grep -rnE --include='*.gradle' --exclude-dir=build \
         'JavaVersion\.VERSION_|JavaLanguageVersion\.of|options\.release' . \
       | grep -vE 'VERSION_17|\.of\(17\)|options\.release = 17' || true)
if [ -n "${left}" ]; then
   echo "solve09: a build file still pins a level other than 17:" >&2
   printf '%s\n' "${left}" >&2
   exit 1
fi
echo "solve09: the level is 17, asked for by release, in one file and no other"

# ==========================================================================
# 3. The patterns.
# ==========================================================================
# `--exclude-dir=build` throughout: a Gradle build leaves copies of sources
# under each project's build/, and this tree additionally keeps two checked-in
# IDE test templates under build/ide that belong to no source set. Counting
# either would make the answer depend on whether anyone had run the build
# before running this.
#
# A type name, an optional type argument list, and a lowercase-initial
# identifier. `instanceof Class<?> raw` matches; `instanceof ArchiveAsset )`
# does not, which is what makes this the bound-pattern population and not a
# count of the word.
IPBOUND='instanceof +[A-Za-z_$][A-Za-z0-9_$.]*( *<[^>()]*>)? +[a-z][A-Za-z0-9_$]*'
# EVERY COUNTING PIPELINE ENDS IN `|| true`, and that is `pipefail` and not
# style. `set -o pipefail` gives `grep ... | wc -l` grep's status, so a count
# that comes out as zero -- exactly the case each of these checks exists to
# report -- would abort the script on the assignment with `set -e` and print
# nothing at all. The `|| true` costs nothing: `wc` has already written the
# number to the substitution by the time `true` runs.
n=$(grep -rhoE --include='*.java' --exclude-dir=build "${IPBOUND}" . | wc -l || true)
if [ "${n}" != 21 ]; then
   echo "solve09: ${n} instanceof site(s) bind a pattern variable, wanted 21" >&2
   exit 1
fi
t=$(grep -rhoE --include='*.java' --exclude-dir=build '\binstanceof\b' . | wc -l || true)
if [ "${t}" != 28 ]; then
   echo "solve09: ${t} instanceof site(s) on the tree, wanted 28 -- this rung" >&2
   echo "solve09: converts them and deletes none" >&2
   exit 1
fi
# The seven that must still be plain, by file and by count. A tree that bound
# all twenty-eight reads 28 above and would pass the totals; what distinguishes
# it is that these five files hold their sites AND bind nothing in them.
for pair in 'impl-base/src/main/java/org/jboss/arquillian/impl/client/container/DeploymentExceptionHandler.java|1' \
            'impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java|1' \
            'impl-base/src/test/java/org/jboss/arquillian/impl/client/container/ContainerDeployControllerTestCase.java|1' \
            'impl-base/src/test/java/org/jboss/arquillian/impl/core/ExceptionHandlingTestCase.java|1' \
            'protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/ProtocolTestCase.java|3'; do
   f=${pair%%|*}; want=${pair#*|}
   got=$(grep -oE '\binstanceof\b' "${f}" | wc -l || true)
   if [ "${got}" != "${want}" ]; then
      echo "solve09: ${f} holds ${got} instanceof test(s), wanted ${want} and all of them plain" >&2
      exit 1
   fi
   if grep -qE "${IPBOUND}" "${f}"; then
      echo "solve09: a pattern variable was bound in ${f} -- there is nothing to bind there" >&2
      exit 1
   fi
done
echo "solve09: 21 of 28 instanceof site(s) bind a variable, 7 stay plain"

# ==========================================================================
# 4. The document.
# ==========================================================================
RUNNER=protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/runner/ServletTestRunner.java
FORMATTER=impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/tool/ToolingDeploymentFormatter.java
q=$(grep -cF '"""' "${RUNNER}" || true)
if [ "${q}" != 2 ]; then
   echo "solve09: ${RUNNER} holds ${q} text block delimiter line(s), wanted 2" >&2
   exit 1
fi
# The literals really gone from the method, not merely joined by one that
# stayed. A newline-bearing literal anywhere in this file is one of the sixteen
# that did not move.
if grep -qE '"([^"\\]|\\.)*\\n([^"\\]|\\.)*"' "${RUNNER}"; then
   echo "solve09: a newline-bearing literal survives in ${RUNNER}" >&2
   exit 1
fi
# The page, spelled by its two ends. java17.py compared the block's value with
# the concatenation of the sixteen literals it removed, byte for byte, under
# the incidental-whitespace rules; this is the cheap outside reading of the
# same fact -- the document still opens and closes where it did.
if ! grep -qE '^ +<html>$' "${RUNNER}" || ! grep -qE '^ +</body>$' "${RUNNER}"; then
   echo "solve09: the text block in ${RUNNER} is not the report page" >&2
   exit 1
fi
h=$(grep -oE '"([^"\\]|\\.)*\\n([^"\\]|\\.)*"' "${FORMATTER}" | wc -l || true)
if [ "${h}" != 7 ]; then
   echo "solve09: ${FORMATTER} holds ${h} newline literal(s), wanted 7" >&2
   exit 1
fi
if grep -qF '"""' "${FORMATTER}"; then
   echo "solve09: ${FORMATTER} gained a text block -- its fragments are not one document" >&2
   exit 1
fi
tb=$(grep -rlF --include='*.java' --exclude-dir=build '"""' . | wc -l || true)
if [ "${tb}" != 1 ]; then
   echo "solve09: ${tb} file(s) hold a text block, wanted 1" >&2
   exit 1
fi
echo "solve09: one text block, in the one method whose literals were one document"

# ==========================================================================
# 5. The hierarchies.
# ==========================================================================
# Re-derived from the tree rather than re-listed here: the permitted names are
# read out of the clauses that were just written, and every one of them is then
# checked to be `final`, `sealed` or `non-sealed`. That is the compiler's own
# rule, and stating it this way means the check cannot drift from the edit --
# a permits clause that gained a name would be checked, not merely counted.
roots=$(grep -rlE --include='*.java' --exclude-dir=build '^ +permits ' . | sort)
nroots=$(printf '%s\n' "${roots}" | grep -c . || true)
if [ "${nroots}" != 11 ]; then
   echo "solve09: ${nroots} type(s) declare a permits clause, wanted 11" >&2
   exit 1
fi
s=$(grep -rhoE --include='*.java' --exclude-dir=build '^public [a-z ]*sealed (class|interface) ' . | wc -l || true)
if [ "${s}" != 11 ]; then
   echo "solve09: ${s} sealed declaration(s), wanted 11" >&2
   exit 1
fi
# The clause may wrap: `permits` opens it and the brace on its own line closes
# it, and two of the eleven need a second line for the names to fit.
# Captured into a variable and walked with a `for`, not piped into a `while`.
# A `while` on the right of a pipe is a subshell, and the `exit 1` inside it
# would end the subshell and leave this script running: the loop below is the
# only place on this rung where a failure has to be able to stop the script
# from inside a loop body, and getting that wrong turns every check in it into
# a message.
names=$(printf '%s\n' "${roots}" | while read -r f; do
   awk '/^[[:space:]]+permits /{p=1; b=$0; next}
        p && /^[[:space:]]*\{/{print b; p=0; b=""; next}
        p{b = b " " $0}' "${f}"
done | sed 's/^[[:space:]]*permits //' | tr ',' '\n' \
     | sed 's/[[:space:]]//g' | grep . || true)
n=$(printf '%s\n' "${names}" | grep -c . || true)
u=$(printf '%s\n' "${names}" | sort -u | grep -c . || true)
if [ "${n}" != 35 ] || [ "${u}" != 35 ]; then
   echo "solve09: the permits clauses name ${n} subtype(s), ${u} distinct, wanted 35 and 35" >&2
   exit 1
fi
for nm in ${names}; do
   src=$(find . -name "${nm}.java" -not -path '*/build/*' | head -1)
   if [ -z "${src}" ]; then
      echo "solve09: a permits clause names ${nm} and no ${nm}.java is on the tree" >&2
      exit 1
   fi
   if ! grep -qE "^public ([a-z-]+ )*(final|sealed|non-sealed) (class|interface) ${nm}\b" "${src}"; then
      echo "solve09: permitted subtype ${nm} is neither final, sealed nor non-sealed" >&2
      exit 1
   fi
done
# The eleven this library is extended through, held open by name. Nothing else
# in this image can see this mistake: nothing in the tree subclasses any of
# them from outside, so sealing them compiles and every recorded behaviour
# replays identically.
for f in impl-base/src/main/java/org/jboss/arquillian/impl/client/ContainerDeploymentContextHandler.java \
         impl-base/src/main/java/org/jboss/arquillian/impl/configuration/ArquillianDescriptorImpl.java \
         impl-base/src/main/java/org/jboss/arquillian/impl/configuration/ContainerDefImpl.java \
         impl-base/src/main/java/org/jboss/arquillian/impl/core/context/AbstractContext.java \
         impl-base/src/main/java/org/jboss/arquillian/impl/core/spi/Typed.java \
         impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/ResourceProvider.java \
         impl-base/src/main/java/org/jboss/arquillian/impl/execution/event/ExecutionEvent.java \
         impl-base/src/test/java/org/jboss/arquillian/impl/DynamicServiceLoaderTestCase.java \
         spi/src/main/java/org/jboss/arquillian/spi/event/suite/LifecycleEvent.java \
         testenrichers/cdi/src/test/java/org/jboss/arquillian/testenricher/cdi/beans/Service.java; do
   if grep -qE '\bsealed\b' "${f}"; then
      echo "solve09: ${f} was sealed -- this tree is extended through it" >&2
      exit 1
   fi
done
echo "solve09: 11 hierarchies closed over 35 permitted names, 11 extension points left open"

# ==========================================================================
# 6. The switches.
# ==========================================================================
# Both files, both halves: enough arrows AND no colon label left. A tree that
# added an arrow switch beside the colon one it already had has done nothing,
# and a tree-wide arrow count reads the two the same way.
for f in impl-base/src/main/java/org/jboss/arquillian/impl/DeployableTestBuilder.java \
         impl-base/src/test/java/org/jboss/arquillian/impl/DynamicServiceLoaderTestCase.java; do
   a=$(grep -cE '^ *case .* -> ' "${f}" || true)
   if [ "${a}" != 2 ]; then
      echo "solve09: ${f} holds ${a} arrow-form case label(s), wanted 2" >&2
      exit 1
   fi
   if grep -qE '^ *case .*: *$' "${f}"; then
      echo "solve09: ${f} still holds a colon-form case label" >&2
      exit 1
   fi
   # Neither file has a loop `break` of its own: the only ones either ever held
   # were the four that ended the four colon-form cases. An arrow case cannot
   # fall through, so a surviving `break` is a half-converted switch.
   if grep -qE '^ *break; *$' "${f}"; then
      echo "solve09: ${f} keeps a break -- an arrow case does not fall through" >&2
      exit 1
   fi
done
echo "solve09: both switches are arrow form, neither keeps a colon label"

# ==========================================================================
# 7. The things this step deliberately does not do.
# ==========================================================================
# Asserted, not merely described. Each is an edit that looks like finishing the
# job and is not one, and a guard is cheaper than the argument.
n=$(grep -rhoF --include='*.java' --exclude-dir=build '@SuppressWarnings' . | wc -l || true)
if [ "${n}" != 44 ]; then
   echo "solve09: ${n} @SuppressWarnings on the tree, wanted 44" >&2
   echo "solve09: a pattern variable removes a cast, not a warning; sealing raises none" >&2
   exit 1
fi
# No record, and this is not tidiness. A record is a different published API --
# accessors named after the component rather than getX() -- and every one of
# these classes has consumers outside this tree.
r=$(grep -rlE --include='*.java' --exclude-dir=build '^public( [a-z-]+)* record ' . | wc -l || true)
if [ "${r}" != 0 ]; then
   echo "solve09: ${r} type(s) became records -- that is a published API change" >&2
   exit 1
fi
# Step 08's work, still standing. The ninth trap in the long form: a step-k
# requirement that quietly makes a step-k-1 test unpassable is invisible from
# inside a step-local mean, and sealing a hierarchy is exactly the kind of edit
# that could take a service provider with it.
#
# BOTH GUARDS ASK THE SHAPE AND NOT THE SPELLING, because this script meets two
# different step-08 trees. Chained behind the other solutions it sees the step
# 08 golden, which puts the manifest attribute in `gradle/modules.gradle` and
# writes the method reference out fully qualified; run against the tree this
# step's baseline was measured on it sees a blind run's own step-08 solution,
# which put the attribute in `gradle/java-module.gradle` behind a
# `rootPackage()` helper and imported `java.util.ServiceLoader.Provider` so
# that the reference reads unqualified. Both are a finished step 08. A guard
# that named one file or one spelling would fail on the other tree and report
# "step 08 was disturbed" about a step 08 nothing had touched.
if ! grep -qF 'Provider::type' \
      impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java; then
   echo "solve09: impl-base no longer names providers without loading them" >&2
   echo "solve09: step 08 moved that lookup onto java.util.ServiceLoader" >&2
   exit 1
fi
amn=$(grep -rlF --include='*.gradle' "attributes('Automatic-Module-Name'" gradle | wc -l || true)
if [ "${amn}" -lt 1 ]; then
   echo "solve09: no build file under gradle/ writes Automatic-Module-Name" >&2
   echo "solve09: step 08's twelve module names are published from there" >&2
   exit 1
fi
mi=$(find . -name 'module-info.java' -not -path '*/build/*' || true)
if [ -n "${mi}" ]; then
   echo "solve09: a module-info.java was written: ${mi}" >&2
   echo "solve09: step 08 claims names only, and this rung does not revisit that" >&2
   exit 1
fi
echo "solve09: 44 @SuppressWarnings, no records, and steps 07 and 08 undisturbed"

echo "solve: step 09 complete"

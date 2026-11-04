#!/usr/bin/env bash
# Golden solution, step 10 -- 10-java21.
#
# Four transforms over the tree step 09 left behind, and three decisions not to
# make a fifth. Nothing here compiles anything; the build that proves the edits
# is the one the step is graded by.
#
#   the level        the build script that declares the compiler level asks for
#                    release 21 and declares source and target compatibility to
#                    match. Every module applies that one file, so the whole
#                    tree moves from major 61 to major 65 in three lines.
#   the patterns     three reflective type tests that stand on their own become
#                    `instanceof` patterns, and the cast that followed each of
#                    them goes
#   the switches     four if-chains that dispatch on the runtime type of one
#                    expression become four pattern switches over ten arms
#   the sequenced    seven guarded first-element reads become getFirst()
#
# WHY THE LEVEL MOVES AGAIN ONE RUNG AFTER IT LAST MOVED. Java 21 is the first
# release since 17 that gives this library anything it can spend: 21 finalised
# pattern matching for `switch` (JEP 441) and added `SequencedCollection` (JEP
# 431), and both land in code this tree already has. The class-file major is
# this rung's level must-flip, and the two language changes are the two that
# alter what the tree does rather than only how it reads -- a pattern switch
# throws on a null selector where the chain it replaces fell through, and
# `getFirst()` throws NoSuchElementException where `get(0)` threw
# IndexOutOfBoundsException.
#
# THE FOURTEEN REFLECTIVE TESTS THAT STAY. Twenty-seven sites in the main
# sources and thirteen of them convert. Nine of the fourteen that do not have a
# Class VALUE where the type would have to go -- `c.isInstance(v)`, with `c` a
# field or a parameter -- and there is no type name to write after
# `instanceof`; a candidate who invents one has written a tree that does not
# compile. The other five have a Class OBJECT where the value would have to go,
# and ask about assignability between two types rather than about the runtime
# type of a value. At the level of a text search all twenty-seven look alike.
#
# THE TWO CHAINS THAT SWITCH ON A `Class` OBJECT -- four of those five.
# `AnnotationDeploymentScenarioGenerator#generateDeployment` and `#validate`
# dispatch on `deploymentMethod.getReturnType()`. A pattern switch binds a
# VALUE to a type and there is no value here to bind. A candidate who writes
# `switch (returnType.getSimpleName())` has replaced a subtype test with a
# string comparison -- a different program that happens to pass the two tests
# this tree has for it.
#
# THE NINE FIRST-ELEMENT READS THAT STAY. Seven take `iterator().next()` off a
# `Set`, a `Collection` or this library's own service-loader type, and
# `getFirst()` does not exist on any of them -- `SequencedCollection` is what
# `List` and `Deque` implement. The other two read `get(0)` with NO guard in
# front of them, off a result list a caller controls, so the exception a caller
# sees would change. All seven that do move are guarded by a size test on the
# same receiver, so on the path that reaches the read the two spellings cannot
# differ.
#
# THE FORTY-FOUR @SuppressWarnings. They are what the tree needs, not what it
# can be talked out of needing, and nothing on this rung adds a forty-fifth: a
# type pattern removes a cast rather than a warning, and a pattern switch over
# an exhaustive-by-default selector raises none.
#
# The block below is embedded verbatim from authoring/solution-src/java21.py by
# authoring/mk-solve10.py, which reads it back out of this file afterwards and
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
# java21.py refuses a tree that does not hold every anchor it was computed
# against -- but a solve.sh that let those lines scroll past unread would still
# be trusting that the program ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it the commonest failure -- run against a tree that is not the one step 09
# produces -- kills the script on the line below with java21.py's whole
# explanation still sitting unprinted inside ${j21}.
j21=$(python3 - "${TREE}" 2>&1 <<'PY_JAVA21'
#!/usr/bin/env python3
"""Step 10's migration, applied to a tree in place.

Java 21, and the second rung in a row whose class-file level moves -- 61 to 65.
Four transforms, one per thing the era gave this library that it can actually
spend:

  the level        every .gradle file that declares the compiler level has the
                   number replaced wherever it appears, so the tree compiles at
                   release 21 and lands at major 65.
  the patterns     three reflective type tests that stand on their own --
                   `X.class.isInstance(v)` followed by `X.class.cast(v)` --
                   become `v instanceof X x`, and the cast goes with them.
  the switches     four if-chains that dispatch on the runtime type of one
                   expression become four pattern switches over ten arms, and
                   each one's answer to a null selector is preserved by hand.
  the sequenced    seven guarded first-element reads become `getFirst()`.

AND THREE POPULATIONS THAT LOOK LIKE THEY BELONG HERE AND DELIBERATELY DO NOT
MOVE.

THE FOURTEEN REFLECTIVE TESTS WITH NO `instanceof` FORM. Twenty-seven sites in
the main sources, and fourteen of them cannot become a language type test. Nine
have a Class VALUE where the TYPE would have to go -- `c.isInstance(v)` and
`c.isAssignableFrom(t)`, with `c` a field or a parameter -- and there is no type
name to write after `instanceof`; a candidate who invents one has written a tree
that does not compile. Five have a Class OBJECT where the VALUE would have to go
-- `X.class.isAssignableFrom(m.getReturnType())` -- and ask about assignability
between two types rather than about the runtime type of a value. At the level of
a text search all twenty-seven look alike, which is the eleventh trap in its
plainest form: the form has to be read, not grepped. The eight further sites in
the repo's own tests are not this program's business.

THE TWO CHAINS THAT SWITCH ON A `Class` OBJECT -- four of those five.
`AnnotationDeploymentScenarioGenerator#generateDeployment` and `#validate`
dispatch on `deploymentMethod.getReturnType()` through
`Archive.class.isAssignableFrom(..)` and `Descriptor.class.isAssignableFrom(..)`.
The selector is a `Class` object and the arms ask about assignability; a
pattern switch binds a VALUE to a type and there is no value here to bind. A
candidate who writes `switch (returnType.getSimpleName())` has replaced a
subtype test with a string comparison -- a different program that happens to
pass the two tests this tree has for it.

THE NINE FIRST-ELEMENT READS THAT STAY. Two questions decide, and both are
visible in the line above the read. Seven of the nine take `iterator().next()`
off a `java.util.Set`, a `java.util.Collection` or this library's own
service-loader type, and `getFirst()` does not exist on any of them --
`SequencedCollection` is what `List` and `Deque` implement. The other two read
`get(0)` with NO guard in front of them, off a result list a caller controls:
`get(0)` throws `IndexOutOfBoundsException` on an empty list and `getFirst()`
throws `NoSuchElementException`, so converting them changes what a caller sees.
All seven that do move are guarded by a size test on the same receiver, so on
the path that reaches the read the two spellings cannot differ.

THE NULL QUESTION IS ANSWERED PER SITE AND NOT BY A RULE. A pattern switch is
the one construct on this rung that changes behaviour while compiling
perfectly: `X.class.isInstance(v)` is false when `v` is null and the chain
falls through, while `switch (v)` over type patterns throws
NullPointerException before any arm is considered. Writing `case null` on all
four would be as wrong as writing it on none:

  both servlet packagers        reached their `throw` by way of
                                `applicationArchive.getClass()`, so a null
                                archive already threw. `default -> throw ...`.
  ResourceInjectionEnricher     `#resolveResource` asked `element.getClass()`
                                before it asked anything else, so null threw
                                before any arm was tried. `default -> null`.
  BeansXMLProtocolProcessor     `#process` fell out of the method on anything
                                else, a null protocol archive included. This
                                one, and only this one, takes
                                `case null, default -> { }`.

EVERY EDIT IS ASSERTED, for the reason step 09's script gives: this runs on
more than one tree -- the chained golden, the blind snapshot, the calibration
arms -- and a transform that quietly matched nothing produces a tree that
builds, passes family A, and scores zero on the leg it was meant to
demonstrate. Nothing here keys on a line number.

WHAT IS COUNTED AND WHAT IS NOT. The census asserts DELTAS where the absolute
is a property of the input tree, and EQUALITY where the whole point is that a
number did not move -- the eight `iterator().next()` reads and the thirty-five
`permits` names step 09 wrote.
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
        sys.exit("java21: %s: expected 1 occurrence of %r, found %d (%s)"
                 % (rel, old[:70], n, why))
    if new in text:
        sys.exit("java21: %s: the replacement is already present (%s)"
                 % (rel, why))
    write(rel, text.replace(old, new))


def sub1_any(rel, pairs, why):
    """Replace the ONE of several spellings this tree happens to carry.

    MORE THAN ONE FINISHED STEP 09 REACHES THIS PROGRAM and they do not all
    spell the same method the same way -- the fact section 1 states about the
    build files is true of the sources too. `findDefaultProtocol` is a
    for-loop and an `if` in the tree step 09's blind run produced and a
    `declaredDefault.or(() -> ...)` with a ternary in the tree step 09's own
    solution produces, because step 05's Optional gate scores the LOOKUP and
    not its shape. Both carry one guarded `protocols.get(0)`, which is what
    this rung asks about, so both are anchored here.

    Exactly one alternative must occur exactly once and none may occur twice.
    Two matches is as much a failure as none: then the program does not know
    which tree it is standing on, and the guard it thinks it is preserving
    may belong to the other one.
    """
    text = read(rel)
    counts = [text.count(o) for o, _ in pairs]
    if max(counts) > 1:
        sys.exit("java21: %s: an alternative spelling occurs %d times (%s)"
                 % (rel, max(counts), why))
    if counts.count(1) != 1:
        sys.exit("java21: %s: %d of %d alternative spellings matched, want "
                 "exactly 1 (%s)" % (rel, counts.count(1), len(pairs), why))
    old, new = pairs[counts.index(1)]
    sub1(rel, old, new, why)


def sources():
    """Every .java file on the tree, build output excluded."""
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build")]
        for nm in sorted(names):
            if nm.endswith(".java"):
                yield os.path.join(dirpath, nm)


def count(pattern, scope=None):
    """Occurrences of `pattern` across the tree's Java sources.

    `scope="main"` restricts to `src/main`, which matters for exactly one
    figure: the reflective type tests are counted there because the repo's own
    tests hold eight more of them and rule 6 leaves those alone. Every other
    census line is a delta and does not care.

    MULTILINE always: the case-label patterns anchor on `^` to mean "a label at
    the head of a line" rather than "the word `case` somewhere in an
    expression".
    """
    rx = re.compile(pattern, re.M)
    n = 0
    for path in sources():
        if scope and (os.sep + "src" + os.sep + scope + os.sep) not in path:
            continue
        with open(path, encoding="utf-8", errors="replace") as fh:
            n += len(rx.findall(fh.read()))
    return n


# The populations the census reads. Each is a spelling that exists on the way
# in, so every figure below is a delta against a measured number rather than a
# constant this program carries.
CAST = r"\.class\.cast\("
ISINSTANCE = r"\.class\.isInstance\("
ISASSIGN = r"\.class\.isAssignableFrom\("
GET0 = r"\.get\(0\)"
GETFIRST = r"\.getFirst\(\)"
ITERNEXT = r"\.iterator\(\)\.next\(\)"
# Every reflective type test, whatever its receiver: the nine held sites ask
# through a Class value and a receiver pattern would miss them.
REFTEST = r"\.is(?:Instance|AssignableFrom)\("
# A type name, an optional type argument list, and a lowercase-initial
# identifier: `instanceof ClassContainer<?> classContainer` matches and
# `instanceof ArchiveAsset )` does not.
BOUND = (r"instanceof\s+(?:[A-Za-z_$][\w$.]*)(?:\s*<[^>()\n]*>)?"
         r"\s+([a-z][\w$]*)\b")
ANY_INSTANCEOF = r"\binstanceof\b"
CASE_PATTERN = (r"^\s*case\s+[A-Z][\w$.]*(?:\s*<[^>\n]*>)?\s+[a-z][\w$]*\s*->")
CASE_NULL = r"^\s*case null\b"
SWITCH = r"\bswitch\s*\("

before_cast = count(CAST)
before_isinstance = count(ISINSTANCE)
before_isassign = count(ISASSIGN)
before_get0 = count(GET0)
before_getfirst = count(GETFIRST)
before_iternext = count(ITERNEXT)
before_reftest = count(REFTEST, scope="main")
before_held = count(ITERNEXT, scope="main") + count(GET0, scope="main")
before_bound = count(BOUND)
before_any = count(ANY_INSTANCEOF)
before_casepat = count(CASE_PATTERN)
before_casenull = count(CASE_NULL)
before_switch = count(SWITCH)

# ---------------------------------------------------------------------------
# 1. The level.
# ---------------------------------------------------------------------------
# A SWEEP OF THE BUILD SCRIPTS AND NOT ONE NAMED FILE. Step 09's own solution
# learned this the hard way (14.12): more than one finished predecessor reaches
# this program, and they do not all put the level in the same file. What moves
# here is the NUMBER, wherever the build happens to say it.
#
# Asking by `--release` rather than by `-target` alone is step 07's decision
# carried forward: it caps the platform API at the same version as the
# bytecode, so a call to something a later JDK added is a compile error here
# rather than a NoSuchMethodError on a Java 21 runtime.
#
# THE PROSE ABOVE A MOVED LINE MOVES WITH IT, and the carry is deliberately
# narrow: the contiguous run of `//` lines immediately above a line that
# actually changed, bare `17` tokens only. `--release`, `-source`, `major
# version 61` and a dependency version elsewhere in the same file are all left
# exactly as they are.
WAS, NOW = "17", "21"
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
    sys.exit("java21: no build script declares a Java level at %s; there is "
             "nothing here to move to %s" % (WAS, NOW))
if not [rel for rel in buildfiles()
        if ("options.release = " + NOW) in read(rel)]:
    sys.exit("java21: the level moved in %s but no build script asks for it by "
             "release" % ", ".join(level_files))

# ---------------------------------------------------------------------------
# 2. The three reflective type tests that stand on their own.
# ---------------------------------------------------------------------------
# The other ten convertible sites are arms of the four chains below and are
# done there, as switch arms rather than as `instanceof` -- converting them
# here and then again there would be two edits undoing each other.
IMPLB = "impl-base/src/main/java/org/jboss/arquillian/impl/"
RESOURCE = ("testenrichers/resource/src/main/java/org/jboss/arquillian/"
            "testenricher/resource/ResourceInjectionEnricher.java")

# The pattern variable takes the name the cast's own local already had where
# there was one, and the type's own lower-camel spelling where the cast was
# inline.
sub1(IMPLB + "client/deployment/DeploymentGenerator.java",
     "            if(ClassContainer.class.isInstance(applicationArchive)) \n"
     "            {\n"
     "               ClassContainer<?> classContainer = ClassContainer.class"
     ".cast(applicationArchive);\n"
     "               classContainer.addClass(testCase.getJavaClass());\n"
     "            }\n",
     "            if(applicationArchive instanceof ClassContainer<?> classContainer)\n"
     "            {\n"
     "               classContainer.addClass(testCase.getJavaClass());\n"
     "            }\n",
     "the deployment's class container")

sub1("protocols/jmx-osgi/src/main/java/org/jboss/arquillian/packager/osgi/"
     "OSGiDeploymentPackager.java",
     "      if(JavaArchive.class.isInstance(bundleArchive))\n"
     "      {\n"
     "         return handleArchive(JavaArchive.class.cast(bundleArchive), "
     "testDeployment.getAuxiliaryArchives());\n"
     "      }\n",
     "      if(bundleArchive instanceof JavaArchive javaArchive)\n"
     "      {\n"
     "         return handleArchive(javaArchive, "
     "testDeployment.getAuxiliaryArchives());\n"
     "      }\n",
     "the OSGi bundle archive")

# THE ONE PATTERN THAT BINDS A VARIABLE THE OLD CODE DID NOT HAVE A LOCAL FOR,
# and the one that shows why nothing on this rung grades `kind=bound`: the
# value is read once, in the comparison on the next line. A pattern here is
# still the right edit -- it is one type test instead of a test and a cast --
# and a candidate who left it as a bare `instanceof Character` would be equally
# right. The verifier asks that the method name the type in a language type
# test, not that it declare a variable.
sub1(RESOURCE,
     "      else if(Character.class.isInstance(currentValue))\n"
     "      {\n"
     "         if( Character.class.cast(currentValue) == (char)0) \n",
     "      else if(currentValue instanceof Character character)\n"
     "      {\n"
     "         if( character == (char)0) \n",
     "the primitive-null Character test")

# ---------------------------------------------------------------------------
# 3. The four chains.
# ---------------------------------------------------------------------------
# The two servlet packagers carry the same three arms over the same selector in
# two files -- v_2_5 and v_3 -- and the replacement text is identical. The
# assertion is per file, so a tree that holds one and not the other is refused
# rather than half-migrated.
SERVLET_OLD = (
    "      if(EnterpriseArchive.class.isInstance(applicationArchive))\n"
    "      {\n"
    "         return handleArchive(EnterpriseArchive.class.cast(applicationArchive), "
    "auxiliaryArchives, protocol, processor);\n"
    "      } \n"
    "\n"
    "      if(WebArchive.class.isInstance(applicationArchive))\n"
    "      {\n"
    "         return handleArchive(WebArchive.class.cast(applicationArchive), "
    "auxiliaryArchives, protocol, processor);\n"
    "      } \n"
    "\n"
    "      if(JavaArchive.class.isInstance(applicationArchive))\n"
    "      {\n"
    "         return handleArchive(JavaArchive.class.cast(applicationArchive), "
    "auxiliaryArchives, protocol, processor);\n"
    "      }\n"
    "\n"
    "      throw new IllegalArgumentException(ServletProtocolDeploymentPackager."
    "class.getName()  + \n"
    "            \" can not handle archive of type \" +  applicationArchive."
    "getClass().getName());\n")

SERVLET_NEW = (
    "      return switch(applicationArchive)\n"
    "      {\n"
    "         case EnterpriseArchive enterpriseArchive ->\n"
    "            handleArchive(enterpriseArchive, auxiliaryArchives, protocol, "
    "processor);\n"
    "         case WebArchive webArchive ->\n"
    "            handleArchive(webArchive, auxiliaryArchives, protocol, processor);\n"
    "         case JavaArchive javaArchive ->\n"
    "            handleArchive(javaArchive, auxiliaryArchives, protocol, processor);\n"
    "         // The chain this replaces reached its throw by way of\n"
    "         // applicationArchive.getClass(), so a null archive threw there too.\n"
    "         default -> throw new IllegalArgumentException("
    "ServletProtocolDeploymentPackager.class.getName()  + \n"
    "               \" can not handle archive of type \" +  applicationArchive."
    "getClass().getName());\n"
    "      };\n")

for version in ("v_2_5", "v_3"):
    sub1("protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/"
         "%s/ServletProtocolDeploymentPackager.java" % version,
         SERVLET_OLD, SERVLET_NEW,
         "the %s servlet packager's dispatch" % version)

# The one STATEMENT switch of the four, and the one that needs `case null`.
sub1("testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/"
     "BeansXMLProtocolProcessor.java",
     "         if(WebArchive.class.isInstance(protocolArchive))\n"
     "         {\n"
     "            WebArchive.class.cast(protocolArchive).addAsWebInfResource("
     "EmptyAsset.INSTANCE, \"beans.xml\");\n"
     "         }\n"
     "         else if(JavaArchive.class.isInstance(protocolArchive))\n"
     "         {\n"
     "            JavaArchive.class.cast(protocolArchive).addAsManifestResource("
     "EmptyAsset.INSTANCE, \"beans.xml\");\n"
     "         }\n",
     "         switch(protocolArchive)\n"
     "         {\n"
     "            case WebArchive webArchive ->\n"
     "               webArchive.addAsWebInfResource(EmptyAsset.INSTANCE, "
     "\"beans.xml\");\n"
     "            case JavaArchive javaArchive ->\n"
     "               javaArchive.addAsManifestResource(EmptyAsset.INSTANCE, "
     "\"beans.xml\");\n"
     "            // The chain this replaces fell out of the method on anything\n"
     "            // else, a null protocol archive included. Without these two\n"
     "            // labels the switch would throw where it used to return.\n"
     "            case null, default -> { }\n"
     "         }\n",
     "the beans.xml protocol archive dispatch")

# The accumulator local goes with the chain: a switch EXPRESSION is the answer
# here because every arm of the chain assigned to it and the method returned it
# unchanged.
sub1(RESOURCE,
     "      Object resolvedResource = null;\n"
     "      // This implementation is based on previous behavior in injectClass()\n"
     "      if (Field.class.isAssignableFrom(element.getClass()))\n"
     "      {\n"
     "         resolvedResource = lookup(getResourceName((Field) element));\n"
     "      }\n"
     "      else if (Method.class.isAssignableFrom(element.getClass()))\n"
     "      {\n"
     "         resolvedResource = lookup(getResourceName(element.getAnnotation("
     "Resource.class)));\n"
     "      }\n"
     "      return resolvedResource;\n",
     "      // This implementation is based on previous behavior in injectClass()\n"
     "      return switch (element)\n"
     "      {\n"
     "         case Field field -> lookup(getResourceName(field));\n"
     "         case Method method -> lookup(getResourceName(method.getAnnotation("
     "Resource.class)));\n"
     "         // The chain this replaces asked element.getClass() before it asked\n"
     "         // anything else, so a null element threw before any arm was tried.\n"
     "         default -> null;\n"
     "      };\n",
     "the annotated-element dispatch")

# ---------------------------------------------------------------------------
# 4. The seven guarded first-element reads.
# ---------------------------------------------------------------------------
# Each anchor carries the GUARD as well as the read, which is the whole of what
# separates these seven from the two that stay: `get(0)` under a size test and
# `get(0)` with nothing in front of it are one keystroke apart in the text and
# a different promise to the caller. The two jmx sites have no guard in the
# three lines above them and are anchored on the assignment alone -- their
# receiver is a fresh `ArrayList` from `MBeanServerFactory.findMBeanServer`,
# which the two lines above create and populate.
SEQUENCED = [
    (IMPLB + "domain/ContainerRegistry.java",
     "      if(containers.size() == 1)\n"
     "      {\n"
     "         return Optional.of(containers.get(0));\n",
     "      if(containers.size() == 1)\n"
     "      {\n"
     "         return Optional.of(containers.getFirst());\n",
     "the only registered container"),
    (IMPLB + "domain/ProtocolRegistry.java",
     # TWO SPELLINGS, ONE GUARDED READ. See sub1_any. Both alternatives carry
     # the `protocols.size() == 1` test in the anchor, so neither can match a
     # get(0) that has lost its guard.
     [("      if(protocols.size() == 1)\n"
       "      {\n"
       "         return Optional.of(protocols.get(0));\n",
       "      if(protocols.size() == 1)\n"
       "      {\n"
       "         return Optional.of(protocols.getFirst());\n"),
      ("      return declaredDefault.or(() -> protocols.size() == 1\n"
       "         ? Optional.of(protocols.get(0))\n",
       "      return declaredDefault.or(() -> protocols.size() == 1\n"
       "         ? Optional.of(protocols.getFirst())\n")],
     None,
     "the only registered protocol"),
    (IMPLB + "enricher/resource/URLResourceProvider.java",
     "            return toURL(context.getServlets().get(0));\n",
     "            return toURL(context.getServlets().getFirst());\n",
     "the context's first servlet"),
    ("junit/src/main/java/org/jboss/arquillian/junit/Arquillian.java",
     "      if(exceptions.size() == 1)\n"
     "      {\n"
     "         throw exceptions.get(0);\n",
     "      if(exceptions.size() == 1)\n"
     "      {\n"
     "         throw exceptions.getFirst();\n",
     "the single collected exception"),
    ("protocols/jmx/src/main/java/org/jboss/arquillian/protocol/jmx/"
     "JMXMethodExecutor.java",
     "         mbeanServer = serverArr.get(0);\n",
     "         mbeanServer = serverArr.getFirst();\n",
     "the jmx protocol's MBean server"),
    ("spi/src/main/java/org/jboss/arquillian/spi/client/deployment/"
     "DeploymentScenario.java",
     "      if(deployments.size() == 1)\n"
     "      {\n"
     "         return Optional.of(deployments.get(0));\n",
     "      if(deployments.size() == 1)\n"
     "      {\n"
     "         return Optional.of(deployments.getFirst());\n",
     "the only deployment in the scenario"),
    ("testenrichers/osgi/src/main/java/org/jboss/arquillian/testenricher/osgi/"
     "OSGiTestEnricher.java",
     "         mbeanServer = serverArr.get(0);\n",
     "         mbeanServer = serverArr.getFirst();\n",
     "the osgi enricher's MBean server"),
]

for rel, old, new, why in SEQUENCED:
    if new is None:
        sub1_any(rel, old, why)
    else:
        sub1(rel, old, new, why)

# ---------------------------------------------------------------------------
# 5. The census.
# ---------------------------------------------------------------------------
after_cast = count(CAST)
after_isinstance = count(ISINSTANCE)
after_isassign = count(ISASSIGN)
after_get0 = count(GET0)
after_getfirst = count(GETFIRST)
after_iternext = count(ITERNEXT)
after_reftest = count(REFTEST, scope="main")
after_held = count(ITERNEXT, scope="main") + count(GET0, scope="main")
after_bound = count(BOUND)
after_any = count(ANY_INSTANCEOF)
after_casepat = count(CASE_PATTERN)
after_casenull = count(CASE_NULL)
after_switch = count(SWITCH)


def moved(name, before, after, want):
    if after - before != want:
        sys.exit("java21: %s went %d -> %d, expected a change of exactly %+d"
                 % (name, before, after, want))


# Thirteen reflective type tests leave the main sources and fourteen stay. This
# pair is C-2's own arithmetic and the only place the census states an ABSOLUTE:
# the leg scores the held half as well as the converted one, so a tree that took
# thirteen out by deleting four more would satisfy the delta and fail the leg.
moved("reflective type tests in the main sources",
      before_reftest, after_reftest, -13)
if after_reftest != 14:
    sys.exit("java21: %d reflective type test(s) left in the main sources, "
             "expected the 14 that have no instanceof form -- nine asking "
             "through a Class value, five about assignability"
             % after_reftest)

# The thirteen by spelling: eleven `X.class.isInstance(v)` and the two
# `isAssignableFrom` arms of the annotated-element chain. Every `.class.cast(..)`
# that stood beside one of them leaves with it -- three beside the standalone
# tests, eight inside the chains.
moved("X.class.cast(..) sites", before_cast, after_cast, -11)
moved("X.class.isInstance(..) sites", before_isinstance, after_isinstance, -11)
moved("X.class.isAssignableFrom(..) sites", before_isassign, after_isassign, -2)
# Three type patterns arrive as `instanceof` and ten as switch arms. Every new
# `instanceof` binds, so the two counts move together: a rise in the bare word
# that outran the rise in bound patterns would be a test written without one.
moved("instanceof sites", before_any, after_any, 3)
moved("bound patterns", before_bound, after_bound, 3)
moved("pattern case labels", before_casepat, after_casepat, 10)
moved("case null labels", before_casenull, after_casenull, 1)
moved("switch statements and expressions", before_switch, after_switch, 4)
moved("get(0) reads", before_get0, after_get0, -7)
moved("getFirst() reads", before_getfirst, after_getfirst, 7)

# The held half, stated as an EQUALITY because the point is that it did not
# move. Seven of the nine are `iterator().next()` off a type that has no
# `getFirst()` to call, and the other two are the unguarded `get(0)` reads whose
# exception a conversion would change.
if after_iternext != before_iternext:
    sys.exit("java21: iterator().next() reads went %d -> %d; not one of them "
             "moves on this rung" % (before_iternext, after_iternext))
if after_held != 9:
    sys.exit("java21: %d first-element read(s) left standing in the main "
             "sources, expected the 9 that cannot move -- 7 off a type with no "
             "getFirst() and 2 unguarded" % after_held)

# The two chains that must stay chains, by file and by shape. Nothing else in
# this image can see this mistake: a `switch` over `getSimpleName()` compiles,
# and the two tests this tree has for these methods both pass.
ADSG = (IMPLB + "client/deployment/AnnotationDeploymentScenarioGenerator.java")
adsg = read(ADSG)
if re.search(SWITCH, adsg):
    sys.exit("java21: %s gained a switch -- its two chains dispatch on a Class "
             "object and have no value to bind" % ADSG)
if adsg.count("Archive.class.isAssignableFrom(") != 2 \
        or adsg.count("Descriptor.class.isAssignableFrom(") != 2:
    sys.exit("java21: %s no longer asks assignability twice for each of Archive "
             "and Descriptor" % ADSG)

# Steps 08 and 09, still standing. The ninth trap in the long form: a step-k
# requirement that quietly makes a step-k-1 test unpassable is invisible from
# inside a step-local mean, and rewriting a dispatch chain is exactly the kind
# of edit that could take a sealed hierarchy or a text block with it.
if count(r'"""') != 2:
    sys.exit("java21: the report page is no longer one text block -- step 09 "
             "wrote it and this rung does not revisit it")
seal = count(r"\bsealed\b")
if seal != 11:
    sys.exit("java21: %d sealed declaration(s) on the tree, expected step 09's "
             "11" % seal)
if not [rel for rel in buildfiles()
        if "attributes('Automatic-Module-Name'" in read(rel)]:
    sys.exit("java21: no build file writes Automatic-Module-Name -- step 08's "
             "twelve module names are published from there")

print("java21: level: source, target and release at 21 in: %s, %d line(s), "
      "%d prose token(s)"
      % (", ".join(level_files), level_lines, level_prose))
print("java21: patterns: 3 standalone reflective test(s) became instanceof "
      "patterns, %d cast(s) gone with them" % (before_cast - after_cast))
print("java21: switches: 4 pattern switch(es) over %d arm(s), %d with a null "
      "label" % (after_casepat - before_casepat,
                 after_casenull - before_casenull))
print("java21: sequenced: %d guarded first-element read(s) became getFirst(), "
      "%d read(s) that cannot move held"
      % (after_getfirst - before_getfirst, after_held))
print("java21: kept: %d reflective test(s) with no instanceof form, the two "
      "chains over a Class object, the nine reads that cannot move"
      % after_reftest)
print("JAVA21 OK")
PY_JAVA21
) || true
printf '%s\n' "${j21}"
for want in \
      'java21: patterns: 3 standalone reflective test(s) became instanceof patterns, 11 cast(s) gone with them' \
      'java21: switches: 4 pattern switch(es) over 10 arm(s), 1 with a null label' \
      'java21: sequenced: 7 guarded first-element read(s) became getFirst(), 9 read(s) that cannot move held' \
      'java21: kept: 14 reflective test(s) with no instanceof form, the two chains over a Class object, the nine reads that cannot move' \
      'JAVA21 OK'; do
   if ! printf '%s\n' "${j21}" | grep -qxF "${want}"; then
      echo "solve10: java21.py did not report: ${want}" >&2
      exit 1
   fi
done
# The level print line includes the file names, which vary by tree shape.
if ! printf '%s\n' "${j21}" | grep -qE '^java21: level: source, target and release at 21 in:'; then
   echo "solve10: java21.py did not report the level move" >&2
   exit 1
fi

# ==========================================================================
# 2. The level, re-read off the tree.
# ==========================================================================
# The level was moved by java21.py, so this section re-reads it off the tree
# rather than setting it. Two questions, and the second is the one that
# matters: does at least one build script now ask for 21 by `--release`, and is
# there any other build file still pinning a level that is not 21?
#
# ASKED AS A SHAPE AND NOT AS A FILE NAME, which is step 09's lesson carried
# forward (14.12): the finished step 09s put the level in different files, and
# an assertion that named one of them crashed on the tree it was not written
# against. Which build file holds it is step 09's business and not this rung's.
if ! grep -rnqE --include='*.gradle' --exclude-dir=build \
        'options\.release = 21' .; then
   echo "solve10: no build script asks for options.release = 21 after the level edit" >&2
   exit 1
fi
left=$(grep -rnE --include='*.gradle' --exclude-dir=build \
         'JavaVersion\.VERSION_|JavaLanguageVersion\.of|options\.release' . \
       | grep -vE 'VERSION_21|\.of\(21\)|options\.release = 21' || true)
if [ -n "${left}" ]; then
   echo "solve10: a build file still pins a level other than 21:" >&2
   printf '%s\n' "${left}" >&2
   exit 1
fi
echo "solve10: the level is 21, asked for by release, in one file and no other"

# ==========================================================================
# 3. The reflective type tests.
# ==========================================================================
# `--exclude-dir=build` throughout: a Gradle build leaves copies of sources
# under each project's build/, and this tree additionally keeps two checked-in
# IDE test templates under build/ide that belong to no source set. Counting
# either would make the answer depend on whether anyone had run the build
# before running this.
#
# THE MAIN SOURCES AND NOT THE TREE. The repo's own tests hold eight more
# reflective type tests, and rule 6 leaves those alone; a tree-wide count would
# be graded against a number this step must not move.
#
# EVERY COUNTING PIPELINE ENDS IN `|| true`, and that is `pipefail` and not
# style. `set -o pipefail` gives `grep ... | wc -l` grep's status, so a count
# that comes out as zero -- exactly the case some of these checks exist to
# report -- would abort the script on the assignment with `set -e` and print
# nothing at all.
r=$(grep -rhoE --include='*.java' --exclude-dir=build \
      '\.is(Instance|AssignableFrom)\(' */src/main */*/src/main 2>/dev/null | wc -l || true)
if [ "${r}" != 14 ]; then
   echo "solve10: ${r} reflective type test(s) left in the main sources, wanted 14" >&2
   echo "solve10: thirteen convert; the fourteen that stay have a Class value or a" >&2
   echo "solve10: Class object where a pattern needs a type or a value" >&2
   exit 1
fi
# The cast that stood beside each converted test is gone with it, and this one
# is an absolute: `X.class.cast(v)` is what a type pattern replaces, and after
# this rung the tree holds none at all.
c=$(grep -rhoF --include='*.java' --exclude-dir=build '.class.cast(' . | wc -l || true)
if [ "${c}" != 0 ]; then
   echo "solve10: ${c} reflective cast(s) survive -- a type pattern replaces the cast too" >&2
   exit 1
fi
# A type name, an optional type argument list, and a lowercase-initial
# identifier. `instanceof ClassContainer<?> classContainer` matches;
# `instanceof ArchiveAsset )` does not, which is what makes this the
# bound-pattern population and not a count of the word.
IPBOUND='instanceof +[A-Za-z_$][A-Za-z0-9_$.]*( *<[^>()]*>)? +[a-z][A-Za-z0-9_$]*'
n=$(grep -rhoE --include='*.java' --exclude-dir=build "${IPBOUND}" . | wc -l || true)
t=$(grep -rhoE --include='*.java' --exclude-dir=build '\binstanceof\b' . | wc -l || true)
if [ "${n}" != 24 ] || [ "${t}" != 31 ]; then
   echo "solve10: ${n} of ${t} instanceof site(s) bind a variable, wanted 24 of 31" >&2
   echo "solve10: step 09 left 21 of 28 and this rung adds three, all of them bound" >&2
   exit 1
fi
# The two chains that must stay chains, by file and by shape. Nothing else in
# this image can see this mistake: a `switch` over `getSimpleName()` compiles,
# and the two tests this tree has for these methods both pass.
ADSG=impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/AnnotationDeploymentScenarioGenerator.java
if grep -qE '\bswitch\b *\(' "${ADSG}"; then
   echo "solve10: ${ADSG} gained a switch" >&2
   echo "solve10: its two chains dispatch on a Class object and have no value to bind" >&2
   exit 1
fi
for ty in Archive Descriptor; do
   a=$(grep -oF "${ty}.class.isAssignableFrom(" "${ADSG}" | wc -l || true)
   if [ "${a}" != 2 ]; then
      echo "solve10: ${ADSG} asks ${ty} assignability ${a} time(s), wanted 2" >&2
      exit 1
   fi
done
echo "solve10: 13 reflective test(s) converted, 14 held, no reflective cast left"

# ==========================================================================
# 4. The switches.
# ==========================================================================
# Both halves per file: the arms that arrived AND the null answer that had to
# be chosen. A tree that wrote `case null` on all four would read the same as
# the golden on a tree-wide arm count and would have changed what three methods
# do with a null argument.
PATCASE='^ *case [A-Za-z_$][A-Za-z0-9_$.]*( *<[^>]*>)? +[a-z][A-Za-z0-9_$]* *->'
p=$(grep -rhoE --include='*.java' --exclude-dir=build "${PATCASE}" . | wc -l || true)
if [ "${p}" != 10 ]; then
   echo "solve10: ${p} pattern case label(s) on the tree, wanted 10" >&2
   exit 1
fi
f=$(grep -rlE --include='*.java' --exclude-dir=build "${PATCASE}" . | wc -l || true)
if [ "${f}" != 4 ]; then
   echo "solve10: ${f} file(s) hold a pattern switch, wanted 4" >&2
   exit 1
fi
# Ten arms in four files, by file, so that a fifth switch somewhere else cannot
# pay for a chain that was never converted.
for pair in 'protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/v_2_5/ServletProtocolDeploymentPackager.java|3' \
            'protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/v_3/ServletProtocolDeploymentPackager.java|3' \
            'testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/BeansXMLProtocolProcessor.java|2' \
            'testenrichers/resource/src/main/java/org/jboss/arquillian/testenricher/resource/ResourceInjectionEnricher.java|2'; do
   src=${pair%%|*}; want=${pair#*|}
   got=$(grep -cE "${PATCASE}" "${src}" || true)
   if [ "${got}" != "${want}" ]; then
      echo "solve10: ${src} holds ${got} pattern arm(s), wanted ${want}" >&2
      exit 1
   fi
done
# The null label, once, in the one method whose chain fell through on null.
BEANS=testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/BeansXMLProtocolProcessor.java
nn=$(grep -rhoE --include='*.java' --exclude-dir=build '^ *case null' . | wc -l || true)
if [ "${nn}" != 1 ]; then
   echo "solve10: ${nn} case null label(s) on the tree, wanted exactly 1" >&2
   echo "solve10: three of the four chains already threw or returned on a null selector" >&2
   exit 1
fi
if ! grep -qE '^ *case null' "${BEANS}"; then
   echo "solve10: the one case null label is not in ${BEANS}" >&2
   echo "solve10: that is the only chain that fell out of its method on null" >&2
   exit 1
fi
# Step 09's two arrow switches are still arrow, and nothing on this rung wrote
# a colon label: fifteen case labels, all of them arrow form.
a=$(grep -rhoE --include='*.java' --exclude-dir=build '^ *case ' . | wc -l || true)
b=$(grep -rhoE --include='*.java' --exclude-dir=build '^ *case .*->' . | wc -l || true)
if [ "${a}" != 15 ] || [ "${b}" != 15 ]; then
   echo "solve10: ${b} of ${a} case label(s) are arrow form, wanted 15 of 15" >&2
   exit 1
fi
if grep -rqE --include='*.java' --exclude-dir=build '^ *case .*: *$' .; then
   echo "solve10: a colon-form case label is on the tree -- step 09 left none" >&2
   exit 1
fi
echo "solve10: 4 pattern switch(es) over 10 arm(s), one null label, no colon form"

# ==========================================================================
# 5. The sequenced reads.
# ==========================================================================
g=$(grep -rhoF --include='*.java' --exclude-dir=build '.getFirst()' \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
if [ "${g}" != 7 ]; then
   echo "solve10: ${g} getFirst() read(s) in the main sources, wanted 7" >&2
   exit 1
fi
i=$(grep -rhoF --include='*.java' --exclude-dir=build '.iterator().next()' \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
z=$(grep -rhoF --include='*.java' --exclude-dir=build '.get(0)' \
      */src/main */*/src/main 2>/dev/null | wc -l || true)
if [ "${i}" != 7 ] || [ "${z}" != 2 ]; then
   echo "solve10: ${i} iterator().next() and ${z} get(0) read(s) left in the main" >&2
   echo "solve10: sources, wanted 7 and 2 -- the nine that cannot move" >&2
   exit 1
fi
# The two that stay, by name. Neither has a guard in front of it, and
# getFirst() throws NoSuchElementException where get(0) throws
# IndexOutOfBoundsException: converting either changes what a caller sees on an
# empty result, and nothing in this image would notice.
for src in impl-base/src/main/java/org/jboss/arquillian/impl/EventTestRunnerAdaptor.java \
           junit/src/main/java/org/jboss/arquillian/junit/JUnitTestRunner.java; do
   if ! grep -qF '.get(0)' "${src}"; then
      echo "solve10: ${src} no longer reads get(0) -- that read is unguarded and stays" >&2
      exit 1
   fi
done
echo "solve10: 7 guarded first-element read(s) moved, 9 that cannot move held"

# ==========================================================================
# 6. The things this step deliberately does not do.
# ==========================================================================
# Asserted, not merely described. Each is an edit that looks like finishing the
# job and is not one, and a guard is cheaper than the argument.
n=$(grep -rhoF --include='*.java' --exclude-dir=build '@SuppressWarnings' . | wc -l || true)
if [ "${n}" != 44 ]; then
   echo "solve10: ${n} @SuppressWarnings on the tree, wanted 44" >&2
   echo "solve10: a type pattern removes a cast, not a warning" >&2
   exit 1
fi
# No record, and this is not tidiness. A record is a different published API --
# accessors named after the component rather than getX() -- and every one of
# these classes has consumers outside this tree.
r=$(grep -rlE --include='*.java' --exclude-dir=build '^public( [a-z-]+)* record ' . | wc -l || true)
if [ "${r}" != 0 ]; then
   echo "solve10: ${r} type(s) became records -- that is a published API change" >&2
   exit 1
fi
# Steps 07, 08 and 09, still standing. The ninth trap in the long form: a
# step-k requirement that quietly makes a step-k-1 test unpassable is invisible
# from inside a step-local mean, and rewriting a dispatch chain is exactly the
# kind of edit that could take a sealed hierarchy or a text block with it.
#
# The permitted names are re-derived from the clauses on the tree rather than
# re-listed here, and every one of them is then checked to be `final`, `sealed`
# or `non-sealed` -- the compiler's own rule, so the check cannot drift from
# the step-09 edit it is guarding.
roots=$(grep -rlE --include='*.java' --exclude-dir=build '^ +permits ' . | sort)
nroots=$(printf '%s\n' "${roots}" | grep -c . || true)
s=$(grep -rhoE --include='*.java' --exclude-dir=build '^public [a-z -]*sealed (class|interface) ' . | wc -l || true)
if [ "${nroots}" != 11 ] || [ "${s}" != 11 ]; then
   echo "solve10: ${nroots} permits clause(s) and ${s} sealed declaration(s), wanted 11 and 11" >&2
   echo "solve10: step 09 closed those hierarchies and this rung does not revisit them" >&2
   exit 1
fi
# Captured into a variable and walked with a `for`, not piped into a `while`.
# A `while` on the right of a pipe is a subshell, and the `exit 1` inside it
# would end the subshell and leave this script running.
names=$(printf '%s\n' "${roots}" | while read -r src; do
   awk '/^[[:space:]]+permits /{p=1; b=$0; next}
        p && /^[[:space:]]*\{/{print b; p=0; b=""; next}
        p{b = b " " $0}' "${src}"
done | sed 's/^[[:space:]]*permits //' | tr ',' '\n' \
     | sed 's/[[:space:]]//g' | grep . || true)
np=$(printf '%s\n' "${names}" | grep -c . || true)
u=$(printf '%s\n' "${names}" | sort -u | grep -c . || true)
if [ "${np}" != 35 ] || [ "${u}" != 35 ]; then
   echo "solve10: the permits clauses name ${np} subtype(s), ${u} distinct, wanted 35 and 35" >&2
   exit 1
fi
for nm in ${names}; do
   src=$(find . -name "${nm}.java" -not -path '*/build/*' | head -1)
   if [ -z "${src}" ]; then
      echo "solve10: a permits clause names ${nm} and no ${nm}.java is on the tree" >&2
      exit 1
   fi
   if ! grep -qE "^public ([a-z-]+ )*(final|sealed|non-sealed) (class|interface) ${nm}\b" "${src}"; then
      echo "solve10: permitted subtype ${nm} is neither final, sealed nor non-sealed" >&2
      exit 1
   fi
done
tb=$(grep -rlF --include='*.java' --exclude-dir=build '"""' . | wc -l || true)
if [ "${tb}" != 1 ]; then
   echo "solve10: ${tb} file(s) hold a text block, wanted 1" >&2
   echo "solve10: step 09 made the report page one document and this rung leaves it" >&2
   exit 1
fi
# ASKED AS A SHAPE AND NOT AS A SPELLING, because this script meets more than
# one finished step 08: the goldens and the blind runs put the manifest
# attribute in different build files and write the method reference qualified
# or not. Both are a finished step 08.
if ! grep -qF 'Provider::type' \
      impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java; then
   echo "solve10: impl-base no longer names providers without loading them" >&2
   echo "solve10: step 08 moved that lookup onto java.util.ServiceLoader" >&2
   exit 1
fi
amn=$(grep -rlF --include='*.gradle' "attributes('Automatic-Module-Name'" gradle | wc -l || true)
if [ "${amn}" -lt 1 ]; then
   echo "solve10: no build file under gradle/ writes Automatic-Module-Name" >&2
   echo "solve10: step 08's twelve module names are published from there" >&2
   exit 1
fi
mi=$(find . -name 'module-info.java' -not -path '*/build/*' || true)
if [ -n "${mi}" ]; then
   echo "solve10: a module-info.java was written: ${mi}" >&2
   echo "solve10: step 08 claims names only, and this rung does not revisit that" >&2
   exit 1
fi
echo "solve10: 44 @SuppressWarnings, no records, and steps 07, 08 and 09 undisturbed"

echo "solve: step 10 complete"

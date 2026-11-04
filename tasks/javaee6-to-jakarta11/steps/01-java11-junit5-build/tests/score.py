"""Step 01 -- Java 11 `--release`, Maven 3.9-era build, JUnit 5 Jupiter.

    reward = migration * behaviour

`behaviour` is the fraction of the 428 recorded observations the built artifact
reproduces, and is computed by run_behaviour.sh. Everything in this file is the
other factor: the construct classes the instruction names, each asked of the
artifact or of the AST, never of the text.

The rung in one sentence: this step is where the project stops being a 2011
build.

What actually stops the pinned tree under JDK 11 is worth stating precisely,
because it is not what it looks like. `-source 1.6` is *not* the blocker: JDK
11's javac still accepts source 6, with a deprecation warning, and the nop run
(NOTES §8) gets six of the seven modules compiled at major 50 before it stops.
It stops in mini-git-server-war, on eight "reference to Module is ambiguous"
errors -- `WebAppInitializer` imports `com.google.inject.*`, and from JDK 9 on
`java.lang.Module` is implicitly in scope in every compilation unit. That is
the one hard compile error the 2011 tree has on this JDK.

`--release` is therefore asked for on its own merits rather than as the fix for
that error: `-source`/`-target` on a newer JDK compiles against the *running*
JDK's API and only fails at run time on the older one, and 1.6 stops being
accepted at all one JDK after this rung's. The rest of what the step drags in
-- the `oss-parent` pom, JUnit 3 `TestCase`, the EasyMock the parent declares
and nothing uses -- is what the gates below enumerate.

Every count this file compares is in CAL below, next to the number it was
derived from. Two rules for those numbers, both learned the hard way:

  * a construct the step *introduces* is measured by the old form going away as
    much as by the new form appearing, so each pair is two gates, not one; and
  * a threshold is derived from the reference solution and the baseline census,
    never chosen to make a run pass. NOTES-authoring §8 records both, and the
    step-(k-1) calibration is what proves the pair discriminates.
"""

import os

import gate_lib as G
import score_lib as S

# ---------------------------------------------------------------------------
# Calibration. Baseline figures are the pinned tree's, measured by this same
# AstScan (NOTES §5); solution figures are the reference tree's (NOTES §8).
# ---------------------------------------------------------------------------

CAL = {
    # tests: 5 concrete JUnit-3 classes, 19 test methods, 0 failures, 0 skips.
    # HookTestCase is abstract and contributes none; surefire's default
    # includes match its *TestCase name but never run it.
    "tests_min": 19,
    "test_classes": ("ConfigUtilTest", "SitePathsTest", "BasicSerializationTest",
                     "IdGeneratorTest", "SocketUtilTest"),

    # Language level. Four of the five idioms at this rung are *replacements*,
    # so each is measured twice -- the new form appearing and the old form
    # falling -- and both numbers are listed as baseline -> reference ->
    # threshold. The fifth, NIO.2, is not a replacement, and is calibrated
    # separately below.
    #
    #   new form                            old form
    #   diamond             0 -> 39 -> 25   explicit-type-args 47 ->  4 -> 10
    #   try-with-resources  0 ->  9 ->  5   try-plain          81 -> 64 -> 76
    #   multi-catch         0 ->  9 ->  4   catch-plain        83 -> 56 -> 79
    #   lambda+method-ref   0 ->  8 ->  4   anonymous          40 -> 35 -> 36
    #
    # Every "present" number starts from a baseline of zero, which is what
    # makes it evidence of work done at this rung rather than of work the tree
    # arrived with -- but zero-to-n on its own is evidence a tree can
    # manufacture, because writing four lambdas into a throwaway class costs
    # nothing and converts nothing. The old form's count is the half that
    # cannot be faked: nothing a candidate adds makes it go down. So each cap
    # is exactly `baseline - minimum` -- "at least `minimum` of the sites that
    # were there became the new form" -- which is the weakest cap that closes
    # the hole and the one that leaves every honest tree alone.
    #
    # The minima themselves sit well below what the reference reaches, because
    # the reference is one way of doing the rung and not the only one: the nine
    # try-with-resources are the nine `finally { close() }` sites the tree has,
    # and a candidate who converts five of them has raised the language level
    # as surely as one who converts all nine. The four explicit type argument
    # lists the reference keeps are all `TypeLiteral`, where reifying the
    # argument is the entire point of the class; the max of ten leaves room for
    # a different call on six more.
    #
    # `lambda` is counted together with `method-ref` because the instruction
    # offers both and `Foo::bar` retires an anonymous class exactly as a lambda
    # does. The reference reaches its eight with lambdas and writes no method
    # reference at all, so the pair is checked rather than assumed: a candidate
    # who answers entirely in method references has done the same work.
    "diamond_min": 25,
    "explicit_type_args_max": 10,
    "try_with_resources_min": 5,
    "try_plain_max": 76,
    "multi_catch_min": 4,
    "catch_plain_max": 79,
    "lambda_min": 4,
    "anonymous_max": 36,

    # NIO.2, the one idiom on the list with no old form to pair against.
    # `File` and `Path` interoperate by design, so adopting NIO.2 does not make
    # java.io.File references fall: every class the reference converts still
    # references java.io.File afterwards, because JGit's RepositoryCache.FileKey
    # takes a File and SitePaths publishes File-typed fields. A cap on
    # java.io.File tight enough to bite would therefore fail the reference,
    # which is rule 4's blocker in the other direction (NOTES 8.1.8, round 2
    # W3).
    #
    # So the two are asked for together in one class instead of separately: a
    # class counts when its own class file references both java.io.File and
    # java.nio.file. That is 1.5's sentence and nothing else -- filesystem work
    # done through Path/Files in code that still hands a File across a library
    # boundary -- and unlike a list of class names it is a property of the
    # candidate's tree, so an agent who moves the work into a class of their
    # own is measured on the work rather than on where it used to live.
    #
    # Measured with the verifier's own ArtifactScan over the built trees, own
    # classes only, inner classes folded into their outer:
    #
    #   tree                          java.io.File  java.nio.file  BOTH
    #   orw3 (OpenRewrite migrate)         13             0          0
    #   orw4 (OpenRewrite + idioms)        13             0          0
    #   sol1 (reference solution)          12             3          3
    #
    # The reference's three are SitePathsTest, LocalDiskRepositoryManager and
    # ToyPubKeyAuth. The minimum of two rather than three leaves one of them as
    # slack, because 1.5 describes the work qualitatively and names three areas
    # without saying all three must move; the two automated rungs reach zero
    # either way, so the number is not what separates them.
    #
    # One valve, in the gate rather than here: a tree in which no own class
    # references java.io.File anywhere has nothing left for NIO.2 to stand
    # beside, and is asked only for the count. It is not a hole -- getting
    # there means retiring every File reference in the tree, including the
    # library boundaries 1.5 explicitly declines to move -- but without it the
    # gate would fail a candidate for converting further than the reference
    # did, which is rule 4 in the direction that matters.
    "nio_file_classes_min": 2,

    # Suppressions may not grow. 31 is the baseline count, counted by argument
    # rather than by site (rule 11): consolidating four annotations into one
    # does not reduce it. Step 9 is the rung that drives it down.
    "suppressions_max": 31,

    # The tree as handed over: 117 .java files, 151 declared types. The
    # reference ends at 116/150, having retired HookTestCase -- the one file at
    # this rung whose deletion is a defensible call, since it contributes no
    # test case and is the only thing on the tree pulling junit:junit back onto
    # the test classpath.
    #
    # The two floors are deliberately not symmetrical. The file floor is the
    # reference's own count: 1.4 allows exactly one retirement and the
    # instruction says the rest of the source is still there, so a second
    # deleted file is a tree that did something the instruction forbids. The
    # type floor keeps a unit of slack, because 1.5 asks for anonymous classes
    # to become lambdas and a candidate who goes one better -- turning a *named*
    # single-method helper class into a lambda as well -- removes a declared
    # type without removing a file. That is the milestone being done well, and
    # a floor that failed it would be stricter than the instruction.
    "source_files_min": 116,
    "source_types_min": 149,

    # Java 11.
    "release": "11",
    "class_major": 55,
}

# JUnit 4 by its own type names, never by the `org.junit` prefix: Jupiter lives
# under org.junit.jupiter, and a prefix match would make the replacement
# indistinguishable from the thing being replaced.
JUNIT4 = ("org.junit.Assert", "org.junit.Assume", "org.junit.Test",
          "org.junit.Before", "org.junit.After", "org.junit.BeforeClass",
          "org.junit.AfterClass", "org.junit.Ignore", "org.junit.Rule",
          "org.junit.ClassRule", "org.junit.FixMethodOrder",
          "org.junit.runner", "org.junit.runners", "org.junit.rules",
          "org.junit.experimental", "org.junit.matchers",
          "org.junit.internal")
JUNIT3 = ("junit.framework", "junit.textui", "junit.extensions")
JUPITER = ("org.junit.jupiter",)
EASYMOCK = ("org.easymock",)

b = G.Bundle()
g = S.Gates()

# ---------------------------------------------------------------------------
# The floor. None of the rest means anything without these, and each fails in
# its own way: a tree that does not parse looks maximally migrated to every AST
# gate, and a tree that produced no artifact passes every "absent" gate
# vacuously. Both are asked out loud rather than left to be inferred from a
# behaviour score of zero.
# ---------------------------------------------------------------------------

G.parses_clean(g, b, weight=1.0)

# "Preserve the supplied source" is stated in every instruction and was, until
# this gate, measured by nothing. It belongs on the floor rather than with the
# idioms because it is what the idiom caps below are denominated in: a cap on
# plain `try` statements falls just as fast for a tree that deletes the file as
# for one that converts it, and only one of those is the milestone.
G.sources_survive(g, b, CAL["source_files_min"], CAL["source_types_min"],
                  weight=1.0)

art = b.artifact_path
g.add("build-and-artifact", b.build_rc == 0 and bool(art) and os.path.exists(art),
      "package rc=%s, artifact=%s" % (b.build_rc, art or "NONE"), 2.0,
      {"build_rc": b.build_rc, "artifact": art})

# The headline of the rung, read off the class-file headers rather than the
# pom: 2011's `-source 1.6` produced major 50, and this step produces major 55
# for every class the project compiles, test classes included.
G.class_release(g, b, CAL["class_major"], weight=3.0, mode="exactly")

# `--release` is the flag the instruction names, and it is the one thing the
# class file cannot witness: on a JDK 11 toolchain `-source 11 -target 11`
# produces identical bytes. Asked next to class_release, never instead of it.
G.compiler_release(g, b, CAL["release"], weight=1.0)

# A sun.* call is a warning at 1.6 and a link error later. Asked from step 1 on
# so that no rung inherits one.
G.jdk_internals(g, b, weight=1.0)

# ---------------------------------------------------------------------------
# The 2011 build
# ---------------------------------------------------------------------------

# org.sonatype.oss:oss-parent:7 -- a 2011 parent pom whose release profiles and
# plugin versions are the reason this build cannot run under a current Maven.
G.no_parent(g, b, "no-oss-parent", ("org.sonatype.oss:oss-parent",), weight=1.0)

# ---------------------------------------------------------------------------
# JUnit 3 and 4 out, Jupiter in. Six gates rather than one, because the ways
# this can be faked are all different: deleting the tests, keeping the old
# tests and adding the new engine alongside, or moving to Jupiter's API while
# junit:junit stays on the test classpath through junit-vintage.
# ---------------------------------------------------------------------------

G.tests_ship_and_pass(g, b, CAL["tests_min"], CAL["test_classes"], weight=3.0)

# `floor` on the three bytecode-absence gates below. All three are about test
# code, and "no compiled class references org.junit.Test" is equally true of a
# ported suite and of a tree whose tests did not compile -- build.sh records
# tests.rc separately but these gates never saw it. Each is handed the universe
# it has to have searched: at least one compiled class per test class the step
# requires, under a test-classes output directory. An empty universe is now a
# failure that says so rather than a pass.
# ArtifactScan relativizes every origin against the target/ root it walked,
# so the test output reads "test-classes/Foo.class" with no leading slash.
TESTS = (len(CAL["test_classes"]), "test-classes/")

G.no_source_types(g, b, "no-junit3-source", JUNIT3, weight=1.0)
G.no_refs(g, b, "no-junit3-classes", JUNIT3, weight=1.0, own=True, floor=TESTS)
G.no_source_types(g, b, "no-junit4-source", JUNIT4, weight=1.0)
G.no_refs(g, b, "no-junit4-classes", JUNIT4, weight=1.0, own=True, floor=TESTS)

# The resolved graph, not the pom: junit:junit arrives transitively through
# junit-vintage-engine and through JGit's own test jar, and neither is visible
# in anything the candidate wrote. Matched on groupId alone -- JUnit 3 and 4
# are `junit:junit` and vintage is `org.junit.vintage:*`, while every Jupiter
# artifact is *named* junit-jupiter-something, so an artifactId prefix of
# "junit" would reject the replacement along with the thing replaced.
G.dep_absent(g, b, "no-junit3-4-dep",
             group_prefixes=("junit", "org.junit.vintage"), weight=1.0)
G.dep_present(g, b, "junit5-dep", group_prefixes=("org.junit.jupiter",),
              minimum=1, weight=1.0)
G.uses_refs(g, b, "junit5-classes", JUPITER, minimum=len(CAL["test_classes"]),
            weight=1.0, own=True)

# EasyMock 3.0 is declared by the parent for every module and used by none of
# them. It goes with the parent; the gate is here so that "nothing used it" is
# a measured fact rather than an assumption.
G.dep_absent(g, b, "no-easymock-dep", group_prefixes=EASYMOCK,
             artifact_prefixes=("easymock",), weight=1.0)
G.no_refs(g, b, "no-easymock-classes", EASYMOCK, weight=1.0, own=True,
          floor=TESTS)

# ---------------------------------------------------------------------------
# Pre-Java-7 idioms. Each is a pair: the new form has to appear in numbers and
# the old form has to fall. A tree that adds one diamond and keeps forty-seven
# explicit type argument lists has not raised its language level.
# ---------------------------------------------------------------------------

def sites(node_type):
    """Every occurrence of one construct, as file:line, for g.present.

    The mirror image of gate_lib.no_nodes, which reports the same list to
    g.absent. A present-gate needs the sites and not just the count for the
    same reason an absent-gate does: a failure that says only "3 < 8" cannot be
    acted on, and a pass that names three files in one package is worth
    reading.
    """
    return ["%s:%s" % (n.get("p", "?"), n.get("line", 0))
            for n in b.ast.nodes(node_type)]


g.present("diamond", sites("diamond"), CAL["diamond_min"], 1.0)
G.nodes_at_most(g, b, "explicit-type-args", "explicit-type-args",
                CAL["explicit_type_args_max"], weight=1.0)

G.converted(g, b, "try-with-resources", "try-with-resources", "try-plain",
            CAL["try_with_resources_min"], CAL["try_plain_max"], weight=1.0)
G.converted(g, b, "multi-catch", "multi-catch", "catch-plain",
            CAL["multi_catch_min"], CAL["catch_plain_max"], weight=1.0)
G.converted(g, b, "lambda", ("lambda", "method-ref"), "anonymous",
            CAL["lambda_min"], CAL["anonymous_max"], weight=1.0)

# NIO.2. Asked of the candidate's own class files (own=True, inside
# uses_refs_beside) rather than of everything under target/: a dependency jar
# that references java.nio.file would otherwise pass this gate for a tree that
# never opened a Path.
#
# And asked only of classes that reference java.io.File as well, which is the
# stand-in for the old form the other four idioms are paired against. See CAL:
# nothing about adopting NIO.2 makes java.io.File references fall, so the half
# that cannot be faked has to be *where* the new form landed rather than what
# left, and the tree's own File references are where the filesystem work is.
G.uses_refs_beside(g, b, "uses-java-nio-file", ("java.nio.file",),
                   ("java.io.File",),
                   minimum=CAL["nio_file_classes_min"], weight=1.0)

G.suppressions_at_most(g, b, CAL["suppressions_max"], weight=1.0)

print("%.6f" % S.finish(g, b.behaviour_pair(), {"calibration": CAL}))

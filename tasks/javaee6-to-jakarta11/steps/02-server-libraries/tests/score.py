"""Step 02 -- upstream JGit 5.13, Apache MINA SSHD 2.x, a container-provided
Servlet 3.1 API.

    reward = migration * behaviour

`behaviour` is the fraction of the 428 recorded observations the built artifact
reproduces, computed by run_behaviour.sh -- and at this rung it is doing more
work than at any other, because 99 of those 428 are overlaid by
`must-flip.jsonl` (NOTES §1.7). A tree that keeps the 2010 libraries fails
those 99; a tree that upgrades them and then shims the old behaviour back fails
the same 99 from the other side. Everything in *this* file is the other factor.

The rung in one sentence: this step is where the three 2010 server libraries
leave, and it is the last step at which the servlet API is still `javax`.

Three libraries move together because they are one subject. The JGit in the
pinned tree is not upstream JGit at all -- it is
`com.madgag:org.eclipse.jgit:0.10.99.7-UNOFFICIAL-ROBERTO-RELEASE`, a personal
fork of a 2011 snapshot published off Maven Central's coordinates. SSHD 0.5.0
predates the library's own I/O layer and drags Apache MINA proper in behind it.
And `org.apache.tomcat:servlet-api:6.0.24` is declared at **compile** scope,
which packages a servlet API inside the WAR -- something a servlet container is
entitled to reject, and something no gate below infers from a pom, because the
pom is not where it is visible. It is visible in `WEB-INF/lib`.

What makes this rung a rung and not a version bump: 5.13.x is the **last** JGit
line on `javax.servlet`, and 2.x is the first SSHD line with a maintained API.
Both are the highest rung reachable before the jakarta step, which is why the
`javax.servlet-api` this step adopts is a dependency the *later* steps remove
rather than a mistake this one makes.

Calibration, as in every step: each number is next to the measurement it came
from, and each was derived from the baseline census and the reference solution
rather than chosen to make a run pass. NOTES §8.2 records both.
"""

import os
import re

import gate_lib as G
import score_lib as S

# ---------------------------------------------------------------------------
# Calibration. "baseline" is step 01's solved tree -- what /app actually holds
# when this step starts, since the ladder is cumulative -- and "reference" is
# this step's own solution. Both measured with this verifier's own ArtifactScan
# and AstScan (NOTES §8.2.3).
# ---------------------------------------------------------------------------

CAL = {
    # Inherited from step 01 and re-asserted here, because a step is allowed to
    # move the build and is not allowed to walk it back. Known trap 9 is a
    # step-local mean hiding an inherited test its own requirements made
    # unpassable, and the SSHD API move touches 19 files in the module whose
    # tests these are not -- so the assertion is made rather than assumed.
    "tests_min": 19,
    "test_classes": ("ConfigUtilTest", "SitePathsTest", "BasicSerializationTest",
                     "IdGeneratorTest", "SocketUtilTest"),
    "release": "11",
    "class_major": 55,

    # Source census: baseline 116 files / 150 declared types, reference
    # 117 / 151. The reference *adds* a file -- jsch's `HostKey` existed in
    # this tree only to render a host-key fingerprint, and the reference moves
    # that method into a class of this project's own rather than keeping a
    # client SSH library for it.
    #
    # The floors are the baseline's, not the reference's, and deliberately so.
    # Adding the class is one way to stop depending on jsch; putting the same
    # four lines in the existing SshUtil is another, and a floor of 117 would
    # fail the second for doing the same work in one fewer file. The type floor
    # keeps two units of slack below the baseline because the SSHD 2.x move
    # replaces several single-method callbacks -- CommandFactory, Command,
    # PublickeyAuthenticator -- and a candidate who writes them as lambdas
    # where the reference wrote named classes removes declared types without
    # removing any work.
    "source_files_min": 116,
    "source_types_min": 148,

    # Two of those files are reached by no fixture in the behaviour suite, and
    # the reason is not that the fixtures are thin: nothing in the pinned tree
    # calls `CookieBase64` or `BlindSSLSocketFactory` at all, so no recorded
    # observation can depend on either. `orphan-types-intact` below floors the
    # number of methods each still declares. Four and thirteen is what the
    # unported 2011 tree declares, and neither number moves in the baseline or
    # in this step's reference, because neither rung has any reason to touch a
    # file nothing calls. They are floors and not equalities, because adding a
    # helper to either file regresses nothing. Matched on the
    # package-qualified tail of the path rather than on the module-relative
    # whole, so a candidate who rearranges modules still passes.
    "orphan_types": (("com/google/gerrit/httpd/CookieBase64.java", 4),
                     ("com/google/gerrit/util/ssl/BlindSSLSocketFactory.java",
                      13)),

    # Suppressions may not grow. 31 is step 01's cap and the baseline's count;
    # the reference reaches 22, because nine of them were suppressing
    # deprecations in the API this step retires. The cap stays at 31 rather
    # than dropping to 22: driving this number down is step 09's rung, and a
    # candidate who needs one @SuppressWarnings("deprecation") to get past a
    # newly-deprecated SSHD call has not regressed anything.
    "suppressions_max": 31,

    # Own classes referencing each library, measured on both trees:
    #
    #   prefix                baseline  reference
    #   com.jcraft (jsch)          4         0
    #   net.schmizz (sshj)         1         0
    #   org.apache.mina            3         0
    #   org.apache.sshd           32        28
    #   org.eclipse.jgit          32        32
    #
    # What both floors count is classes, not types. `uses_refs` asks
    # `refs_by_class`, so the number is how many of the candidate's own
    # compiled classes carry a reference to the prefix anywhere in their
    # constant pool: one class that names fourteen JGit types counts once, and
    # fourteen classes that name one apiece count fourteen. It measures how
    # much of this tree still talks to the library, not how much of the
    # library's API the tree reaches for.
    #
    # Neither floor is an anti-facade check and neither is written as one: a
    # facade that leaves the original call sites naming the library clears both
    # at full count. What they buy is anti-deletion, which is what `uses_refs`
    # is for -- a tree that declares the new dependencies and deletes or stubs
    # the code that used the old ones passes every removal gate in this file
    # and every presence gate beside them, and these are the two it fails.
    # The facade is forbidden in words instead, by §1.2's closing paragraph
    # (the classes that reach for each library today still reach for it
    # themselves; neither is gathered up behind a wrapper or re-exposed under
    # a project-local name), and the completeness of the port is carried by
    # the build -- a half-ported call site does not compile -- and by the 428
    # recorded observations, 99 of which only a tree running on the new
    # libraries reproduces.
    #
    # The two are not the same number because the two moves are not the same
    # kind of move.
    #
    # JGit's is 25 against 32, which is what *both* trees measure. That move is
    # a version change: the same types, under the same names, called from the
    # same places, so the classes that talked to JGit before still talk to it
    # afterwards, and the reference port confirms it rather than assuming it --
    # 32 before, 32 after, no class stopped naming JGit on the way across. A
    # third, independent port -- the blind run's tree, written by an agent that
    # never saw this file -- measures 33, one above the baseline, which is the
    # same story from a tree with no stake in it. The
    # seven units of slack are for the candidate who gathers two
    # `RepositoryCache.FileKey` call sites into one helper and so leaves the
    # two classes that held them naming no JGit type at all. A tree below 25
    # has stopped naming JGit in a quarter of the classes that name it today,
    # which is the move §1.1 and §1.2 forbid rather than one they allow -- so
    # the floor cannot fail a port that did what the instruction says.
    #
    # SSHD's cannot be that number, and used to try. The 2.x redesign folds
    # types together: `CommandFactory.createCommand` now returns a `Command`
    # that is also its own `ChannelSessionAware`, the session and channel
    # hierarchies were merged, and the cipher, MAC and KEX factories that were
    # one named type each in 0.5 are enum constants and builder calls in 2.x. A
    # class whose only reach into SSHD was one of those folded types can come
    # out of a faithful port naming no sshd type at all, because the builder
    # call that replaced it belongs to the class that configures the daemon
    # rather than to the class that used to name the factory. The count of
    # referencing classes therefore falls for a tree that ported correctly --
    # 32 to 28 in the reference -- and it falls by an amount that depends on
    # how much of the 2.x API a candidate reaches for by name rather than
    # through a builder. Nothing measured here bounds that amount, and the one
    # further measurement there is says so rather than the opposite: the blind
    # run's tree, an independent port of the same instruction, measures 26,
    # two below the reference. Two correct-enough ports two apart is a spread,
    # not a floor, and neither number is evidence about where the bottom is. The previous
    # 16 was the reference's 28 less a dozen classes whose only sshd reference
    # a builder could absorb, which is an estimate and not a lower bound, and
    # an unprinted estimate can fail a port that did exactly what §1.2 asks.
    # So this floor asks only what the measurement supports and what the gate
    # is for: the SSH daemon is still written against SSHD at all. What that
    # refuses is the module modernised in its pom and gutted in its code --
    # dependencies rewritten, sshd-core resolving, and nothing here naming it.
    # That tree clears every absence on this rung -- `no-sshd-0x`,
    # `no-mina-dep`, `no-mina-classes` and the three `no-ssh-client-*` -- and
    # the `sshd-2x` presence gate beside them, and fails only this one.
    # Deleting the module outright fails the presence gate as well, and
    # deleting less than the module is caught elsewhere: `sources-survive` at
    # 116 files, `tests-ship-and-pass`, and the 129 SSH observations in the
    # behaviour suite, none of which a missing command answers.
    "sshd_classes_min": 1,
    "jgit_classes_min": 25,

    # The universe every absence gate below has to have searched. 187 own
    # classes in the baseline, 184 in the reference; 150 is the floor at which
    # "no class references com.jcraft" starts to mean something other than
    # "there are no classes". Origins carry no leading slash (own_classes).
    "own_floor": (150, "classes/"),

    # The 5.13 line specifically, which is both what §1.1 names and the only
    # 5.x line the sentence it gives as the reason is true of: 5.13 is the last
    # JGit release train built against `javax.servlet`, and this tree's
    # classpath is `javax.servlet`. Any 5.13.x passes rather than
    # 5.13.3.202401111512-r exactly, because a patch release inside the line is
    # the same line and the instruction's reason does not distinguish them.
    # 6.x is excluded, and would fail the build anyway, since 6.x is where
    # JGit's HTTP server became jakarta.
    "jgit_version_re": r"^5\.13\.",

    # Two SSHD regexes, because the absence gate and the presence gate are not
    # asking the same question. Absence asks "nothing older than 2.x is still
    # resolved anywhere", which is satisfied by any 2.x and would be wrong to
    # narrow: a candidate who lands on 2.12 has not left an 0.5 behind. Presence
    # asks for the version §1.2 names. Sharing one regex between them made the
    # absence gate as strict as the presence gate, which is stricter than the
    # instruction in the direction rule 4 forbids.
    "sshd_version_re": r"^2\.",
    "sshd_present_re": r"^2\.19\.",

    # The Servlet 3.1 API §1.4 names. `3\.` would also admit 3.0, which is a
    # different specification -- no `ServletRequest#upgrade`, no
    # `HttpServletResponse` status without a reason phrase, a different
    # `web-app` schema -- and the version this step adopts is the one its
    # `web-app` descriptor and its `ServletContainerInitializer` are written
    # against.
    "servlet_version_re": r"^3\.1\.",

    # And the same number for the descriptor, as (major, minor). The gate asks
    # for at least this, not exactly this: §1.4 retires an undeclared version
    # and anything below 3.1, and a candidate who declares 4.0 against a 3.1
    # API has done the thing the instruction asked for and then some.
    "web_xml_min_version": (3, 1),

    # Step 01's thresholds, copied rather than imported -- see the inherited
    # tripwire at the bottom of this file for why there are two gates and not
    # fifteen, and why the numbers live here. Every one of them was re-measured
    # against *this* step's reference tree with this verifier's own AstScan
    # (NOTES §8.2.3): diamond 39, explicit-type-args 4, try-with-resources 9,
    # try-plain 63, multi-catch 10, catch-plain 54, lambda+method-ref 9,
    # anonymous 31, nio-file classes 3. The port moves several of them and
    # clears all of them; `anonymous` moves furthest, and in the safe
    # direction, because the SSHD 2.x callbacks the reference writes as named
    # classes were anonymous classes in the 0.5 code.
    #
    # The five minima are step 01's unchanged. The four old-form caps are not.
    # Step 01's caps were measured on a rung that only rewrote Java already in
    # the tree, and this rung writes new Java against the SSHD 2.x and JGit
    # 5.13 APIs, where the old-form counters count the new code too. An SSHD
    # 2.x port writes `PasswordAuthenticator`, `CommandFactory` and
    # `SessionListener` callbacks that had no counterpart in the 0.5 code, and
    # every one of them written as an anonymous class is another `anonymous`;
    # every new `try` or `catch` around an I/O call the new API throws from is
    # another `try-plain` and another `catch-plain`; every `Map` built for a
    # new 2.x builder that a candidate spells out is another explicit type
    # argument list. The baseline already sits at 35 anonymous against step
    # 01's cap of 36, so one new anonymous callback zeroed the whole bag for a
    # tree that had regressed nothing.
    #
    # Each cap is raised to leave that room and no more, and the invariant that
    # bounds it is that every one of them stays strictly below the count in the
    # unported 2011 tree -- the fork as it stood before step 01 converted any
    # of it, which is the tree these caps exist to fail. A candidate who walked
    # step 01's conversions back therefore misses the cap as well as the
    # minimum, which is what keeps these four from becoming decoration:
    #
    #   counter               baseline  cap here  unported 2011  headroom
    #   explicit-type-args           4        20             47        16
    #   try-plain                   64        80             81        16
    #   catch-plain                 56        82             83        26
    #   anonymous                   35        39             40         4
    #
    # Headroom is what this rung's new code may add before a cap bites; the
    # unported column, less one, is the ceiling that bounded how much of it
    # there was to give. `anonymous` gets the least because its ceiling is the
    # tightest: 40 sits five above the baseline's 35, where try-plain's 81 sits
    # seventeen above the baseline's 64. Four is still more than this rung
    # needs, since the reference writes its SSHD 2.x callbacks as named classes
    # and comes down to 31.
    "inherited": {
        "diamond_min": 25,
        "explicit_type_args_max": 20,
        "try_with_resources_min": 5,
        "try_plain_max": 80,
        "multi_catch_min": 4,
        "catch_plain_max": 82,
        "lambda_min": 4,
        "anonymous_max": 39,
        "nio_file_classes_min": 2,
    },
}

JSCH = ("com.jcraft",)
SSHJ = ("net.schmizz",)
MINA = ("org.apache.mina",)
SSHD = ("org.apache.sshd",)
JGIT = ("org.eclipse.jgit",)

# JUnit 4 by its own type names, never by the `org.junit` prefix: Jupiter lives
# under org.junit.jupiter, and a prefix match would make the replacement
# indistinguishable from the thing being replaced. Step 01's lists verbatim.
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

# A servlet API jar by the names the three plausible ones ship under. Matched
# on the archive entry rather than on a coordinate, because the question is
# what is inside the deployable and `provided` is precisely the scope that does
# not change any coordinate the candidate wrote.
SERVLET_JARS = re.compile(
    r"(^|/)(servlet-api|javax\.servlet-api|jakarta\.servlet-api|"
    r"javax\.servlet(-api)?-\d|tomcat-servlet-api)[-.].*\.jar$")

b = G.Bundle()
g = S.Gates()

# ---------------------------------------------------------------------------
# The floor. A tree that does not parse looks maximally migrated to every AST
# gate, and a tree that produced no artifact passes every absence gate
# vacuously. Both are asked out loud.
# ---------------------------------------------------------------------------

G.parses_clean(g, b, weight=1.0)
G.sources_survive(g, b, CAL["source_files_min"], CAL["source_types_min"],
                  weight=1.0)


# The floor under the floor, for the two types the replay cannot speak for.
# `sources-survive` counts files and declared types, and both counts survive a
# candidate who opens `CookieBase64` and deletes everything between the braces:
# the file is still there and the type is still declared. Every other file in
# the tree has the behaviour suite behind it, and these two have nothing behind
# them, because nothing in the pinned tree calls them (run_behaviour.sh states
# that acceptance in full). So the next question down is asked here instead:
# the two files are still present, and each still declares at least the methods
# the baseline declared.
#
# What this does not do is look inside a body. A method whose statements are
# replaced by `return null;` still declares itself and still counts here, and
# no gate at this rung catches that. This is an anti-stripping floor, not a
# claim that either type still works.
def orphan_types_intact():
    counts = {}
    for m in b.ast.kind("method"):
        rel = (m.get("p") or "").replace("\\", "/")
        for suffix, _floor in CAL["orphan_types"]:
            if rel.endswith(suffix):
                counts[suffix] = counts.get(suffix, 0) + 1
    short = [s for s, floor in CAL["orphan_types"] if counts.get(s, 0) < floor]
    return g.add("orphan-types-intact", not short,
                 ", ".join("%s declares %d method(s), need >=%d"
                           % (suffix.rsplit("/", 1)[-1], counts.get(suffix, 0),
                              floor)
                           for suffix, floor in CAL["orphan_types"]),
                 0.5,
                 {"counts": {s: counts.get(s, 0)
                             for s, _f in CAL["orphan_types"]},
                  "floors": dict(CAL["orphan_types"]),
                  "short": short})


orphan_types_intact()

art = b.artifact_path
g.add("build-and-artifact", b.build_rc == 0 and bool(art) and os.path.exists(art),
      "package rc=%s, artifact=%s" % (b.build_rc, art or "NONE"), 2.0,
      {"build_rc": b.build_rc, "artifact": art})

# ---------------------------------------------------------------------------
# Inherited from step 01. Re-asserted, not assumed: this is the rung where a
# candidate is most tempted to reach for an older Java to make a 2010 API
# compile, and the cheapest way to make 19 tests stop failing is to stop
# shipping them.
# ---------------------------------------------------------------------------

G.class_release(g, b, CAL["class_major"], weight=2.0, mode="exactly")
G.compiler_release(g, b, CAL["release"], weight=1.0)
G.jdk_internals(g, b, weight=1.0)
G.tests_ship_and_pass(g, b, CAL["tests_min"], CAL["test_classes"], weight=2.0)
G.suppressions_at_most(g, b, CAL["suppressions_max"], weight=1.0)

# ---------------------------------------------------------------------------
# Questions the shared dependency gates cannot ask
# ---------------------------------------------------------------------------
#
# `dep_absent` and `dep_present` OR their group and artifact prefixes, which is
# the right shape for "this library, however it is spelled" and the wrong one
# for most of this rung's coordinates.
#
# The fork is a *conjunction*: `com.madgag:org.eclipse.jgit` carries upstream's
# artifactId under someone else's groupId, so neither half alone names it --
# and this project's own groupId is `com.madgag` too, so a gate on the groupId
# alone reports the six reactor modules as the thing to remove. That was not
# hypothetical; it is what the first oracle run of this file said.
#
# A stale SSHD is an artifact *at a version*, which no prefix expresses at all.
#
# And the servlet API this step adopts is a conjunction three ways: the same
# groupId also publishes `javax.servlet:servlet-api:2.5`, so an OR passes a
# tree that swapped one 2005 coordinate for another.
#
# Written here rather than widened in gate_lib, because a `dep_absent` that
# ANDed its arguments would silently change what every other step's gates mean.


def dep_gate_absent(name, pred, weight):
    if b.deps_rc != 0:
        return G._deps_unusable(g, name, b, weight)
    return g.absent(name, sorted("%s:%s:%s" % t for t in G.deps(b) if pred(*t)),
                    weight)


def dep_gate_present(name, pred, minimum, weight):
    if b.deps_rc != 0:
        return G._deps_unusable(g, name, b, weight)
    return g.present(name, sorted("%s:%s:%s" % t for t in G.deps(b) if pred(*t)),
                     minimum, weight)


# ---------------------------------------------------------------------------
# JGit: off the fork, onto upstream
# ---------------------------------------------------------------------------

# The fork is the point, and the package name is no help: `org.eclipse.jgit.*`
# is what both the fork and upstream put in their class files. What differs is
# who published it, so that is what is asked -- upstream's artifactId under a
# groupId that is not upstream's.
dep_gate_absent("no-jgit-fork",
                lambda gid, aid, ver: (aid.startswith("org.eclipse.jgit")
                                       and gid != "org.eclipse.jgit"), 2.0)

# The presence gate below is satisfied by two 5.13 artifacts and would stay
# satisfied with a 6.x resolved beside them. 6.x is where JGit's HTTP server
# became jakarta, so a tree that reached it is no longer on the `javax.servlet`
# classpath §1.1 and §1.4 both require.
#
# On its own this gate is vacuous in the other direction, and is meant to be: a
# tree with no `org.eclipse.jgit` groupId resolving at all -- one that never
# left the fork, or deleted the dependency -- has nothing off 5.13 and passes.
# It is an absence, and an absence is only ever half a question. The other half
# is the presence gate immediately below, which that tree fails; the pair is
# what says "upstream JGit, on the 5.13 line", and neither half says it alone.
dep_gate_absent("no-jgit-off-5-13",
                lambda gid, aid, ver: (gid == "org.eclipse.jgit"
                                       and not re.match(CAL["jgit_version_re"],
                                                        ver)), 1.0)

# Two artifacts, because the HTTP servlets are a separate jar and a tree that
# upgraded only the core would not compile against it.
G.dep_present(g, b, "jgit-upstream-5-13", group_prefixes=JGIT, minimum=2,
              weight=2.0, version_re=CAL["jgit_version_re"])

# How many of the candidate's own compiled classes still name the JGit package
# at all. The gates above ask what resolves; this one asks whether anything
# here still calls it, which is the half a pom cannot answer -- a tree that
# leaves the dependency declared and empties out the code that used it clears
# all three above and fails this one. See CAL for why 25 rather than the
# reference's 32, and for what carries the no-facade sentence instead.
G.uses_refs(g, b, "jgit-classes", JGIT, minimum=CAL["jgit_classes_min"],
            weight=1.0, own=True)

# ---------------------------------------------------------------------------
# SSH: MINA SSHD 0.5 -> 2.x, and the two client libraries that came with it
# ---------------------------------------------------------------------------

# Everything under org.apache.sshd has to be 2.x. A tree that adds sshd-core
# 2.19 while 0.5.0 is still resolved through some other path has two SSH
# servers on the classpath, and the one that wins is whichever the classloader
# reaches first -- which is not a property of the tree at all.
dep_gate_absent("no-sshd-0x",
                lambda gid, aid, ver: (gid.startswith(SSHD)
                                       and not re.match(CAL["sshd_version_re"],
                                                        ver)), 1.0)

# Two halves of one 2.0, because §1.2 names an artifact and a version and a
# prefix gate cannot ask for both of the same resolution. `dep_present` ORs its
# group and artifact prefixes, so "some org.apache.sshd artifact resolves at
# 2.19" is the most it can say; `sshd-core` 2.12 beside a transitive
# `sshd-common` 2.19 says exactly that, and clears `no-sshd-0x` too, since
# every resolution is 2.x and one of them is 2.19. The one module §1.2 names by
# coordinate would still be seven minor releases behind the version it names.
#
# So the conjunction is written here, the way every other conjunction on this
# rung is: filtered in this file and handed to `g.present`, which prints the
# coordinates that matched. `sshd-2x` keeps its own question -- a 2.19 is
# resolved somewhere -- at half the weight, and the pair still totals 2.0, so
# the split costs a mixed tree half of the presence rather than all of it and
# costs a tree that followed §1.2 nothing.
G.dep_present(g, b, "sshd-2x", group_prefixes=SSHD, minimum=1, weight=1.0,
              version_re=CAL["sshd_present_re"])
dep_gate_present("sshd-core-2-19",
                 lambda gid, aid, ver: (gid.startswith(SSHD)
                                        and aid == "sshd-core"
                                        and re.match(CAL["sshd_present_re"],
                                                     ver) is not None),
                 1, 1.0)

# The same question for SSHD, at a floor of 1 for the reason CAL gives: the
# 2.x redesign takes sshd references out of classes a faithful port keeps, so
# no count above "still written against SSHD at all" is a property of a correct
# port. The weight stays at 2.0 for what a floor of 1 still refuses -- an SSH
# module modernised in its pom and gutted in its code, which clears every
# absence on this rung and the presence gate above it. The SSH daemon is half
# of this rung; nothing else here fails that tree.
G.uses_refs(g, b, "sshd-classes", SSHD, minimum=CAL["sshd_classes_min"],
            weight=2.0, own=True)

# Apache MINA proper. SSHD 0.5 is built on it; SSHD 2.x has its own NIO layer
# and does not pull it, so a resolved mina-core means something in the tree
# still asks for it by name.
G.dep_absent(g, b, "no-mina-dep", group_prefixes=MINA, weight=1.0)
G.no_refs(g, b, "no-mina-classes", MINA, weight=1.0, own=True,
          floor=CAL["own_floor"])

# jsch and sshj are *client* SSH libraries, in a server. jsch is here to format
# a host-key fingerprint and sshj to parse an authorized_keys line -- both
# things SSHD 2.x does itself. Asked three ways because they can be left behind
# three ways: still resolved, still referenced by a compiled class, still
# imported by a .java that no longer compiles into anything.
G.dep_absent(g, b, "no-ssh-client-libs", group_prefixes=JSCH + SSHJ,
             artifact_prefixes=("jsch", "sshj"), weight=1.0)
G.no_refs(g, b, "no-ssh-client-classes", JSCH + SSHJ, weight=1.0, own=True,
          floor=CAL["own_floor"])
G.no_source_types(g, b, "no-ssh-client-source", JSCH + SSHJ, weight=1.0)

# bcprov-jdk16 is SSHD 0.5's transitive Bouncy Castle, and it is a *different*
# artifact from the bcprov-jdk15 the WAR declares at `provided` scope for its
# own reasons. Matched on the exact artifactId §1.3 names and on nothing else:
# `bcpg-jdk16` is the OpenPGP jar, this tree does not resolve it, and no
# sentence in the instruction retires it -- so a gate that banned it would be
# asking for a removal the candidate was never told to make.
G.dep_absent(g, b, "no-bc-jdk16", artifact_prefixes=("bcprov-jdk16",),
             weight=1.0)

# ---------------------------------------------------------------------------
# The servlet API: out of the WAR, and off Tomcat's coordinates
# ---------------------------------------------------------------------------

# Tomcat's servlet API artifacts, and not the whole `org.apache.tomcat`
# groupId. That groupId publishes the whole of Tomcat -- the embedded
# containers, the JDBC pool, the annotations jar -- and §1.4 retires one
# coordinate from it. A tree that pulls `tomcat-embed-core` for some reason of
# its own has not failed this rung, so the gate asks for the group *and* an
# artifactId that is the servlet API under one of the two names Tomcat has
# published it as.
dep_gate_absent("no-tomcat-servlet-api",
                lambda gid, aid, ver: (gid == "org.apache.tomcat"
                                       and aid in ("servlet-api",
                                                   "tomcat-servlet-api")), 1.0)


def servlet_api(gid, aid, ver):
    """The dependency §1.4 names: all three fields, not any one of them.

    `dep_present` ORs its group and artifact prefixes, so a groupId of
    `javax.servlet` alone satisfies it -- and `javax.servlet:servlet-api:2.5`
    is a real coordinate, still on Central, which is the Servlet 2.5 API under
    the groupId this rung moves *to*. Swapping Tomcat's 6.0.24 for that one
    would pass an OR gate while leaving the tree on a 2005 specification.

    The version is part of the name for the same reason, and both gates below
    are built on this one predicate so that "the right coordinate" and "at the
    right scope" cannot drift apart.
    """
    return (gid == "javax.servlet" and aid == "javax.servlet-api"
            and re.match(CAL["servlet_version_re"], ver) is not None)


dep_gate_present("servlet-api-3", servlet_api, 1, 1.0)


# `provided` is the other half of §1.4's first bullet, and it is a fact about
# the resolved graph rather than about the archive: `servlet-api-not-packaged`
# below witnesses the *consequence* of the scope in the deployable, which is
# the half that matters most, but a candidate can reach an empty WEB-INF/lib
# by excluding the dependency from the war plugin while still compiling against
# a `compile`-scoped servlet API everywhere else. That tree ships no servlet
# jar and is still not what the instruction asked for.
#
# Read with scoped_deps rather than deps, since the scope is the last field of
# the coordinate and DEP stops before it.
def servlet_api_scope():
    name = "servlet-api-provided"
    if b.deps_rc != 0:
        return G._deps_unusable(g, name, b, 1.0)
    got = sorted(t for t in G.scoped_deps(b) if servlet_api(t[0], t[1], t[2]))
    wrong = ["%s:%s:%s:%s" % t for t in got if t[3] != "provided"]
    if not got:
        detail = "the Servlet 3.1 API resolves in no module at any scope"
    elif wrong:
        detail = ("resolved at a scope other than provided: %s"
                  % ", ".join(wrong[:8]))
    else:
        detail = "resolves at provided scope and at no other"
    return g.add(name, bool(got) and not wrong, detail, 1.0,
                 {"resolutions": ["%s:%s:%s:%s" % t for t in got],
                  "not_provided": wrong})


servlet_api_scope()

# The half a pom cannot witness. `provided` and `compile` produce identical
# coordinates and differ only in what lands in WEB-INF/lib, so this gate asks
# the deployable. The baseline ships `servlet-api-6.0.24.jar` there; the
# reference ships no servlet API jar at all.
#
# Asked of every archive entry rather than of WEB-INF/lib by name, because a
# candidate who repackages is still shipping it.
packaged = sorted({e.get("o", "") for e in b.art.entries()
                   if SERVLET_JARS.search(e.get("o", ""))})
jars = sorted({os.path.basename(p) for p in packaged})
g.add("servlet-api-not-packaged", not packaged and len(b.art.entries()) > 0,
      ("%d servlet API jar(s) inside the artifact: %s"
       % (len(jars), ", ".join(jars)) if packaged
       else "no servlet API jar among %d artifact entries"
            % len(b.art.entries())),
      2.0, {"sites": packaged, "jars": jars,
            "entries": len(b.art.entries())})

# ---------------------------------------------------------------------------
# The deployment descriptor
# ---------------------------------------------------------------------------
#
# Written as an absence rather than a presence: the thing this rung retires is
# a descriptor below 3.1, and there is more than one correct way not to have
# one. Declaring `version="3.1"` is the obvious one. Deleting web.xml and
# letting the Servlet 3.0 annotation and ServletContainerInitializer machinery
# do the work is the other, and this tree already has a WebAppInitializer, so
# it is a route a candidate can reasonably take. A presence gate would fail the
# second for succeeding harder than the first.
#
# A descriptor that is there and says 2.5, or says nothing at all -- which is
# what the baseline's `<web-app>` does -- is what fails. So is one that no
# longer parses: a descriptor is not retired by breaking its syntax, and a
# reader that skipped what it could not read would reward exactly that.


def stale_descriptors():
    """Every `<web-app>` in the tree that declares less than 3.1, as sites.

    Parsed, not grepped (rule 11). The regex this replaces read the first 64
    KiB of the file and matched `<web-app ... version=`, which is three
    separate lies about the same document: a `<web-app` inside an XML comment
    counts, a `version` attribute pushed past the cut-off does not, and a
    document whose root is something else entirely counts as soon as the string
    appears anywhere in it.

    The root element is found the way gate_lib.xml_descriptors finds one -- by
    local name, namespace or not -- because the 3.1 descriptor is a namespaced
    document (`http://xmlns.jcp.org/xml/ns/javaee`) and the 2.3-era one this
    tree ships has no namespace at all.
    """
    import xml.etree.ElementTree as ET
    sites = {}
    for p in S.find_files(b.app, names={"web.xml"}):
        rel = os.path.relpath(p, b.app)
        try:
            root = ET.parse(p).getroot()
        except Exception as exc:                        # noqa: BLE001 - reported
            sites["%s (unparsable: %s)" % (rel, str(exc)[:100])] = True
            continue
        if G._local(root.tag) != "web-app":
            continue
        declared = (root.get("version") or "").strip()
        m = re.match(r"^(\d+)(?:\.(\d+))?$", declared)
        if not m:
            sites["%s (<web-app> declares %s)"
                  % (rel, "version=%r" % declared if declared
                     else "no version")] = True
        elif (int(m.group(1)), int(m.group(2) or 0)) < CAL["web_xml_min_version"]:
            sites["%s (<web-app version=\"%s\">)" % (rel, declared)] = True
    return sites


g.absent("no-pre-3-1-web-xml", stale_descriptors(), 1.0)

# ---------------------------------------------------------------------------
# Still javax, not jakarta
# ---------------------------------------------------------------------------
#
# §1's preamble and §1.4's last bullet say the same thing twice, and it is the
# sentence that makes this rung a rung: the servlet API stays `javax.servlet`,
# in the imports and on the classpath. Nothing above asks it. `servlet-api-3`
# is a *presence* gate, so it is satisfied by `javax.servlet:javax.servlet-api`
# resolving with a jakarta API resolved beside it -- exactly the way
# `jgit-upstream-5-13` would be satisfied by a 6.x beside the two 5.13s, which
# is why that one has `no-jgit-off-5-13` next to it and this one needs the
# same. And the candidate most likely to reach for jakarta here is the one
# doing the rest of the rung correctly: JGit 6.x and a jakarta servlet API are
# what "modernise this WAR" returns, and §1.1 and §1.4 spend four bullets
# saying not yet.
#
# Three gates. The classpath, as the resolved graph reports it: nothing
# resolves under a `jakarta.servlet` coordinate, in either field, however it
# arrived. The imports: no `.java` names the package, in an import or written
# out inline. And the classpath as the deployable actually ships it, in the
# bytecode, which is where the first two stop being enough.
#
# The third one was left out of the first draft on the argument that an
# arrival, unlike a removal, cannot reach the bytecode without passing through
# a pom and a source file on the way. That is true of the candidate's own
# source and false of the classpath a WAR carries. A jakarta servlet API shaded
# into somebody else's jar arrives under that project's coordinate and under
# whatever archive name it chose, so `no-jakarta-servlet-dep` has no coordinate
# to match and `SERVLET_JARS` has no name to match, and the
# `jakarta/servlet/*.class` entries are under `WEB-INF/lib` all the same.
# `Class.forName("jakarta.servlet.ServletContext")` is the other one: a string
# literal is not a type use, so the source gate does not see it, and it is in
# the constant pool, which is precisely what the bytecode scan reads. What no
# name-based gate reaches is a copy relocated into a different package, which
# is not the same arrival: a relocated class is no longer the servlet API this
# tree could compile or deploy against.
#
# Asked with `own=False`, because a shaded jar is the case it exists for and
# those class files are not the candidate's own; and with the `own_floor`,
# because "no class references jakarta.servlet" is also what an artifact with
# no classes in it says.
dep_gate_absent("no-jakarta-servlet-dep",
                lambda gid, aid, ver: (gid.startswith("jakarta.servlet")
                                       or aid.startswith("jakarta.servlet")),
                1.0)
G.no_source_types(g, b, "no-jakarta-servlet-source", ("jakarta.servlet",),
                  weight=1.0)
G.no_refs(g, b, "no-jakarta-servlet-classes", ("jakarta.servlet",), weight=1.0,
          own=False, floor=CAL["own_floor"])

# ---------------------------------------------------------------------------
# What the tree arrived with, in two gates
# ---------------------------------------------------------------------------
#
# §1.5 says the tree keeps what it already does. Eight of those properties are
# gates of their own above, because this rung is where a candidate is most
# likely to break them: reaching for an older JDK to make a 2010 API compile,
# or deleting the tests the SSHD move keeps failing. The other fifteen are
# asked here, together, at a weight of 1.5 between them. They are: no
# oss-parent; no JUnit 3 type and no JUnit 4 type, asked of the source and of
# the bytecode, which is four checks because each framework is asked both ways;
# no junit and no vintage engine on the resolved graph; Jupiter on the graph
# and Jupiter named by the test classes; EasyMock neither resolved nor
# referenced; and the five pre-Java-7 idioms, each of which is a conversion
# rather than a removal and so carries both a new-form minimum and an old-form
# cap.
#
# Fifteen rows and not two would be fifteen weights. At the ones step 01 gave
# them that is 15 of 52.5, more than the JGit and SSHD moves put together (6.0
# and 5.0), and this step would become a re-run of the previous one with a
# library upgrade attached: a candidate could lose the whole SSHD move and
# still out-score one who did it and let a single inherited idiom slip. What
# the weight has to buy instead is a tripwire -- regressing an inherited
# property is a real and visible loss, and no pile of them is worth more than
# the work this step actually asks for.
#
# Two rows and not one, because the fifteen are not one kind of thing and a
# single all-or-nothing row priced them as though they were.
# `inherited-properties-hold` carries the ten that are facts about the build --
# a parent pom, a test framework, a mocking library. Each of those is binary,
# none of them moves when a candidate ports a call site, and a tree that put
# JUnit 3 back has undone the previous rung rather than tripped over this one,
# so that row keeps the larger 1.0. `inherited-idioms-hold` carries the five
# conversions at 0.5, because they are the checks this rung's own work can
# nudge: the port writes new Java, the old-form counters count new code, and a
# tree that leaves one extra plain `try` in a new SSHD callback has not undone
# step 01 -- it has written one line in the old shape. Half a gate is what that
# costs; one and a half was what it used to cost. Ten and five is also fifteen,
# which is the count the comment above CAL["inherited"] now carries too.
#
# Each row stays all-or-nothing within itself, which puts the burden on the
# detail -- which is why the sub-checks are run as real gate_lib calls into a
# collector rather than reimplemented as booleans: each one keeps step 01's own
# predicate, its own scan and its own threshold, and the failure names which
# properties regressed. A gate whose failure says only "false" cannot be acted
# on by a candidate or by an author.
#
# The thresholds are step 01's, copied into CAL["inherited"] rather than
# imported. Each step's score.py is standalone -- it runs in its own image,
# with only this directory mounted -- and an import across steps would make
# this file unreadable without a file that is not there.

# ArtifactScan relativizes every origin against the target/ root it walked, so
# the test output reads "test-classes/Foo.class" with no leading slash. Step
# 01's floor, verbatim: an absence gate over test code has to be told the
# universe it must have searched, or a tree whose tests did not compile passes
# it by having nothing to find.
TESTS = (len(CAL["test_classes"]), "test-classes/")


class _Collector(S.Gates):
    """A Gates that collects without journalling.

    The sub-checks are gate_lib calls so that they ask exactly what step 01
    asked. Handing them the real `g` would put fifteen more rows in the gate
    journal and in report.json, each reading like a gate with a weight of its
    own -- which is the dilution this section exists to avoid. So they land
    here and are reduced to two.
    """

    def add(self, name, ok, detail, weight=1.0, evidence=None):
        r = S.Result(name, ok, detail, weight, evidence)
        self.results.append(r)
        return r


def inherited_properties():
    """The ten inherited properties that are facts about the build."""
    h = _Collector()

    # The 2011 build's parent pom.
    G.no_parent(h, b, "no-oss-parent", ("org.sonatype.oss:oss-parent",))

    # JUnit 3 and 4 gone and Jupiter in, asked in source, in bytecode and in
    # the resolved graph, because each can be regressed on its own: a vintage
    # engine added to get one old test running puts junit:junit back on the
    # test classpath without changing a line of source.
    G.no_source_types(h, b, "no-junit3-source", JUNIT3)
    G.no_refs(h, b, "no-junit3-classes", JUNIT3, own=True, floor=TESTS)
    G.no_source_types(h, b, "no-junit4-source", JUNIT4)
    G.no_refs(h, b, "no-junit4-classes", JUNIT4, own=True, floor=TESTS)
    G.dep_absent(h, b, "no-junit3-4-dep",
                 group_prefixes=("junit", "org.junit.vintage"))
    G.dep_present(h, b, "junit5-dep", group_prefixes=("org.junit.jupiter",),
                  minimum=1)
    G.uses_refs(h, b, "junit5-classes", JUPITER,
                minimum=len(CAL["test_classes"]), own=True)

    G.dep_absent(h, b, "no-easymock-dep", group_prefixes=EASYMOCK,
                 artifact_prefixes=("easymock",))
    G.no_refs(h, b, "no-easymock-classes", EASYMOCK, own=True, floor=TESTS)
    return h.results


def inherited_idioms():
    """The five pre-Java-7 conversions, on this rung's own caps."""
    h = _Collector()
    INH = CAL["inherited"]

    # Four of the five are pairs -- the new form present and the old form
    # fallen -- and `converted` is exactly that conjunction, so diamond and its
    # explicit type argument lists are written as one call here where step 01
    # wrote them as two gates. The two counts are the ones step 01 read; the
    # caps are this rung's own, for the reason CAL["inherited"] gives, and the
    # row they report into is all-or-nothing anyway.
    G.converted(h, b, "diamond", "diamond", "explicit-type-args",
                INH["diamond_min"], INH["explicit_type_args_max"])
    G.converted(h, b, "try-with-resources", "try-with-resources", "try-plain",
                INH["try_with_resources_min"], INH["try_plain_max"])
    G.converted(h, b, "multi-catch", "multi-catch", "catch-plain",
                INH["multi_catch_min"], INH["catch_plain_max"])
    G.converted(h, b, "lambda", ("lambda", "method-ref"), "anonymous",
                INH["lambda_min"], INH["anonymous_max"])
    G.uses_refs_beside(h, b, "uses-java-nio-file", ("java.nio.file",),
                       ("java.io.File",), minimum=INH["nio_file_classes_min"])
    return h.results


def held(name, checks, weight):
    """One row out of a collector: the count, the names, and the sub-gates."""
    regressed = [r for r in checks if not r.ok]
    return g.add(name, not regressed,
                 ("%d of %d inherited check(s) regressed: %s"
                  % (len(regressed), len(checks),
                     "; ".join("%s (%s)" % (r.name, r.detail)
                               for r in regressed[:6]))
                  if regressed else
                  "all %d inherited checks hold" % len(checks)),
                 weight,
                 {"checked": len(checks),
                  "regressed": [r.name for r in regressed],
                  "sub_gates": [r.as_dict() for r in checks]})


held("inherited-properties-hold", inherited_properties(), 1.0)
held("inherited-idioms-hold", inherited_idioms(), 0.5)

print("%.6f" % S.finish(g, b.behaviour_pair(), {"calibration": CAL}))

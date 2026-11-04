"""Step 10 -- the platform rung, and the last one. Spring Boot 4, Spring
Framework 7, Jakarta EE 11, and the four source conventions that arrive with
them.

    reward = migration * behaviour

`behaviour` is the fraction of the 428 recorded observations the built artifact
reproduces, computed by run_behaviour.sh. Everything in *this* file is the
other factor.

The rung in one sentence: steps 04 through 09 put this tree on a platform and
then on the language release that platform's successor needs, and this is where
the platform itself moves -- Boot 3.5 to 4.1, Framework 6.2 to 7.0, Servlet 6.0
to 6.1, JUnit 5 to 6 -- along with the conventions Boot 4 is designed around and
which this tree does not yet follow.

What that means, measured on the tree this step is handed:

  * every `org.springframework.boot` coordinate resolves at 3.5.16 and every
    `org.springframework` one at 6.2.19, out of a `spring-boot-dependencies`
    import that names 3.5.16;
  * the web half arrives through `spring-boot-starter-web`. Boot 4 still
    publishes that artifact, and its own pom describes it as "deprecated in
    favor of spring-boot-starter-webmvc" -- so this is a rename a tree can
    decline to make and still build, which is precisely why there is a row for
    it;
  * `junit-bom` is imported by hand at 5.10.2, ahead of the platform bom that
    would otherwise name the version, and the suite runs on Jupiter 5.10.2;
  * **zero** of the 18 packages holding main source say anything about
    nullness, while 18 methods in them return the null literal and not one is
    annotated;
  * 31 constructors carry `@Autowired`, and every single one is the only
    constructor its class declares;
  * 12 fields are named `serialVersionUID` and none carries `@Serial`.

Why the last three are rows and not tidying. Boot 4's own headline is that its
API is annotated for nullness with JSpecify and that it is compiled with
`-parameters` so that constructor arguments resolve by name; both of those are
statements about how an application on it is *written*, not about which jar it
resolves. A tree that moves the version numbers and leaves the source alone has
taken the upgrade and not the platform, and the four source rows below are the
difference. `@Autowired` on a sole constructor has been redundant since Spring
4.3 and is now noise that hides the one case where it still means something;
`@Serial` has been available since Java 14 and is the only thing that makes
javac check a serialization member's shape.

What makes it a dangerous rung, and it is the most dangerous in the ladder.
**Nothing here is observable.** All 428 observations pass on the tree this step
is handed and on the reference alike -- a version bump, an annotation, a
deleted `@Autowired` and a renamed starter all compile to a server that answers
identically, and `must-flip.jsonl` gains no records at this rung and carries
step 05's 123 unchanged. Movement in the behaviour half here is a defect in the
step, not a signal about the candidate. Every discriminating statement this
rung makes, it makes in this file.

Which is why every row below is *derived from the tree under test* rather than
keyed to a fixture. From step 08 on this ladder has two legitimate
predecessors, and measured with this file's own AstScan they differ in exactly
the places these rows read: the reference chain's `mini-git-server-app` tree holds
18 packages of main source, 71 types with a sole constructor, 549 declared
methods and 12 `serialVersionUID`; a chain that kept the `mini-git-server-war` tree
holds 17, 70, 565 and 13. A site list keyed on paths would be unpassable from
one of them. So `nullness-marked` asks that *every* package it finds is marked
rather than that eighteen named ones are, `serial-annotated` asks that every
`serialVersionUID` it finds is annotated, and the numbers in CAL beside them
are anti-vacuity floors set below both chains rather than either chain's count.

The one instrument on this rung that is somebody else's software.
`rewrite-clean` runs OpenRewrite's own `UpgradeSpringBoot_4_0` over a copy of
the candidate's tree in dry-run mode and asks what it would still change. Three
things make that usable as a gate rather than as a lottery:

  * it runs on a copy, because `rewrite:dryRun` binds to `process-test-classes`
    and drives a real build -- an instrument that can damage the thing it
    measures is not one this ladder will use;
  * the plugin and the recipe module are pinned by full coordinates and cached
    into the image at build time, so neither the tree under test nor Maven
    Central gets to choose the instrument or its availability;
  * and a hunk that changes only a `<version>` element or a `*Version`
    property is *not* a finding. Those values are resolved from live Central
    metadata, and a row that counted them would fail a correct tree on the day
    a plugin released. Measured: against the reference the recipe still emits
    one hunk, bumping maven-compiler-plugin 3.13.0 and maven-surefire-plugin
    3.2.5, and nothing else. Java hunks and `<groupId>`/`<artifactId>` hunks
    are findings by construction, which is what makes the row bite: the tree
    this step is handed produces 31 `@Autowired` deletions, twelve `@Serial`
    insertions and a starter rename before it reaches any version number.

Three things the task line names that this rung does not ask for, each dropped
on a measurement rather than on a judgement:

  * **Jakarta Persistence 3.2.** There is no JPA here and no database -- the
    project removed both before the pin, which is the whole reason this server
    is filesystem-based. A row would be asking a candidate to add persistence
    so that a gate could find it.
  * **Spring 7's API versioning.** Measured with this file's own AstScan: zero
    `@Controller`, zero `@RestController` and zero `@RequestMapping` in the
    tree. The HTTP surface is servlets and filters registered programmatically,
    so there is nothing for a version predicate to attach to.
  * **`spring-boot-properties-migrator`.** The whole application sets exactly
    one Spring property, `spring.threads.virtual.enabled`, and it is current in
    Boot 4. The migrator reports nothing on the reference and nothing on the
    tree this step is handed, so the row would be a 1.5-weight statement that
    no tree can fail -- which raises the floor and separates nothing.

What this rung inherits and does not re-litigate. Steps 01 to 09's gates are
still evaluated here and reported as twelve rows. Step 09's ten, together with
`class-release`, `compiler-release` and `no-jdk-internals`, become
`inherited-java21-hold` by the same rebinding every rung below used -- and that
fold is load-bearing rather than cosmetic, because `lint-is-enforced` is inside
it: `-Xlint:all -Werror` is still on, so every row below has to be reachable
without producing a warning. Measured on the reference: 18 new
`package-info.java` files, 17 `@Nullable`, 12 `@Serial` and a Boot 4 classpath,
and the full build transcript still contains no `[WARNING]` at all.

Two inherited version pins are widened here rather than re-pinned, so that an
inherited check does not become unpassable under this rung's own requirements,
handled the way step 06 handled `one-log4j-provider`.
`spring-boot-dep` accepted `^3\\.` and now accepts `^3\\.` or `^4\\.`;
`spring-6` becomes `spring-6-or-7` and accepts either line. Widened and not
moved to `^4\\.`/`^7\\.`, because a hold row says "this rung did not break what
an earlier rung established" -- the statement that the platform *has* moved is
`boot-4` and `framework-7` below, and a hold that duplicated them would price
one property twice and leave the hold unable to mean what its name says.

Calibration, as in every step: each number is next to the measurement it came
from, and each was derived from the predecessor tree and this step's reference
solution.
"""


import os
import re

import gate_lib as G
import score_lib as S

# ---------------------------------------------------------------------------
# Calibration. "baseline" is step 02's solved tree -- what /app actually holds
# when this step starts, since the ladder is cumulative -- and "reference" is
# this step's own solution. Both measured with this verifier's own ArtifactScan
# and AstScan.
# ---------------------------------------------------------------------------

CAL = {
    # Inherited from steps 01 and 02 and re-asserted here. A step-local mean
    # can hide an inherited test that the step's own requirements made
    # unpassable, and this rung has a particular way of doing that: `-Werror`
    # applies to `testCompile` as well as `compile`, so a candidate who turns
    # it on without looking at `src/test` gets a reactor that stops at the
    # first warning in a test source. Measured on both control trees: the five
    # test classes compile clean under `-Xlint:all` and the nineteen tests
    # still run 19 of 19 with 0 failures under JDK 21, so this rung asks for
    # nothing in `src/test` -- but the assertion is made rather than assumed,
    # because the failure it would catch is silent in every other row here.
    "tests_min": 19,
    "test_classes": ("ConfigUtilTest", "SitePathsTest", "BasicSerializationTest",
                     "IdGeneratorTest", "SocketUtilTest"),

    # The toolchain, and the two independent ways of asking about it that
    # step 01 established: what the build was told (`maven.compiler.release`,
    # or `source`/`target`, which this ladder forbade at step 01) and what
    # came out (the class-file major version of every class the reactor
    # compiled). 21 and 65, unchanged from step 08, which is where they moved
    # from 17 and 61.
    #
    # Inherited rows at this rung and not the rung's own, which is the reverse
    # of step 08: there the pair belonged to column (a), because `release=17`
    # will not compile `Thread.ofVirtual` without `--enable-preview` and a
    # candidate who did not move the reactor could not do the step at all.
    # Here the tree arrives at 21 and this rung asks for no API that 21
    # introduced -- pattern matching for instanceof is Java 16, lambdas are 8,
    # `java.time` is 8. Every construct below would compile at `release=17`.
    #
    # Which is exactly why the pair is restated rather than dropped. This is
    # the one rung in the ladder whose work is done by editing the compiler
    # plugin's configuration, and the compiler plugin's configuration is where
    # `<release>` lives. A candidate who rewrites that block to fit
    # `<compilerArgs>` in, and drops or downgrades `<release>` on the way, has
    # broken something no other row in this file would notice: every idiom row
    # below still passes at 17, and the tree would ship class files a JDK 17
    # runtime accepts under a step that spent its predecessor moving off it.
    # `exactly` mode is step 01's shape and is kept for step 01's reason -- a
    # tree may not overshoot, and nothing above 65 is installed in the agent
    # image -- so what it refuses here is a reactor that compiles six modules
    # at 65 and the one whose pom was edited at 61.
    #
    # Measured at this rung: both control trees compile every class at major
    # 65 with `release=21` and pass both rows.
    "release": "21",
    "class_major": 65,

    # Source census. This rung is the first since 02 that adds files: §1.4
    # asks for a nullness declaration on every package that has main source,
    # and a package's declaration lives in a `package-info.java` that does not
    # exist yet. Measured: the predecessor holds 110 files declaring 135
    # types, the reference holds 128 declaring 135 -- eighteen new files, not
    # one new type, because `package-info.java` declares none. Everything else
    # this rung does is made in place: an annotation is added to a method that
    # was already there, an `@Autowired` is deleted from a constructor that
    # stays, a `<version>` moves. Nothing is removed or renamed.
    #
    # So the census still cannot see most of this rung's work, which is the
    # point of keeping it: it is the instrument that says the work was done
    # *in* the tree rather than *to* it. A floor, not a fingerprint -- the
    # eighteen arrivals are counted by `nullness-marked`, which is a gate about
    # what those files say, and a candidate who satisfies §1.4 by some other
    # arrangement of declarations is not failed here for holding 110 files.
    #
    # So the floors start from step 04's 105 / 132 rather than from this
    # rung's arrival. Step 04 derived them from its own
    # enumeration -- the two files that rung retires, less one unit of slack --
    # and they sit five files and three types below what a correct step-08
    # tree holds. Re-anchoring them to this rung's arrival, which is also what
    # its reference leaves, would fail every candidate who reached step 04's
    # requirements by writing fewer classes than the reference did, which step
    # 04 explicitly licensed and which this rung has no opinion about.
    #
    # What the floor is for is unchanged: the tree that answers a rung by
    # deleting a subsystem rather than porting it. At this rung the shape that
    # would take is narrow and real -- `-Werror` turns 21 warnings into 21
    # compile errors, and the cheapest way to make a file stop producing a
    # warning is to stop shipping it. The five files carrying the most
    # warnings between them are `CmdLineParser`, `OptionHandlerFactory`,
    # `IdGenerator`, `ProjectServlet` and `HostPlatform`, and a tree that has
    # deleted any of them is below no floor here -- 109 of 110 files clears
    # 105 easily. What stops that tree is `orphan-types-intact` for two of
    # them and the 428 observations for the rest; the census is the coarse
    # backstop and is honest about being one.
    #
    # `sources_survive` counts what `AstScan` emitted over the whole
    # repository: every `.java` file under `/app` and every type declared in
    # one, test sources and nested types included. There is no main-only cut
    # on either number, so the counts above are whole-tree counts.
    # Lowered from there in three moves. First, 98 / 120 rather than the 105 /
    # 132 derived above. Those were the enumeration of the *reference's* step
    # 03 tree, but a tree that passes step 03 may legitimately hand over as
    # few as step 03's own floors, 100 files / 124 types (its configuration
    # classes are "a matter of taste"), and this rung licenses two deletions
    # and four types on top. A floor above that fails a correct chain for a
    # shape an earlier rung allowed.
    # Second, the same arithmetic carried through the
    # rungs after step 04. Step 06's reference
    # ports `SshLog` from four declared types to two (110 / 137 -> 110 / 135)
    # and step 07's retires a cache file and type (-> 109 / 134), so a chain
    # at the step-05 floor that makes the same two moves stands at 97 / 117
    # here and at every later rung (08 and 10 only add), and 98 / 120 would
    # fail it.
    # Third, 96 / 115 rather than 97 / 117. Two ports
    # the rungs license and the reference does not take: step 06's
    # `PatternLayout` over MDC leaves `SshLog` one type rather than two, and
    # step 07's `cache_types_min` leaves room to fold the lazily-bound holder
    # into `CacheProvider`, one file and one type fewer. The subsystem
    # deletions this census was written against are stopped by the behaviour
    # suite and the cache rows, not by it.
    "source_files_min": 96,
    "source_types_min": 115,

    # Step 02's floor under the floor, verbatim, and for the same reason:
    # nothing in the pinned tree calls `CookieBase64` or `BlindSSLSocketFactory`
    # at all, so no recorded observation can depend on either, and
    # `sources-survive` counts a file whose methods have all been deleted.
    # Four and thirteen is what the unported 2011 tree declares and what both
    # this step's baseline and its reference still declare. At this rung the
    # row stops being a formality: `BlindSSLSocketFactory` is dead code that
    # nothing calls, and dead code under `-Werror` is dead code whose warnings
    # a candidate has to fix for no observable benefit. Measured -- neither
    # file produces a warning under `-Xlint:all`, so nothing here forces the
    # question; but "delete the file nothing tests" is the answer this row
    # exists to refuse, and a rung that makes the compiler shout is the rung
    # where it would be reached for.
    "orphan_types": (("com/google/gerrit/httpd/CookieBase64.java", 4),
                     ("com/google/gerrit/util/ssl/BlindSSLSocketFactory.java",
                      13)),

    # -- this rung's own numbers ------------------------------------------
    #
    # Every one of the nine below is a pair: what the tree this step is handed
    # measures, and what this step's reference solution measures, both read
    # with this verifier's own AstScan and ArtifactScan. Where a threshold
    # sits between the two it is stated here and argued beside the gate that
    # uses it.

    # The legacy date and time API, as a prefix set for a bytecode absence
    # row. `java.text.DateFormat` is the abstract parent of
    # `SimpleDateFormat` and is named so that a tree cannot answer by holding
    # the same object through its supertype; `GregorianCalendar` for the same
    # reason under `Calendar`. Floor: 7 own classes, 18 written uses in 4
    # files. Reference: 0 and 0.
    "legacy_date_types": ("java.util.Date", "java.util.Calendar",
                          "java.util.GregorianCalendar", "java.util.TimeZone",
                          "java.text.DateFormat", "java.text.SimpleDateFormat"),

    # ...and the floor under the row that stops the port being a deletion.
    # Own classes referencing anything under `java.time`: 4 on arrival (steps
    # 07 and 08 left `Duration`s in `CachePool$Factory` and
    # `WorkQueue$Executor` and an `Instant` in `SshDaemon`), 7 in the
    # reference. 6 asks for two of the three new sites and leaves one unit of
    # slack for the candidate who folds two of them into a shared helper.
    "java_time_min": 6,

    # Anonymous classes that are really callbacks, and the lambdas and method
    # references that replace them. The functional side asks >=22: floor 20 on
    # one predecessor and 21 on the other, reference 23, one unit of slack.
    #
    # The old side asks <=9, and 9 rather than the 7 that reading one chain
    # alone suggests, because this rung has two legitimate predecessors and
    # they do not hand down the same residue. Measured on both. The chain this
    # step's own solution comes up leaves 11 anonymous classes: 5 are the
    # command callbacks §1.3 is about and 6 cannot be lambdas at all --
    # `HttpServlet` twice, `ProjectCommandRunnable`, sshd's `SessionFactory`,
    # Spring's `Scope` and `X509TrustManager`, every one of them an abstract
    # class or an interface with more than one method. The other chain leaves
    # 14, because its `WebConfiguration` registers three servlets as anonymous
    # `HttpServlet` bodies where the first registers one, and because its
    # `WorkQueue` still carries an anonymous `UncaughtExceptionHandler`. Its
    # non-convertible residue is 8, not 6, so a tree that converts every
    # convertible callback it was handed still ends at 8, and a cap of 7 fails
    # it for the shape of the predecessor it was given rather than for
    # anything it did.
    #
    # 8 would still be wrong by one: the next entry allows one command
    # callback to remain a class, and a cap that refuses the ninth anonymous
    # body contradicts the allowance directly below it. So 9 = the larger
    # residue plus the one unit that allowance already grants. The slack for
    # the callbacks themselves lives in that allowance, which
    # counts the callbacks the rung is actually about instead of every
    # anonymous class within reach.
    "anonymous_max": 9,
    "functional_min": 22,

    # The third condition on that row, and the one that makes the other two
    # mean what they say. A whole-tree count of anonymous classes falls just
    # as far when four of them are lifted into named classes as when four of
    # them become lambdas, and a whole-tree count of lambdas rises just as far
    # when two are written where no callback ever was: a tree that does both
    # clears 7 and 22 having converted nothing. What distinguishes the two is
    # that a conversion leaves no class behind. So: implementations of the two
    # single-abstract-method command callbacks, counted as *classes* wherever
    # they are declared -- an anonymous body or a named type, nested or not.
    # Floor 5 (`BaseCommand`'s `startThread` wrapper, `AdminKill`, `ShowQueue`,
    # `AdminShowConnections`, `CommandFactoryProvider`); reference 0. The cap
    # is 1, the same one unit of slack the other two halves carry, so a
    # candidate who converts four of the five and has a reason to leave the
    # fifth is not failed for it.
    #
    # Interfaces are not counted, only classes: `BaseCommand.CommandRunnable`
    # is the interface itself and `ProjectCommandRunnable` extends it with a
    # second method, which is why `AbstractGitCommand`'s anonymous body is one
    # of the six that must stay and is not in this population at all.
    "command_callback_types": ("CommandRunnable", "CommandFactory"),
    "command_callback_classes_max": 1,

    # `instanceof` followed by a cast to the type just tested, against the
    # binding-pattern form. Floor 25 / 0, reference 7 / 18; the row asks <=9
    # and >=16. The 7 that survive are tests whose value is the boolean
    # itself, which have nothing to bind.
    "instanceof_plain_max": 9,
    "instanceof_pattern_min": 16,

    # args4j's generic types, the two files allowed to write one raw, and the
    # universe an absence over them must have searched. Floor 8 disallowed raw
    # uses at 4 files, reference 0; 17 uses of the three names in both trees,
    # floored at 12. The allowance is by base name so that moving either class
    # to another package keeps it.
    "raw_cli_types": ("Setter", "OptionHandler", "OptionHandlerFactory"),
    "raw_cli_allow": ("CmdLineParser.java", "OptionHandlerUtil.java"),
    "raw_cli_seen_min": 12,

    # JEP 411's terminally deprecated security-manager API. The tree uses two
    # of these, both in `HostPlatform`; the whole set is named because the row
    # is about a class of API and not about the two names that happen to be
    # here. `java.lang.SecurityManager` is in it so that replacing
    # `doPrivileged` with an explicit manager check is not an answer.
    "jep411": ("java.security.AccessController",
               "java.security.AccessControlContext",
               "java.security.AccessControlException",
               "java.security.PrivilegedAction",
               "java.security.PrivilegedExceptionAction",
               "java.security.PrivilegedActionException",
               "java.security.Policy",
               "java.lang.SecurityManager"),

    # Where build.sh puts the transcript of the `package` this verifier ran.
    # Named here rather than hard-coded in the gate because it is the one
    # piece of evidence this file reads that is not under `/score/out`, and a
    # reader checking what the warning row measures should find the path in
    # the same place as every other calibration constant.
    "build_log": "/score/log/build.log",

    # Suppressions, and this is the rung step 02 was pointing at when it said
    # "driving this number down is step 09's". The cap has been 31 since step
    # 01 -- a ceiling nothing in this ladder has come near, whose only job was
    # to refuse growth -- and here it drops to 8, which is below the ten
    # arguments the tree this step is handed carries. The row moves out of the
    # floor block and into this rung's own work with it; the long form of why
    # 8 is beside the call.
    #
    # Measured: floor 10 arguments at 10 sites, reference 6 at 5. Counted by
    # argument, so `@SuppressWarnings({"rawtypes", "unchecked"})` is two.
    "suppressions_max": 8,

    # The universe every absence gate below has to have searched, and this
    # rung adds two of its own to the list -- `no-legacy-date-api` and
    # `no-deprecated-for-removal`, both of them absences over own bytecode
    # and both of them read identically on a tree that fixed every site and on
    # a tree that compiled nothing.
    #
    # 130 is step 04's floor and stays, for step 04's reason: it is the count
    # below which "no class references com.google.inject" stops meaning
    # anything. It is not re-anchored to what the control trees measure,
    # because a candidate is allowed to have arrived here with fewer classes
    # than the reference (see the source census above, which makes the same
    # argument in the same units). Origins carry no leading slash
    # (own_classes), and the substring is `classes/`, which matches
    # `test-classes/` too -- deliberately, since a tree that deleted its tests
    # to get past `-Werror` should not also gain slack in this universe.
    "own_floor": (130, "classes/"),

    # The 6.x line, which is what step 03's `spring-5-3` becomes here. 5.3 was
    # the last Spring Framework train on `javax.servlet` and 6.0 is the first
    # jakarta-only one, so the gate that pinned this tree to 5.3 for three
    # rungs is exactly the gate this rung retires -- asked unchanged it fails
    # a correct tree.
    #
    # The whole major and not `^6\.2\.`: unlike 5.3, where the line boundary
    # carried the servlet namespace, every 6.x is jakarta and there is nothing
    # this rung wants that 6.0 does not have. Measured on the reference: 6.2.19,
    # which is what Boot 3.5.16 manages.
    #
    # There is no upper bound, and at this rung the lower one moves. Step 09
    # pinned this to `^6\.` and said why: "Spring 7 exists and is step 10's; a
    # candidate who reaches it early fails `spring-6` and that is the right
    # answer." This is step 10. The reference resolves seven
    # `org.springframework` coordinates, all 7.0.9, and `^6\.` matches none of
    # them.
    #
    # Widened rather than re-pinned to `^7\.`, and the difference is the
    # ladder's own arithmetic. This row is asked inside `inherited-di-hold`,
    # and a hold row's job is to be a row the *floor* passes -- the floor being
    # step 09's finished tree scored with this verifier. Measured on step 09:
    # its floor is 27.0 of 59.5, which is exactly its inherited total. The
    # floor passes every inherited row and fails every own row, and that is
    # what makes the floor measure this rung's leap and nothing else.
    # Re-pinning to `^7\.` would break it: the floor would fail
    # `inherited-di-hold` (and `inherited-boot-hold`, one layer up) on the
    # version alone, dropping it to 21.5 of 57.0, and "the platform moved"
    # would be priced once in `framework-7` below and a second time inside a
    # hold whose name says it is holding what an earlier rung established.
    #
    # Step 06 settled the shape of this already, widening `one-log4j-provider`
    # from "exactly one" to "at most one" for the same reason: an inherited
    # bound that this rung's own requirements invalidate is widened to the
    # union of what was right then and what is right now, and the new
    # statement is made by a new row rather than by editing the old one. So: 6
    # or 7 here, and `framework-7` below is the row that says which. The gate
    # is renamed to match -- a row called `spring-6` that accepts 7.0.9 is a
    # row whose name lies to whoever reads report.json.
    "spring_version_re": r"^(6|7)\.",

    # Own classes naming the container's API at all, counted over
    # `org.springframework` and `jakarta.inject` together: 72 in the tree this
    # step is handed and 72 in the reference (72 under `org.springframework`
    # alone -- this reference happens to spell every qualifier Spring's way,
    # and the broader set costs it nothing, which is also why swapping the
    # JSR-330 half of the prefix set from `javax` to `jakarta` moves neither
    # number).
    #
    # The two prefixes are one question and not two. Spring reads the JSR-330
    # annotations natively: `jakarta.inject.Qualifier` on an annotation makes
    # it a qualifier the container recognises on its own, which is exactly
    # what §1.3 asks of the seven that survive, and `jakarta.inject.Provider`
    # is a container factory interface the same way `ObjectFactory` is. A port
    # written that way is a Spring port -- those annotations do something here
    # only because Spring is the container reading them -- so a gate counting
    # `org.springframework` alone would fail a correct tree for having chosen
    # the standard spelling over the proprietary one. The gate keeps the name
    # `spring-classes` because Spring is still the container either way.
    #
    # `javax.inject` is not a third, for the reason CONTAINER_API gives:
    # Spring 6 does not read it. An import the container ignores is not
    # contact with the container, and this is the rung where that sentence
    # changes which of the two spellings it excludes.
    #
    # The floor is 9 and not 72, and the gap is deliberate. What a correct port
    # measures here is not a property of the port -- it is a property of how
    # much of the container's API the candidate reaches for *by name*. Every
    # `@Autowired` is a Spring reference and Spring 4.3 onwards needs none of
    # them: a single-constructor class is injected without any annotation at
    # all, so a port that leans on that convention comes out with thirty-two
    # fewer referencing classes than this reference and is no less a port.
    # Writing the surviving provider classes as `java.util.function.Supplier`
    # rather than `ObjectFactory` takes out thirteen more the same way.
    #
    # So the floor asks only what the gates and the instruction beside it force
    # a tree to have under *either* spelling, which is a real lower bound and
    # not an estimate:
    #
    #   7   the seven binding annotations §1.3 keeps. Each has to be marked as
    #       a qualifier the container recognises on its own, and there are two
    #       ways to write that mark --
    #       `org.springframework.beans.factory.annotation.Qualifier` or
    #       `jakarta.inject.Qualifier` -- both of them inside this gate's prefix
    #       set. Measured on the reference: seven annotation classes, each
    #       naming Spring's `Qualifier` and nothing else.
    #   1   the `@Configuration` class `java-configuration` requires. This one
    #       has no JSR-330 equivalent at all --
    #       `org.springframework.context.annotation.Configuration` is the only
    #       way to write it -- and that gate forces it independently of this
    #       one.
    #   1   the class that stands the contexts up. Whatever shape it takes, it
    #       names a context or an initializer type, and both are Spring's
    #       alone.
    #   --
    #   9
    #
    # What a floor of 9 does not refuse: it does not refuse the
    # decorative tree. Nine is reached by annotating the seven surviving
    # qualifiers and writing one `@Configuration` class with a context
    # reference in it, and a tree that did exactly that and then wired the
    # object graph by hand behind a project-local registry passes this gate.
    # This count is a lower bound on *contact with the container* and was
    # never a measurement of how much of the graph moved.
    #
    # What refuses that tree is the conjunction, not this number. Built for
    # real, a hand-wired tree passes this gate with 55. To reach a
    # decorative pass a candidate must also write three distinct
    # container types as types rather than as annotations
    # (`container-api-used`), a class that stands a context up
    # (`container-bootstrap`), an implementation of Spring's `Scope` and the
    # call that registers it (`container-scope`), three components on the
    # container's own lifecycle contracts (`container-lifecycle`) and fifteen
    # bean definitions in a tree that has a context to define them into
    # (`bean-definitions`) -- all of it dead code that nothing reads, which is
    # more work than porting the wiring and leaves a tree whose own classes
    # describe a container it never asked to build anything. Measured, the
    # same tree takes 35.5 of 46.5 -- it passes `qualifier-annotations`,
    # because the seven marks are exactly the decoration it does do.
    #
    # And the limit: no gate in this file can prove that a context was
    # ever refreshed. Every one of them reads a class file or an AST, and a
    # `@Bean` method that is never called compiles exactly like one that is.
    # §1.1's last bullet -- the application really gets its objects from the
    # container -- is backed by this conjunction plus the 428 behavioural
    # observations, which fail a server whose graph was never built, and not
    # by proof.
    "spring_classes_min": 9,

    # How many *distinct* container types this project's own production
    # source has to write as types -- a field, a parameter, a return type, an
    # `extends`, a `new`, a cast, a `.class` literal -- or reach through a
    # bytecode supertype. Not annotations: `container-api-used` reads the AST
    # precisely so that `@Configuration` and `@Named` do not count here, and
    # not sites either, so that a port is not asked to repeat itself.
    #
    # Three, derived from the places the instruction leaves no choice:
    #
    #   1   §1.4: "the application stands its contexts up itself ... it is one
    #       of this project's own classes that does it". A class cannot stand
    #       a context up without writing a context type or extending one of
    #       the container's own bootstrap classes.
    #   1   §1.3: the SSH session scope goes "against Spring's [Scope] --
    #       implemented, and then made known to the container under a name".
    #       Implementing an interface writes it.
    #   1   making it known under a name is a call on whatever holds the
    #       scope registry, and that receiver is a second container type
    #       written at the call site.
    #   --
    #   3
    #
    # Everything §1.3 leaves free is excluded from the derivation on purpose.
    # The fifteen producers are the case that decides the number: §1.3's own
    # bullet allows `java.util.function.Supplier` in place of `Provider<T>`,
    # so a port that takes that option writes no container type for any of
    # them, and a floor that counted on producers would refuse a port the
    # instruction permits. The seven qualifier annotations are excluded for
    # the same reason from the other direction -- they are annotations, and
    # this gate does not read annotations at all.
    #
    # The reference measures 16 distinct types across 126 sites -- 105 of them
    # by U1 in 33 source files, none by U2 and 21 by U3 -- so the floor is a
    # fifth of what a full port reaches. That headroom is
    # the point: the gate is here to separate "wrote container types" from
    # "wrote none", which is the distinction §1.1's last bullet draws, and
    # not to price how many.
    "container_api_types_min": 3,

    # At least one type carrying `@Configuration`. One and not the reference's
    # eight, because the number of configuration classes is a matter of taste
    # and not of correctness: this tree stands up four contexts, the reference
    # gives them eight configuration classes between them, and a candidate who
    # writes one class per context or one class for the lot has done the same
    # work in a different shape. The gate's question is whether the container
    # is configured from Java at all.
    #
    # There is deliberately no floor on `@Bean` methods beside it. The obvious
    # one -- "the values injected by qualifier are jgit `Config`s, `File`s,
    # `Executor`s and `String`s, none of which is a class this project can
    # annotate, so each needs a `@Bean`" -- is false: a `@Component` class
    # implementing `FactoryBean<Config>` carries the qualifier itself and needs
    # no `@Bean` method anywhere. The count is reported in the evidence and
    # gated on by nothing.
    "configuration_types_min": 1,

    # Classes that still produce a value for injection, measured as own types
    # implementing one of `ObjectFactory`, `ObjectProvider`, `Provider`,
    # `Supplier` or `FactoryBean`, or extending an own type that does, with
    # the Guice ones excluded: 0 in the baseline, 15 in the reference --
    # thirteen `ObjectFactory`, one `Supplier`, and one subclass of that
    # `Supplier`. The baseline's own sixteen match on the simple name and
    # every one of them is `com.google.inject.Provider`, which is why the gate
    # excludes rather than counts them; without the exclusion the untouched
    # tree would score this gate outright.
    #
    # The two widenings close false negatives that the reference itself
    # would otherwise trip over. `FactoryBean` is a
    # producer class by any reading and CAL recommends it eight entries up.
    # Inheritance matters because a producer whose subclass is the bean is
    # still one class per produced value: this reference's
    # `HttpCanonicalWebUrlProvider` extends the `Supplier` that
    # `CanonicalWebUrlProvider` implements and declares no clause of its own,
    # and a gate reading only direct clauses would not see it.
    #
    # The floor is 10 and not 14. §1.3 allows a factory interface to go with
    # the generator where the caller already holds every argument, and a
    # one-method producer written as a lambda or folded into a `@Bean` body is
    # a shape a correct port may pick for a few of them; four units is what
    # that is worth against a reference of 14. What 10 refuses is the fold
    # §1.3 names outright -- fifteen providers becoming fifteen `@Bean`
    # bodies -- which `sources-survive` catches only when it is total (96
    # files against a floor of 100) and misses entirely when it is partial.
    #
    # What it does not refuse: ten empty
    # `class DummyN implements Supplier<String>` types under `src/main/java`
    # pass this gate. Intersecting with the types a configuration actually
    # declares as beans does not help -- this reference's own
    # producers carry no stereotype and reach the container through `@Import`
    # and through `new` inside a `@Bean` body, so the intersection tight
    # enough to refuse the dummies is the one that pins this reference's
    # exact shape, and a gate only the reference passes is worse than a
    # census. So this stays a census of a construct, at 1.0, and it is
    # *not* part of what refuses a tree that only looks Spring-shaped. The
    # five floors below are.
    #
    # Production source only, both the direct pass and the subclass closure:
    # `b.ast.kind("type")` scans `src/test/java` too, and a producer written
    # in a fixture is a fixture.
    "provider_classes_min": 10,

    # §1.3's second bullet keeps seven of the eight Guice binding annotations
    # and retires the eighth. The seven are named here by the names §1.3
    # itself prints: the instruction lists all seven in
    # the very bullet this gate measures, and says outright that `CommandName`
    # is the exception that goes. A gate that instead counted "any annotation
    # type marked as a qualifier" would pass a tree that deleted all seven and
    # wrote seven new ones, which is the one thing §1.3's "those annotations
    # stay, as annotations" forbids.
    "qualifier_annotations": ("GerritPersonIdent", "RemotePeer",
                              "CanonicalWebUrl", "GerritServerConfig",
                              "SitePath", "CommandExecutor",
                              "StreamCommandExecutor"),

    # What "marked as one" means, and there are exactly two spellings of it.
    # Spring 6 recognises a custom qualifier annotation on its own when the
    # annotation is meta-annotated with its own `@Qualifier` or with JSR-330's
    # -- which it now reads from `jakarta.inject`, for CONTAINER_API's reason.
    # A third route exists in Spring, `CustomAutowireConfigurer`, and it is
    # deliberately not here: §1.3 rules it out in the same sentence, "because
    # the annotation itself is marked as one and not because the container was
    # separately told about it".
    #
    # The reference marks all seven Spring's way and marked them Spring's way
    # at step 03, so this rung does not move it. The swap is here so that a
    # tree which took the JSR-330 route at step 03 and renamed its imports
    # along with everything else still passes, and so that one which took that
    # route and did *not* rename them fails -- which is correct, because
    # `javax.inject.Qualifier` on a Spring 6 annotation is a marker nothing
    # reads.
    "qualifier_marks": (
        "org.springframework.beans.factory.annotation.Qualifier",
        "jakarta.inject.Qualifier"),

    # All seven, and not a majority. §1.3 does not leave a choice here the way
    # it leaves one about `Provider<T>`'s target interface or about folding an
    # assisted-inject factory away: each of the seven distinguishes one of
    # several values of a common type -- two `Executor`s, a jgit `Config`
    # among others, a `File`, a `String` -- so dropping any one of them drops
    # a distinction the application needs and leaves the injection point
    # ambiguous. Measured: reference 7 of 7, each meta-annotated
    # `org.springframework.beans.factory.annotation.Qualifier` and nothing
    # else; floor seed 0 of 7, all eight annotations (the seven plus
    # `CommandName`) still meta-annotated `com.google.inject.BindingAnnotation`.
    # So this gate fails the floor rather than passing it vacuously.
    "qualifier_annotations_min": 7,

    # --- the container is not only imported, it is stood up and used -------
    #
    # Five floors for what the gates above do not
    # ask. Almost every gate before this line is satisfied by a tree that
    # names the container: the poms resolve it, the classes import it, one
    # class carries `@Configuration`, ten classes implement a factory
    # interface. (`qualifier-annotations` is the exception -- it asks for a
    # specific, named edit to seven specific annotation declarations -- but it
    # asks about declarations too, not about a running context.) None of them
    # asks whether anything ever builds a context, or whether the bindings the
    # deleted modules declared were declared again. A tree can pass all of
    # them and still wire itself by hand.
    #
    # These five do not close that hole -- see the note under
    # `spring_classes_min`, no static gate can -- they raise its price. Every
    # one of them reads production classes and production source only: `own`
    # elsewhere in this file means "not inside a jar", which keeps
    # `target/test-classes`, and a context stood up inside a unit test is not
    # the application standing its context up.
    #
    # Each of the five takes at least two independent routes in, and each
    # names its routes in its own docstring. That is a rule and not a style:
    # a gate with one route is a gate that only the reference's spelling
    # passes, and §1.4 leaves the shape of every one of these to the
    # candidate.
    #
    # 1. Something stands a context up, and something drives it. Asked as a
    #    conjunction of two halves, because either half alone is a name and
    #    not a container.
    #
    #    Half A -- an application context object exists in this tree -- three
    #    routes:
    #
    #      A1  production source writes the name of a context implementation.
    #          Read from `typeuse`, which is one record per written
    #          `ClassOrInterfaceType`, so `new AnnotationConfigWebApplicationContext()`,
    #          a field declared `GenericApplicationContext ctx` and a
    #          `GenericWebApplicationContext.class` literal all produce one.
    #      A2  an own production class's *superclass* is one of the types in
    #          `bootstrap_types`, read from the bytecode. This is the route
    #          for a port that only extends Spring's own initializer
    #          machinery: a subclass of
    #          `AbstractAnnotationConfigDispatcherServletInitializer`
    #          overrides `getRootConfigClasses()`, `getServletConfigClasses()`
    #          and `getServletMappings()`, whose descriptors are `Class[]` and
    #          `String[]`, and names no Spring context type anywhere except in
    #          its `extends` clause.
    #      A3  an own production class carries the dotted name of one of them
    #          as a string constant. That is how `ContextLoaderListener` is
    #          told its `contextClass`, and `ArtifactScan` records string
    #          literals that look like type names for exactly this reason.
    #      A4  production source writes one of `boot_context_drivers`. The
    #          routes above all assume a tree that builds
    #          its context by naming the context; an application that starts
    #          its own container names the thing that builds the context
    #          instead, and `new SpringApplicationBuilder(...)` is as much a
    #          context object as `new AnnotationConfigApplicationContext(...)`.
    #      A5  an own production class references one of
    #          `boot_context_driver_fqcns` in its constant pool. The bytecode
    #          twin of A4, and not redundant with it: the one-line
    #          `SpringApplication.run(App.class, args)` is a static call on a
    #          name expression, writes no `ClassOrInterfaceType` and so emits
    #          no `typeuse` for A4 to read.
    #
    #    Half B -- something in this tree drives that context -- four routes:
    #
    #      B1  a production call to one of `context_verbs`, the methods only a
    #          context or a bean factory has;
    #      B2  a production declaration of one of `context_template_methods`,
    #          which exist to be overridden and mean nothing anywhere else;
    #      B3  a production use of one of `definition_carriers`, the types
    #          that carry definitions into a context or a context into the
    #          servlet container;
    #      B4  a production call to one of `boot_driver_verbs` in a file that
    #          holds a `boot_context_drivers` name. `run` is what drives a
    #          `SpringApplication` or a `SpringApplicationBuilder`, and the
    #          file condition is what stops it from meaning any of the
    #          `Runnable`s this codebase is full of.
    #
    #    What is deliberately *not* a Half A route is naming one of the
    #    context interfaces. `@Scope(WebApplicationContext.SCOPE_REQUEST)`
    #    names one and creates nothing; a method declared
    #    `install(ServletContext, WebApplicationContext)` names one in its own
    #    descriptor and creates nothing. A gate that counted any
    #    constant-pool reference to any context type, interfaces included,
    #    would pass a tree whose object
    #    graph is built with `new` behind a project-local registry
    #    on the strength of a `@Scope` attribute. Naming a context is not
    #    creating one.
    #
    #    `bootstrap_types` therefore serves A2 and A3 only, and is matched
    #    with `_under`, which is exact-FQN-or-below. It keeps the interfaces
    #    (harmless: an interface is never a superclass, and a string constant
    #    naming one is not a way in that costs anything) and includes Spring's
    #    four initializer base classes.
    #
    #    Holding a context *interface* is still not a route, and A4 does not
    #    quietly make it one: `ConfigurableApplicationContext ctx = ...` is
    #    the type this rung's entry point declares its field as, and it is
    #    `SpringApplicationBuilder` on the other side of that assignment that
    #    counts, not the field. The halves also stay a conjunction. A4 and A5
    #    widen what can satisfy Half A; they do not let Half A stand in for
    #    Half B, and B4 fires only where a driver name was already written.
    #
    #    Measured on this rung's reference: A=4 (A1 1, A4 2, A5 1), B=13
    #    (B1 11, B4 2). A2, A3, B2 and B3 are routes this reference does not
    #    take and a different correct port does. The tree this step is handed
    #    measures A=17 B=18 and passes; the numbers differ that much because
    #    the 16 A1 sites it has are all in the initializer §1.4 retires.
    "context_impls": (
        # Instantiable contexts, matched on the written simple name. Every
        # concrete `ApplicationContext` Spring 5.3 ships that a candidate
        # could reasonably build; the abstract bases are in `bootstrap_types`
        # below, where they are matched as superclasses instead.
        "AnnotationConfigApplicationContext",
        "AnnotationConfigWebApplicationContext",
        "GenericApplicationContext",
        "GenericWebApplicationContext",
        # Boot's two, which are the ones a tree that starts its own container
        # holds. `GenericWebApplicationContext` above is Boot's as well: it is
        # what an embedded container's context degrades to when the
        # application is not a web one.
        "ServletWebServerApplicationContext",
        "AnnotationConfigServletWebServerApplicationContext",
        "GenericXmlApplicationContext",
        "GenericGroovyApplicationContext",
        "ClassPathXmlApplicationContext",
        "FileSystemXmlApplicationContext",
        "XmlWebApplicationContext",
        "StaticApplicationContext",
        "StaticWebApplicationContext",
    ),
    "bootstrap_types": (
        "org.springframework.web.WebApplicationInitializer",
        "org.springframework.web.SpringServletContainerInitializer",
        "org.springframework.web.context.ContextLoader",
        "org.springframework.web.context.ContextLoaderListener",
        "org.springframework.web.context.WebApplicationContext",
        "org.springframework.web.context.AbstractContextLoaderInitializer",
        "org.springframework.web.context.support."
        "AnnotationConfigWebApplicationContext",
        "org.springframework.web.context.support.GenericWebApplicationContext",
        "org.springframework.web.context.support."
        "AbstractRefreshableWebApplicationContext",
        "org.springframework.web.servlet.support."
        "AbstractDispatcherServletInitializer",
        "org.springframework.web.servlet.support."
        "AbstractAnnotationConfigDispatcherServletInitializer",
        "org.springframework.context.annotation."
        "AnnotationConfigApplicationContext",
        "org.springframework.context.support.AbstractApplicationContext",
        "org.springframework.context.support.GenericApplicationContext",
        "org.springframework.boot.web.servlet.context."
        "ServletWebServerApplicationContext",
        "org.springframework.boot.web.servlet.context."
        "AnnotationConfigServletWebServerApplicationContext",
        "org.springframework.context.ConfigurableApplicationContext",
        "org.springframework.context.ApplicationContext",
    ),
    "context_verbs": (
        # Methods a context or a bean factory has and a plain object does
        # not. `register` is not here on purpose: a project-local registry
        # plausibly has one, and every shape §1.4 allows reaches one of these
        # anyway -- the constructor-configured context through `getBean`, the
        # hand-refreshed one through `refresh`, the functional one through
        # `registerBean`, the initializer subclass through B2.
        "refresh", "getBean", "getBeanProvider", "getBeanFactory",
        "getAutowireCapableBeanFactory", "createBean", "autowireBean",
        "registerBean", "registerBeanDefinition", "registerSingleton",
        "registerShutdownHook", "setConfigLocation", "setConfigLocations",
    ),
    "context_template_methods": (
        # `onStartup` is not here: it is `WebApplicationInitializer`'s, and a
        # tree can implement that interface without ever building a context.
        "getRootConfigClasses", "getServletConfigClasses",
        "createRootApplicationContext", "createServletApplicationContext",
        "createWebApplicationContext",
    ),
    "definition_carriers": (
        "ContextLoaderListener", "AnnotatedBeanDefinitionReader",
        "ClassPathBeanDefinitionScanner", "BeanDefinitionRegistry",
        "BeanDefinitionBuilder", "BeanDefinitionRegistryPostProcessor",
        "ApplicationContextInitializer",
    ),
    "boot_context_drivers": (
        # Not contexts, which is why they are not in `context_impls`: they are
        # the two objects Boot ships whose whole job is to build a context,
        # refresh it and hand it back. A tree that writes one of these has a
        # context as surely as a tree that writes `new
        # AnnotationConfigApplicationContext(...)`, and has it in the spelling
        # this rung's entry point is actually written in. They are kept out of
        # `context_impls` because that tuple also answers "which files hold a
        # context" for `bean-definitions`' scan route, and a builder held in a
        # `main` says nothing about where scanning was switched on.
        "SpringApplication", "SpringApplicationBuilder",
    ),
    "boot_context_driver_fqcns": (
        # The same two from the bytecode side. `SpringApplication.run(App.class,
        # args)` -- the one-line entry point Boot's own documentation opens
        # with -- is a static call on a name expression and writes no
        # `ClassOrInterfaceType`, so it produces no `typeuse` record at all.
        # The constant pool is the only place that tree says the name.
        "org.springframework.boot.SpringApplication",
        "org.springframework.boot.builder.SpringApplicationBuilder",
    ),
    "boot_driver_verbs": (
        # What driving one of the above looks like. Matched only in a file
        # that holds a `boot_context_drivers` name, because `run` on its own
        # is the most common method name in Java.
        "run",
    ),

    # 2. The bindings came across, in a form a container would receive.
    #    Four routes, summed, because §1.4 says the container is configured
    #    "from Java, not from XML" and does not say "from annotations":
    #
    #      R1  a `@Bean` method whose *owner type* carries `@Configuration` or
    #          a stereotype. The owner requirement is Spring's own
    #          rule: a `@Bean` method on a class the container never
    #          receives declares nothing.
    #      R2  a type carrying a stereotype -- but only when production source
    #          switches component scanning on somewhere: a `@ComponentScan`,
    #          a `ClassPathBeanDefinitionScanner`, or a `scan(...)` call in a
    #          file that also names a context implementation. Fifteen
    #          `@Named` classes and no scan is fifteen annotations, not
    #          fifteen bean definitions.
    #      R3  a functional registration site: a call to `registerBean`,
    #          `registerBeanDefinition` or `registerSingleton`, or a use of
    #          one of the definition builders and readers. Without this
    #          route a correct Spring
    #          5 port that builds a `GenericApplicationContext` and calls
    #          `registerBean` forty times would score 0.
    #      R4  a class named in an `@Import`. Each one is a definition the
    #          importing class hands to the container, and `@Import` is how a
    #          port that keeps its configuration in several classes assembles
    #          them.
    #
    #    The limit, because it is the one thing this gate cannot see:
    #    switching scanning on (R2) or writing a configuration class full of
    #    `@Bean` methods (R1) is still only a declaration. Nothing here can
    #    tell a `@ComponentScan` a context reads from one a context never
    #    sees. `container-bootstrap` is the gate that asks whether anything
    #    loads it, and the two are meant to be read together: this one asks
    #    whether the graph was re-declared, that one asks whether a container
    #    exists to receive it.
    #
    #    The floor stays 15. The seventeen deleted files declared 43 distinct
    #    `bind(X.class` targets between them, so 15 is below any honest
    #    re-declaration of that graph and above what a tree can reach by
    #    annotating its configuration class and stopping. It is the same
    #    number §1.3 uses for the fold it refuses, and for the same reason.
    #
    #    Measured: baseline 0; reference 68 (34 by R1, 0 by R2, 2 by R3,
    #    32 by R4).
    "bean_definitions_min": 15,
    "stereotypes": ("Component", "Service", "Repository", "Controller",
                    "Named"),
    "scan_annos": ("ComponentScan", "ComponentScans"),
    "scan_carriers": ("ClassPathBeanDefinitionScanner",),
    # A `scan(...)` call is only read as switching scanning on when the file
    # that makes it also writes the name of a context implementation. `scan`
    # on its own is far too common a method name to be a route; beside an
    # `AnnotationConfigApplicationContext` it is unambiguous, and it is the
    # shape a port that scans from a main method actually writes.
    "scan_calls": ("scan",),
    "definition_calls": ("registerBean", "registerBeanDefinition",
                         "registerSingleton"),
    "definition_builders": ("BeanDefinitionBuilder", "RootBeanDefinition",
                            "GenericBeanDefinition", "ChildBeanDefinition",
                            "AnnotatedBeanDefinitionReader",
                            "ClassPathBeanDefinitionScanner"),
    "import_annos": ("Import",),

    # 3. Objects that start and stop still start and stop, on the container's
    #    terms. `no-lifecycle-classes` and `no-lifecycle-source` say the
    #    project's own `com.google.gerrit.lifecycle` package is gone; nothing
    #    said anything took its place, and deleting it satisfies both.
    #
    #    Three routes, and the unit is one managed object, not one site:
    #
    #      L1  an own production class whose bytecode supertypes name one of
    #          `lifecycle_types`. That is `Lifecycle` and `SmartLifecycle`,
    #          `InitializingBean` and `DisposableBean`, and
    #          `ApplicationListener` and `SmartInitializingSingleton`.
    #          An `ApplicationListener<ContextRefreshedEvent>` that starts the
    #          SSH daemon when the context comes up is doing exactly the work
    #          `LifecycleManager` did.
    #      L2  a `@PostConstruct` or `@PreDestroy` in the JSR-250 spelling
    #          Spring reads natively, or a Spring `@EventListener` -- the
    #          annotation form of L1's first addition, and the one a candidate
    #          who does not want a marker interface writes.
    #      L3  a `@Bean` that sets `initMethod` or `destroyMethod`.
    #
    #    Only the `jakarta.annotation` spelling of the JSR-250 pair counts,
    #    for CONTAINER_API's reason, and this rung is where that flips: 5.3's
    #    `CommonAnnotationBeanPostProcessor` read `javax.annotation` and
    #    Spring 6's reads `jakarta.annotation`. `@EventListener` is Spring's
    #    own and has no such twin.
    #
    #    The floor is 3 against 6. §1.3's `LifecycleManager` bullet names the
    #    work and the tree has six objects doing it, but two of the six are
    #    the same `Lifecycle` nested class pattern and a port may reasonably
    #    merge them; 3 is what refuses a tree that deleted the lifecycle
    #    package and wrote nothing.
    #
    #    Measured: baseline 0; reference 6.
    "lifecycle_types": (
        "org.springframework.context.Lifecycle",
        "org.springframework.context.SmartLifecycle",
        "org.springframework.context.ApplicationListener",
        "org.springframework.beans.factory.DisposableBean",
        "org.springframework.beans.factory.InitializingBean",
        "org.springframework.beans.factory.SmartInitializingSingleton",
    ),
    "lifecycle_annos": ("PostConstruct", "PreDestroy"),
    "lifecycle_event_annos": ("EventListener",),
    "lifecycle_min": 3,

    # 4. The request scope is a scope the container knows about. Guice's
    #    `SshScope` handed out per-request objects through a custom
    #    `com.google.inject.Scope`; §1.3 asks for the same thing on Spring's
    #    side, and `no-guice-classes` is satisfied by throwing it away and
    #    passing the objects as arguments instead.
    #
    #    Asked as a conjunction, because either half alone is decoration. An
    #    own type implementing `org.springframework.beans.factory.config.Scope`
    #    is a scope nothing uses until it is registered; a `registerScope`
    #    call with no implementation behind it does not compile. Baseline 0
    #    and 0; reference 1 and 1.
    #
    #    Each half has two routes. The implementation half accepts a direct
    #    implementation of `Scope` *or* a subclass of one of Spring's own
    #    scope base classes -- `SimpleThreadScope` and
    #    `AbstractRequestAttributesScope` are the two a per-connection scope
    #    is naturally built on, and a class that extends either has `Scope`
    #    nowhere in its own direct interface list, which is what the bytecode
    #    records. The registration half accepts a `registerScope` call or a
    #    `CustomScopeConfigurer`, which is the declarative spelling of the
    #    same registration.
    #
    #    The implementation half is read from the bytecode and not from the
    #    `implements` clause, and that is not a preference. This reference
    #    writes its `Scope` as an anonymous class -- `SshScope.REQUEST = new
    #    Scope() {...}`, compiled to `SshScope$1` -- which has no `implements`
    #    clause anywhere in the source. An AST gate on the clause would fail
    #    the reference. The registration half has to be the AST, the other way
    #    round: the constant pool records the owner and descriptor of an
    #    invocation but not the name of the method invoked, so `registerScope`
    #    is only visible in the source text.
    #
    #    The limit, stated here and in the docstring because it is not
    #    fixable from a static scan: a `new Scope(){}` with six methods that
    #    all throw, beside a `registerScope` call on a code path nothing
    #    reaches, passes this gate. Whether the scope actually scopes anything
    #    is a behavioural question, and the 428 observations are where it is
    #    asked -- every SSH command in the suite goes through the per-request
    #    objects this scope hands out.
    "scope_types": (
        "org.springframework.beans.factory.config.Scope",
        "org.springframework.context.support.SimpleThreadScope",
        "org.springframework.web.context.request."
        "AbstractRequestAttributesScope",
        "org.springframework.web.context.request.RequestScope",
        "org.springframework.web.context.request.SessionScope",
    ),
    "scope_registration": ("registerScope",
                           "org.springframework.beans.factory.config."
                           "CustomScopeConfigurer"),

    # 5. The objects Guice's generated factories built are still built by the
    #    container. §1.3's assisted-injection bullet says an object whose
    #    caller supplies some of its arguments still has the rest resolved for
    #    it -- "the caller's arguments passed in, the container's dependencies
    #    resolved, the object autowired on the way out" -- and allows the
    #    factory interface itself to go where the caller already holds every
    #    argument. This is the count of surviving factories that §1.5
    #    refers to.
    #
    #    Four routes, because Spring has four ordinary answers to it and §1.3
    #    names none of them as required:
    #
    #      F1  a call to one of `assisted_verbs` -- the `AutowireCapableBean-
    #          Factory` methods, which exist for precisely this;
    #      F2  a production use of one of `assisted_types` -- that factory
    #          interface itself, or `ObjectProvider`, whose `getObject(args)`
    #          is the same construct with a nicer face;
    #      F3  a `@Scope` naming the prototype scope, which is the answer for
    #          an object that is built fresh per caller;
    #      F4  a `@Lookup` method, which is Spring's own container-generated
    #          factory method and the closest thing it has to
    #          `FactoryModule.factory`.
    #
    #    The floor is 1, not a count: how many of this tree's two assisted
    #    factories survive as interfaces is exactly the judgement §1.3 leaves
    #    to the candidate, and a gate with a count in it would be a gate
    #    against that sentence. What 1 refuses is a tree in which every object
    #    a generated factory used to build is now built with `new` and
    #    injected by nobody.
    #
    #    Measured: baseline 0; reference 37 (2 by F1, 26 by F2, 9 by F3,
    #    0 by F4).
    "assisted_verbs": ("createBean", "autowireBean", "initializeBean",
                       "autowire", "autowireBeanProperties", "configureBean"),
    "assisted_types": ("AutowireCapableBeanFactory", "ObjectProvider"),
    "assisted_scope_names": ("prototype", "SCOPE_PROTOTYPE"),
    "assisted_annos": ("Lookup",),

    # Steps 01 and 02's thresholds, copied rather than imported -- see the
    # inherited tripwires at the bottom of this file for why there are four
    # rows and not thirty, and why the numbers live here.
    #
    # The five idiom pairs were re-measured against *this* step's reference
    # with this verifier's own AstScan: diamond 34,
    # explicit-type-args 0, try-with-resources 9, try-plain 61, multi-catch 10,
    # catch-plain 52, lambda+method-ref 13, anonymous 18, nio-file classes 4.
    # Every one of them clears step 02's cap or minimum with room to spare, and
    # two move a long way in the safe direction: `anonymous` falls from 31 to
    # 18 because the anonymous `Provider` and `TypeLiteral` subclasses the
    # Guice DSL required are `@Bean` methods now, and `explicit-type-args`
    # reaches zero because the four that were left were `new TypeLiteral<...>`.
    #
    # So the caps and minima are step 02's unchanged. That is a different
    # decision from step 02's, which raised four of step 01's caps, and the
    # reason is that this rung does not write new Java against a new API in the
    # way that one did. It rewrites constructors and moves bindings into
    # `@Bean` methods -- declarative code, in a shape that has no `try`, no
    # `catch` and no anonymous class in it. The measured direction of travel is
    # down on every old-form counter, so leaving the caps where they are costs
    # a candidate nothing and keeps the invariant step 02 established: every
    # cap stays strictly below the count in the unported 2011 tree
    # (explicit-type-args 47, try-plain 81, catch-plain 83, anonymous 40),
    # which is the tree these caps exist to fail.
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

    # Step 02's own thresholds, for the library tripwire. Same copying rule,
    # with one exception this rung forces and one entry it retires.
    #
    # The exception is JGit. Step 02 pinned `^5\.13\.` and steps 03 and 04
    # copied it; here it becomes `^7\.`, and the reason is not that a newer
    # library is nicer. `org.eclipse.jgit.http.server` is a servlet
    # application: `GitServlet` extends `HttpServlet` and `GitFilter`
    # implements `Filter`. Measured, on the jars themselves -- 61 classes in
    # each, 48 of them naming a servlet type:
    #
    #   6.10.1.202505221210-r   48 classes name javax/servlet,   0 jakarta
    #   7.0.0.202409031743-r     0                              48 jakarta
    #   7.8.0.202609011348-r     0                              48 jakarta
    #
    # There is no 6.x that is jakarta and no 7.x that is javax: the package
    # rename is the 7.0 boundary. A candidate who moves the rest of the tree
    # to `jakarta.servlet` and leaves JGit at 5.13 or 6.10 has a tree that
    # compiles -- its own code never names a JGit servlet type directly --
    # and a `GitServlet` the container cannot register, because the
    # `HttpServlet` it extends is a class no longer on the path. Every
    # git-over-HTTP observation in the recorded suite fails on it. So the pin
    # moves with the rung, `inherited-libraries-hold` reports it under the
    # name `no-jgit-before-7`, and the predecessor tree fails that row for a
    # reason that is this rung's work rather than a regression. That is not
    # new: step 04's `inherited-servlet-hold` failed on the predecessor for
    # the same kind of reason, and its weight table says so out loud.
    #
    # The coordinate pin is inherited hygiene and not the gate that does the
    # work. `jgit-http-jakarta`, in this rung's own section, asks the property
    # rather than the number -- whatever version resolves, the packaged HTTP
    # server names jakarta servlet types and no javax ones -- because the
    # property is what makes the application serve requests and the version
    # string is only the usual way of arriving at it.
    #
    # So the regex refuses the retired lines rather than pinning the current
    # one. 7.0 is the first major on the jakarta side of the boundary
    # measured above, and 8, 9 and every major after them are on that side
    # too, so `^([7-9]|[1-9][0-9])\.` admits the 7 line and everything later
    # and refuses 6 and below. §1.4 says the version is a means and the
    # requirement is the property, and `jgit-http-jakarta` is the row that
    # measures the property; a pin of `^7\.` would instead have failed a held
    # row on a JGit 8 port for being newer than the reference, which is not a
    # question this rung asks. What it still refuses is 5.13 and 6.10 -- 5.13
    # is what the predecessor tree resolves, and refusing it is why
    # `inherited-libraries-hold` fails on that tree.
    "jgit_version_re": r"^([7-9]|[1-9][0-9])\.",
    "jgit_classes_min": 25,
    "sshd_version_re": r"^2\.",
    "sshd_classes_min": 1,

    # `sshd_present_re` (`^2\.19\.`) is retired here rather than copied
    # forward, for `servlet_version_re`'s reason below. It was what `sshd-2x`
    # and `sshd-core-2-19` matched on, which made a row named for the 2.x
    # line refuse SSHD 2.20: §1.5 says everything under `org.apache.sshd` is
    # on 2.x and says nothing about which patch line. Both readers now use
    # `sshd_version_re`, which is the sentence they were always measuring,
    # and the second is renamed `sshd-core-2x` to match what it now asks.
    # That leaves this key with no reader, and a calibration entry nothing
    # reads is reported to the grader as though it were a threshold.

    # `servlet_version_re` is retired here rather than copied forward. Step
    # 02 used it for `servlet-api-provided`, which asked that
    # `javax.servlet:javax.servlet-api:3.1` resolve at `provided` scope; step
    # 04 retired that gate when the embedded container became the source of
    # the API, and the constant has had no reader since. A calibration entry
    # nothing reads is reported to the grader as though it were a threshold,
    # and at this rung it would be reported as `^3\.1\.` beside a tree whose
    # servlet API is 6.0. It is deleted, not corrected: there is nothing for
    # it to be correct about.


    # --- step 04's own ------------------------------------------------------

    # The Spring Boot line. 3.x now, and this is the entry that moves the
    # whole rung: Boot 3 is built on Spring Framework 6, Spring Framework 6
    # is compiled against `jakarta.servlet`, and Boot 3's servlet-container
    # starters resolve Tomcat 10.1 / Jetty 12 -- containers that implement
    # Servlet 6.0. There is no Boot 3 that is javax and no Boot 2 that is
    # jakarta, so a candidate cannot rename imports and stay on 2.7, and
    # cannot move to 3.x and keep them. `spring_version_re` (`^(6|7)\.`) says
    # the same thing one layer down and is asked inside `inherited-di-hold`.
    #
    # Any release inside the line passes rather than one version exactly, for
    # `spring_version_re`'s reason: a release inside the line is the same line.
    #
    # Measured when this was step 04's own row: that rung's reference resolved
    # six coordinates, all 3.5.16 (spring-boot, spring-boot-autoconfigure,
    # spring-boot-starter, spring-boot-starter-json,
    # spring-boot-starter-tomcat, spring-boot-starter-web), and the tree it was
    # handed resolved the same six at 2.7.18 and failed the pin, which was the
    # point of it.
    #
    # Widened here from `^3\.`, for `spring_version_re`'s reason and in the
    # same words: this row is asked inside `inherited-boot-hold`, a hold is a
    # row the floor has to pass, and the floor is a Boot 3.5.16 tree. `^4\.`
    # would make the floor fail it and price one platform move twice. Measured
    # on this rung's reference: fourteen `org.springframework.boot`
    # coordinates, all 4.1.1. `boot-4` below is the row that reads the 4; this
    # one goes on asking only that Boot is what supplies the framework.
    #
    # The gate keeps its name, where `spring-6` did not. `spring-boot-dep`
    # names the property rather than the version, so it does not stop being
    # true when the version moves.
    "boot_version_re": r"^(3|4)\.",

    # Three, and `spring-boot` by name. The count is a floor and not a shape:
    # a candidate who takes `spring-boot-starter-web` resolves six, one who
    # assembles `spring-boot` + `spring-boot-autoconfigure` +
    # `spring-boot-starter-tomcat` by hand resolves three, and both have
    # adopted Boot. `spring-boot` is named because it is the one artifact no
    # Boot application can do without -- `SpringApplication`, the repackaged
    # jar's loader and the embedded-container bootstrap all live in it -- so a
    # tree that resolved three artifacts under the groupId and not that one
    # has taken something else.
    "boot_artifacts_min": 3,
    "boot_required_artifacts": ("spring-boot",),

    # --- what is inside the deployable, measured inside it -------------------
    #
    # Every count below is of classes whose scanned origin begins with the
    # artifact's own filename followed by `!`, which is what `inside_artifact`
    # tests. Not "somewhere under target/": at this rung target/ also holds the
    # loose `target/classes` output, and for a candidate who kept a war module
    # it holds an exploded webapp directory whose contents the scanner reports
    # a second time under a second origin. What these gates ask is what the
    # *deployment* contains.

    # The servlet API, inside the artifact. Step 04 asked this of
    # `javax.servlet` and it is the same question about the same jar; what
    # moved is which package the answer is in, and that move is this rung.
    #
    # One floor, two readers, because the two questions are different:
    #
    #   `servlet-api-packaged`, inside `inherited-boot-hold`, counts classes
    #   under *either* spelling. What step 04 bought was that the API travels
    #   inside the deployable instead of belonging to a host, and that is
    #   true of a tree at either rung. Asked in step 04's spelling it would
    #   report "0 javax.servlet classes" against a correct step-05 tree, and
    #   a hold row that fails on the right answer is not holding anything.
    #
    #   `jakarta-servlet-api-packaged`, in this rung's own section, counts the
    #   jakarta spelling only, and `no-javax-ee-apis-packaged` beside it says
    #   the javax one is gone. Those two carry the rung's weight; the hold row
    #   carries step 04's.
    #
    # Measured inside the deployable: predecessor 91 `javax.servlet` classes
    # and 0 jakarta, all 91 from `BOOT-INF/lib/tomcat-embed-core-9.0.83.jar`;
    # reference 88 jakarta and 0 javax, all 88 from
    # `BOOT-INF/lib/tomcat-embed-core-10.1.55.jar`. The count fell by three
    # and the three are not arbitrary -- see `servlet_6_removed`.
    #
    # The floor stays 20 and not 88, for step 04's reason: the count is a
    # property of which container was embedded and of how much of the API
    # that container's jars happen to carry. Measured on the standalone API
    # jars a Jetty route brings instead, `jakarta.servlet-api` is 84 classes
    # at 6.0.0 and at 6.1.0. 20 is below every plausible packaging of the API
    # and far above the zero a tree that packages no API at all produces.
    "servlet_api_classes_min": 20,

    # The container itself, inside the artifact, by the implementations Spring
    # Boot ships starters for. Matched by package and not by jar name, because
    # the jar name is a packaging decision and the package is the API.
    #
    # The set is unchanged from step 04 and all three entries stay reachable,
    # which is deliberate even though this step's §1.3 now names Tomcat.
    # Step 04 does leave the choice open -- a Jetty port scores 428/428
    # there -- so a tree can arrive at this rung on
    # any of the three, and this gate's job is to say that a container is
    # packaged, not which one. A gate that accepts more than the instruction
    # asks for never fails a compliant tree; one accepting only
    # `org.apache.catalina` would fail an inherited Jetty tree for a reason
    # this gate does not measure, and the behaviour suite answers that
    # question directly. Same reason `servlet_6_markers` is measured on the
    # standalone `jakarta.servlet-api` jars a Jetty or Undertow route
    # resolves as well as on the `tomcat-embed-core` a Tomcat route does.
    #
    # Why §1.3 names one at all is a behaviour fact rather than a packaging
    # one, and it is recorded here because this is where a reader will come
    # looking for it. Measured on three trees differing only in container,
    # each scored under this step's own verifier:
    #
    #   Tomcat 10.1  428/428    Jetty 12  423/428    Undertow 2.3  424/428
    #
    # The nine cases involved are all ambiguous or percent-encoded request
    # targets, and Jetty's five and Undertow's four are disjoint: Jetty
    # refuses targets the recording answers, Undertow answers targets the
    # recording refuses. Servlet 6.0 left that to the implementation, so this
    # step's §1.6 -- and the container-independence it claims -- survives
    # only on the container the observations were recorded from.
    #
    # Measured: predecessor 670 under `org.apache.catalina` (Tomcat 9.0.83),
    # reference 671 (Tomcat 10.1.55). The floor is 100: a servlet container
    # is a large thing whichever one it is, and nothing short of one reaches
    # three figures.
    "container_packages": ("org.apache.catalina", "org.eclipse.jetty",
                           "io.undertow"),
    "container_classes_min": 100,

    # `org.apache.log4j`, and how many archives inside the deployable provide
    # it. Exactly one -- the only gate in this file that asks for a number
    # from both directions.
    #
    # Four of this project's own production classes (`SshLog` and its three
    # nested types) name `org.apache.log4j` directly, so zero providers is a
    # NoClassDefFoundError on the first SSH connection. Two is the hazard this
    # rung introduces, and the reason the gate is here rather than in the
    # inherited tripwire below: `slf4j-log4j12` is a relocation to
    # `slf4j-reload4j`, which brings in `ch.qos.reload4j:reload4j` -- a fork
    # of log4j 1.2 carrying the same `org.apache.log4j` packages as the log4j
    # this reactor compiles against. Which of the two wins at class-load time
    # is a function of classpath order, and a candidate who takes Boot's
    # managed slf4j without looking gets both. The relocation appeared at
    # 1.7.36, which is what Boot 2.7 manages; Boot 3.5 manages 2.0.18 and the
    # relocation is still there, so the hazard crosses this rung unchanged
    # and the artifact this rung's reference resolves is
    # `slf4j-reload4j-2.0.18.jar`.
    #
    # Measured inside the deployable, both trees: exactly one archive
    # provides `org.apache.log4j`, `log4j-1.2.16.jar`, 308 classes, the fork
    # excluded at the binding in each. So this row is held rather than
    # leapt -- it sits in `inherited-boot-hold` at this rung, which is where
    # a tripwire on a way of getting step 04 wrong belongs once step 04 is
    # behind the candidate.
    "logging_packages": ("org.apache.log4j",),

    # --- the entry point, and what replaces the container's callbacks --------

    # Spring Boot's own loader, in the two spellings `executable-jar` needs it
    # in: the zip-entry prefix it occupies inside the deployable, and the
    # class-name prefix `Main-Class` points at.
    #
    # The entry prefix is anchored at the *root* of the jar and that is the
    # whole point of it. `repackage` writes the loader's classes beside
    # `BOOT-INF/` rather than inside it, because they are the classes the JVM
    # has to load before anything in the jar knows how to read
    # `BOOT-INF/lib`. A copy of `spring-boot-loader` that arrived as an
    # ordinary dependency sits at `BOOT-INF/lib/spring-boot-loader-3.5.16.jar`
    # instead, and does not match a prefix anchored at the root.
    #
    # Both prefixes stop at `loader`. `repackage` writes `WarLauncher` and
    # `PropertiesLauncher` as well as `JarLauncher`, all three under this
    # package, and Boot 3 moved the launchers down one level to
    # `org.springframework.boot.loader.launch` -- a later rung inheriting this
    # file should not have to edit a class name.
    #
    # Step 04 measured 68 classes under `org/springframework/boot/loader/` at
    # the root and `Main-Class: org.springframework.boot.loader.JarLauncher`.
    # Re-measured at this rung, on the reference jar Boot 3.5.16 repackages:
    # 99 classes under the same root prefix, and `Main-Class:
    # org.springframework.boot.loader.launch.JarLauncher`. Both numbers moved
    # and neither constant did, which is the whole reason the prefixes stop
    # at `loader`: the launchers went down a level at Boot 3.2 and a prefix
    # written to the class name would have had to be edited here. The
    # predecessor jar still answers 68 and the old `Main-Class`, and both
    # trees clear the gate, as a held row should.
    "boot_loader_entry_prefix": "org/springframework/boot/loader/",
    "boot_loader_class_prefix": "org.springframework.boot.loader.",

    # How many places the tree registers a servlet, a filter or a listener
    # with the embedded container. The application has two filters
    # (`RequestCleanupFilter`, `RequireSslFilter`) and three servlets
    # (`SshInfoServlet`, `ProjectServlet`, and the 404 under `/servlet/*`), so
    # five is what a complete port writes, and five is the floor. A floor of
    # 3 would be the wrong number: §1.4 says *every* servlet and filter the
    # application installs is registered by the application, and a floor
    # below the count of them is a floor that a tree which ported the
    # construct for some of its registrations and left the rest unported
    # clears. There is no reading of §1.4 under which that tree is right.
    #
    # Five is reachable on both shapes §1.4 licenses, which was measured on
    # built trees rather than reasoned about, because the union is over sites
    # and not over registrations and the two come apart. Measured over the
    # union of three routes (see `servlet-registration-beans`), R3 excluded
    # so that these are the source routes alone:
    #
    #   reference                                     13  (12 R1, 1 R3)
    #   registration beans, tersest legal spelling:
    #     five one-line @Bean methods, no helpers      5  (5 R1)
    #   servlet annotations with a scan:
    #     @WebServlet/@WebFilter on all five classes   5  (5 R2)
    #   the tree this step is handed                   0
    #
    # The annotation shape is the tight one and it is tight for a reason
    # worth writing down: `AstScan` emits an `anno` record only for an
    # annotation that carries arguments, so a bare `@ServletComponentScan`
    # contributes nothing to R2 and the count is exactly one site per
    # annotated servlet or filter. That is also why R3 stays in the union --
    # on a compiled tree the scan annotation is in the constant pool, and R3
    # is the margin the annotation shape would otherwise not have.
    #
    # There is deliberately no ceiling and no per-type requirement.
    "registration_sites_min": 5,

    # The servlet-3.1 bootstrap contract this rung retires, by FQCN. Matched
    # against the resolved bytecode hierarchy as well as the source clause, so
    # an intermediate base class does not hide one.
    #
    # `ServletContextListener` is not in this set. It is a servlet-container
    # callback that a Boot application may perfectly well still register --
    # `ServletListenerRegistrationBean` exists for exactly that -- so retiring
    # it is a choice this reference made and not something the rung requires.
    # What the rung requires is that the container stop being the thing that
    # *starts* the application, and these two are the interfaces that say it
    # does.
    #
    # Both spellings of the servlet interface are named. `javax.servlet.
    # ServletContainerInitializer` is a type no tree at this rung can
    # implement and compile -- the class is not on the path once Boot 3 is --
    # so on its own it would be a gate nothing can fail, which is a gate
    # worth nothing. The jakarta spelling is the one a candidate at this rung
    # can actually leave behind: rename the import in
    # `WebAppInitializer`-shaped code and it compiles, registers nothing, and
    # never fires. The javax entry is kept beside it because a tree that
    # declares the type and does not compile is caught by route R2, which
    # reads the source clause rather than the resolved hierarchy, and that
    # tree is exactly the half-done rename this row should name.
    #
    # Measured: the 2011 baseline 1 (`WebAppInitializer implements
    # WebApplicationInitializer`), predecessor 0, reference 0.
    "retired_bootstrap": ("org.springframework.web.WebApplicationInitializer",
                          "javax.servlet.ServletContainerInitializer",
                          "jakarta.servlet.ServletContainerInitializer"),

    # --- which servlet API, asked of the API itself -------------------------
    #
    # `servlet-api-6` is the gate that says the platform moved rather than the
    # imports. It reads the API inside the deployable from two sides, because
    # a package name is not a version and this rung's most plausible near-miss
    # is a tree that found *a* jakarta servlet API and not a Servlet 6 one.
    # Jakarta Servlet 5.0 is the pure rename of Servlet 4.0 -- same types,
    # `javax` to `jakarta`, nothing added and nothing taken away -- and it
    # ships as `jakarta.servlet:jakarta.servlet-api:5.0.0` and inside Tomcat
    # 10.0.x, both of which resolve, compile and start. A tree on Servlet 5
    # renamed every import and moved nothing.
    #
    # The positive side is step 04's four programmatic-registration types,
    # in the jakarta spelling. They are still the right presence check: they
    # are what `ServletRegistrationBean` needs to exist at all, and a tree
    # whose packaged API predates them has a jar that starts and a
    # registration that cannot work.
    #
    #   ServletRegistration          / ServletContext.addServlet's return
    #   ServletContainerInitializer  / the 3.0 bootstrap callback itself
    #   annotation.WebServlet        / the annotation-driven alternative
    #   AsyncContext                 / the other half of what 3.0 added
    #
    # All four, and not "any of", because they arrive together in every
    # servlet API jar that has any of them.
    #
    # The negative side is the version discriminator, and it is three types
    # rather than a version string because there is no version string to
    # read: the API arrives inside `tomcat-embed-core` or as
    # `jakarta.servlet-api`, and the gate should not have to know which.
    # Servlet 6.0 is the first release to *remove* anything, and it removed
    # exactly these three long-deprecated types:
    #
    #   SingleThreadModel            / removed in 6.0, deprecated since 2.4
    #   http.HttpSessionContext      / removed in 6.0, deprecated since 2.1
    #   http.HttpUtils               / removed in 6.0, deprecated since 2.3
    #
    # Measured on real jars, counting classes under the servlet package and
    # asking for each of the three:
    #
    #   tomcat-embed-core 9.0.83   91 javax.servlet    all three present
    #   tomcat-embed-core 10.0.27  92 jakarta.servlet  all three present
    #   tomcat-embed-core 10.1.55  88 jakarta.servlet  none present
    #   jakarta.servlet-api 5.0.0  85 jakarta.servlet  all three present
    #   jakarta.servlet-api 6.0.0  84 jakarta.servlet  none present
    #   jakarta.servlet-api 6.1.0  84 jakarta.servlet  none present
    #
    # Three of three, both routes, at every level, with the four positive
    # markers present throughout -- which is why the positive set cannot do
    # this job and the negative set can. The 88-versus-92 difference is these
    # three plus one inner class of `HttpUtils`.
    "servlet_6_markers": ("jakarta.servlet.ServletRegistration",
                          "jakarta.servlet.ServletContainerInitializer",
                          "jakarta.servlet.annotation.WebServlet",
                          "jakarta.servlet.AsyncContext"),
    "servlet_6_removed": ("jakarta.servlet.SingleThreadModel",
                          "jakarta.servlet.http.HttpSessionContext",
                          "jakarta.servlet.http.HttpUtils"),

    # --- step 05's own: the namespace, from source to deployable ------------

    # How many of this tree's own Java files name a `jakarta.servlet` type --
    # `source_type_sites(..., by_file=True)`, which is the same two halves
    # `no_source_types` reads (every import, plus every use written with its
    # package inline) keyed on the file rather than on the type.
    #
    # Measured on the reference: 28 raw import sites, 0 type uses -- all 28
    # are imports and not one is an inline use -- across 7 files. The
    # predecessor is 0 here, with the same 28 raw sites under `javax.servlet`
    # in the same 7 files. The seven are `HttpCanonicalWebUrlProvider`,
    # `HttpRemotePeerProvider`, `ProjectServlet`, `RequestCleanupFilter`,
    # `RequireSslFilter`, `WebConfiguration` and `SshInfoServlet`.
    #
    # The floor is all 7, with no slack, and the slack is zero because the
    # number is not a census. It is the sentence. §1.1 tells the candidate
    # that the tree they were handed writes servlet types across seven files
    # and that in the migrated tree every one of those seven names the jakarta
    # types instead: seven is the figure the instruction itself prints, so a gate
    # that asked for six would be waving through a file the instruction says
    # must name the API. Flooring a measurement at its own value is a hidden
    # line -- 10 pairs on a
    # reference that has 10 pairs, say; flooring a published requirement at its own
    # value is not a line at all, it is the requirement.
    #
    # The unit is the file for the same reason, and a raw site floor could not
    # be set honestly at all. `RequireSslFilter` and `RequestCleanupFilter`
    # each import 6 types from `jakarta.servlet`, and IntelliJ's
    # organize-imports collapses a package to `.*` at 5 by default: run over
    # this reference it turns 6+6 into 1+1 and reports **18** raw sites, under
    # a site floor of 20, and a full collapse of all 7 files reports **10**.
    # So with a site floor, the candidate who did this rung exactly
    # right and then let the IDE tidy the imports it had just made them
    # rewrite would lose a 3.0 gate. The file count is **7** under all three -- 28
    # raw sites, 18 after a default tidy, 10 after a full collapse -- which is
    # §1.1's "whether you write one import per type or collapse a package to a
    # wildcard is your business; which files name the API is not", measured.
    #
    # A (file, package) pair count is 10 on this reference and survives the
    # wildcard too, but it is not the unit
    # the instruction states. `WebConfiguration` names `jakarta.servlet`
    # through a single import of `Filter`, used once as the bound of a private
    # registration helper; a port that replaces that helper with two concrete
    # overloads registers the same two filters on the same patterns in the
    # same order, still names jakarta types in all seven files, and reports 9
    # of 10. That tree does what §1.1 asks and there is no sentence it could
    # read to find out why it lost the row.
    #
    # And breadth is not what this row was carrying anyway. It is carried in
    # the same import-style-invariant way by three rows beside it:
    # `jakarta-servlet-classes` reads the constant pool and wants
    # `jakarta_servlet_own_classes_min` = 8 own classes, which no import style
    # affects; `no-javax-servlet-source` wants **zero** javax sites, so a tree
    # cannot rename two of the seven files and leave five -- the five would
    # fail it outright; and `sources-survive` refuses the tree that reaches
    # zero javax sites by deleting the files. A tree that clears all four has
    # renamed every file that named the API.
    "jakarta_servlet_source_min": 7,

    # The same question of the compiled output, own classes only. This is the
    # half that cannot be satisfied by an import the compiler erased, and the
    # half that survives a candidate who writes `jakarta.servlet.*` once and
    # uses simple names everywhere.
    #
    # Measured: reference 11 own classes name a `jakarta.servlet` type
    # (`HttpCanonicalWebUrlProvider`, `HttpRemotePeerProvider`,
    # `ProjectServlet` and its three nested types, `RequestCleanupFilter`,
    # `RequireSslFilter`, `WebConfiguration` and its anonymous 404 servlet,
    # `SshInfoServlet`); predecessor 0, with the same 11 under
    # `javax.servlet`. The floor is 8: the three nested `ProjectServlet`
    # types and the anonymous class are shapes a candidate may well not
    # write, and the eight that remain are the classes this application
    # cannot be this application without.
    "jakarta_servlet_own_classes_min": 8,

    # The EE APIs whose javax spelling must not be inside the deployable.
    # Every one of these has a jakarta counterpart that an application on
    # this rung's platform uses instead, so a copy of the javax one inside
    # the jar is either a stale container or a second API provider, and
    # neither is a tree that has moved.
    #
    # `javax.annotation` joins the set at this rung, and it is the one entry
    # here that is about this step rather than about step 05. Steps 05 and 06
    # left it out, and said so at length: `com.google.code.findbugs:jsr305`
    # packages 35 classes under `javax.annotation` -- `@Nullable`, `@Nonnull`,
    # `@CheckReturnValue` and the rest of JSR-305 -- `mini-git-server-server`
    # and `mini-git-server-httpd` each declared it outright, and a set that
    # named the package would have failed a correct step-05 tree for a
    # dependency step 05 had no business removing. This rung removes it. Both
    # declarations go, nothing in this project's source ever named the
    # package, and the reference jar carries zero classes under it.
    #
    # Note what the addition does and does not ask. It counts classes a
    # packaged archive *provides* under the package, not references to it:
    # `spring-aop`, `spring-core`, `spring-context` and `micrometer-commons`
    # between them carry 16 constant-pool references to `javax/annotation`
    # names, because Spring still supports the JSR-250 annotations when they
    # happen to be present. Those are Spring's own soft references to an API
    # that is not there, they are unremovable by anything a candidate can do
    # to this tree, and `packaged_providers` does not see them.
    #
    # Measured inside the deployable, by package and providing archive:
    #
    #   step 05's predecessor  javax.servlet 91 (tomcat-embed-core-9.0.83)
    #                javax.el 56 (tomcat-embed-el-9.0.83)
    #                javax.websocket 56 (tomcat-embed-websocket-9.0.83)
    #                javax.security.auth.message 34 (tomcat-embed-core-9.0.83)
    #                javax.annotation 50 (jsr305 35 + jakarta.annotation-api
    #                                     1.3.5's 15 javax-packaged classes)
    #   this step's predecessor  the five above: 0
    #                javax.annotation 35, jsr305-1.3.9 alone
    #   reference    all six javax packages: 0
    #                jakarta.servlet 88, jakarta.el 52,
    #                jakarta.websocket 55, jakarta.annotation 17,
    #                jakarta.security.auth.message 34
    #
    # So the predecessor fails on `javax.annotation` alone and the reference
    # is clean on all six. The set is wider than the packages the two trees
    # differ on: `javax.persistence`, `javax.validation`, `javax.transaction`,
    # `javax.inject` and `javax.enterprise` are zero in both trees and are
    # named anyway, because a candidate who reaches for a jakarta API this
    # project does not currently use should not be able to bring its javax
    # twin along beside it.
    "javax_ee_packages": ("javax.servlet", "javax.el", "javax.websocket",
                          "javax.security.auth.message", "javax.persistence",
                          "javax.validation", "javax.transaction",
                          "javax.inject", "javax.enterprise",
                          "javax.annotation"),

    # `jgit-http-jakarta`: the packaged git HTTP server, by the property
    # rather than by the version. `jgit_http_archive_stem` does double duty,
    # because for this module the artifactId and the package are the same
    # string: a class counts when the archive's filename starts with that
    # stem, which is what survives being packaged into `BOOT-INF/lib` with or
    # without a classifier, or when the class's own name is under that
    # package, which is what survives a shade into the deployable. The
    # version in the filename is deliberately not read on either route.
    #
    # Measured (61 classes in every one of the three):
    #   6.10.1  48 name javax/servlet,   0 jakarta  -- status fix, wrong API
    #   7.0.0    0                      48 jakarta
    #   7.8.0    0                      48 jakarta  -- the reference
    #
    # The floor is 40 and not 48 so that a repackaging which relocates or
    # drops part of the module does not fail a tree whose HTTP server is the
    # right one. A shade that keeps the package is counted in full by the
    # package route above and needs no slack; the slack is for the relocating
    # case. The javax side has no floor, because one class naming
    # `javax.servlet.http.HttpServlet` is one class that will not load.
    "jgit_http_archive_stem": "org.eclipse.jgit.http.server",
    "jgit_http_jakarta_min": 40,

    # `jdk-javax-intact`: the `javax.*` packages that are part of the JDK and
    # have no jakarta spelling at all. `sed -i s/javax/jakarta/g` over the
    # source tree is the most plausible wrong answer to this rung, and this
    # is the row that refuses it.
    #
    # Measured in the reference, which is to say in a tree that has finished
    # this rung: 13 sites in `HtmlDomUtil.java` under `javax.xml`
    # (`parsers.DocumentBuilder`, `parsers.DocumentBuilderFactory`,
    # `parsers.ParserConfigurationException`, `transform.OutputKeys`,
    # `transform.Transformer`, `transform.TransformerException`,
    # `transform.TransformerFactory`, `transform.dom.DOMSource`,
    # `transform.stream.StreamResult`, `xpath.XPathConstants`,
    # `xpath.XPathExpression`, `xpath.XPathExpressionException`,
    # `xpath.XPathFactory`) and 5 sites in `BlindSSLSocketFactory.java` under
    # `javax.net` (`SocketFactory`, `ssl.SSLContext`, `ssl.SSLSocketFactory`,
    # `ssl.TrustManager`, `ssl.X509TrustManager`). 18 sites, all imports,
    # 13 + 5 across those two files, and the predecessor tree has exactly the
    # same 18: this is a row nothing in the rung is supposed to move.
    #
    # The floor is the whole 18 and not a fraction of it, because §1.1 asks
    # for exactly that -- these uses stay exactly as they are, all of them --
    # and a correct port therefore arrives with all 18 still standing. The
    # arithmetic behind that is worth writing down so the next reader does not
    # redo it: the 18 sites span only 7 distinct (file, package) pairs, which
    # are `javax.xml.parsers`, `javax.xml.transform`,
    # `javax.xml.transform.dom`, `javax.xml.transform.stream` and
    # `javax.xml.xpath` in `HtmlDomUtil` and `javax.net` and `javax.net.ssl`
    # in `BlindSSLSocketFactory`. A full wildcard collapse therefore lands on
    # 7, not on 15. A floor of 15 does not tolerate wildcards by any reading;
    # it tolerates 3 sites of loss, and a tree that tidied these imports at
    # all would be rewriting two files this rung does not ask it to touch.
    #
    # This is why this row stays on raw sites while
    # `jakarta_servlet_source_min` counts distinct files. There the candidate
    # has to rewrite every import in question and tidying them afterwards is a
    # tidy of their own work, at an IDE default that does fire on that file
    # set, and §1.1 states that requirement per file. Here the largest single
    # package in either file is 4 imports, below the 5 at which
    # organize-imports collapses one, so no default reformat moves this count
    # and any movement is the candidate editing two files §1.1 told them to
    # leave alone.
    #
    # There is no matching bytecode check: `javax.xml.parsers` and
    # `javax.net.ssl` are JDK packages, so a blanket-renamed tree does not
    # compile and has no bytecode to read. That is why this row reads the
    # source and why it is priced as a tripwire rather than as a leap.
    "jdk_javax_packages": ("javax.xml", "javax.net"),
    # 15 rather than 18. 18 is the
    # reference's own count; a correct step-01 multi-catch merge can already
    # drop a subclass import (`TransformerConfigurationException` under
    # `TransformerException` is how the reference got from 19 to 18), and the
    # rung cannot see the tree it was handed. 15 keeps the tripwire's point --
    # a blanket javax -> jakarta rename lands on 0 and a wildcard collapse on
    # 7 -- with the three units of slack the arithmetic above already prices.
    "jdk_javax_sites_min": 15,

    # -----------------------------------------------------------------
    # Step 06: the logging stack. Every number below was measured on the
    # step-05 tree -- the predecessor, which is what /app holds when this
    # rung starts -- with this verifier's own ArtifactScan, or read out of
    # one scored run of it.
    # -----------------------------------------------------------------

    # The three package families this rung moves between. `logging_packages`
    # is step 04's key, reused rather than re-spelled: it is the same
    # `org.apache.log4j`, and two names for one prefix is how a widened gate
    # and its successor drift apart.
    "logback_packages": ("ch.qos.logback",),
    "slf4j_packages": ("org.slf4j",),
    "jcl_packages": ("org.apache.commons.logging",),

    # The file the audit logger lives in. Matched as a path suffix, so a
    # candidate who moves the module does not break the gate and a candidate
    # who renames the class does -- which is the right way round: the class
    # is what the instruction names.
    "audit_logger_file": "com/google/gerrit/sshd/SshLog.java",

    # Logback's appenders that write to a file. `RollingFileAppender` extends
    # `FileAppender`, and either is a correct answer to "the audit log goes to
    # a file"; the tree had a rolling one and does not have to keep it.
    "logback_file_appenders": ("FileAppender", "RollingFileAppender"),

    # The two artifacts an ordinary logback ships as. Matched on the jar name
    # with the version stripped, so a version bump does not move the gate.
    "logback_artifacts": ("logback-classic", "logback-core"),
    # Measured on the reference: logback-classic 1.5.x and logback-core
    # together put a little over 500 classes in BOOT-INF/lib. The floor is a
    # long way under that because its job is only to refuse zero and refuse a
    # stub -- `logback_artifacts` is what actually identifies the backend.
    "logback_classes_min": 100,

    # Every coordinate that is log4j 1.2 wearing a different name. See
    # `no_log4j_dep` for what each of them is and why the facade is on the
    # list. Measured in the predecessor's resolved graph: `log4j:log4j:1.2.16`
    # and `org.slf4j:slf4j-reload4j:2.0.18` (which the pom reaches by asking
    # for `slf4j-log4j12` and following Maven's relocation).
    "log4j_coords": (("log4j", "log4j"),
                     ("ch.qos.reload4j", "reload4j"),
                     ("org.slf4j", "slf4j-log4j12"),
                     ("org.slf4j", "slf4j-reload4j"),
                     ("org.slf4j", "log4j-over-slf4j")),

    # Configuration files for a backend this rung retires. `log4j2.*` is on
    # the list so that a candidate who read "retire log4j 1.2" as "upgrade to
    # Log4j 2" is told which gate refused them.
    "log4j_configs": ("log4j.properties", "log4j.xml",
                      "log4j2.properties", "log4j2.xml", "log4j2.yaml",
                      "log4j2.yml", "log4j2.json"),

    # Where this project's own resources sit inside a Spring Boot jar, and
    # the file names the two supported ways of declaring a level use.
    "boot_classes_entry_prefix": "BOOT-INF/classes/",
    "logback_configs": ("logback.xml", "logback-spring.xml",
                        "logback-test.xml"),
    "boot_property_files": ("application.properties", "application.yaml",
                            "application.yml"),

    # The level map, reduced to the entries that still steer code inside the
    # deployable. Thirteen were declared in the 2011 `log4j.properties`; eight
    # named libraries steps 01 and 02 retired, step 06 asked for the six that
    # were left, and this rung takes it to five. The root is keyed on the empty
    # string because logback spells it `<root>` and Boot's property form spells
    # it `logging.level.root`, and neither spelling belongs in a comparison.
    #
    # `net.sf.ehcache` is the entry that leaves, and it leaves for the reason
    # the other eight did: measured against the reference jar it now resolves
    # to zero classes, because this rung removed the archive that provided the
    # 169 it used to resolve to. Step 06's own header says a gate demanding
    # configuration for code that is not there is the wrong gate; this is that
    # sentence applied to step 06's own list, which would otherwise be an
    # inherited check its successor's requirements made unpassable.
    #
    # `velocity` is not in this list and was not in step 06's either: the 2011
    # file declared it, step 06 counted it among the eight it dropped because
    # Velocity reached commons-logging under that name rather than under a
    # package prefix, and this rung removes the library. It is named in
    # CAL["retired_log_categories"] below, which is the complementary
    # question the widening opens up -- five must be declared, and these two
    # must not.
    #
    # Class counts inside the reference jar, measured: com.google.gerrit 150,
    # org.apache.sshd.common 742, org.apache.sshd.server 159,
    # FileKeyPairProvider 1.
    "log_levels": {
        "": "DEBUG",
        "com.google.gerrit": "INFO",
        "org.apache.sshd.common": "WARN",
        "org.apache.sshd.server": "INFO",
        "org.apache.sshd.common.keyprovider.FileKeyPairProvider": "INFO",
    },

    # The two categories a tree that finished this rung must *not* declare.
    # Not a taste rule: a level line for a package with no classes behind it
    # is a line an operator will edit expecting it to do something. Matched on
    # the declared name exactly or as a dotted prefix of it, so
    # `net.sf.ehcache.store` fails too.
    "retired_log_categories": ("net.sf.ehcache", "velocity"),

    # The audit log: where it is, what a record looks like, and how many of
    # them each profile writes.
    #
    # The grammar is the format `SshLog.MyLayout` emits today, field for
    # field: a bracketed timestamp with milliseconds and a numeric zone, an
    # eight-hex-digit session id, the user name, the account id, then the
    # message. Reachable in logback from a `PatternLayout` over MDC
    # properties, and equally from the tree's own hand-written `Layout`
    # wrapped in a `LayoutWrappingEncoder`; the gate does not ask which.
    #
    # The floors count *records*, not lines. Measured on the reference: 195,
    # 9 and 3 physical lines, of which 193, 9 and 3 match -- the two that do
    # not are the continuation lines of a single record whose ssh command
    # contains an embedded newline, which the log writes through verbatim.
    # The floors sit under the measurement rather than on it, but not far:
    # these are deterministic replays of a fixed case list.
    "audit_log_name": "sshd_log",
    "audit_line_re": (
        r"^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d{3} [+-]\d{4}\] "
        r"[0-9a-f]{8} \S+ \S+ .+$"),
    # The two spellings that make "this appender drops
    # nothing" explicit. Measured on the reference `SshLog`:
    # `async.setDiscardingThreshold(0)` and `async.setNeverBlock(false)`.
    # The second is matched only in its *enabling* form, so a file that never
    # mentions it passes -- false is the default and restating it is optional.
    "audit_discarding_zero_re": r"setDiscardingThreshold\s*\(\s*0\s*\)",
    "audit_never_block_re": r"setNeverBlock\s*\(\s*true\s*\)",

    "audit_records_min": {"main": 150, "alt": 6, "ssl": 2},

    # The console, read by its level token rather than by its pattern. A gate
    # on the console *pattern* would be dishonest: log4j's `%x` is NDC,
    # logback has neither NDC nor `%x`, so the line format cannot survive this
    # rung intact. The level within it can. Every default pattern of both
    # stacks carries one -- log4j's `%-5p`, logback's and Boot's `%-5level` --
    # near the front of the line, so counting lines whose first
    # `console_level_scan_chars` characters hold a DEBUG token reads the root
    # level without reading the format.
    #
    # Volume alone would be the wrong measure. Measured on the
    # reference: 29229, 10352 and 9638 console lines, of which 13152, 8707 and
    # 8346 carry a DEBUG token. At Boot's default root of INFO those runs
    # would carry no DEBUG token at all -- but `main` would still emit 14716
    # lines, because 13600 of the reference's lines are continuations of ERROR
    # records and a stack trace does not shorten when DEBUG is silenced. Any
    # line floor low enough for `alt` (10352) to clear would be cleared by a
    # `main` with DEBUG switched off entirely. The DEBUG count separates
    # against zero instead, and the floor is set at roughly a third of the
    # smallest measurement so that a candidate may quieten a noisy logger or
    # two without tripping it.
    "console_debug_lines_min": {"main": 3000, "alt": 3000, "ssl": 3000},
    "console_level_scan_chars": 120,

    # The slf4j call sites the tree already had, which this rung must not
    # convert. Step 06 measured fourteen own production classes referencing
    # `org.slf4j` and floored the gate there; the reference for this rung
    # measures thirteen, so the floor comes down to thirteen with the one
    # class it lost named here: `CachePool`. Its three call sites were
    # `Enabling disk cache`, `Can't write to disk cache` and `Can't create
    # disk cache`, all three about a disk store that Caffeine does not have
    # and that this rung drops on purpose. Held at fourteen, it would be an
    # inherited check whose successor's requirements made it unpassable, and
    # the repair is to re-measure it rather than to keep a dead message alive so
    # a count holds still.
    #
    # Lowering it does not blunt it. The cheap wrong answer this whole rung
    # guards against is deleting the cache package outright, and
    # `PopulatingCache` and `SimpleCache` still log, so that tree measures
    # eleven and fails this row as well as `cache-api-intact`. A floor of 13
    # allows exactly the one deletion the rung requires.
    "slf4j_classes_min": 13,

    # And the binding underneath them, identified by its service registration
    # rather than by a package: `logback-classic` registers
    # `ch.qos.logback.classic.spi.LogbackServiceProvider` and publishes
    # nothing under `org.slf4j` at all, so a package count would read as this
    # rung's own work rather than as a regression. Measured on the
    # predecessor: exactly one archive registers it, `slf4j-reload4j`.
    "slf4j_provider_service": "org.slf4j.spi.SLF4JServiceProvider",

    # The two strings that tie this tree to a log4j-backed Velocity log
    # system. Both live in `GerritGlobalConfiguration.VelocityLifecycle`, and
    # a case-sensitive search for `log4j` finds only the second of them: the
    # class name spells it `Log4J`.
    # Held as pattern source rather than as a compiled object: `CAL` is
    # serialised into the report, and a compiled pattern is not JSON. The
    # strings below are what a reader of the report needs anyway; the
    # compiled forms live beside it.
    "velocity_log4j_re":
        r"velocity\.runtime\.log\.\w*Log4[Jj]|runtime\.log\.logsystem\.log4j",

    # -----------------------------------------------------------------
    # Step 07's own numbers.
    # -----------------------------------------------------------------

    # What has to leave the resolved graph, as (groupId, artifactId).
    # Measured by diffing `dependency:list` over all seven reactor modules
    # between the predecessor and the reference: exactly these fourteen
    # coordinates are in the first and not the second, and nothing else is.
    # Nine are declared in a pom; five arrive under one of them
    # (`antlr-runtime` under antlr, `commons-collections` and `oro` under
    # velocity, `commons-compress` under util-compress, `bcprov-jdk15` under
    # bcpg) and leave when it does.
    #
    # Absolute rather than version-gated, and each entry earns that
    # separately. Eleven of the fourteen have no release after 2011 at these
    # coordinates -- the maintained successors renamed (`commons-lang3`,
    # `commons-collections4`, `antlr4-runtime`, `org.ehcache:ehcache`,
    # `velocity-engine-core`, `bcpg-jdk18on`), so an absolute absence cannot
    # collide with a candidate who chose a current library. The three that
    # could -- `commons-io`, `commons-net` and `org.apache.commons:
    # commons-compress`, all of which still publish -- are absolute because
    # §4 of the instruction says what replaces them by name: the JDK. This
    # rung is not "upgrade the pins", it is "stop carrying a library for what
    # java.nio.file and java.util.zip already do", and a tree that swapped
    # commons-io 2.0.1 for commons-io 2.20.0 has not done it.
    #
    # `commons-codec` is deliberately absent from this list. See
    # `legacy_pins` below and the header.
    "retired_coords": (
        ("bouncycastle", "bcpg-jdk15"),
        ("bouncycastle", "bcprov-jdk15"),
        ("com.google.code.findbugs", "jsr305"),
        ("com.madgag", "util-compress"),
        ("commons-collections", "commons-collections"),
        ("commons-io", "commons-io"),
        ("commons-lang", "commons-lang"),
        ("commons-net", "commons-net"),
        ("net.sf.ehcache", "ehcache-core"),
        ("org.antlr", "antlr"),
        ("org.antlr", "antlr-runtime"),
        ("org.apache.commons", "commons-compress"),
        ("org.apache.velocity", "velocity"),
        ("oro", "oro"),
    ),

    # The nine of those fourteen that a pom in this tree actually writes down,
    # and which therefore have to be *deleted* rather than merely fall out.
    # Measured on the predecessor: seven in `mini-git-server-server/pom.xml`,
    # `jsr305` a second time in the httpd pom, and all nine again as managed
    # versions in the root pom -- seventeen declaration sites over four files.
    #
    # Asked of the pom text and not of the graph because the two questions are
    # not the same one. A `<dependencyManagement>` entry for a coordinate
    # nothing depends on resolves to nothing and is invisible to
    # `dependency:list`; it is still a 2009 version pinned in this project's
    # build, and it is still what the next person who adds the dependency
    # back will get.
    "retired_declarations": (
        ("bouncycastle", "bcpg-jdk15"),
        ("com.google.code.findbugs", "jsr305"),
        ("com.madgag", "util-compress"),
        ("commons-io", "commons-io"),
        ("commons-lang", "commons-lang"),
        ("commons-net", "commons-net"),
        ("net.sf.ehcache", "ehcache-core"),
        ("org.antlr", "antlr"),
        ("org.apache.velocity", "velocity"),
    ),

    # And the coordinate that stays. commons-codec is pinned at 1.4 (2009) in
    # the root pom's `<dependencyManagement>` and declared without a version
    # of its own in `mini-git-server-sshd/pom.xml`, and `SshUtil` has the
    # tree's only two call sites. Removing both declarations does not remove
    # the coordinate: measured on the reference, `commons-codec:commons-codec:
    # 1.18.0` is still in the resolved graph and still in `BOOT-INF/lib`,
    # under `org.eclipse.jgit`. So the absence this rung can ask for is the
    # *pin*, not the artifact -- which is why this is asked of the pom text
    # rather than being another entry in `retired_coords`.
    #
    # A bare pair and not a version pattern, because §1.2 does not describe a
    # range of bad versions. It says: delete the `<version>`, "and do not
    # replace it with a newer pin. Whatever JGit resolves to is the version. A
    # declaration with no version of its own is not a pin and is not asked
    # about either way." The rule the gate implements is that rule: any
    # non-empty `<version>` on this coordinate is a hit, in a `<dependencies>`
    # block or a `<dependencyManagement>` one, whatever its text -- the fossil
    # `1.4`, a current `1.18.0`, a range `[1.4,2)`, a `${codec.version}` that
    # expands to one of those or to nothing at all. A pattern would have had
    # to enumerate the shapes a pin can take, and each shape it did not cover
    # would be a version this project freezes JGit's codec to by nearest-wins
    # while the row reported clean.
    #
    # Measured: the predecessor writes both sites, one of which is a pin; the
    # reference declares the coordinate nowhere at all and so has nothing to
    # match, and neither does a candidate who deletes only the `<version>` and
    # leaves the sshd pom's declaration standing.
    "legacy_pins": (("commons-codec", "commons-codec"),),

    # The archives, by filename stem, that must not be inside the deployable.
    # Measured: 54 nested archives in the predecessor's jar, 45 in the
    # reference's, and these fourteen stems account for every one of the
    # difference except `args4j-2.0.12.jar`, which is replaced rather than
    # dropped -- twelve of them are packaged in the predecessor, the two
    # bouncycastle entries being `provided`. `bcpg-jdk15` is `provided` and was never packaged; it is here
    # so that the list is the same list as `retired_coords` and a reader does
    # not have to work out why one is shorter.
    #
    # A stem match is a prefix on the filename, so `commons-lang-` does not
    # match `commons-lang3-3.19.0.jar` and `antlr-` does not match
    # `antlr4-runtime-4.13.2.jar`. That is the intended reading: the rung
    # retires these artifacts, not every artifact whose name starts the same
    # way.
    "retired_archive_stems": (
        "antlr-", "antlr-runtime-", "bcpg-jdk15-", "bcprov-jdk15-",
        "commons-collections-", "commons-compress-", "commons-io-",
        "commons-lang-", "commons-net-", "ehcache-core-", "jsr305-",
        "oro-", "util-compress-", "velocity-",
    ),

    # The same retirement asked of packages rather than of filenames, for the
    # candidate who unpacks a jar into the build output or shades one in.
    # Measured on the reference: zero archives inside the deployable provide
    # any class under any of these, `org.apache.commons.collections` included
    # and probed separately. Eleven archives provide 1292 classes between them
    # on the predecessor.
    #
    # One package per retired artifact, so that every one of the fourteen is
    # asked twice -- once by the filename its publisher gave it, once by what
    # is inside it. `org.apache.commons.codec` is the exception and is not
    # here, because jgit 7 brings the coordinate back at 1.18.0 and a tree
    # that ships it is correct; the source list below asks that one instead.
    # `javax.annotation` is here and also joins `javax_ee_packages` above,
    # `jsr305-1.3.9.jar` having been its only provider. The two rows overlap
    # on that one package and that is fine: they fail for different reasons
    # and a reader wants both.
    "retired_packages": (
        "antlr", "com.madgag.compress", "javax.annotation",
        "net.sf.ehcache", "org.antlr", "org.apache.commons.collections",
        "org.apache.commons.compress", "org.apache.commons.io",
        "org.apache.commons.lang", "org.apache.commons.net",
        "org.apache.oro", "org.apache.velocity", "org.bouncycastle",
    ),

    # The same retirement asked of this project's source and of its own
    # bytecode, where `org.apache.commons.codec` *is* in scope: the coordinate
    # survives, the call sites do not.
    #
    # Measured on the predecessor: eleven source files name one of these
    # (five ehcache, two velocity, and `LocalDiskRepositoryManager` and
    # `SshUtil` between them naming commons-io, util-compress's
    # `com.madgag.compress` and commons-codec), and eleven compiled classes
    # reference one. On the reference both counts are zero. The five
    # coordinates with no call site at all -- antlr, bouncycastle,
    # commons-lang, commons-net, javax.annotation -- are in the list anyway,
    # so that a candidate who *adds* a use of one while deleting its
    # declaration is caught by the same row.
    "retired_source_packages": (
        "antlr", "com.madgag.compress", "javax.annotation",
        "net.sf.ehcache", "org.antlr", "org.apache.commons.codec",
        "org.apache.commons.collections", "org.apache.commons.compress",
        "org.apache.commons.io", "org.apache.commons.lang",
        "org.apache.commons.net", "org.apache.oro", "org.apache.velocity",
        "org.bouncycastle",
    ),

    # What replaces Ehcache, named because §5 of the instruction names it.
    # The precedent is step 06's `logback-packaged`: where the rung's sentence
    # says which library, the gate says which library, and where it does not
    # -- see `cache_bound_calls` below, which takes any of Caffeine's three
    # expiry policies -- the gate stays out of the design.
    # Measured on the reference: `com.github.ben-manes.caffeine:caffeine:
    # 3.2.4` resolves (from Boot's BOM, so no version is written in any pom)
    # and provides 718 classes inside the deployable.
    "caffeine_group": "com.github.ben-manes.caffeine",
    "caffeine_version_re": r"^3\.",
    "caffeine_packages": ("com.github.benmanes.caffeine",),
    "caffeine_classes_min": 400,

    # The cache layer that has to survive the port, measured on both trees.
    #
    # This is the rung's central anti-deletion instrument, and it is a floor
    # on a package rather than a list of required class names for a reason:
    # thirteen types are declared under `com.google.gerrit.server.cache` in
    # the predecessor and thirteen in the reference, but they are not the same
    # thirteen -- `EvictionPolicy` and `ProxyEhcache` go, `ProxyCache` and a
    # private failure type arrive. A gate naming `ProxyEhcache` would forbid
    # the port; a gate naming `ProxyCache` would require this particular one.
    # Nine is the floor: it is the ten source files the reference ships minus
    # one, which leaves room for a candidate who folds the lazily-bound holder
    # into `CacheProvider` and none for one who deletes the package.
    "cache_package": "com.google.gerrit.server.cache",
    "cache_types_min": 9,

    # The four API surfaces inside it that are named, because each is a
    # sentence in the instruction. `Cache` is the tenant-facing interface and
    # its five methods are what every consumer of a cache in this tree would
    # call if any existed; `NamedCacheBinding` is the declaration DSL and its
    # three methods are the knobs a `CacheModule` sets; `CacheModule.core` is
    # how a module declares one. Measured on both trees: 5, 3 and 2
    # declarations respectively, unchanged by the port.
    #
    # `diskLimit` and `evictionPolicy` were the fourth and fifth knobs and are
    # deliberately *not* here. Caffeine is in-memory only and its admission
    # policy is not selectable, so both knobs lost their meaning at this rung;
    # requiring them would be requiring the candidate to keep dead
    # configuration to pass a gate.
    "cache_api": (
        ("com/google/gerrit/server/cache/Cache.java",
         ("get", "put", "remove", "removeAll", "getTimeToLive")),
    ),
    "cache_dsl": (
        ("com/google/gerrit/server/cache/NamedCacheBinding.java",
         ("memoryLimit", "maxAge", "populateWith")),
        ("com/google/gerrit/server/cache/CacheModule.java",
         ("core",)),
    ),

    # The bounds the pool applies when it builds a cache, as call names in the
    # cache package's source. Two questions, each answered by any member of a
    # set, because Caffeine spells each bound more than one way and the rung
    # has no opinion on which: a size bound (`maximumSize` counts entries,
    # `maximumWeight` weighs them) and an expiry (`expireAfterWrite` is what
    # the predecessor's `timeToLiveSeconds` meant, `expireAfterAccess` and the
    # `expireAfter(Expiry)` form are the other two).
    # Measured on the reference: `maximumSize` and `expireAfterWrite`, both in
    # `CachePool.Factory.open`.
    "cache_bound_calls": ("maximumSize", "maximumWeight"),
    "cache_expiry_calls": ("expireAfterWrite", "expireAfterAccess",
                           "expireAfter"),

    # And the configuration those bounds are read out of, which is the half a
    # candidate who hard-codes `maximumSize(1024)` would lose. The section and
    # the two keys are the tree's own spelling, lower-cased as JGit's Config
    # reads them, and they are asked of the cache package's source text
    # because a string literal with no dot in it is not in the AST and not in
    # the constant-pool view `refs()` builds.
    # Measured on both trees: all three present in `CachePool`, alongside two
    # config reads (`Config.getInt` and `ConfigUtil.getTimeUnit`).
    "cache_config_keys": ('"cache"', '"memorylimit"', '"maxage"'),
    "cache_config_calls": ("getInt", "getLong", "getString", "getBoolean",
                           "getTimeUnit", "getEnum"),
    "cache_config_calls_min": 2,

    # And the type that carries the configuration, as the cache package's own
    # compiled classes name it. The keys above are raw source text, which a
    # comment satisfies, and the call names above are AST call names, which
    # `getInt` on anything satisfies; between them they do not establish that
    # a `Config` ever reached the code that builds the caches. The constant
    # pool does. Measured on both trees: `CachePool` declares a
    # `private final Config config` field and takes one in its constructor, so
    # the descriptor is there whichever cache library is underneath -- the
    # predecessor names it over Ehcache and the reference over Caffeine, which
    # is what makes this half a hold on the port rather than a second way of
    # asking for Caffeine.
    "cache_config_types": ("org.eclipse.jgit.lib.Config",),

    # The two files this rung could gut without any of the 428 noticing, with
    # the floor step 02 put under `CookieBase64` and for step 02's reason.
    #
    # `SshUtil` declares exactly three methods in the pinned tree, in the
    # predecessor and in the reference: `parse`, `toOpenSshPublicKey` and the
    # nested `parseOpenSSHKey`. Nothing in this tree calls the first two --
    # step 02 measured that and left them alone -- and they hold this rung's
    # only two commons-codec call sites, so "delete both methods" passes every
    # absence gate in this file and every observation in the suite.
    #
    # `LocalDiskRepositoryManager` expands any `*.zip` under the base path
    # before the first repository is opened. No fixture ships a zip, so the
    # same is true of deleting the scan.
    #
    # A declared-method floor is the wrong instrument for this second file and
    # the measurement says so: the predecessor expands zips in four library
    # calls and declares eleven methods, the reference expands them in four
    # private helpers and declares fifteen, and a candidate who deletes the
    # expansion altogether lands back at eleven. So the row asks the two
    # things that are actually true of a tree that still expands zips and
    # false of one that does not -- the class's own bytecode names a JDK zip
    # API, and its source still says which files it is looking for.
    #
    # Two packages are accepted for the first half because there are two JDK
    # ways to read a zip and the rung has no opinion: `java.util.zip`, which
    # is what the reference uses, and the zip filesystem provider reached
    # through `java.nio.file.FileSystems`. Measured: the reference's class
    # names `java.util.zip`; the predecessor's names neither, because it
    # named `org.apache.commons.io` and `com.madgag.compress` instead.
    # Measured on both trees: `SshUtil` declares exactly three methods, and
    # the reference's compiled class names `java.util.Base64` where the
    # predecessor's named `org.apache.commons.codec`. Three is therefore a
    # floor that a candidate cannot reach by deleting either orphan method,
    # and the decoder half is what separates "ported the two call sites" from
    # "deleted the two call sites" -- both of which satisfy every absence row
    # in this file on their own.
    "sshutil_file": "com/google/gerrit/sshd/SshUtil.java",
    "sshutil_class": "com.google.gerrit.sshd.SshUtil",
    "sshutil_methods_min": 3,
    "sshutil_decoder_types": ("java.util.Base64",),
    "zip_file": "com/google/gerrit/server/git/"
                "LocalDiskRepositoryManager.java",
    "zip_api_packages": ("java.util.zip", "java.nio.file.FileSystems"),
    # The literal the class still has to write, and it takes the extension
    # with or without its dot. The reference writes `endsWith(".zip")` and the
    # predecessor writes `new String[]{"zip"}`, so a pattern requiring the dot
    # separates the two trees -- and separates them on a spelling, not on the
    # property. The bytecode half above is what discriminates here; this half
    # is an anti-deletion corroboration, and it should not fail a candidate
    # who ports the walk faithfully and keeps the predecessor's spelling of
    # the extension.
    "zip_suffix_re": r"\.?zip[\"']",
    "zip_scan_class": "com.google.gerrit.server.git."
                      "LocalDiskRepositoryManager",

    # args4j, the one retirement that is a version and nothing else. 2.0.12
    # (2009) to 2.37: measured on both trees, thirteen own classes reference
    # `org.kohsuke.args4j` and the same thirteen do afterwards, because the
    # four annotations and two types this tree uses are spelled identically in
    # both releases. The floor is that count, so a candidate who "retires"
    # args4j by deleting the CLI module's option parsing fails here.
    # The pattern accepts 2.33 and later -- the first release built for a JDK
    # this ladder targets -- rather than 2.37 exactly, because pinning the
    # reference's own version would be pinning the day it was authored.
    "args4j_version_re": r"^2\.(3[3-9]|[4-9][0-9])",
    "args4j_packages": ("org.kohsuke.args4j",),
    "args4j_classes_min": 13,

    # The floor under every "nothing inside the deployable" row in this file.
    # An absence over an empty archive list is what a build that produced no
    # jar says, in the same words a clean tree uses. Measured: 54 nested
    # archives in the predecessor's deployable and 45 in the reference's, so
    # 20 is well under either and well over zero. `build-and-artifact` fails
    # first when there is no jar at all; this catches the narrower case of a
    # jar that was produced but packages nothing.
    "packaged_archives_min": 20,

    # --- step 08's own calibration ------------------------------------------
    #
    # The live half first. Both frame groups were read off real thread dumps
    # of both reference trees rather than written from the source, and both
    # are an AND of ORs (see `gate_lib.on_stack`): one group says "this thread
    # is in this project's own code for that subsystem", the other says "and
    # it is inside the library that does the work". Either alone is reachable
    # from somewhere else -- an idle sshd session thread sits in
    # `com.google.gerrit.sshd` all day, and JGit frames appear under the HTTP
    # side too.
    #
    # Measured stacks, reference and predecessor alike:
    #   com.google.gerrit.sshd.AbstractGitCommand.service -> ... ->
    #   org.eclipse.jgit.transport.UploadPack.sendAdvertisedRefs
    "cmd_frames": (("com.google.gerrit.sshd.",),
                   ("org.eclipse.jgit.transport.",)),

    # And on the servlet side. Two spellings in the first group because the
    # frame that is always present is Tomcat's filter chain, while
    # `HttpServlet.service` is present only on the stacks that reached the
    # servlet itself -- either satisfies the group, and a container that is
    # not Tomcat still has the second.
    "req_frames": (("jakarta.servlet.http.HttpServlet.service",
                    "org.apache.catalina.core.ApplicationFilterChain."),
                   ("com.google.gerrit.httpd.",)),

    # How many virtual threads the whole JVM should show at dump time, asked
    # once and without reference to what any of them is doing. The reference
    # has six: two running commands, two parked on the queue's permit, two
    # serving requests. Four, because two of the six are a consequence of the
    # semaphore this particular solution uses and no instruction requires one.
    # The predecessor has zero.
    "virtual_threads_min": 4,

    # The interactive bound, measured rather than declared. `probe.sh` offers
    # four `git-upload-pack` sessions at once into a fixture configured with
    # `sshd.threads = 4` and `sshd.batchThreads = 2`, which leaves two permits
    # for interactive work. Both reference trees run exactly two and queue the
    # other two. The floor is 1 and not 2 because a loaded shared machine can
    # leave the second session short of `UploadPack` when the dump is taken;
    # the ceiling is what the row is about.
    "commands_offered": 4,
    "inflight_commands": (1, 2),

    # Where the excess waits (step 08 instruction section 1.4, "Waiting happens
    # on the command's own thread, not on the caller's"). The caller is the SSH
    # session's i/o thread: it is the thread with Apache SSHD's inbound-message
    # frames on its stack, which every i/o factory routes through, and a
    # command's own thread never has them. Such a thread with this project's
    # code on its stack and not RUNNABLE is parked inside the hand-off -- a
    # submit that blocks on a permit (`setConcurrencyLimit`, a semaphore taken
    # before handing the task over, a blocking `put`). Measured
    # on the dumps of three trees under this probe: the reference
    # 0 such threads; the predecessor 0 (its excess sits in the executor's
    # queue, on no thread); the reference with its permit moved from
    # `Task.run` into `Executor.submit` 2, both
    # `sshd-SshDaemon[...]-nio2-thread-N` in WAITING (parking) under
    # `Semaphore.acquire <- WorkQueue$Executor.submit <- BaseCommand.startThread
    # <- CommandFactoryProvider$Trampoline.start`, while every other row of
    # this rung passes it. RUNNABLE is
    # excluded so that a session still authenticating when the dump is taken
    # is not read as a parked one.
    "io_frames": (("org.apache.sshd.common.session.helpers."
                   "AbstractSessionIoHandler.messageReceived",
                   "org.apache.sshd.common.session.helpers."
                   "AbstractSession.messageReceived"),
                  ("com.google.gerrit.",)),

    # --- and the structural half --------------------------------------------
    #
    # The JDK pool types, asked of the bytecode and of the written source.
    # `ForkJoinPool` is in the list because it is the one a candidate reaches
    # for when told "use a modern executor" -- it is also the virtual-thread
    # scheduler, but nothing in this project should be naming it directly.
    # Measured: the predecessor names `ScheduledThreadPoolExecutor` twice
    # (`WorkQueue$Executor`'s supertype, and the `extends` clause in the
    # source); the reference names none of the three.
    "pool_types": ("java.util.concurrent.ThreadPoolExecutor",
                   "java.util.concurrent.ScheduledThreadPoolExecutor",
                   "java.util.concurrent.ForkJoinPool"),

    # The `Executors` factories that hand back a pool of platform threads. The
    # implementation type never appears in the caller, so this is the only way
    # to see them. Deliberately *not* in the list:
    # `newSingleThreadScheduledExecutor`, because a virtual-thread scheduler
    # still needs one platform thread to do the timing -- Spring's
    # `SimpleAsyncTaskScheduler` has one internally -- and
    # `newVirtualThreadPerTaskExecutor`, which is the answer rather than the
    # problem. Neither tree calls any of the five.
    "pool_factories": ("newFixedThreadPool", "newCachedThreadPool",
                       "newScheduledThreadPool", "newSingleThreadExecutor",
                       "newWorkStealingPool"),

    # Thread factories, asked as `implements` and not as a reference:
    # `Thread.ofVirtual().factory()` *returns* a `ThreadFactory` and is a
    # perfectly good answer, so the row is about writing one. Measured: three
    # anonymous implementations in the predecessor -- `WorkQueue$Executor$1`,
    # `CommandExecutorQueueProvider$1`, `StreamCommandExecutorProvider$1`,
    # none of which has an `implements` clause in the source -- and none in
    # the reference. `setThreadFactory` is the other end of the same wire and
    # exists on nothing but a pool: four calls in the predecessor, none in the
    # reference. `newThread` is *not* here, for the same reason the type is
    # not: it is what a caller does to a virtual factory too.
    "thread_factory_types": ("java.util.concurrent.ThreadFactory",),
    "factory_calls": ("setThreadFactory",),

    # `Thread.setPriority` is specified to do nothing on a virtual thread and
    # `getPriority` there always answers `NORM_PRIORITY`. Two calls in the
    # predecessor, both `MIN_PRIORITY` on a batch queue
    # (`CommandExecutorQueueProvider.java:60`,
    # `StreamCommandExecutorProvider.java:49`); none in the reference.
    "priority_calls": ("setPriority",),

    # The one `new Thread` that stays. `Runtime.addShutdownHook` takes a
    # `Thread` by signature and a hook that is not joined is a hook that did
    # not run, so the site is excused -- by its *file* calling
    # `addShutdownHook`, not by the file's name, so that a candidate who moves
    # the hook keeps the exemption. Measured: two production `new-thread`
    # nodes in the predecessor, of which `MiniGitServer.java:85` is the hook
    # and `BaseCommand.java:238` is the per-command worker this rung exists to
    # move; one in the reference, the hook.
    "shutdown_hook_call": "addShutdownHook",

    # The third way to build a platform thread, and the one a rewrite reaches
    # for once `new Thread(...)` and `extends Thread` are refused:
    # `Thread.ofPlatform()`, which is a 21 API and so is only available to a
    # tree that has already done this rung's toolchain half. Refused under the
    # same file-scoped exemption as the other two, because
    # `Thread.ofPlatform().unstarted(...)` is a perfectly good way to write the
    # shutdown hook. Neither tree calls it.
    "platform_builder_calls": ("ofPlatform",),

    # Saying it out loud, three ways, any one of which satisfies the row.
    # Boot's own property is the canonical answer and moves the servlet
    # container and every `TaskExecutor` Boot owns; the four calls are the JDK
    # route and Spring's builder route; the two packages are where
    # `SimpleAsyncTaskExecutor` and `SimpleAsyncTaskScheduler` live. Measured:
    # the predecessor has none of the three, the reference has two (a
    # `setVirtualThreads` call in `WorkQueue.java` and a
    # `org.springframework.scheduling` reference from `WorkQueue$Executor`).
    "virtual_property": "spring.threads.virtual.enabled",
    "virtual_calls": ("ofVirtual", "startVirtualThread",
                      "newVirtualThreadPerTaskExecutor", "setVirtualThreads"),
    "task_api": ("org.springframework.core.task",
                 "org.springframework.scheduling"),

    # The task registry, as (simple name, kind, required supertypes, required
    # methods by name and arity). Both trees declare all of it; this is the
    # anti-deletion row, and every entry in it is something no observation in
    # the 428 reads.
    #
    # What is deliberately absent:
    # `WorkQueue.Executor` is not required as a type, and neither `submit` nor
    # `schedule` is required as a method: the predecessor inherits both from
    # `ScheduledThreadPoolExecutor` and declares neither, so requiring them
    # would be requiring this rung's own answer -- and their callers are in
    # the tree and would not compile if they went missing, which makes the
    # build the gate for those two. `Task implements RunnableScheduledFuture`
    # is not required either: a tree that stops extending a scheduled pool may
    # reasonably hand back a plainer future. `Lifecycle` is required as a type
    # with `start`/`stop`, but not as a `SmartLifecycle` -- step 03's
    # `container-lifecycle`, held one row up, is where the interface is asked
    # about.
    "queue_api": (
        ("WorkQueue", "class", (),
         (("getDefaultQueue", 0), ("createQueue", 2), ("getTasks", 0),
          ("getTask", 1), ("stop", 0))),
        ("Lifecycle", "class", (), (("start", 0), ("stop", 0))),
        ("CancelableRunnable", "interface", ("Runnable",), (("cancel", 0),)),
        ("Task", "class", (),
         (("getTaskId", 0), ("getState", 0), ("cancel", 1), ("isCancelled", 0),
          ("isDone", 0), ("run", 0), ("toString", 0))),
        ("State", "enum", (), ()),
        ("ProjectTask", "class", ("ProjectRunnable",),
         (("getProjectNameKey", 0), ("getRemoteName", 0),
          ("hasCustomizedPrint", 0))),
    ),

    # The six states a task can be in, which is the part of the registry
    # `show-queue` prints and the part a rewrite is most likely to collapse
    # into a boolean. Read out of the source text, because neither scan
    # records an enum's constants: the AST emits no field nodes and the class
    # file's constant pool carries the names nowhere this verifier reads. Read
    # out of the *constant list* and not the file -- see
    # `_enum_constant_words` -- so that a javadoc or a `switch` label naming a
    # state the enum no longer declares satisfies nothing, and paired with the
    # structural requirement that `State` is still an enum scoped to the queue
    # type and that `Task.getState()` is still declared.
    "queue_states": ("DONE", "CANCELLED", "RUNNING", "READY", "SLEEPING",
                     "OTHER"),

    # The three `gerrit.config` keys that size the queues, and the config-read
    # calls each must sit beside. A key with no dot in it is a string literal
    # and appears in neither scan, so this is a source-text question; pairing
    # it with a call in the same file is what a comment cannot satisfy.
    # Measured: both trees read all three,
    # `CommandExecutorQueueProvider.java` (`threads`, `batchThreads`) and
    # `StreamCommandExecutorProvider.java` (`streamThreads`).
    "concurrency_keys": ("threads", "batchThreads", "streamThreads"),
    "config_read_calls": ("getInt", "getBoolean", "getString"),

    # --- step 10's own ------------------------------------------------------

    # The Boot 4 line, read by `boot-4`. This is the pin `boot_version_re`
    # above stopped being when it was widened so the floor could keep its
    # hold: one row says Boot supplies the framework, and this one says which
    # Boot.
    #
    # `^4\.` and not `^4\.1\.`, for the reason every version pin on this
    # ladder is a line rather than a release: 4.0 and 4.1 are one platform,
    # every convention this rung asks for arrived in 4.0, and a candidate who
    # lands on 4.0.5 has done this step. Measured: the reference resolves
    # fourteen `org.springframework.boot` coordinates, all 4.1.1; the tree
    # this step is handed resolves six, all 3.5.16, and fails on every one.
    "boot4_version_re": r"^4\.",

    # Six: one under what the *floor* resolves, and nothing like what the
    # reference does. This number cannot separate the two trees and is not
    # meant to. The
    # reference resolves fourteen because Boot 4 broke Boot 3's single
    # `spring-boot-starter-web` into `spring-boot-webmvc`,
    # `spring-boot-http-converter`, `spring-boot-jackson`,
    # `spring-boot-servlet`, `spring-boot-tomcat`, `spring-boot-web-server`
    # and the starters over them -- the count rose for a reason that has
    # nothing to do with whether a tree adopted anything, and requiring
    # fourteen would be requiring one arrangement of starters.
    # `modular-starters` asks that question properly, by name.
    #
    # It is here as the anti-vacuity half of `boot-4`: "no coordinate under
    # this groupId is off the 4 line" is true of a tree that resolves none,
    # and the version is what discriminates. Measured: the floor resolves
    # seven -- spring-boot, spring-boot-autoconfigure, spring-boot-starter,
    # spring-boot-starter-json, spring-boot-starter-logging,
    # spring-boot-starter-tomcat, spring-boot-starter-web. Six is one under
    # that, which is where the absence stops being informative and still
    # leaves a candidate room to resolve one artifact fewer than step 09's
    # arrangement did.
    "boot4_artifacts_min": 6,

    # The Framework 7 line and its floor, read by `framework-7`: the same pair
    # of statements one layer down. `spring_version_re` above accepts 6 or 7
    # so that the floor keeps `inherited-di-hold`, and this says which.
    #
    # Measured: the reference resolves seven `org.springframework` coordinates
    # at 7.0.9 -- spring-aop, spring-beans, spring-context, spring-core,
    # spring-expression, spring-web, spring-webmvc. The floor resolves eight at
    # 6.2.19: those seven plus `spring-jcl`, which Framework 7 dropped. Five is
    # the anti-vacuity floor, under both counts for `boot4_artifacts_min`'s
    # reason and under the floor's eight so that a tree legitimately resolving
    # fewer artifacts than either control is not failed for the count.
    "spring7_version_re": r"^7\.",
    "spring7_artifacts_min": 5,

    # JUnit 6, asked of both of its groupIds, read by `junit-6`.
    # `org.junit.jupiter` is the programming model and `org.junit.platform` is
    # what runs it; since JUnit 6.0 the two share a version, where the
    # platform's own line was 1.x throughout JUnit 5. So a tree on 5.10.2 /
    # 1.10.2 fails both halves, and a tree that moved the aggregate and left
    # the platform behind fails the second.
    #
    # Measured: the reference resolves six coordinates, all 6.0.3 --
    # junit-jupiter, junit-jupiter-api, junit-jupiter-engine,
    # junit-jupiter-params, junit-platform-commons, junit-platform-engine. The
    # floor resolves the same six at 5.10.2 and 1.10.2. Two is the anti-vacuity
    # floor: a tree needs a model and a runner, and no arrangement of JUnit
    # resolves fewer than two coordinates across these two groups.
    #
    # Nothing here pins a JUnit version in a pom, and the reference's poms name
    # none. It arrives from `spring-boot-dependencies`, which manages JUnit
    # since Boot 4 -- which is why step 09's reference imported `junit-bom`
    # ahead of it and this one does not. That is the point of asking: a
    # candidate who moved the platform is handed this, and one who moved the
    # platform and then pinned `junit-bom` back to 5.x has said something the
    # row is entitled to notice.
    "junit6_version_re": r"^6\.",
    "junit_groups": ("org.junit.jupiter", "org.junit.platform"),
    "junit6_artifacts_min": 2,

    # The bom whose whole job was to name a JUnit version, and the shape of a
    # property that does the same thing by another route. Both are read off
    # the poms, not the graph: section 1.1 asks that the reactor stop naming a
    # version, and the graph cannot tell a version nobody named from a version
    # named correctly. `junit-bom` is matched by artifactId alone because its
    # groupId is `org.junit`, which is neither of the two above and is not
    # worth a third entry there -- nothing else in Central publishes that
    # artifactId, and a candidate who vendored something that did would be
    # naming a JUnit version anyway.
    #
    # Measured: the floor names one (the `junit-bom` 5.10.2 import step 09's
    # reference declares); the reference names none.
    "junit_bom_artifact": "junit-bom",
    "junit_version_prop_re": r"(?i)junit.*version$",

    # The groups `spring-boot-dependencies` manages, read by `boot-4`'s
    # fourth clause. A prefix match, so `org.springframework.boot` and
    # `org.springframework.security` come in under `org.springframework` and
    # `org.junit.jupiter` and `org.junit.platform` under `org.junit`.
    #
    # Measured against `spring-boot-dependencies:4.1.1`'s own
    # `<dependencyManagement>`: every prefix below appears there, and none of
    # the coordinates this reactor pins for itself does. That second half is
    # the load-bearing one -- `com.madgag:org.eclipse.jgit`,
    # `org.apache.sshd:sshd-*` and `args4j:args4j` are not managed by anything
    # and *must* keep their versions, so a prefix that swept one of them in
    # would fail a correct tree. `org.apache.tomcat.embed` is written out in
    # full for exactly that reason: `org.apache.sshd` shares no prefix with it
    # and `org.apache` alone would take both.
    #
    # `jakarta.` is a trailing-dot prefix and matches nothing else; this
    # reactor declares no jakarta coordinate today and a candidate who adds a
    # servlet API by hand is adding one the platform already manages, which is
    # the mistake the row is for.
    "managed_groups": [
        "org.springframework", "org.junit", "org.jspecify",
        "ch.qos.logback", "org.slf4j", "org.apache.tomcat.embed",
        "com.fasterxml.jackson", "jakarta",
    ],

    # Anti-vacuity for that clause. A tree that declares no managed
    # dependency at all names no managed version either, and would pass an
    # absence. Measured: the reference declares six -- `spring-boot-starter-
    # webmvc`, `spring-context` twice, `spring-web`, `junit-jupiter` and
    # `jspecify` -- every one of them without a version. Four leaves room for
    # a candidate who arranges its modules differently and still refuses a
    # tree that reached the absence by emptying the poms.
    "managed_unversioned_min": 4,

    # The one type that tells Servlet 6.1 from Servlet 6.0, read by
    # `servlet-api-6-1` out of the deployable.
    #
    # Measured across four jars in one container, so the standalone API and a
    # container's own copy are counted the same way. Counts are of
    # `jakarta.servlet*` classes in the jar:
    #
    #   jakarta.servlet:jakarta.servlet-api:6.0.0            84
    #   jakarta.servlet:jakarta.servlet-api:6.1.0            84
    #   org.apache.tomcat.embed:tomcat-embed-core:10.1.55    88   (Servlet 6.0)
    #   org.apache.tomcat.embed:tomcat-embed-core:11.0.24    89   (Servlet 6.1)
    #
    # Present in both 6.1 jars and in neither 6.0 jar: exactly one type,
    # `jakarta.servlet.http.HttpSession$Accessor`. That is the marker, and it
    # is a real 6.1 addition -- the supported way to hold a reference to a
    # session across threads, which is what a virtual-thread server wants.
    #
    # There is no negative marker, and that is the measurement rather than an
    # omission: nothing present in both 6.0 jars is absent from both 6.1 jars.
    # The standalone API's 6.0-to-6.1 difference does contain one further name,
    # `jakarta.servlet.http.Cookie$1`, and it is not usable -- a
    # compiler-generated class present in the 6.0 standalone jar and in neither
    # Tomcat, so a row reading it would be reading the API jar's javac. This
    # row is therefore positive-only, and `servlet-api-6` above, which has both
    # halves, is what goes on refusing an API older than 6.0.
    "servlet_6_1_marker": "jakarta.servlet.http.HttpSession$Accessor",

    # The two companions that stop a one-class answer.
    #
    # A positive row on a single type is satisfied by a single type, and a
    # deployable is a place a single type can be put. `HttpSession$Accessor`
    # is an interface with two methods and no dependencies; a candidate who
    # could not move the container could write it, drop it in, and pass a row
    # about Servlet 6.1 with Servlet 6.0 on the classpath. So the archive that
    # carries the marker has to look like a servlet API and not like a note:
    # >=20 `jakarta.servlet*` classes of its own, which is
    # `servlet_api_classes_min`'s number for the same reason -- 20 is a
    # quarter of the smallest real one (84) and four times what any plausible
    # shim holds.
    #
    # And no archive may provide the pre-6.1 API beside it. `HttpSession` is
    # in every servlet API ever published, so "provides HttpSession but not
    # its 6.1 nested type" is exactly the shape of a 6.0 jar; a deployable
    # holding one of those and one 6.1 jar has two servlet APIs and a
    # classloader deciding which `HttpServletRequest` the program sees.
    # `no-javax-ee-apis-packaged` catches the javax-spelled version of this
    # mistake and cannot see this one, because both jars here are jakarta.
    #
    # Measured: the reference packages `HttpSession` from
    # `tomcat-embed-core-11.0.24.jar` and from nowhere else, and that jar
    # holds 89 `jakarta.servlet*` classes. The floor packages it from
    # `tomcat-embed-core-10.1.55.jar`, which holds 88 and no marker -- the
    # floor fails this row on the marker's absence, before either companion
    # is reached.
    "servlet_6_1_anchor": "jakarta.servlet.http.HttpSession",
    "servlet_6_1_provider_min": 20,

    # The starter rename, read by `modular-starters` off the resolved graph.
    #
    # Boot 4 split `spring-boot-starter-web` into modules and deprecated the
    # aggregate in favour of `spring-boot-starter-webmvc`. Upstream says so in
    # its own words: `spring-boot-starter-web:4.1.1` still publishes, and that
    # pom's `<description>` reads "... (deprecated in favor of
    # spring-boot-starter-webmvc)". So this is a migration step a tree can
    # decline while still building and still passing every version row above,
    # which is exactly what makes it worth a row of its own rather than a line
    # in someone else's.
    #
    # Distinguishable from the resolved graph alone, with no pom parsing: Boot
    # 4's `spring-boot-starter-web` depends on `spring-boot-starter-jackson`,
    # `spring-boot-starter-tomcat`, `spring-boot-http-converter` and
    # `spring-boot-webmvc` -- and not on `spring-boot-starter-webmvc`.
    # Measured: the reference resolves `spring-boot-starter-webmvc` and
    # `spring-boot-webmvc` and no `spring-boot-starter-web`; the floor resolves
    # `spring-boot-starter-web` and neither of the other two.
    "aggregate_web_starter": "spring-boot-starter-web",
    "modular_web_starters": ("spring-boot-starter-webmvc",
                             "spring-boot-webmvc"),

    # And the third clause of that row: something still serves HTTP. "The
    # aggregate is gone" is also true of a tree that deleted the web layer, and
    # this rung has no licence to -- nearly all 428 observations are HTTP. Any
    # one of these resolving satisfies it, so a candidate who moved off Tomcat
    # is not failed for having done so.
    "container_artifacts": ("spring-boot-starter-tomcat", "spring-boot-tomcat",
                            "spring-boot-starter-jetty", "spring-boot-jetty",
                            "spring-boot-starter-undertow",
                            "spring-boot-undertow",
                            "tomcat-embed-core", "jetty-server",
                            "undertow-core"),

    # --- the source conventions Boot 4 is designed around --------------------
    #
    # All of these are read out of the AST scan, restricted to
    # `/src/main/java/`, and every floor below sits under *both* legitimate
    # predecessors. There are two, and they are not the same tree: the
    # reference chain arrives at this rung through `mini-git-server-app` and
    # another correct chain through `mini-git-server-war`. Measured, main
    # source only:
    #
    #                                  `-app` chain   `-war` chain
    #     packages                             18          17
    #     types with exactly one ctor          71          70
    #     declared methods                    549         571
    #     serialVersionUID fields              12          11
    #     sole ctors taking a parameter        58          58
    #
    # A floor pinned to the `-app` chain's number would fail a correct `-war`
    # tree on four of those five rows. Each number below is under both, and
    # each row asks "every one this scan finds" rather than "these eighteen
    # named ones", which is what keeps the difference from changing the
    # statement at all.

    # `@NullMarked` coverage, read by `nullness-marked`. 14 and not 18: under
    # both predecessors, with slack for a candidate who merged two packages or
    # emptied one on the way here. The row itself requires *every* package it
    # finds to be covered; this only stops that requirement from being vacuous
    # on a tree with no main source left.
    "nullmarked_packages_min": 14,

    # The paired cap on `nullable-returns`. Measured: the reference annotates
    # 17 methods `@Nullable` and 17 methods return the null literal, the two
    # sets being the same one; the floor has 18 null-returning methods and no
    # annotations anywhere.
    #
    # The cap exists because "no null-returning method lacks `@Nullable`" is
    # also satisfied by annotating all 549 of them, which is a tree that has
    # said nothing. 60 is a little over three times the reference's 17 and a
    # little over a tenth of the `-war` chain's 571 declared methods: room for
    # a candidate who annotates overrides and interface declarations the
    # reference happened not to need, and no room for a blanket pass.
    "nullable_methods_max": 60,

    # The anti-vacuity half of `implicit-injection`. That row asks that no sole
    # constructor carries `@Autowired`, which is trivially true of a tree with
    # no injected constructors at all. 45 is under both predecessors' count of
    # sole constructors taking at least one parameter -- 58 on each, out of 71
    # and 70 sole constructors -- with room for a candidate who collapsed a few.
    #
    # Measured: the floor carries `@Autowired` on 31 of its sole constructors
    # and fails this row on all 31; the reference carries it on none. Spring
    # has injected a sole constructor without the annotation since 4.3, and
    # the annotation is the vestige this rung removes.
    "sole_ctor_types_min": 45,

    # The anti-vacuity half of `serial-annotated`: 8, under both predecessors
    # (12 and 11). Measured: the reference annotates all 12 of its
    # `serialVersionUID` fields `@Serial`, the floor none of its 12. `@Serial`
    # has existed since Java 14 and is the compiler's only way to check that a
    # serialization member is spelled the way the runtime will look for it.
    # The fields are there either way; the row is about whether the check is
    # switched on.
    "serial_fields_min": 8,

    # The nullness vocabularies that are not JSpecify, read by
    # `jspecify-only-nullness` off the import lists and the type-use scan.
    #
    # Measured: both control trees import none of these -- the tree this step
    # is handed states nullness nowhere at all, so this half of the row is a
    # guard against a plausible wrong answer rather than a finding about either
    # tree, and it should be read as one. A candidate reaching for a
    # `@Nullable` has a dozen to choose between; the framework's own signatures
    # are JSpecify from Framework 7.0; and two vocabularies in one tree are two
    # dialects that a checker reads as one.
    #
    # `org.springframework.lang` is listed as a whole package rather than by
    # type name, because it is entirely a nullness vocabulary and because two
    # of its four members are the package-level forms `NonNullApi` and
    # `NonNullFields` -- a prefix of a sibling type name would not have caught
    # them. Framework 7.0 deprecated the package in favour of JSpecify, so a
    # use of it is a deprecation warning -- an error under this rung's
    # `-Xlint:all -Werror` -- as well as a second dialect. The others are
    # named by type, since
    # their packages hold annotations that are not about nullness at all.
    "foreign_nullness_prefixes": (
        "javax.annotation.Nullable", "javax.annotation.Nonnull",
        "javax.annotation.CheckForNull",
        "javax.annotation.ParametersAreNonnullByDefault",
        "jakarta.annotation.Nullable", "jakarta.annotation.Nonnull",
        "org.jetbrains.annotations.Nullable",
        "org.jetbrains.annotations.NotNull",
        "edu.umd.cs.findbugs.annotations", "org.checkerframework",
        "androidx.annotation.Nullable", "androidx.annotation.NonNull",
        "org.springframework.lang",
        "com.google.common.base.Nullable"),
    "jspecify_package": "org.jspecify.annotations",
    "jspecify_coord": ("org.jspecify", "jspecify"),

    # --- the recipe run ------------------------------------------------------

    # `rewrite-clean` reads the patch OpenRewrite's own `UpgradeSpringBoot_4_0`
    # produces over the candidate's tree, run by scan-extra.sh. This is the one
    # thing in that patch the row does not count.
    #
    # A hunk that changes only a `<version>` element or a `<somethingVersion>`
    # property is not a finding. Those values are resolved from live Central
    # metadata at the moment the recipe runs, so a row that counted them would
    # fail a correct tree on the day a plugin releases -- and a gate whose
    # answer depends on the date is a clock, not a gate.
    #
    # Measured on the reference: 754 bytes, one hunk, two changed lines --
    # maven-compiler-plugin 3.13.0 -> 3.16.0 and maven-surefire-plugin 3.2.5 ->
    # 3.6.0, both `<version>` elements of plugins this reactor pins on purpose.
    # Zero findings once this filter is applied.
    "rewrite_version_only_re":
        r"^<(version|[A-Za-z0-9_.-]*[Vv]ersion)>[^<]*</\1>$",
    "rewrite_patch": "/score/out/rewrite.patch",
    "rewrite_rc": "/score/out/rewrite.rc",
}

AUDIT_LINE_RE = re.compile(CAL["audit_line_re"])
DEBUG_TOKEN_RE = re.compile(r"\bDEBUG\b")
VELOCITY_LOG4J_RE = re.compile(CAL["velocity_log4j_re"], re.IGNORECASE)

GUICE = ("com.google.inject",)
LIFECYCLE = ("com.google.gerrit.lifecycle",)
SPRING = ("org.springframework",)

# The container's API by both the names a correct port of this tree may spell
# it under. `SPRING` is the groupId question -- what resolves, and on which
# line -- and stays narrow. `CONTAINER_API` is the bytecode question -- does
# this project's own code reach for the container -- and a port that marks the
# seven surviving binding annotations with `jakarta.inject.Qualifier` and
# writes its provider classes as `jakarta.inject.Provider` has reached for it
# exactly as much as one that used Spring's own names. See
# CAL["spring_classes_min"].
#
# The JSR-330 half of this pair swaps spelling at this rung and that is the
# whole of the rung in one line. Spring 5.3 read the annotations from
# `javax.inject` and only from there; Spring 6 reads them from
# `jakarta.inject` and only from there. So `javax.inject` is out and
# `jakarta.inject` is in -- not because the reference uses either (it uses
# neither; there is no `javax.inject` or `jakarta.inject` import anywhere in
# this tree, at any step) but because this is a gate about what the container
# would read, and crediting a spelling the container ignores is the mistake
# in both directions. Steps 03 and 04 credited `javax.inject` for exactly
# three rungs and this one stops.
CONTAINER_API = SPRING + ("jakarta.inject",)

# The container's own profile machinery, by the two types that select one.
# `@Profile` is the annotation `no-profile-selection` refuses; these are the
# programmatic halves of the same switch, and a tree that calls one has the
# construct whether or not it wrote the annotation.
PROFILES_API = ("org.springframework.core.env.Profiles",)

# The interfaces a class that produces a value for injection can be ported
# onto, matched on the simple name because that is what an `implements` clause
# says in the source. Spring's three, the JSR-330 one, and the JDK's -- the
# reference uses `ObjectFactory` thirteen times and `Supplier` once, and
# §1.3 names four of them as answers to Guice's `Provider<T>`.
#
# `FactoryBean` is the fifth and is here because it is the same construct
# under a different contract: a class whose whole job is to produce the
# injected value, registered as a bean so the container calls it. §1.3's menu
# names it beside the other four, so a candidate who picked it picked
# something the instruction offered, and a gate that then failed the tree for
# it would be grading against a shape the document did not ask for. (The note
# on `configuration_types_min` reaches for the same construct for a different
# reason -- that one is why there is no floor on `@Bean` methods -- but that
# note is a comment in this file and no candidate ever reads it.)
FACTORY_IFACES = ("ObjectFactory", "ObjectProvider", "Provider", "Supplier",
                  "FactoryBean")

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

# A servlet API jar by the names the three plausible ones ship under. Step
# 02's, verbatim: matched on the archive entry rather than on a coordinate,
# because the question is what is inside the deployable and `provided` is
# precisely the scope that does not change any coordinate the candidate wrote.
SERVLET_JARS = re.compile(
    r"(^|/)(servlet-api|javax\.servlet-api|jakarta\.servlet-api|"
    r"javax\.servlet(-api)?-\d|tomcat-servlet-api)[-.].*\.jar$")

# `WEB-INF/web.xml` as an archive entry, in the two places the scan reports one
# -- inside the packaged war (`...war!WEB-INF/web.xml`) and in the exploded
# directory the war plugin leaves beside it. Anchored on the separator so that
# a file the candidate happens to call `my-web.xml` is not this.
PACKAGED_WEB_XML = re.compile(r"(^|[/!])WEB-INF/web\.xml$")

# The namespaces a `<web-app>` has been published under, oldest first. Listed
# so that the descriptor gate finds one whose root element it cannot read the
# local name of, and so that an unparsable file naming any of them is reported
# rather than skipped.
WEB_APP_NS = ("http://java.sun.com/xml/ns/j2ee",
              "http://java.sun.com/xml/ns/javaee",
              "http://xmlns.jcp.org/xml/ns/javaee",
              "https://jakarta.ee/xml/ns/jakartaee")

# And the one a Spring bean definition document has carried since 2.0. The
# root element is `beans` in every version of it; the namespace is given as
# well so that an unparsable file is still reported.
SPRING_BEANS_NS = ("http://www.springframework.org/schema/beans",)

b = G.Bundle()
g = S.Gates()


class _Collector(S.Gates):
    """A Gates that collects without journalling.

    The sub-checks are gate_lib calls so that they ask exactly what the steps
    they came from asked. Handing them the real `g` would put thirty more rows
    in the gate journal and in report.json, each reading like a gate with a
    weight of its own -- which is the dilution this section exists to avoid.
    """

    def add(self, name, ok, detail, weight=1.0, evidence=None):
        r = S.Result(name, ok, detail, weight, evidence)
        self.results.append(r)
        return r

# Seven collapsed rungs, one row each: step 03's twenty-two container gates,
# step 04's twelve Boot gates, step 05's jakarta gates, step 06's eleven
# logging gates, step 07's thirteen legacy-library gates, step 08's thirteen
# concurrency gates and -- new here -- step 09's thirteen idiom gates. The
# gates between each pair of rebindings below are those steps', unchanged, and
# `g` is rebound around them so that each one writes into its collector
# instead of into the journal. Unchanged is the point: a hold that
# re-implemented its predecessor's question as a simpler boolean would be a
# different question wearing the same name.
#
# Collapsing the immediately preceding rung is the rule rather than an
# exception -- step 03 collapsed step 02's, step 04 collapsed step 03's, and
# so on down to this one -- and it is what keeps the arithmetic honest. Step
# 09's own rows carry 36.5 between them; carried forward whole they would make
# a tree that had finished step 09 and touched nothing else score most of this
# rung's total, and the floor measurement -- score step 09's finished tree
# with this verifier -- is the number that says whether a rung is testing its
# own leap. Measured here: collapsed, that tree takes 25.5 of 57.0; carried
# forward whole it would take 59.5 of 91.0.
#
# Three of the rows folded into `IDIOM` have not been in a collapsed rung
# before: `class-release`, `compiler-release` and `no-jdk-internals` have sat
# in the top-level floor block at every rung since step 01. They move into the
# hold because at this rung they are step 09's statement and nobody's leap --
# 21 is the release step 09 set, and this rung does not move it, so leaving
# them at the top level would price a settled property above every row that
# advances anything. `tests-ship-and-pass` deliberately does not move with
# them: it is the anti-deletion floor each rung needs in its own right, and
# this rung's own rows reach every constructor and every null-returning method
# in the tree, which is a great deal of compiling for a candidate to make stop
# failing by deleting the tests.
#
# `LOG`, `LEG`, `VIRT` and `IDIOM` are filled where the collapsed rungs' gates
# already live, near the bottom of this file rather than here; `DI`, `BOOT`
# and `JAK` are filled in the three regions below. All seven are reported
# together at the end.
DI = _Collector()
BOOT = _Collector()
JAK = _Collector()
LOG = _Collector()
LEG = _Collector()
VIRT = _Collector()
IDIOM = _Collector()


def annotated(records, simple):
    """Records carrying `@simple`, however the candidate wrote the name.

    AstScan records an annotation under the name in the source text, which is
    `Configuration` for the import form and
    `org.springframework.context.annotation.Configuration` for the inline one.
    Both are the same annotation and the second is legal Java, so both count.
    Matched on the tail rather than on the whole, which is why this is one
    helper and not an `in` test at four call sites.
    """
    out = []
    for r in records:
        for a in (r.get("annos") or []):
            if a == simple or a.endswith("." + simple):
                out.append(r)
                break
    return out


# ---------------------------------------------------------------------------
# How the 59.5 is split, in three columns and not two
#
# Same accounting as steps 03 to 08, re-measured for this rung by running the
# predecessor tree -- step 08's finished one, untouched, which is exactly what
# `/app` holds when this step starts -- through this file and partitioning the
# journal by whether each gate passed.
#
#   (a) 32.5   rung gates the predecessor FAILS, over 10 gates. The
#              discriminating leap, and at this rung it is the whole of this
#              rung's own work: `no-legacy-date-api` 5.0,
#              `lint-is-enforced` 5.0, `pattern-instanceof` 4.5,
#              `callbacks-are-lambdas` 3.5, `no-raw-cli-types` 3.5,
#              `suppressions` 2.5, `java-time-in-use` 2.5,
#              `no-deprecated-for-removal` 2.0, `no-owned-stringbuffer` 2.0,
#              `warning-free-build` 2.0.
#
#   (b)  0.0   rung gates the predecessor PASSES: none. Every rung from 04 to
#              08 has had a column here -- the anti-deletion rows, which by
#              construction the starting tree already satisfies -- and this
#              one does not, which is a fact about the rung and not an
#              omission. See below.
#
#   (c) 27.0   not this rung's work: 10.5 over the eight common gates and
#              16.5 over the eleven held rows. 32.5 + 0.0 + 27.0 = 59.5, and
#              10 + 0 + 19 = 29 rows.
#
# Measured floor: 27.0 of 59.5 = 0.453782 at 428/428 behaviour. Reference:
# 59.5 of 59.5 = 1.000000 at 428/428.
#
# Three things about this split are particular to this rung.
#
# The first is the empty column (b), and it is worth being precise about what
# replaces it. The deletion answer is real here and is worse than at most
# rungs: none of the 428 recorded observations reads a rendered date, so a
# candidate who answers `no-legacy-date-api` by deleting the three renderings
# rather than porting them scores 428 of 428 and loses nothing in the
# behaviour half. What refuses that tree is not a row the predecessor passes
# -- it is the conjunction of two rows the predecessor fails.
# `no-legacy-date-api` falls when the old API goes; `java-time-in-use` rises
# only when the new one arrives; and the predecessor fails both, because it
# has neither ported nor deleted. An anti-deletion row that the starting tree
# also fails is unusual and it is the honest shape here: "6 own classes name
# `java.time`" is a statement the arriving tree cannot make, since it has 4.
#
# The second is `audit-log-intact`, which is not in this file's column (a) or
# (b) at all -- it is inside `inherited-logging-hold`, priced at 2.0 with ten
# other logging gates -- and which is nonetheless the strongest single
# constraint on this rung's largest row. It matches every line of `sshd_log`
# against a grammar beginning `[yyyy-MM-dd HH:mm:ss,SSS +0000]`, so the audit
# timestamp's wire format survives the port or the hold row fails. That is the
# design this ladder keeps returning to: a rung's own rows say what changed,
# and an inherited hold says what may not. Here the hold happens to pin the
# one place where a date is observable, and the rung's own rows carry the two
# places where it is not.
#
# The third is that `lint-is-enforced` at 5.0 is priced level with
# `no-legacy-date-api`, which touches four files and two wire formats, and
# that is deliberate. It is the only row in this file that makes any of the
# others permanent. Every other gate here is a census of a tree at one moment
# -- 0 raw types, 0 legacy dates, 18 binding patterns -- and a census is
# satisfied by a commit and unsatisfied by the next one. `-Xlint:all -Werror`
# in the compiler plugin is what stops the tree going back, and a candidate
# who does all nine other rows and sets no flags has left the project exactly
# where it was with respect to the thing this rung is about.
#
# Weights inside column (a), briefly:
#
#   `no-legacy-date-api` 5.0 is the largest because it is the largest piece of
#   work and the only one with a wire format behind it. Four files, two
#   formatter patterns that have to come out the other side character for
#   character, and a jgit API boundary -- `PersonIdent(String, String, Instant,
#   ZoneId)` -- that only exists because step 07 moved this tree to jgit 6.
#
#   `pattern-instanceof` 4.5 is the largest population: 25 sites in 12 files.
#   It is priced below the date row because each site is mechanical and none
#   of them can change behaviour -- a binding pattern that compiles has the
#   same semantics as the test-and-cast it replaced.
#
#   `no-raw-cli-types` 3.5 and `callbacks-are-lambdas` 3.5 are level because
#   they are the same size measured differently: five callback sites against
#   six raw-type sites, and the raw-type work is the only place in this rung
#   where a candidate has to redesign an interface this project owns rather
#   than rewrite a body.
#
#   `warning-free-build` 2.0 is the cheapest row in the column and is not
#   redundant with the 5.0 above it. The tree that sets `-Xlint:all` and not
#   `-Werror` builds, ships and prints all 21 warnings: it fails this row on
#   the evidence and fails half of `lint-is-enforced`, and this is the row
#   whose evidence tells the candidate which 21. The tree that sets both and
#   has not done the work does not reach either row -- `-Werror` makes the
#   build fail, there is no artifact, behaviour is 0 of 428, and
#   `reward = migration * behaviour` is exactly 0.
#
#   `no-deprecated-for-removal` 2.0 is one site and is priced for what it is.
#   It is in the file because it is the one item on javac's list with a wrong
#   fix available -- `@SuppressWarnings("removal")` silences it and changes
#   nothing -- so it reads the bytecode instead of the warning stream, and the
#   suppression cap above is tight enough that taking the wrong fix costs an
#   argument out of a budget of eight.
# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# The floor. A tree that does not parse looks maximally migrated to every AST
# gate, and a tree that produced no artifact passes every absence gate
# vacuously. Both are asked out loud.
# ---------------------------------------------------------------------------

G.parses_clean(g, b, weight=1.0)
G.sources_survive(g, b, CAL["source_files_min"], CAL["source_types_min"],
                  weight=1.0)


def orphan_types_intact():
    """The two types no recorded observation can speak for, still whole.

    Step 02's gate, unchanged. `sources-survive` counts files and declared
    types, and both counts survive a candidate who opens `CookieBase64` and
    deletes everything between the braces. Every other file in the tree has the
    behaviour suite behind it; these two have nothing behind them, because
    nothing in the pinned tree calls them.

    What this does not do is look inside a body. A method whose statements are
    replaced by `return null;` still declares itself and still counts here.
    This is an anti-stripping floor, not a claim that either type still works.
    """
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
# Inherited from step 01, re-asserted. This is the rung where a candidate
# touches every constructor and every null-returning method in the tree, and
# the cheapest way to make a test that no longer compiles stop failing is to
# stop shipping it.
# ---------------------------------------------------------------------------

# The three release rows write into `IDIOM` rather than into the journal; the
# collector block above says why this is the first rung at which they are a
# hold and not a floor. Rebound here rather than moved down into step 09's
# region, because moving them would file step 01's statement under step 09's
# name, and the regions in this file are organised by which rung wrote a row.
_journal, g = g, IDIOM
G.class_release(g, b, CAL["class_major"], weight=2.0, mode="exactly")
G.compiler_release(g, b, CAL["release"], weight=1.0)
G.jdk_internals(g, b, weight=1.0)
g = _journal

# This one does not go with them. It is the floor that says the tests are
# still here and still green, and it is priced at the top level at every rung
# because every rung can be passed by deleting them -- this one perhaps most
# easily of all, since `nullness-marked` and `nullable-returns` between them
# reach into every package and every null-returning method in the tree.
G.tests_ship_and_pass(g, b, CAL["tests_min"], CAL["test_classes"], weight=2.0)

# `suppressions` is not here either, and at this rung it is not a row of its
# own at all. It was a 1.0 floor in this block from step 01 to step 08 ("may
# not grow", cap 31 against a tree sitting at 18); step 09 dropped the cap to
# 8, below what that rung's predecessor carried, and made it a 2.5 leap row.
# Here it is step 09's statement like the rest of them, so it is asked where
# it was asked -- under "Step 09's rung" below -- and collected into `IDIOM`
# with its twelve neighbours.


# ---------------------------------------------------------------------------
# Step 03's rung, held (start of the collected region)
# ---------------------------------------------------------------------------
#
# Everything from here to the matching `g = _journal` below is step 03's
# file, verbatim, less its `no-spring-boot` gate -- which this step exists
# to make unpassable -- an inherited
# check that this rung's own requirements contradict. It is deleted rather
# than inverted; the positive statement that Boot is now what supplies the
# framework is made by this step's own gates further down.
#
# The rebinding is what makes twenty-two gates into one row. It is done at
# module level and not with a context manager because the gates below are
# module-level statements too, and a wrapper would have to indent a
# thousand lines that are otherwise byte-identical to their source.
_journal, g = g, DI

# ---------------------------------------------------------------------------
# Questions the shared dependency gates cannot ask
# ---------------------------------------------------------------------------
#
# `dep_absent` and `dep_present` OR their group and artifact prefixes, which is
# the right shape for "this library, however it is spelled" and the wrong one
# for a version bound: "some org.springframework artifact that is not on the
# 5.3 line" is a conjunction of a groupId and a *negated* version, and no
# prefix expresses it. Written here rather than widened in gate_lib, because a
# `dep_absent` that ANDed its arguments would silently change what every other
# step's gates mean. Step 02's helpers, verbatim.


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
# Guice, out three ways
# ---------------------------------------------------------------------------
#
# Three coordinates in the baseline -- `com.google.inject:guice:2.0`,
# `com.google.inject.extensions:guice-servlet:2.0` and
# `com.google.inject.extensions:guice-assisted-inject:2.0` -- and the groupId
# prefix reaches all three, so this one is the shape `dep_absent` was written
# for. The artifactId prefix is there for the candidate who finds Guice under
# some other groupId, which the library has been published under before.
G.dep_absent(g, b, "no-guice-dep", group_prefixes=GUICE,
             artifact_prefixes=("guice",), weight=2.0)

# The same question of the bytecode, and asked with `own=False` rather than
# `own=True`, which is the one difference from how the rest of this file asks a
# removal. The Guice that has to be gone is not only the Guice this project
# compiled against: a WAR that ships `guice-2.0.jar` in `WEB-INF/lib` because
# some other dependency drags it in still deploys a second injector into the
# container, and a Guice shaded into somebody else's jar has no coordinate for
# `no-guice-dep` to match. `own=False` reads every class in the deployable,
# which is where both of those are visible.
#
# Safe to ask that way round because it was measured that way round: no class
# anywhere in the reference WAR -- 17,887 of them, dependency jars included --
# names `com.google.inject`. The count in the baseline is 709.
#
# With the `own_floor`, because "no class references com.google.inject" is also
# what an artifact with no classes in it says.
G.no_refs(g, b, "no-guice-classes", GUICE, weight=2.0, own=False,
          floor=CAL["own_floor"])

# And of the source, which is the half the bytecode cannot answer: a `.java`
# that still imports `com.google.inject.AbstractModule` but has been dropped
# from the build compiles into nothing and references nothing.
G.no_source_types(g, b, "no-guice-source", GUICE, weight=2.0)

# `aopalliance:aopalliance:1.0` is declared outright in two of this tree's
# poms, and it is there for Guice: the `MethodInterceptor` interface Guice 2.0
# takes its AOP types from. Nothing else in the tree names it. Matched on the
# coordinate and on nothing else -- deliberately never as a class scan --
# because Spring's own `spring-aop` jar *bundles* the same
# `org.aopalliance.**` interfaces, so a bytecode gate would report 115 hits
# against a reference that has done exactly what was asked. This is the trap
# this gate is written around rather than into.
G.dep_absent(g, b, "no-aopalliance-dep",
             group_prefixes=("aopalliance", "org.aopalliance"),
             artifact_prefixes=("aopalliance",), weight=1.0)

# `com.google.gerrit.lifecycle` is this project's own package and it is Guice's
# all the same: a `LifecycleListener` interface, a `LifecycleManager` that
# holds a list of them and walks it up and back down, and a `LifecycleModule`
# that exists to give Guice a multibinder to put them in. Spring's container
# does that itself -- `SmartLifecycle` beans are started at refresh in
# ascending phase and stopped at close in descending phase -- so the package
# has no work left. Asked both ways for the reason every removal here is: a
# source file excluded from the build references nothing, and a compiled class
# can survive a source tree that no longer imports it.
G.no_refs(g, b, "no-lifecycle-classes", LIFECYCLE, weight=1.0, own=True,
          floor=CAL["own_floor"])
G.no_source_types(g, b, "no-lifecycle-source", LIFECYCLE, weight=1.0)

# ---------------------------------------------------------------------------
# Production cuts, and the two questions every gate below this line asks twice
#
# `own=True` elsewhere in this file means "not inside a jar", which keeps the
# loose `target/test-classes` output. From here down that is not the cut the
# gates want, and the first four helpers are the one they do want -- four and
# not five, deliberately: every gate below reaches the production cut through
# `production_classes`, `production_refs`, `in_tests` or `own_implements`, and
# a fifth spelling of the same cut is how a gate ends up counting the test
# tree without meaning to.
#
# After them come the five record-shape conveniences the container gates share
# (`_simple`, `_prod_typeuses`, `_prod_calls`, `_prod_annos`, `_site`) and then
# the two questions more than one gate needs the answer to:
# `container_types_in_code` and `context_objects`. Those two are memoised, so
# a gate may ask either without worrying about what it costs, and they are
# here rather than beside `container-bootstrap` because `spring-classes`,
# `container-api-used` and `java-configuration` all come before it.
# ---------------------------------------------------------------------------


def production_classes():
    """This project's own class files, with the test tree left out.

    `own=True` elsewhere in this file means "not inside a jar", which keeps
    the loose `target/classes` output and drops both the dependency jars and
    this project's own packaged copies of the same classes. These four gates
    need one more cut. `target/test-classes` is loose output too, and a
    context stood up inside a unit test is not the application standing its
    context up: a candidate who writes one Spring test and then wires
    production by hand would pass the very gate that exists to refuse that
    tree. The same goes for a `Scope` implemented only in a fixture.

    The other gates in this file are not narrowed this way and should not be.
    `sources-survive` and the suppression census are about the whole tree, and
    the absence gates have to see a Guice import wherever it is.
    """
    return [c for c in b.art.own_classes()
            if "test-classes/" not in (c.get("o") or "")]


def production_refs(prefixes):
    """`refs_by_class(prefixes, own=True)` over `production_classes()`."""
    hits = {}
    for c in production_classes():
        names = set(c.get("r") or []) | set(c.get("l") or []) \
            | set(c.get("a") or []) | set(c.get("i") or [])
        if c.get("s"):
            names.add(c["s"])
        m = sorted(n for n in names if S._under(n, prefixes))
        if m:
            hits[c.get("n", "?")] = m
    return hits


def production_refs_in(package, prefixes):
    """`production_refs(prefixes)`, narrowed to own classes under a package.

    Two rows need the same narrowing and it is the same question both times:
    does this project's own compiled code *in this package* name the thing,
    as opposed to the thing being on the classpath (which the artifact census
    answers) or being written somewhere in the package's source (which the
    text and AST halves answer). Neither of those is evidence that the port
    happened; a class file that carries the name in its constant pool is.

    `production_refs` keys on the dotted class name and writes a nested type
    with a `$` -- `com.google.gerrit.server.cache.CachePool$Factory` -- so
    `S.under` on the package name, which accepts both separators, is the whole
    of the narrowing.
    """
    return dict((n, m) for n, m in production_refs(prefixes).items()
                if S.under(n, (package,)))


def in_tests(path):
    """Whether a scanned source path is under a module's test source root."""
    return "/src/test/" in ("/" + (path or ""))


def own_implements(types):
    """Own classes whose supertype or interface list names one of `types`.

    Asked of the bytecode and not of the `implements` clause, because the
    clause is not where the answer always is. An anonymous `new Scope() {...}`
    implements the interface and writes no clause at all; so does a subclass
    of a base that implements it. The class file records the resolved
    hierarchy either way, and `production_classes` keeps the question about
    this project's application code.
    """
    want = set(types)
    out = {}
    for c in production_classes():
        names = set(c.get("i") or [])
        if c.get("s"):
            names.add(c["s"])
        m = sorted(names & want)
        if m:
            out[c.get("n", "?")] = m
    return out


def _simple(name):
    """The tail of a written or dotted type name: `a.b.C.D` -> `D`."""
    return (name or "").rsplit(".", 1)[-1].split("<", 1)[0].strip()


def _prod_typeuses():
    return [u for u in b.ast.typeuses() if not in_tests(u.get("p"))]


def _prod_calls():
    return [n for n in b.ast.nodes("call") if not in_tests(n.get("p"))]


def _prod_annos(simples):
    """Production annotation records whose written name ends in one of them."""
    want = set(simples)
    return [a for a in b.ast.kind("anno")
            if not in_tests(a.get("p")) and _simple(a.get("n")) in want]


def _site(r, what):
    return "%s:%s (%s)" % (r.get("p", "?"), r.get("line", "?"), what)


_IN_CODE = {}


def container_types_in_code():
    """Container types this project writes *as types*, in code.

    The distinction this draws is between naming the API and using it. A tree
    can carry `org.springframework` in the constant pool of every class it
    compiles and never put a single container type in the path of an
    instruction: `@Configuration`, `@Bean`, `@Autowired`, `@Qualifier` and
    `@Scope("request")` are all references to the API, and every one of them
    is inert if nothing loads it. A *written type* is not inert in the same
    way -- a field, a parameter, a return type, an `extends` or `implements`
    clause, a `new`, a cast or a `.class` literal is a type the compiler
    resolved and the bytecode reaches through.

    `AstScan` makes the distinction available with no new record, because it
    emits one `typeuse` per `ClassOrInterfaceType` and an annotation is an
    `AnnotationExpr`. Two consequences, both verified on this reference:

      - `@Scope(WebApplicationContext.SCOPE_REQUEST)` emits no `typeuse`.
        The scope constant is a field access, and the three sites in
        `WebConfiguration` that write it produce no record here. Naming a
        context to reach a constant on it is not using one.
      - `@Import({Foo.class, Bar.class})` does emit one per class literal,
        because a `.class` literal contains a type. That is correct: the
        imported class is a type the compilation unit names.

    Three routes in, and the name recorded is the resolved package rather
    than a spelling this file chose:

      U1  a written type name whose simple name resolves, through that same
          file's own import list, into a `CONTAINER_API` package. The import
          list is the authority on what a simple name means, so this needs no
          list of Spring class names and no list can go stale.
      U2  a written type name already under `CONTAINER_API` -- the fully
          qualified spelling, which needs no import. A qualifier segment is
          itself a `ClassOrInterfaceType`, so `org.springframework.context.
          ApplicationContext` written inline arrives as four records, three
          of which name packages; only the longest name at a given file and
          line is kept, which leaves the one type that was written. Without
          that cut a single fully-qualified name would clear a floor of
          three on its own.
      U3  an own production class whose bytecode supertype or interface is
          under `CONTAINER_API`. This is the route for a file that reaches
          the API through a wildcard import, where U1 cannot attribute a
          simple name, and for a subclass of one of Spring's own initializer
          or context bases that names nothing else.

    `jakarta.inject` counts beside `org.springframework` for CONTAINER_API's
    reason, and it is load-bearing here in the other direction too: a port
    that writes its producers as `jakarta.inject.Provider` has reached for a
    contract only a container honours and Spring 6 honours natively, and
    refusing it would be a gate against §1.3's own list. A port that writes them as
    `java.util.function.Supplier` has not reached for one, which is why the
    floor in `container-api-used` is derived from the places §1.3 and §1.4
    leave no choice about and not from a count of producers.

    Returns {site: resolved name}.

    Measured: reference **16 distinct types over 126 sites** -- 105 by U1 in
    33 source files, 0 by U2 (the reference reaches the API through imports,
    never through a fully-qualified spelling) and 21 by U3. Baseline 0; a
    hand-wired tree zero on all three routes, with 55 of its
    own classes still passing `spring-classes`.
    """
    if _IN_CODE.get("done"):
        return _IN_CODE["hits"]
    hits = {}
    imports = {}
    for f in b.ast.files():
        p = f.get("p")
        if in_tests(p):
            continue
        m = {}
        for im in (f.get("imports") or []):
            nm = im.split(" ")[0]
            if not nm.endswith(".*"):
                m[_simple(nm)] = nm
        imports[p] = m
    inline = {}
    for u in _prod_typeuses():
        n = u.get("n") or ""
        if S.under(n, CONTAINER_API):
            inline.setdefault((u.get("p"), u.get("line")), []).append(u)
            continue
        fq = (imports.get(u.get("p")) or {}).get(_simple(n))
        if fq and S.under(fq, CONTAINER_API):
            hits[_site(u, fq)] = fq
    for group in inline.values():
        names = [u.get("n") or "" for u in group]
        for u in group:
            n = u.get("n") or ""
            if any(m != n and m.startswith(n + ".") for m in names):
                continue
            hits[_site(u, n)] = n
    for c in production_classes():
        names = set(c.get("i") or [])
        if c.get("s"):
            names.add(c["s"])
        for n in sorted(names):
            if S.under(n, CONTAINER_API):
                hits["%s extends/implements %s" % (c.get("n", "?"), n)] = n
    _IN_CODE["hits"], _IN_CODE["done"] = hits, True
    return hits


_BOOT_DRIVERS = {}


def boot_driver_sites():
    """Where this tree writes the object that builds Boot's context.

    Returns `(sites, files)`. `sites` is `{site: route}` for Half A's A4 and
    A5; `files` is the set of production source paths that hold one of the
    driver names, which is what scopes Half B's B4 to the file that has a
    driver in it rather than to every `run` in the repository.

    A5 reads the constant pool because A4 cannot see the case that matters
    most: `SpringApplication.run(App.class, args)` is a static call on a name
    expression, so JavaParser produces a `MethodCallExpr` and no
    `ClassOrInterfaceType`, and `AstScan` emits no `typeuse` record for the
    name `SpringApplication` anywhere in that file. The class file is the only
    place that tree writes it down.
    """
    if _BOOT_DRIVERS.get("done"):
        return _BOOT_DRIVERS["sites"], _BOOT_DRIVERS["files"]
    names = set(CAL["boot_context_drivers"])
    sites, files = {}, set()
    for u in _prod_typeuses():
        if _simple(u.get("n")) in names:
            sites[_site(u, u.get("n"))] = "A4"
            files.add(u.get("p"))
    refs = production_refs(CAL["boot_context_driver_fqcns"])
    if refs:
        declared = {t.get("n"): t.get("p") for t in b.ast.kind("type")
                    if not in_tests(t.get("p"))}
        for cls, named in sorted(refs.items()):
            sites["%s references %s" % (cls, ", ".join(named))] = "A5"
            if declared.get(cls):
                files.add(declared[cls])
    _BOOT_DRIVERS["sites"] = sites
    _BOOT_DRIVERS["files"] = files
    _BOOT_DRIVERS["done"] = True
    return sites, files


_CONTEXTS = {}


def context_objects():
    """Sites where this project builds or holds a container context.

    This is `container-bootstrap`'s Half A, factored out because three other
    gates need the same answer. A `@Configuration` class, a bean definition
    and a prototype scope are all statements addressed *to* a container, and
    in a tree where no container object exists they are statements to nobody.
    Asked each on its own, those three pass a hand-wired tree -- whose
    graph is built with `new` behind a project-local
    registry -- on dead annotations while it fails every gate
    that asks for the container itself.

    Five routes. The last two are this rung's, because the first three all
    assume a tree whose context is built by naming a context class, and an
    application that starts its own container does not write one.

      A1  production source writes the name of a context implementation
          (`context_impls`) as a type: `new AnnotationConfigWebApplicationContext()`,
          a field declared `GenericApplicationContext ctx`, a
          `GenericWebApplicationContext.class` literal. Boot's own two
          servlet contexts are in that tuple.
      A2  an own production class's bytecode *superclass* is one of
          `bootstrap_types` -- the route for a port that only extends
          Spring's own machinery and names no context type in its own source.
      A3  an own production class carries the dotted name of one of them as a
          string constant, which is how a `contextClass` init-param is
          spelled.
      A4  production source writes one of `boot_context_drivers`.
      A5  an own production class references one of
          `boot_context_driver_fqcns` in its constant pool.

    Without A4 and A5, an entry point that writes `SpringApplication`,
    `SpringApplicationBuilder` and a held `ConfigurableApplicationContext` --
    which is every shape §1.7 licenses -- clears Half A only by accident, on
    the strength of an unrelated `main` elsewhere in the tree that still says
    `new AnnotationConfigApplicationContext(...)`. A port that tidied that
    `main` away, as this rung invites, would fail four gates for
    having moved further in the direction the rung asks for.

    Naming a context *interface* is still deliberately not a route, and
    `@Scope(WebApplicationContext.SCOPE_REQUEST)` is the case that decides
    it: that attribute names a context and creates nothing, and it emits no
    `typeuse` at all, which is what makes the cut possible without a new
    scanner record. A4 does not smuggle the interfaces back in: what it
    matches is the builder on the right of the assignment, never the
    `ConfigurableApplicationContext` on the left.

    Returns {site: route}.  Measured: reference 4, by A1 (1), A4 (2) and
    A5 (1); the tree this step is handed 17, all by A1; the hand-wired tree 0.
    The reference's single A1 site is in a `main` this rung does not touch,
    and A4 and A5 are what hold when that `main` is tidied away.
    """
    if _CONTEXTS.get("done"):
        return _CONTEXTS["hits"]
    impls = set(CAL["context_impls"])
    hits = {}
    for u in _prod_typeuses():
        if _simple(u.get("n")) in impls:
            hits[_site(u, u.get("n"))] = "A1"
    for c in production_classes():
        sup = c.get("s")
        if sup and S.under(sup, CAL["bootstrap_types"]):
            hits["%s extends %s" % (c.get("n", "?"), sup)] = "A2"
        for lit in (c.get("l") or []):
            if S.under(lit, CAL["bootstrap_types"]):
                hits['%s names "%s"' % (c.get("n", "?"), lit)] = "A3"
    hits.update(boot_driver_sites()[0])
    _CONTEXTS["hits"], _CONTEXTS["done"] = hits, True
    return hits


def present_given_context(name, hits, minimum, weight, missing):
    """`Gates.present`, plus a container to receive what it counted.

    The detail text of a passing tree is `Gates.present`'s exactly, so a
    passing gate reports the same counts either way. A tree that has the
    sites and no context gets one extra clause naming the half that was
    missing, because a gate whose failure says only "false" cannot be acted
    on.
    """
    ctx = context_objects()
    items = sorted(hits.keys()) if isinstance(hits, dict) else sorted(hits)
    ok = len(items) >= minimum and bool(ctx)
    detail = "%d site(s), need >=%d" % (len(items), minimum)
    if items:
        detail += ": " + ", ".join(str(i) for i in items[:8])
    if not ctx:
        detail += "; and " + missing
    return g.add(name, ok, detail, weight,
                 {"count": len(items), "need": minimum,
                  "sites": [str(i) for i in items[:200]],
                  "context_objects": len(ctx)})


# ---------------------------------------------------------------------------
# Spring Framework 5.3, in
# ---------------------------------------------------------------------------

# Three artifacts at least, because the container this tree needs is more than
# one jar and a tree that declared only `spring-core` would not compile: the
# `@Configuration` machinery is in `spring-context`, the injection annotations
# and `ObjectFactory` are in `spring-beans`, and the request scope and the
# `WebApplicationInitializer` hook are in `spring-web`. The reference resolves
# seven, four of them transitively.
#
# And two of them by name, because the count alone does not ask what that
# paragraph just argued: `spring-core` plus `spring-jcl` plus
# `spring-expression` is three artifacts under this groupId on this line, and
# it is three a tree could resolve without ever taking the application context
# or the web application context §1.1 asks for. `spring-context` is named
# because that is where `@Configuration` lives, and `spring-web` because this
# stays a servlet application with a request scope in it. `spring-beans` is
# not named: it arrives transitively with `spring-context` in every packaging
# of the line, so requiring it would add a row to the failure message and
# nothing to the question. §1.1 names the same two, which is the condition on
# a gate asking for an artifact by name -- it has to be something the
# instruction told the candidate to resolve.
#
# Measured at this rung, on both control trees: the reference resolves seven
# coordinates, all 7.0.9 -- `spring-aop`, `spring-beans`, `spring-context`,
# `spring-core`, `spring-expression`, `spring-web`, `spring-webmvc` -- and the
# tree this step is handed resolves eight at 6.2.19, those seven plus
# `spring-jcl`, which Framework 7 dropped. Both satisfy the count and both
# named artifacts, and both sit on a line this row accepts. That is what a
# hold row is for; `framework-7` below is the row the floor fails.
#
# Renamed from `spring-6`, which is what this was called from step 03 through
# step 09. The bound moved -- see `spring_version_re`, which sets out why it
# was widened rather than re-pinned -- and a row whose name states a bound it
# no longer holds misreports itself in report.json, which is the one place a
# reader goes to find out what a verifier actually asked.
G.dep_present(g, b, "spring-6-or-7", group_prefixes=SPRING, minimum=3,
              weight=2.0, version_re=CAL["spring_version_re"],
              require_artifacts=("spring-context", "spring-web"))

# The other half of that presence. Step 03 wrote this gate to stop a 6.x
# arriving beside the 5.3s and taking the tree off `javax.servlet` a rung
# early; it is asked here for the mirror reason, to stop a 5.3 surviving
# beside the 6.xs. A single `spring-web:5.3.39` left pinned in one module's
# pom resolves `javax.servlet.http.HttpServletRequest` onto the compile
# classpath of a tree whose sources import `jakarta.servlet` -- it compiles,
# because nothing in that module happens to use the 5.3 types, and it is
# exactly the half-migration this rung exists to refuse.
#
# Asked of the `org.springframework` groupId exactly rather than of the prefix,
# because the prefix is also `org.springframework.boot`,
# `org.springframework.security` and `org.springframework.data`, whose version
# numbers are their own and have never tracked the framework's. Boot is asked
# separately below; the other two are not in this tree and no sentence in the
# instruction brings them in, so a gate that failed them would be asking for a
# removal the candidate was never told to make.
#
# The line it refuses moved with `spring_version_re`: "off 6" has become "off
# 6 and off 7", which still catches the `spring-web:5.3.39` above and no
# longer catches the seven 7.0.9 coordinates the reference resolves. Renamed
# for the reason the row above it was.
dep_gate_absent("no-spring-off-6-or-7",
                lambda gid, aid, ver: (gid == "org.springframework"
                                       and not re.match(CAL["spring_version_re"],
                                                        ver)), 1.0)

# Step 03's `no-spring-boot` stood here. This is the step that step 03 was
# holding the line for, so the gate is gone rather than held: see the note at
# the top of this region.


def no_profile_selection():
    """No bean is selected by a profile or a condition.

    §1.7 says the application gains no new way to select behaviour and names
    this one outright: no Spring profile, and no bean conditional on one. The
    sentence is there because this is the rung that first makes one possible
    -- a profile is precisely a switch that changes which objects the
    container builds, and a tree wired by Guice 2.0 had no way to write one.
    A `@Profile("prod")` on a configuration class is a second application
    hiding inside the first, and whichever half the 428 observations were
    recorded against, the other half is unmeasured.

    Asked as `@Profile` and as the three other ways a profile is selected: a
    reference to `org.springframework.core.env.Profiles`, a call to
    `setActiveProfiles`, and the `spring.profiles.active` property named in
    source or baked into the artifact. An annotation without one of those is
    inert, and a `setActiveProfiles` call without the annotation is a switch
    all the same -- the construct is the pair, so both halves are refused.
    Annotations are matched on a type and on a method by the `annotated`
    helper, so `@Profile` and
    `@org.springframework.context.annotation.Profile` are the same answer, and
    the annotation's import is refused beside them so that a file naming it
    without the AST recording a use is still a site.

    `@Conditional` was in this list and has been taken out. It is not a
    profile: `@ConditionalOnMissingBean` and its relatives are how a library
    supplies a default the application can override, and a candidate who
    writes one has added no new way to select behaviour at deploy time --
    which is what §1.7 forbids. Refusing it made this gate fail a correct
    port for an idiom §1.7 has no quarrel with.

    The predecessor tree passes this vacuously, and so would any tree that
    never heard of Spring -- it is a one-directional gate that refuses
    overshoot, the same shape as `no-spring-boot` and `no-xml-bean-definitions`
    and not part of what separates a port from the tree it started as. The
    weight-split comment above counts it in that column. Measured 0 on the
    baseline and 0 on the reference.
    """
    sites = {}
    for t in annotated(b.ast.kind("type"), "Profile"):
        sites["%s (type @Profile)" % t.get("n", "?")] = "Profile"
    for m in annotated(b.ast.kind("method"), "Profile"):
        sites["%s.%s (method @Profile)"
              % (m.get("owner", "?"), m.get("sig", "?"))] = "Profile"
    for f in b.ast.files():
        for im in (f.get("imports") or []):
            nm = im.split(" ")[0].rstrip(".*").rstrip(".")
            if nm in ("org.springframework.context.annotation.Profile",
                      "org.springframework.core.env.Profiles"):
                sites["%s (import %s)" % (f.get("p", "?"), nm)] = nm
    for n in b.ast.nodes("call"):
        if n.get("d") == "setActiveProfiles":
            sites["%s:%s (setActiveProfiles)"
                  % (n.get("p", "?"), n.get("line", "?"))] = "setActiveProfiles"
    for c, refs in b.art.refs_by_class(PROFILES_API, own=True).items():
        sites["%s (references %s)" % (c, ", ".join(refs))] = refs
    for c in b.art.classes():
        if "!" in (c.get("o") or ""):
            continue
        for v in (c.get("l") or []):
            if v == "spring.profiles.active":
                sites["%s (literal spring.profiles.active)"
                      % c.get("n", "?")] = v
    return g.absent("no-profile-selection", sites, 0.5)


no_profile_selection()

# How many of this project's own compiled classes name the container's API at
# all. The gates above ask what resolves; this one asks whether anything here
# names it, which is the half a pom cannot answer.
#
# What it does not ask: a constant-pool reference is not use. ASM cannot
# separate an annotation descriptor from a type in the path of an instruction -- both land in `r` --
# so a tree that annotates 55 hand-wired classes `@Configuration`, `@Bean` and
# `@Named` and builds its object graph with `new` passes this gate honestly,
# on 55 classes. That is the correct answer to the question this gate asks and
# the wrong answer to the rung, which is why `container-api-used` below asks
# the narrower question over the AST, where annotations and written types are
# different records. This gate stays as it is: a cheap, stable floor that no
# port of §1.3 can miss, whose limit is stated here.
#
# Counted over `org.springframework` and `jakarta.inject` together -- that is
# what `CONTAINER_API` holds here, and `javax.inject` has just left it:
# 5.3's `AutowiredAnnotationBeanPostProcessor` read the `javax` spelling and
# Spring 6's reads the `jakarta` one. A port that marks the seven surviving
# binding annotations with `jakarta.inject.Qualifier` rather than Spring's
# own, and writes its provider classes as `jakarta.inject.Provider` rather
# than `ObjectFactory`, has done what §1.3 asks: Spring 6 reads those
# annotations natively, and they mean something in this tree only because
# Spring is the container underneath them. Spelling the same decision the
# standard way is not less of a port and does not cost 2.0 here. See CAL for
# the derivation of 9 under either spelling, against a reference that measures
# 72.
#
# Read over `production_classes()` rather than `own=True`, for the reason that
# helper gives: `own` keeps `target/test-classes`, and nine Spring-using test
# classes beside a hand-wired production graph is not this rung. On this
# reference the cut costs nothing -- 72 own classes name the API and none of
# them is a test class -- which is what makes it safe to apply here rather
# than only to the five gates below.
g.present("spring-classes", production_refs(CONTAINER_API),
          CAL["spring_classes_min"], 2.0)


def container_api_used():
    """This project's own code reaches for the container, as types.

    §1.1's last bullet is the sentence this gate exists for: "the tree cannot
    come out the other side with Spring declared in every pom, resolving on
    every classpath, and nothing in the project's own compiled classes naming
    it ... wiring done by hand with `new` behind a project-local registry is
    the old container under a new dependency, not a new container."
    `spring-classes` alone cannot measure it: it counts constant-pool
    references, and an annotation descriptor is one, so a tree that decorates
    a hand-wired graph passes it on annotations alone.

    The routes are `container_types_in_code`'s, and there are three of them:
    a simple name resolved through the file's own import list (U1), a fully
    qualified name written inline (U2), and a bytecode supertype or interface
    under the API (U3). U3 is what keeps this from being a gate on one
    spelling: a port whose every Spring reference arrives through a wildcard
    import, or which writes no Spring type at all beyond extending
    `AbstractAnnotationConfigDispatcherServletInitializer`, comes in on the
    bytecode and never touches U1 or U2.

    Counted in distinct types rather than sites, so that a port is not asked
    to repeat itself: writing one container type in forty files is one type.

    The floor is three, and it is derived from the places §1.1-§1.5 leave the
    candidate no choice about rather than from what the reference happens to
    do:

      1  §1.4 -- "the application stands its contexts up itself ... it is one
         of this project's own classes that does it". Standing a context up
         means writing a context type, or extending one of the container's
         own bootstrap classes.
      2  §1.3 -- the SSH session scope is "against Spring's [Scope] --
         implemented, and then made known to the container under a name".
         Implementing that interface writes it.
      3  registering it under a name is a call on the object that holds the
         registry, which is a second container type written at the call site.

    Anything §1.3 leaves a free choice about is deliberately not in the
    derivation. Producers are the clearest case: §1.3 allows
    `java.util.function.Supplier` in place of `Provider<T>`, so a port that
    takes that option writes no container type for any of its fifteen
    producers and must still clear this floor from the three above alone.

    Measured: reference 16 distinct types over 126 sites (105 U1 in 33 source
    files, 0 U2, 21 U3); a hand-wired tree zero on all three
    routes while passing `spring-classes` with 55; baseline zero.
    """
    hits = container_types_in_code()
    types = sorted(set(hits.values()))
    need = CAL["container_api_types_min"]
    return g.add("container-api-used", len(types) >= need,
                 ("%d container type(s) written in production code, need >=%d"
                  % (len(types), need))
                 + (": " + ", ".join(types[:8]) if types else
                    "; the API resolves and nothing here writes it"),
                 2.0,
                 {"types": types, "need": need, "sites": len(hits),
                  "examples": sorted(hits)[:60]})


container_api_used()


def java_configuration():
    """The container is configured from Java, in classes that declare it.

    §1.3 names `@Configuration` and `@Bean` as the answer to `AbstractModule`,
    and this asks for the first of them.

    Read from the AST rather than from the bytecode because that is where the
    question is cheapest to ask honestly: `@Configuration` has CLASS retention
    in the class file and RUNTIME in the annotation itself, so both would work,
    but the AST record names the file to open when the gate fails.

    Two independent routes have to meet. On its own the
    floor of 1 makes the annotation half free: one `@Configuration` class on a
    hand-wired tree would take the whole 2.0, and a `@Configuration` class no
    container ever receives configures nothing. So the count is read
    against `context_objects()` -- a context this project builds, holds or
    extends. Neither half names the other's spelling: the annotation can be
    Spring's `@Configuration` anywhere in production source, and the context
    can arrive by written type (A1), by bytecode superclass (A2) or by a
    `contextClass` string (A3), so a port that bootstraps by extending
    `AbstractContextLoaderInitializer` and writes no context type satisfies
    the second half without writing anything the reference wrote.

    The `@Bean` count is reported beside it and gated on by nothing, for the
    reason CAL gives.

    Production source only. A `@Configuration` class under `src/test/java` is
    an ordinary thing for a Spring test to need. The reference has eight
    `@Configuration` types and none of them is in the test tree, so the cut
    moves no number here; nor does the conjunction, which the reference meets
    with 17 context sites.
    """
    types = [t for t in annotated(b.ast.kind("type"), "Configuration")
             if not in_tests(t.get("p"))]
    beans = [m for m in annotated(b.ast.kind("method"), "Bean")
             if not in_tests(m.get("p"))]
    need = CAL["configuration_types_min"]
    ctx = context_objects()
    return g.add("java-configuration", len(types) >= need and bool(ctx),
                 ("%d type(s) annotated @Configuration, need >=%d"
                  % (len(types), need))
                 + ("; %d @Bean method(s) in %d of them"
                    % (len(beans), len({m.get("owner") for m in beans}))
                    if beans else "; no @Bean method anywhere")
                 + ("" if ctx else
                    "; and no context object anywhere to receive them"),
                 2.0,
                 {"configuration_types": sorted(t.get("n", "?") for t in types),
                  "configuration_min": need,
                  "bean_methods": len(beans),
                  "bean_owners": sorted({m.get("owner", "?") for m in beans}),
                  "context_objects": len(ctx)})


java_configuration()


def qualifier_annotations():
    """The binding annotations survive, marked as the container's own.

    §1.3's second bullet, which until now was stated and unmeasured:

      "The seven binding annotations survive as qualifiers ... Those
      annotations stay, as annotations, on the injection points that carry
      them today; what changes is the container that reads them, so each
      becomes a qualifier Spring understands rather than a Guice binding
      annotation -- one the container recognises on its own, because the
      annotation itself is marked as one and not because the container was
      separately told about it."

    Nothing else caught it. `spring-classes` derives seven of its floor of
    nine from these annotations, but its floor is nine against a reference of
    seventy-two, so a tree that deleted all seven has sixty-three classes of
    slack to find the nine in. `sources-survive` needs 124 declared types
    against a reference of 138, and deleting seven leaves 131. A port that
    dropped the seven and told the beans apart by name instead would have
    scored full marks on a requirement the instruction states in bold.

    The route is the AST and nothing else. `AstScan`'s `type` record carries
    `kindOf` and the written names of the annotations on the declaration, and
    that list includes markers -- the `keys.isEmpty()` early return that drops
    argument-less annotations lives in the separate `anno` record emitter, not
    here. So an own production `type` with `kindOf == "annotation"` whose name
    is one of the seven, carrying an annotation that resolves under
    `qualifier_marks`, is the whole measurement -- and where a name is
    declared more than once, every declaration of it, because a marked decoy
    in a second package beside an unmarked original is not the edit. A name
    counts when at least one production declaration of it exists and all of
    them are marked. Resolution is
    `container_types_in_code`'s: written fully qualified, or a simple name
    resolved through that file's own import list, or through a wildcard import
    of the package that holds it.

    What this gate does not check is the other half of the sentence -- "on the
    injection points that carry them today". An annotation that survives, is
    marked, and is then used nowhere leaves two `Executor`s and a handful of
    `Config`, `File` and `String` values with nothing to tell them apart; the
    context does not start, and the behaviour suite takes the tree to single
    digits. That half is carried by a run rather than by a count here, which
    is the right place for it.
    """
    marks = CAL["qualifier_marks"]
    want = set(CAL["qualifier_annotations"])
    exact, wild = {}, {}
    for f in b.ast.files():
        p = f.get("p")
        if in_tests(p):
            continue
        e, w = {}, []
        for im in (f.get("imports") or []):
            nm = im.split(" ")[0]
            if nm.endswith(".*"):
                w.append(nm[:-2])
            else:
                e[_simple(nm)] = nm
        exact[p], wild[p] = e, w

    def resolve(p, written):
        if S.under(written, marks):
            return written
        simple = _simple(written)
        cand = (exact.get(p) or {}).get(simple)
        if cand and S.under(cand, marks):
            return cand
        for pkg in wild.get(p) or []:
            cand = pkg + "." + simple
            if S.under(cand, marks):
                return cand
        return None

    decls, unmarked = {}, []
    for t in b.ast.kind("type"):
        p = t.get("p")
        if in_tests(p) or t.get("kindOf") != "annotation":
            continue
        simple = _simple(t.get("n") or "")
        if simple not in want:
            continue
        mark = None
        for a in (t.get("annos") or []):
            mark = resolve(p, a)
            if mark:
                break
        marked, bare = decls.setdefault(simple, ([], []))
        if mark:
            marked.append(mark)
        else:
            bare.append(p)
            unmarked.append("%s at %s:%s carries %s" % (
                simple, p, t.get("line", "?"), t.get("annos") or []))

    # A name counts once every declaration of it is marked, not once any one
    # of them is. Two packages can declare the same simple name, so keying
    # the hit on the name alone would let seven marked decoys under a new
    # package stand in for the seven declarations the injection points
    # actually resolve to, left unmarked where they are. One unmarked
    # declaration of a name is that name unported, wherever it sits.
    hits = {simple: "%s marked %s" % (simple, ", ".join(sorted(set(marked))))
            for simple, (marked, bare) in decls.items() if marked and not bare}

    need = CAL["qualifier_annotations_min"]
    missing = sorted(want - set(hits))
    return g.add("qualifier-annotations", len(hits) >= need,
                 ("%d of the %d surviving binding annotation(s) marked as a "
                  "qualifier the container recognises on its own, need >=%d"
                  % (len(hits), len(want), need))
                 + ("; missing " + ", ".join(missing) if missing else ""),
                 1.0,
                 {"marked": sorted(hits.values()), "missing": missing,
                  "unmarked": unmarked, "need": need})


qualifier_annotations()


def provider_classes():
    """A class that produces a value for injection is still a class.

    §1.3 refuses the fold -- fifteen `Provider<T>` implementations are not to
    become fifteen `@Bean` bodies -- and §1.5 says the survivors are ported
    onto the container's own factory interface. Until now the only thing
    behind either sentence was `sources-survive`, which is a census and not a
    construct: the complete fold reaches 96 files against a floor of 100 and
    fails, but folding five and leaving ten lands above the floor and says
    nothing at all. This asks the construct.

    Own types whose `implements` clause names one of the interfaces §1.3
    offers as the answer to Guice's `Provider<T>` -- Spring's `ObjectFactory`
    and `ObjectProvider`, the JSR-330 `Provider`, the JDK's `Supplier` -- or
    `FactoryBean`, which is the same construct under a different contract.
    Read from the AST because `implements` is the thing the instruction talks
    about, because the simple name is what the clause actually says, and
    because a failure then names the file to open rather than a class file.

    A type also counts when it extends an own type that counts, transitively.
    A producer whose concrete subclass is the registered bean is still one
    class per produced value, and this reference has one: the class that
    supplies the canonical web URL over HTTP extends the one that implements
    `Supplier` and writes no clause of its own. Refusing it would have been a
    gate telling a candidate not to use inheritance. Only own types are
    walked, so extending a library class named `Provider` is not a way in.

    The Guice exclusion is not optional and is the whole reason this reads
    files and types together. `jakarta.inject.Provider` and
    `com.google.inject.Provider` are both written `Provider` at the use site,
    and the baseline's sixteen provider classes implement the second one -- so
    a gate on the simple name alone would hand a full pass to the untouched
    tree this rung exists to move. The declaring file's import list is what
    tells them apart, and a type that spells `com.google.inject.Provider`
    inline in the clause is excluded on the clause.

    The floor is 10 against a reference measuring 14, and the four units of
    slack are real: §1.3's own text allows a factory interface to go with the
    generator where the caller already holds every argument, and a one-method
    producer written as a lambda or as a `@Bean` body is a shape a correct
    port may reasonably choose for a few of them. What 10 refuses is the
    wholesale fold, which is what the instruction refuses.

    Production source only, both passes. A producer under `src/test/java` is
    a fixture, not a port, and the closure must not walk into the test tree
    either. The reference has fifteen and none of them is a test type, so the
    cut moves no number here.

    The limit, and it is a real one: this counts a type with the right
    `implements` clause, and nothing more. Ten empty
    `class DummyN implements Supplier<String>` types under `src/main/java`
    pass it. The obvious tightening -- intersect with the types some
    configuration actually declares as beans -- does not work,
    because it does not hold on a correct port: this reference's own
    producers carry no stereotype and reach the container through `@Import`
    and through `new` inside a `@Bean` body, so an intersection tight enough
    to refuse the dummies would have pinned the reference's exact shape, and
    a gate only the reference passes is worse than a census. So
    this gate is a census of a construct, it is worth 1.0, and it is not part
    of what refuses a decorative tree -- `container-api-used`,
    `container-bootstrap` and `bean-definitions` are. The CAL block says the
    same thing.

    Measured: reference 15 (13 `ObjectFactory`, 1 `Supplier`, 1 inherited,
    none excluded); baseline 0 (16 types match on the simple name, all 16
    excluded as Guice's).
    """
    files = {f.get("p"): f for f in b.ast.files()}

    def guice_provider_import(path):
        for im in (files.get(path, {}).get("imports") or []):
            nm = im.split(" ")[0].rstrip(".*").rstrip(".")
            if S.under(nm, GUICE) and (nm.endswith(".Provider")
                                       or nm in GUICE):
                return nm
        return None

    hits, excluded, direct = {}, {}, {}
    prod_types = [t for t in b.ast.kind("type") if not in_tests(t.get("p"))]
    for t in prod_types:
        named = [i for i in (t.get("implements") or [])
                 if i.rsplit(".", 1)[-1] in FACTORY_IFACES]
        if not named:
            continue
        why = next((i for i in named if S.under(i, GUICE)), None) \
            or guice_provider_import(t.get("p"))
        if why:
            excluded["%s (%s)" % (t.get("n", "?"), why)] = why
            continue
        hits["%s implements %s" % (t.get("n", "?"), ", ".join(named))] = named
        direct[t.get("n", "?")] = t
    # Then the subclasses, to a fixed point: a producer whose subclass is the
    # bean is still a producer. Matched on the simple name as well as the
    # whole, because an `extends` clause in the same package writes neither.
    pending = [t for t in prod_types if t.get("n", "?") not in direct]
    grown = True
    while grown:
        grown = False
        for t in list(pending):
            sup = t.get("extends") or []
            if isinstance(sup, str):
                sup = [sup]
            for sname in sup:
                tail = sname.rsplit(".", 1)[-1]
                base = next((n for n in direct
                             if n == sname or n.rsplit(".", 1)[-1] == tail),
                            None)
                if base is None:
                    continue
                if guice_provider_import(t.get("p")):
                    excluded["%s (extends %s, file imports Guice)"
                             % (t.get("n", "?"), base)] = base
                else:
                    hits["%s extends %s" % (t.get("n", "?"), base)] = [base]
                    direct[t.get("n", "?")] = t
                pending.remove(t)
                grown = True
                break
    if excluded:
        S.record("provider-classes.guice-excluded",
                 "%d type(s): %s" % (len(excluded),
                                     ", ".join(sorted(excluded)[:8])))
    r = g.present("provider-classes", hits, CAL["provider_classes_min"], 1.0)
    r.evidence["guice_excluded"] = sorted(excluded)[:200]
    return r


provider_classes()


# ---------------------------------------------------------------------------
# The container is stood up, filled, and told when to start
#
# Everything above this line is satisfied by a tree that names Spring. These
# four ask whether anything builds a context, whether the bindings the deleted
# modules declared were declared again, whether the objects that started and
# stopped still do, and whether the per-request scope survived as a scope. See
# the CAL block for each floor's derivation and for the limit: none of
# them can prove a context was ever refreshed.
# ---------------------------------------------------------------------------


def container_bootstrap():
    """Something in this project stands a context up, and something drives it.

    `spring-6-or-7` says the container resolves and `spring-classes` says this
    project's classes name it; neither asks whether anything ever builds one.
    A tree that annotates everything and constructs its graph with `new`
    behind a project-local registry passes both. This is the gate that is
    supposed to refuse that tree, and it is asked as a conjunction because
    either half alone is a name rather than a container.

    **Half A -- a context object exists in this tree.** Three routes in:

      A1  production source writes the name of a context implementation
          (`context_impls`). Read from `typeuse`, one record per written
          `ClassOrInterfaceType`, so `new AnnotationConfigWebApplicationContext()`,
          a field declared `GenericApplicationContext ctx` and a
          `GenericWebApplicationContext.class` literal each produce one.
      A2  an own production class's *superclass* is one of `bootstrap_types`,
          read from the bytecode. This is the route for a port that only
          extends Spring's own machinery: a subclass of
          `AbstractAnnotationConfigDispatcherServletInitializer` overrides
          three methods returning `Class[]` and `String[]` and names no
          Spring context type anywhere but its `extends` clause.
      A3  an own production class carries the dotted name of one of them as a
          string constant -- how a `contextClass` init-param is spelled.
      A4  production source writes one of `boot_context_drivers`:
          `SpringApplication` or `SpringApplicationBuilder`. Neither is a
          context, and both exist only to build one, refresh it and hand it
          back, which is the same claim on the tree that A1 makes.
      A5  an own production class references one of those two in its constant
          pool. Not redundant with A4: `SpringApplication.run(App.class,
          args)` writes no `ClassOrInterfaceType` and emits no `typeuse`, so
          the shortest correct entry point in the rung has no A4 site at all.

    A4 and A5 are why this gate does not depend on an unrelated `main`.
    An application that starts its own container writes the thing that builds
    the context rather than the context; the three routes above it all
    assume a tree where those are the same sentence, and in this rung
    they are not.

    Naming a context interface is deliberately *not* a route.
    `@Scope(WebApplicationContext.SCOPE_REQUEST)` names one and creates
    nothing; so does a method declared
    `install(ServletContext, WebApplicationContext)`. A gate that counted
    any constant-pool reference to any context type, interfaces included,
    would pass a hand-wired tree
    on the strength of a `@Scope` attribute.

    **Half B -- something in this tree drives that context.** Four routes:

      B1  a production call to one of `context_verbs`: `refresh`, `getBean`,
          `registerBean`, `registerSingleton`, `getBeanFactory` and the rest
          -- methods a context or a bean factory has and a plain object does
          not. `register` is not among them, because a project-local registry
          plausibly has one.
      B2  a production declaration of one of `context_template_methods`,
          which exist to be overridden and mean nothing anywhere else. This
          is what an initializer-base port hits when it hits nothing else.
      B3  a production use of one of `definition_carriers` -- the types that
          carry definitions into a context, or a context into the servlet
          container.
      B4  a production call to one of `boot_driver_verbs` -- `run` -- in a
          file that holds a `boot_context_drivers` name. `run` is how a
          `SpringApplication` or a `SpringApplicationBuilder` is driven, and
          it is also the commonest method name in Java, so it is read only
          where a driver was written; the file is the scope because the call
          record carries the method name and the line and not the receiver.
          For the bytecode-only A5 case the file is recovered from the
          declaration of the class that referenced the driver.

    The halves stay a conjunction and B4 does not quietly merge them. A tree
    with no Boot driver has no B4 site no matter how many `run` methods it
    has, and a tree with a driver and nothing else still has to reach one of
    B1-B4 before the gate passes.

    Weight 2.0, because this is the positive half of `no-guice-classes`, which
    is worth 2.0 for deleting the old container, and the two ask the same
    question from the two sides.

    The limit: a context that is built and refreshed on a code path
    nothing reaches passes this. Whether the container is the thing actually
    serving the requests is a behavioural question, and the 428 observations
    are where it is asked.

    Measured: a hand-wired tree A=0 B=0; reference A=4 B=13; the tree this
    step is handed A=17 B=18.
    """
    carriers = set(CAL["definition_carriers"])
    half_a, half_b = dict(context_objects()), {}
    for u in _prod_typeuses():
        if _simple(u.get("n")) in carriers:
            half_b[_site(u, u.get("n"))] = "B3"
    verbs = set(CAL["context_verbs"])
    for n in _prod_calls():
        if n.get("d") in verbs:
            half_b[_site(n, n.get("d"))] = "B1"
    tmpl = set(CAL["context_template_methods"])
    for m in b.ast.kind("method"):
        if in_tests(m.get("p")):
            continue
        if (m.get("sig") or "").split("(", 1)[0] in tmpl:
            half_b["%s.%s" % (m.get("owner", "?"), m.get("sig", "?"))] = "B2"
    driver_verbs = set(CAL["boot_driver_verbs"])
    driver_files = boot_driver_sites()[1]
    for n in _prod_calls():
        if n.get("d") in driver_verbs and n.get("p") in driver_files:
            half_b[_site(n, n.get("d"))] = "B4"
    ok = bool(half_a) and bool(half_b)
    return g.add("container-bootstrap", ok,
                 ("%d site(s) build or hold a context, need >=1; %d site(s) "
                  "drive one, need >=1" % (len(half_a), len(half_b)))
                 + ("" if ok else
                    "; missing: " + " and ".join(
                        ([] if half_a else ["a context object"])
                        + ([] if half_b else ["anything that drives one"]))),
                 2.0,
                 {"context_objects": sorted(half_a)[:200],
                  "context_drivers": sorted(half_b)[:200],
                  "routes_a": sorted(set(half_a.values())),
                  "routes_b": sorted(set(half_b.values()))})


container_bootstrap()


def bean_definitions():
    """The bindings the deleted modules declared were declared again.

    `java-configuration` asks for one `@Configuration` class and no more; a
    tree with one such class and nothing in it passes. The seventeen files
    §1.2 deletes declared 43 distinct `bind(X.class` targets between them, and
    this gate asks where they went. Counting `@Bean` methods and stereotyped
    types wherever they are written is not what §1.4 says. It says the
    container is configured "from Java, not from XML". It does not say
    "from annotations".

    Four routes, summed, because four shapes are all real ports of a `bind`:

      R1  a `@Bean` method whose *owner type* carries `@Configuration` or a
          stereotype. The owner requirement is Spring's own rule: a `@Bean`
          method on a class no container ever receives declares nothing.
      R2  a type carrying a stereotype -- but only when production source
          switches scanning on: a `@ComponentScan`, a
          `ClassPathBeanDefinitionScanner`, or a `scan(...)` call in a file
          that also names a context implementation. Fifteen `@Named` classes
          and no scan is fifteen annotations, not fifteen bean
          definitions.
      R3  a functional registration: a call to `registerBean`,
          `registerBeanDefinition` or `registerSingleton`, or a use of one of
          the definition builders and readers. Without this route a correct
          Spring 5 port that
          builds a `GenericApplicationContext` and calls `registerBean` forty
          times would score 0.
      R4  a class named in an `@Import`. Each is a definition the importing
          class hands to the container, and `@Import` is how a port that
          keeps its configuration in several classes assembles them.

    `@Named` counts among the stereotypes for CONTAINER_API's reason: 5.3
    reads it as one natively. An R1 method, an R2 type, an R3 call site and
    an R4 imported class cannot be the same record, so the sum
    double-counts nothing; R4 is deduplicated on the imported name, so a
    class imported by two configurations is one definition.

    The limit: switching scanning on and writing `@Bean` methods are
    still declarations. Nothing static can tell a `@ComponentScan` a context
    reads from one a context never sees. `container-bootstrap` asks whether a
    container exists to receive any of it, and the two are meant to be read
    together.

    Measured: baseline 0; reference 68 (34 R1, 0 R2, 2 R3, 32 R4).
    """
    hits = {}
    stereo = set(CAL["stereotypes"])
    receivers = set()
    for t in b.ast.kind("type"):
        if in_tests(t.get("p")):
            continue
        for a in (t.get("annos") or []):
            if _simple(a) == "Configuration" or _simple(a) in stereo:
                receivers.add(t.get("n"))
                break
    for m in annotated(b.ast.kind("method"), "Bean"):
        if in_tests(m.get("p")) or m.get("owner") not in receivers:
            continue
        hits["%s.%s (@Bean)" % (m.get("owner", "?"), m.get("sig", "?"))] = "R1"

    # R2's precondition: is component scanning switched on anywhere in
    # production source? Three spellings, and the third is only read as one
    # beside a context implementation -- `scan` alone is too common a name.
    scanning = bool(_prod_annos(CAL["scan_annos"])) or any(
        _simple(u.get("n")) in CAL["scan_carriers"] for u in _prod_typeuses())
    if not scanning:
        ctx_files = {u.get("p") for u in _prod_typeuses()
                     if _simple(u.get("n")) in CAL["context_impls"]}
        scanning = any(n.get("d") in CAL["scan_calls"] and n.get("p") in ctx_files
                       for n in _prod_calls())
    if scanning:
        for simple in CAL["stereotypes"]:
            for t in annotated(b.ast.kind("type"), simple):
                if not in_tests(t.get("p")):
                    hits["%s (@%s)" % (t.get("n", "?"), simple)] = "R2"

    calls = set(CAL["definition_calls"])
    for n in _prod_calls():
        if n.get("d") in calls:
            hits[_site(n, n.get("d"))] = "R3"
    builders = set(CAL["definition_builders"])
    for u in _prod_typeuses():
        if _simple(u.get("n")) in builders:
            hits[_site(u, u.get("n"))] = "R3"
    for a in _prod_annos(CAL["import_annos"]):
        for arg in (a.get("args") or []):
            nm = str(arg).strip()
            if nm.endswith(".class"):
                hits["@Import %s" % nm[:-len(".class")]] = "R4"
    r = present_given_context("bean-definitions", hits,
                              CAL["bean_definitions_min"], 1.0,
                              "no context object anywhere to define into")
    r.evidence["routes"] = {k: sum(1 for v in hits.values() if v == k)
                            for k in ("R1", "R2", "R3", "R4")}
    return r


bean_definitions()


def container_lifecycle():
    """The objects that started and stopped still do, on the container's terms.

    `no-lifecycle-classes` and `no-lifecycle-source` say the project's own
    `com.google.gerrit.lifecycle` package is gone. Both are satisfied by
    deleting it and letting the caches, the work queue, the repository manager
    and the SSH daemon never be told to stop -- a tree that scores 2.0 on this
    rung for removing the thing and 0 for replacing it, which is the shape of
    an absence gate with no presence beside it. This is the presence.

    Three routes, and they are alternatives rather than a hierarchy: §1.3
    names the work, not the contract, and a candidate arriving from the
    JSR-250 side reaches for `@PostConstruct` first.

      L1  an own production class whose bytecode supertypes name one of
          `lifecycle_types`: `Lifecycle` and `SmartLifecycle`,
          `InitializingBean` and `DisposableBean`, and
          `ApplicationListener` and `SmartInitializingSingleton`. An
          `ApplicationListener<ContextRefreshedEvent>` that starts the daemon
          when the context comes up is doing exactly what `LifecycleManager`
          did.
      L2  `@PostConstruct` or `@PreDestroy` in the JSR-250 spelling Spring
          reads natively, or Spring's own `@EventListener`, which is the
          annotation form of L1's first addition and what a candidate who
          wants no marker interface writes.
      L3  a `@Bean` that sets `initMethod` or `destroyMethod`.

    Only the `jakarta.annotation` spelling of the JSR-250 pair counts, for
    CONTAINER_API's reason: 5.3's `CommonAnnotationBeanPostProcessor` read
    `javax.annotation`, Spring 6's reads `jakarta.annotation`, and this is the
    rung that moves the line. The annotation is matched on its simple name, so
    the import list is what tells the two apart -- unless the use site wrote
    the name out in full, which carries its own package.

    The reference reaches none of this (it takes L1 six times over), so the
    swap costs it nothing and is here for the candidate who took the JSR-250
    route at step 03: renaming those two imports along with the servlet ones
    keeps the row, leaving them behind loses it, and losing it is right --
    a `javax.annotation.@PostConstruct` under Spring 6 is a method that is
    never called, and the object it was initialising is not managed.

    The unit is one managed object, not one site, and the keys are built to
    make that true:

      - L1 keys are bytecode names and L2 keys are AST names, so `Outer$Inner`
        is normalised to `Outer.Inner` and the two families meet.
      - An anonymous `new Lifecycle() {...}` inside `Outer` keys as `Outer.1`
        and does *not* merge with `Outer`. That is correct and not a missed
        merge: the anonymous object and its enclosing class are two objects,
        and the container manages them separately.
      - An L3 `@Bean(initMethod=...)` is keyed by the site, because each
        `@Bean` declares a different bean and the configuration class holding
        them is not the object being managed -- but it is *dropped* when the
        method's return type is already counted by L1 or L2. That case is one
        object named twice, and must not be counted twice. The return
        type comes from the `ret` field `AstScan` records for every method.

    Measured: baseline 0; reference 6 (six interface implementations, no
    annotation and no `@Bean` attribute -- the other spellings are here for
    the candidate, not for the reference).
    """
    objs = {}

    def note(ident, reason):
        objs.setdefault(str(ident).replace("$", "."), set()).add(reason)

    for c, ifs in own_implements(CAL["lifecycle_types"]).items():
        note(c, "implements " + ", ".join(i.rsplit(".", 1)[-1] for i in ifs))
    jsr250 = {f.get("p"): [im.split(" ")[0].rstrip(".*").rstrip(".")
                           for im in (f.get("imports") or [])]
              for f in b.ast.files()}

    def reads_jsr250(path, simple, written):
        if written.startswith("jakarta.annotation."):
            return True
        ims = jsr250.get(path) or []
        return (("jakarta.annotation." + simple) in ims
                or "jakarta.annotation" in ims)

    for simple in CAL["lifecycle_annos"] + CAL["lifecycle_event_annos"]:
        for r in annotated(b.ast.kind("type"), simple) \
                + annotated(b.ast.kind("method"), simple):
            if in_tests(r.get("p")):
                continue
            written = next((a for a in (r.get("annos") or [])
                            if a == simple or a.endswith("." + simple)), simple)
            if simple in CAL["lifecycle_annos"] \
                    and not reads_jsr250(r.get("p"), simple, written):
                continue
            note(r.get("owner") or r.get("n", "?"), "@" + simple)

    # L3, last, so that `counted` is the finished L1+L2 set. An annotation
    # record carries no return type, and the method record that does is the
    # one with the greatest line at or before it in the same file --
    # JavaParser begins a method declaration at its first annotation, so the
    # `@Bean` line and its method's line coincide when `@Bean` is written
    # first and the method's line is the smaller one when it is not.
    counted = {k.rsplit(".", 1)[-1] for k in objs}
    by_file = {}
    for m in b.ast.kind("method"):
        by_file.setdefault(m.get("p"), []).append(m)

    def produced_by(path, line):
        best = None
        for m in by_file.get(path) or []:
            ln = m.get("line") or 0
            if ln <= line and (best is None or ln >= (best.get("line") or 0)):
                best = m
        return _simple((best or {}).get("ret") or "")

    for a in _prod_annos(("Bean",)):
        keys = [k for k in (a.get("keys") or [])
                if k in ("initMethod", "destroyMethod")]
        if not keys:
            continue
        ret = produced_by(a.get("p"), a.get("line") or 0)
        if ret and ret in counted:
            continue  # same object, already counted as an L1/L2 implementation
        note("%s:%s" % (a.get("owner", "?"), a.get("line", "?")),
             "@Bean " + "/".join(keys))
    hits = {"%s (%s)" % (k, ", ".join(sorted(v))): sorted(v)
            for k, v in objs.items()}
    return g.present("container-lifecycle", hits, CAL["lifecycle_min"], 1.0)


container_lifecycle()


def container_scope():
    """The per-request scope survived as a scope the container knows about.

    Guice's `SshScope` handed out per-request objects through a custom
    `com.google.inject.Scope`, and §1.3 asks for the same thing on Spring's
    side. `no-guice-classes` is satisfied by deleting it and threading the
    request objects through as method arguments instead -- a correct-looking
    tree that has quietly dropped a container feature the 428 observations
    exercise on every SSH command.

    Asked as a conjunction because either half alone is decoration. A `Scope`
    implementation nobody registers is a class the container never consults;
    a `registerScope` call with no implementation behind it does not compile.
    Both are required and the failure message says which half is missing.
    Each half has two routes in:

      S1  an own production class whose bytecode supertypes name Spring's
          `Scope` interface, or
      S2  one that extends one of Spring's own scope base classes --
          `SimpleThreadScope` and `AbstractRequestAttributesScope` are the
          two a per-connection scope is naturally built on, and a class that
          extends either has `Scope` nowhere in its own interface list.
      R1  a `registerScope` call in production source, or
      R2  a `CustomScopeConfigurer`, the declarative spelling of the same
          registration.

    The implementation half reads the bytecode and not the `implements`
    clause, and that is not a preference: this reference writes its scope as
    `new Scope() {...}`, compiled to `SshScope$1`, which has no clause in the
    source anywhere, so an AST gate on the clause would fail the reference.
    The registration half has to be the source, the other way round -- the
    constant pool records an invocation's owner and descriptor but not the
    name of the method invoked, so `registerScope` is only visible in text.

    The limit, which is not fixable from a static scan: a `new Scope(){}`
    whose six methods all
    throw, beside a `registerScope` call on a path nothing reaches, passes
    this gate. Whether the scope scopes anything is behavioural, and the 428
    observations are where it is asked -- every SSH command in the suite goes
    through the per-request objects this scope hands out.

    Measured: baseline 0 and 0; reference 1 and 1.
    """
    impls = own_implements(CAL["scope_types"])
    regs = {}
    for n in _prod_calls():
        if n.get("d") == "registerScope":
            regs[_site(n, "registerScope")] = "registerScope"
    for f in b.ast.files():
        if in_tests(f.get("p")):
            continue
        for im in (f.get("imports") or []):
            nm = im.split(" ")[0].rstrip(".*").rstrip(".")
            if nm in CAL["scope_registration"]:
                regs["%s (import %s)" % (f.get("p", "?"), nm)] = nm
    for u in _prod_typeuses():
        if _simple(u.get("n")) == "CustomScopeConfigurer":
            regs[_site(u, "CustomScopeConfigurer")] = u.get("n")
    ok = bool(impls) and bool(regs)
    return g.add("container-scope", ok,
                 ("%d own type(s) implement or extend a container scope, "
                  "need >=1; %d registration site(s), need >=1"
                  % (len(impls), len(regs)))
                 + ("" if ok else
                    "; missing: " + " and ".join(
                        ([] if impls else ["a Scope implementation"])
                        + ([] if regs else ["a registration of it"]))),
                 1.0,
                 {"scope_types": sorted(impls),
                  "scope_registrations": sorted(regs)[:50]})


container_scope()


def assisted_construction():
    """Objects the generated factories built are still built by the container.

    §1.3's assisted-injection bullet asks that an object whose caller supplies
    some of its arguments still has the rest resolved for it -- "the caller's
    arguments passed in, the container's dependencies resolved, the object
    autowired on the way out" -- and allows the factory interface itself to go
    where the caller already holds every argument. §1.5 says the
    survivors are counted, and this gate is that count.

    Four routes, because Spring has four ordinary answers and §1.3 requires
    none of them:

      F1  a production call to one of `assisted_verbs` -- the
          `AutowireCapableBeanFactory` methods, which exist for precisely
          this;
      F2  a production use of `AutowireCapableBeanFactory` or
          `ObjectProvider`, whose `getObject(args...)` is the same construct
          with a nicer face;
      F3  a `@Scope` naming the prototype scope, the answer for an object
          built fresh per caller;
      F4  a `@Lookup` method, Spring's own container-generated factory method
          and the nearest thing it has to `FactoryModule.factory`.

    The floor is 1 and not a count. How many of this tree's assisted
    factories survive as interfaces is exactly the judgement §1.3 leaves to
    the candidate, and a gate with a count in it would be a gate against that
    sentence. What 1 refuses is a tree in which every object a generated
    factory used to build is now built with `new` and injected by nobody.

    The limit: a single `@Scope("prototype")` on a class nothing asks
    for twice passes this. It is worth 1.0 for that reason.

    Measured: baseline 0; reference 37 (2 F1, 26 F2, 9 F3, 0 F4).
    """
    hits = {}
    verbs = set(CAL["assisted_verbs"])
    for n in _prod_calls():
        if n.get("d") in verbs:
            hits[_site(n, n.get("d"))] = "F1"
    types = set(CAL["assisted_types"])
    for u in _prod_typeuses():
        if _simple(u.get("n")) in types:
            hits[_site(u, u.get("n"))] = "F2"
    for a in _prod_annos(("Scope",) + CAL["assisted_annos"]):
        if _simple(a.get("n")) == "Scope":
            for arg in (a.get("args") or []):
                if any(w.lower() in str(arg).lower()
                       for w in CAL["assisted_scope_names"]):
                    hits[_site(a, "@Scope " + str(arg))] = "F3"
        else:
            hits[_site(a, "@" + _simple(a.get("n")))] = "F4"
    r = present_given_context("assisted-construction", hits, 1, 1.0,
                              "no context object anywhere to build from")
    r.evidence["routes"] = {k: sum(1 for v in hits.values() if v == k)
                            for k in ("F1", "F2", "F3", "F4")}
    return r


assisted_construction()


# ---------------------------------------------------------------------------
# The deployment descriptor, and the one that never existed
# ---------------------------------------------------------------------------
#
# Step 02 wrote this as an absence of a descriptor *below* 3.1, because there
# were two correct answers there and one of them was to keep the file and
# declare a version. This rung has one: the three things `web.xml` declares are
# `GuiceFilter`, its `/*` mapping and the Guice context listener, and when
# Guice goes there is nothing left for the file to say. So the gate is the
# stronger one, and it subsumes step 02's -- a tree with no `<web-app>` in it
# has no `<web-app>` below 3.1 either, which is why `no-pre-3-1-web-xml` is not
# in the inherited tripwire below.
#
# Parsed and not grepped, and matched on the root element rather than
# on the filename, because a descriptor moved to `src/main/webapp/WEB-INF/`
# under another name is still the thing being retired and a `web.xml` holding
# something else entirely is not. `xml_descriptors` reports an unparsable file
# as a hit when its raw text names one of the namespaces, which is the answer
# to retiring a descriptor by breaking its syntax.
G.xml_descriptors(g, b, "no-web-xml", roots=("web-app",),
                  namespaces=WEB_APP_NS, weight=2.0)

# And the same question of the deployable, which is not the same question. The
# gate above walks `/app` and sees what the candidate's source tree holds; this
# one reads the archive the build produced, which is where a descriptor
# generated by a plugin, copied from a filtered resource directory or carried
# in by an overlay becomes visible. The baseline WAR ships two of them -- one
# inside the packaged archive and one in the exploded directory beside it --
# and the reference ships none among 937 entries.
packaged_web_xml = sorted({e.get("o", "") for e in b.art.entries()
                           if PACKAGED_WEB_XML.search(e.get("o", ""))})
g.add("no-web-xml-packaged",
      not packaged_web_xml and len(b.art.entries()) > 0,
      ("%d WEB-INF/web.xml entr(y/ies) inside the artifact: %s"
       % (len(packaged_web_xml), ", ".join(packaged_web_xml[:4]))
       if packaged_web_xml else
       "no WEB-INF/web.xml among %d artifact entries" % len(b.art.entries())),
      1.0, {"sites": packaged_web_xml, "entries": len(b.art.entries())})

# The other descriptor -- the one this tree has never had and the one a
# candidate is most likely to reach for. "Port Guice to Spring" has a 2004
# answer and a 2013 answer, and the 2004 answer is a `<beans>` document listing
# every class with its constructor arguments spelled out in XML. §1.3 asks for
# the other one by name, and this is the gate that means it.
#
# Written with `xml_descriptors` and not `no_files` for the reason that helper
# exists: `web.xml` is always called web.xml, and a Spring bean definition is
# whatever the person who wrote it decided to call it. Matched on the root
# element, which is `<beans>` in every version of the schema.
G.xml_descriptors(g, b, "no-xml-bean-definitions", roots=("beans",),
                  namespaces=SPRING_BEANS_NS, weight=1.0)

# End of the collected region: every gate above this line went into `DI` and
# is reported as one row, `inherited-di-hold`, at the bottom of this file.
g = _journal


# ---------------------------------------------------------------------------
# Step 04's rung, held (start of the second collected region)
# ---------------------------------------------------------------------------
#
# Everything from here to the matching `g = _journal` below is step 04's
# file, verbatim but for two widenings named where they are made:
# `servlet-api-packaged` counts either spelling of the servlet package, and
# `retired_bootstrap` names the jakarta spelling of the bootstrap interface
# beside the javax one. Both are the same edit in opposite directions and
# both are forced by the same thing -- an inherited gate written in a
# spelling this rung retires is a gate that fails the right answer, or a
# gate nothing can fail, and neither holds anything.
#
# Three of step 04's twelve do fail on the predecessor tree at this rung, and
# they fail for this rung's reasons rather than for a regression:
# `spring-boot-dep` is pinned to `^3\.` and `servlet-api-packaged`'s
# jakarta-side reader is `jakarta-servlet-api-packaged` below. Step 04's own
# file did the same thing to step 02's `servlet-api-3` and said so in its
# weight table; this is that, at the next rung.
_journal, g = g, BOOT

# ---------------------------------------------------------------------------
# Step 04's rung: the deployable
# ---------------------------------------------------------------------------
#
# Twelve gates, and every one of them is the same change seen from a different
# side: the build stops producing an archive that a server somewhere else
# unpacks, and starts producing a program.
#
# Four of them read what the build actually produced -- its packaging, its
# manifest, and what is inside it. Two read the dependency graph. Four read
# this project's own source and bytecode. The last two are the two halves of
# this rung a candidate is most likely to get half-right: the container
# callback it has to stop implementing, and the logging provider it can
# duplicate without noticing.
#
# The rule the container gates in the held region above established applies
# here too, and for the same reason: every gate that can take more than one
# route in takes more than one, and names its routes in its own docstring. The
# instruction leaves the shape of almost all of this to the candidate -- which
# container to embed, whether the entry point is one class or four, whether a
# servlet is registered by a `ServletRegistrationBean` or by `@WebServlet` --
# and a gate with one route is a gate only the reference's shape passes.

ART_BASE = os.path.basename(art) if art else ""


def inside_artifact(origin):
    """Whether a scanned origin is a path inside the deployable itself.

    ArtifactScan walks every `target/` directory, which at this rung means the
    packaged jar, the loose `target/classes` beside it and -- for a candidate
    who kept a war module -- an exploded webapp directory whose every class is
    then reported a second time under a second origin. `own_classes` already
    separates loose output from archived; this separates the one archive that
    is the deployment from every other archive under `target/`.

    The origin of a class two archives deep reads
    `outer.jar!BOOT-INF/lib/inner.jar!pkg/Name.class`, so the test is a prefix
    on the outermost segment. An empty artifact path makes this false for
    everything, which is exactly what it should mean: a build that produced no
    deployable packaged nothing. Every gate built on this therefore fails when
    `build-and-artifact` fails, rather than passing vacuously.
    """
    return bool(ART_BASE) and (origin or "").startswith(ART_BASE + "!")


def packaged_providers(prefixes):
    """(archive -> class count, total) for classes under `prefixes`, inside it.

    Reported by providing archive rather than by class, because the archive is
    the actionable unit: `tomcat-embed-core-9.0.83.jar` names the dependency
    that brought the package in, and two entries say the classpath has two
    providers of one package. The innermost `!` segment is the archive; a
    class packaged directly in the deployable reports the deployable.
    """
    arcs, total = {}, 0
    for c in b.art.classes():
        o = c.get("o") or ""
        if not inside_artifact(o) or not S.under(c.get("n", ""), prefixes):
            continue
        total += 1
        key = os.path.basename(o.rsplit("!", 1)[0])
        arcs[key] = arcs.get(key, 0) + 1
    return arcs, total


def _pom_coords(el):
    """(groupId, artifactId, scope) of a `<dependency>` or `<parent>`."""
    kv = {}
    for ch in el:
        kv[G._local(ch.tag)] = (ch.text or "").strip()
    return (kv.get("groupId", ""), kv.get("artifactId", ""),
            kv.get("scope", ""))


# ---------------------------------------------------------------------------
# What the build produces
# ---------------------------------------------------------------------------

def no_war_packaging():
    """No module in the reactor builds a war any more.

    Two routes in, because a pom says "war" two ways and a candidate who
    changes one and not the other still has a reactor that produces one: the
    `<packaging>` element, and a binding of `maven-war-plugin`. Read from the
    parsed document and not from the text, for `compiler_release`'s reason --
    a commented-out `<packaging>war</packaging>` is not a declaration, and
    `<packaging>${pkg}</packaging>` is, so the value is resolved against the
    reactor's merged properties before it is compared.

    A pom that does not parse fails this gate outright, as it fails
    `compiler-release` and unlike `no_parent`. This is a claim about the whole
    reactor, and a module nobody can read is a module whose packaging is
    unknown; "I could not tell" is a failure of a positive claim about every
    module, not a pass.
    """
    docs = G._pom_docs(b)
    props = G._pom_properties(docs)
    hits, broken = [], []
    for rel, root, _text in docs:
        if root is None:
            broken.append(rel)
            continue
        for el in root.iter():
            tag = G._local(el.tag)
            txt = (el.text or "").strip()
            if not txt:
                continue
            if tag == "packaging" and G._resolve(txt, props) == "war":
                hits.append("%s (<packaging>war</packaging>)" % rel)
            elif tag == "artifactId" and txt == "maven-war-plugin":
                hits.append("%s (maven-war-plugin)" % rel)
    if broken:
        return g.add("no-war-packaging", False,
                     "%d pom(s) did not parse, so the reactor's packaging "
                     "is unknown: %s"
                     % (len(broken), ", ".join(sorted(broken)[:4])), 1.5,
                     {"unparsable": sorted(broken), "poms": len(docs)})
    return g.absent("no-war-packaging", sorted(set(hits)), 1.5)


def no_war_artifact():
    """The build produced a jar, and produced no war anywhere.

    Scoped to the `target/` directories scan.sh enumerated rather than to the
    whole of /app, and that scope is what makes the question answerable:
    build.sh deletes every one of those directories before it builds, so
    anything found in one now was produced by this run from the candidate's
    own source. A `.war` checked into `src/test/resources` would be a fixture;
    a `.war` under `target/` is what this reactor still makes.

    The two halves are asked together because either alone is satisfiable
    without the other: a reactor can produce a jar *and* a war, and a reactor
    that produces neither has no war under target either.
    """
    roots = G._lines(os.path.join(b.out, "scan-roots.txt"))
    wars = sorted({os.path.relpath(p, b.app)
                   for r in roots
                   for p in S.find_files(r, suffixes=(".war",),
                                         skip_dirs=(".git",))})
    ok = bool(art) and art.endswith(".jar") and not wars
    return g.add("no-war-artifact", ok,
                 "artifact=%s; %d war file(s) under target/%s"
                 % (os.path.basename(art) if art else "NONE", len(wars),
                    (": " + ", ".join(wars[:4])) if wars else ""),
                 0.5, {"artifact": art, "wars": wars})


def executable_jar():
    """`java -jar <artifact>` is the whole deployment. Five conjuncts.

    Read from the artifact itself -- its manifest and its entry list -- because
    that is the only place the answer is. A repackaged Boot jar carries two
    names: `Main-Class` is Boot's loader, which is what `java -jar` invokes,
    and `Start-Class` is the application class the loader then calls.
    `Start-Class` is preferred when it is there and `Main-Class` is the
    fallback, so the entry class is found whichever of the two names it.

      1  the artifact is a `.jar` and its manifest can be read;
      2  the entry class is one this project compiled. Checked against
         `production_classes`, so a manifest naming a class out of a
         dependency, or naming nothing at all, fails here. This is the
         conjunct that stops `Main-Class: org.springframework.boot.loader.
         JarLauncher` from being the whole answer. It does not contradict
         conjunct 5, which requires exactly that `Main-Class`: the two names
         are different names, and this one is asked of the *entry* class,
         which is `Start-Class` when the manifest has one;
      3  that class declares a `public static main(String[])` in this tree's
         source. The AST and the bytecode are two different instruments and
         this is deliberately the source one: `Start-Class` is a string in a
         file a plugin wrote, and the question is whether the tree really has
         the entry point it advertises;
      4  the manifest carries `Spring-Boot-Version`. This conjunct is
         forgeable and is kept anyway, which is worth being plain about.
         `Spring-Boot-Version` is an ordinary manifest attribute:
         `maven-jar-plugin` `<manifestEntries>` writes it as cheaply as it
         writes `Start-Class`, so on its own it is evidence of nothing. It is
         kept because it is the attribute §1.1 names, and because a tree that
         really did build a Boot application and only forgot to repackage it
         is better served by a failure message about the attribute it is
         missing than by one about a zip layout it has never had to think
         about. What it catches by itself is the uber-jar that did not bother
         to forge a second line;
      5  the jar carries Boot's loader, and `Main-Class` is that loader. Both
         halves. At least one `.class` under
         `org/springframework/boot/loader/` at the *root* of the jar -- not
         inside `BOOT-INF/lib/`, which is where a copy of `spring-boot-loader`
         that arrived as an ordinary dependency would sit -- and a
         `Main-Class` under `org.springframework.boot.loader`. This is the
         conjunct that cannot be written by hand. Two forged manifest lines
         cost a candidate one block of XML; putting the loader's classes at
         the root of the jar and pointing the JVM's entry at them is what
         `spring-boot-maven-plugin`'s `repackage` does, and a candidate who
         arranges it some other way has done the repackaging by the time they
         have finished. Without this conjunct a shaded uber-jar with an
         embedded container and no Spring Boot anywhere clears this row on
         two lines of manifest.

    Conjunct 5 does not pick a winner between the two routes §1.2 licenses:
    an imported BOM and a Boot `<parent>` both run the same `repackage` goal
    and both produce this layout, which was measured on a built tree of each
    rather than argued from the plugin. The prefix stops at `loader` rather than
    naming `JarLauncher`, which also covers `WarLauncher` and
    `PropertiesLauncher` and covers Boot 3/4's `...loader.launch.JarLauncher`
    -- see `CAL["boot_loader_entry_prefix"]`.

    Which file is asked about at all is a different and deliberately looser
    question, and it is not asked here. `build.sh:find_bootjar()` accepts
    either manifest attribute, because its job is to locate the deployable so
    that behaviour can be measured, not to grade it; "is this a Boot jar" is
    this gate's question and this gate asks it with the stricter predicate. A
    tree that forges `Start-Class` onto a shaded jar is therefore discovered
    and launched, and if it answers it is scored on all 428 observations
    before being refused this row.

    `SshDaemon.main` already exists in the baseline, which is why there is no
    "the tree declares a main method" gate anywhere in this file: the tree
    arrived with one. What is new is that the *deployable* names one.

    Header continuation is unfolded before the attributes are read. A manifest
    line longer than 72 bytes is wrapped onto a following line beginning with
    a single space, and `Start-Class: com.google.gerrit.app.MiniGitServer` is
    close enough to that limit that a slightly longer package name would be
    split -- read naively, the entry class would come out truncated and this
    gate would fail a correct tree.
    """
    import zipfile
    ev = {"artifact": art}
    if not art or not os.path.exists(art) or not art.endswith(".jar"):
        return g.add("executable-jar", False,
                     "no .jar artifact (%s)" % (art or "NONE"), 2.0, ev)
    try:
        with zipfile.ZipFile(art) as z:
            entries = z.namelist()
            name = next((n for n in entries
                         if n.upper() == "META-INF/MANIFEST.MF"), None)
            raw = z.read(name).decode("utf-8", "replace") if name else ""
    except Exception as e:                              # noqa: BLE001
        return g.add("executable-jar", False,
                     "artifact has no readable manifest: %s" % str(e)[:80],
                     2.0, ev)
    attrs, last = {}, None
    for ln in raw.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        if ln.startswith(" ") and last:
            attrs[last] += ln[1:]
            continue
        key, sep, val = ln.partition(":")
        if sep:
            last = key.strip()
            attrs[last] = val.strip()
    main_class = attrs.get("Main-Class", "")
    entry = attrs.get("Start-Class", "") or main_class
    own = {c.get("n") for c in production_classes()}
    mains = [m for m in b.ast.kind("method")
             if m.get("owner") == entry
             and (m.get("sig") or "").startswith("main(")
             and "static" in (m.get("mods") or "")]
    boot_version = attrs.get("Spring-Boot-Version", "")
    # Conjunct 5, both halves. The entry prefix is matched from the start of
    # the entry name, which is what confines it to the root of the jar: a
    # nested `BOOT-INF/lib/spring-boot-loader-*.jar` is one zip entry whose
    # name begins with `BOOT-INF/`, and an exploded copy under
    # `BOOT-INF/classes/` begins with that.
    loader_prefix = CAL["boot_loader_entry_prefix"]
    loader = [n for n in entries
              if n.startswith(loader_prefix) and n.endswith(".class")]
    main_is_loader = main_class.startswith(CAL["boot_loader_class_prefix"])
    ev.update({"main_class": main_class, "entry": entry,
               "entry_is_own": entry in own, "main_methods": len(mains),
               "boot_version": boot_version, "loader_classes": len(loader),
               "main_is_loader": main_is_loader,
               "manifest_keys": sorted(attrs)})
    why = []
    if not main_class:
        why.append("the manifest has no Main-Class")
    if entry not in own:
        why.append("the entry class %s is not one this project compiled"
                   % (entry or "(none)"))
    if not mains:
        why.append("no public static main(String[]) is declared on %s"
                   % (entry or "(none)"))
    if not boot_version:
        why.append("the manifest has no Spring-Boot-Version, so the jar was "
                   "not repackaged as a Spring Boot one")
    if not loader:
        why.append("the jar carries no Spring Boot loader class at its root: "
                   "nothing under %s, so nothing repackaged it" % loader_prefix)
    if main_class and not main_is_loader:
        why.append("Main-Class=%s is not Spring Boot's loader (expected one "
                   "under %s)" % (main_class, CAL["boot_loader_class_prefix"]))
    return g.add("executable-jar", not why,
                 ("Main-Class=%s, entry=%s, declared in this tree, "
                  "Spring-Boot-Version=%s, %d Boot loader class(es) under %s"
                  % (main_class or "none", entry or "none", boot_version,
                     len(loader), loader_prefix))
                 if not why else "; ".join(why), 2.0, ev)


def servlet_api_packaged():
    """The servlet API is inside the deployable now, in either spelling.

    Step 02's `servlet-api-not-packaged`, inverted. There the API belonged to
    the container and a packaged copy was a WAR fighting its host over the
    same classes; here there is no host, and an absent copy is a jar that
    cannot answer a request.

    Counted as classes under the servlet package whose origin is inside the
    artifact, rather than as a jar whose *name* looks like a servlet API.
    That is not a stylistic choice: step 02's gate matched
    `servlet-api|javax.servlet-api|...` on the filename, and the jar step
    04's reference API arrives in is called `tomcat-embed-core-9.0.83.jar`.
    Run unchanged against step 04's reference, step 02's gate reports "no
    servlet API jar among 813 artifact entries" and passes -- while 91
    `javax.servlet` classes sit inside it. A name is not a measurement.

    This is the first of the two widenings the second collected region's
    header names. Step 04 counted `javax.servlet`; here the count is over
    both spellings, because what step 04 bought was that the API *travels
    with the application* rather than belonging to a host, and that is true
    of a correct tree at either rung. Left in step 04's spelling this row
    would report "0 javax.servlet classes" against a finished step-05 tree
    and fail it -- a hold row failing the right answer, which holds nothing.
    The jakarta-only reading is not lost: `jakarta-servlet-api-packaged` in
    this rung's own section asks it at this rung's weight, and
    `no-javax-ee-apis-packaged` beside it says the old copy is gone.

    The detail prints the two spellings separately, and the widening is
    exactly why it has to: a row that deliberately no longer discriminates on
    spelling passes a jar carrying both APIs, and `inherited-boot-hold` would
    then say "all N inherited checks hold" over a deployable with 91
    `javax.servlet` classes still inside it. `no-javax-ee-apis-packaged` is
    the row that refuses that jar, so the split is here to let a reader of
    the hold row see a dual-API deployable rather than infer it from a row
    that passed.

    A split in this row's own detail is not enough for that, because `held`
    reports the collector and not its members: on the success path its whole
    message is the count of checks that held. So when the javax count is
    non-zero this sets `evidence["note"]`, which `held` appends to the hold
    row's message -- naming the count and the archives it is in, which is
    what a candidate has to act on. A correct tree has javax 0 and sets no
    note, so the hold row's message is just that count.
    """
    both = ("javax.servlet", "jakarta.servlet")
    jav_arcs, jav = packaged_providers(("javax.servlet",))
    jak_arcs, jak = packaged_providers(("jakarta.servlet",))
    arcs = dict(jav_arcs)
    for k, v in jak_arcs.items():
        arcs[k] = arcs.get(k, 0) + v
    total = jav + jak
    ev = {"count": total, "need": CAL["servlet_api_classes_min"],
          "javax": jav, "jakarta": jak,
          "archives": arcs, "packages": list(both)}
    if jav:
        ev["note"] = ("servlet-api-packaged holds on either spelling and %d "
                      "javax.servlet API class(es) are packaged inside %s, "
                      "from %s"
                      % (jav, ART_BASE or "NONE",
                         ", ".join("%s:%d" % kv
                                   for kv in sorted(jav_arcs.items())[:4])
                         or "none"))
    return g.add("servlet-api-packaged",
                 total >= CAL["servlet_api_classes_min"],
                 "%d servlet API class(es) inside %s (javax %d, jakarta %d), "
                 "need >=%d%s"
                 % (total, ART_BASE or "NONE", jav, jak,
                    CAL["servlet_api_classes_min"],
                    (", from " + ", ".join(sorted(arcs)[:4])) if arcs else ""),
                 1.5, ev)


def embedded_container_packaged():
    """A servlet container is inside the deployable, not around it.

    The positive half of `no-war-packaging`: that gate says the reactor stopped
    producing something a container unpacks, and this one says the container
    came along instead. Matched by package over the three implementations
    Spring Boot 3.5 has starters for -- tomcat, jetty and undertow, all three
    managed by the 3.5 BOM -- and not by artifact name, because the artifact
    name is a packaging decision and the package is the API.

    The floor is three figures for a reason that is about what a container is
    rather than about which one: `org.apache.catalina` alone is 670 classes in
    this reference, Jetty and Undertow are the same order, and nothing that is
    not a servlet container has a hundred classes in one of those packages.

    Matching all three is intentional and is not a contradiction of this
    step's §1.3, which names Tomcat: this gate asks whether a container is inside the
    deployable, and the behaviour suite -- not a packaging gate -- is what
    holds the container to the recorded request-target handling. See the
    `container_packages` calibration note for the measurement behind it.
    """
    arcs, total = packaged_providers(CAL["container_packages"])
    return g.add("embedded-container-packaged",
                 total >= CAL["container_classes_min"],
                 "%d embedded-container class(es) inside %s, need >=%d%s"
                 % (total, ART_BASE or "NONE", CAL["container_classes_min"],
                    (", from " + ", ".join(sorted(arcs)[:4])) if arcs else ""),
                 1.5, {"count": total, "need": CAL["container_classes_min"],
                       "archives": arcs,
                       "packages": list(CAL["container_packages"])})


no_war_packaging()
no_war_artifact()
executable_jar()
servlet_api_packaged()
embedded_container_packaged()

# ---------------------------------------------------------------------------
# Spring Boot, in the dependency graph
# ---------------------------------------------------------------------------

# Three coordinates at least, and `spring-boot` among them by name, on the 3
# or the 4 line -- `boot_version_re` is this step's, not step 04's, and at this
# rung it accepts both so that a finished step-09 tree keeps this hold. `boot-4`
# below is the row that says which of the two. CAL says why each of these
# numbers is what it is. Asked of the
# resolved graph and not of any pom, because a starter's transitive closure is
# where most of Boot arrives and no pom in this tree names `spring-boot`
# directly even in the reference.
G.dep_present(g, b, "spring-boot-dep",
              group_prefixes=("org.springframework.boot",),
              minimum=CAL["boot_artifacts_min"], weight=3.0,
              version_re=CAL["boot_version_re"],
              require_artifacts=CAL["boot_required_artifacts"])


def boot_managed_versions():
    """Versions come from Boot's dependency management, not one pin at a time.

    Two routes, because Boot offers two and the choice between them is a
    matter of whether a project already has a parent pom it wants to keep:

      R1  a pom imports a Boot bom -- any `org.springframework.boot`
          coordinate declared with `<scope>import</scope>`, which is legal
          only inside `<dependencyManagement>`;
      R2  a pom's `<parent>` is an `org.springframework.boot` one, which is
          `spring-boot-starter-parent`.

    This reactor has its own parent and takes R1, but a candidate who makes
    `mini-git-server-app` a child of `spring-boot-starter-parent` has done the
    same thing, and a gate that asked for the import alone would fail them.

    Why this is a gate at all, when `spring-boot-dep` already says Boot
    resolves: a tree can depend on `spring-boot` and go on hand-pinning
    `spring-web`, `spring-core` and `tomcat-embed-core` at versions of its own
    choosing, and that tree has the jar without the thing the jar is for. The
    2011 pom this task started from pinned twenty-three artifact versions by
    hand; retiring that is part of the rung and not a side effect of it.
    """
    docs = G._pom_docs(b)
    props = G._pom_properties(docs)
    hits = []
    for rel, root, _text in docs:
        if root is None:
            continue
        for el in root.iter():
            tag = G._local(el.tag)
            if tag not in ("dependency", "parent"):
                continue
            gid, aid, scope = _pom_coords(el)
            gid = G._resolve(gid, props)
            if not gid.startswith("org.springframework.boot"):
                continue
            if tag == "parent":
                hits.append("%s (<parent> %s)" % (rel, aid or "?"))
            elif scope == "import":
                hits.append("%s (imports %s)" % (rel, aid or "?"))
    return g.present("boot-managed-versions", sorted(set(hits)), 1, 1.5)


boot_managed_versions()

# ---------------------------------------------------------------------------
# The application starts itself
# ---------------------------------------------------------------------------

# The annotations that make a class the one Boot starts from, by FQCN for the
# bytecode route and by simple name for the source one.
# `@SpringBootApplication` is in both sets because it is the composed form of
# both.
_BOOT_APP = "org.springframework.boot.autoconfigure.SpringBootApplication"
BOOT_CONFIG_ANNOS = ("SpringBootConfiguration", "SpringBootApplication")
BOOT_AUTO_ANNOS = ("EnableAutoConfiguration", "SpringBootApplication")
BOOT_CONFIG_FQ = ("org.springframework.boot.SpringBootConfiguration",
                  _BOOT_APP)
BOOT_AUTO_FQ = (
    "org.springframework.boot.autoconfigure.EnableAutoConfiguration",
    _BOOT_APP)

# The two types that run one. A `SpringApplication` built and run directly, or
# the builder, which is what a tree standing up more than one context reaches
# for.
# The same two types `container-bootstrap`'s A4 and A5 read, and deliberately
# the same tuple rather than a second copy: this gate asks whether the entry
# point launches Boot, that one asks whether a context gets built, and if the
# two ever disagreed about which types those are, one of them would be wrong.
LAUNCH_TYPES = CAL["boot_context_drivers"]
LAUNCH_FQ = CAL["boot_context_driver_fqcns"]


def _annotated_classes(fqcns):
    """Own production classes carrying one of these annotations, in bytecode.

    The companion route to `annotated()`, which reads the source. Both of the
    annotations this gate asks about are `RUNTIME`-retained, so ASM records
    them on the class file, and a class file is evidence the source scan
    cannot give: it survives a candidate who writes the annotation on a class
    the AST scan did not reach.
    """
    want = set(fqcns)
    out = []
    for c in production_classes():
        m = sorted(set(c.get("a") or []) & want)
        if m:
            out.append("%s (%s)" % (c.get("n"), ", ".join(m)))
    return out


def boot_configuration_class():
    """Something is the Boot application, and auto-configuration is on.

    Two halves, ANDed, and each reachable two ways (source annotation,
    bytecode annotation):

      A  a production type carries `@SpringBootConfiguration` or the composed
         `@SpringBootApplication`. This is what makes a class the one
         `SpringApplication` treats as the primary source, and it is what
         `@SpringBootTest` and the repackager's `Start-Class` search look for.
      B  a production type carries `@EnableAutoConfiguration` or the composed
         `@SpringBootApplication`. This is the half that stands the embedded
         container up and binds `server.port`; without it a Boot jar starts a
         context and serves nothing.

    Both rather than either, because they are genuinely different: a tree can
    carry `@SpringBootConfiguration` and configure a
    `TomcatServletWebServerFactory` by hand, and a tree can carry
    `@EnableAutoConfiguration` on a plain `@Configuration` class. Neither of
    those is wrong, and neither on its own is what §1 asks for.
    `@SpringBootApplication` satisfies both at once, which is why it is in
    both sets.

    There is deliberately no `@ComponentScan` requirement. This reference goes
    out of its way *not* to scan -- the four contexts it stands up have
    disjoint bean sets and a scan would collapse them -- and a gate that asked
    for one would fail the reference for being careful.
    """
    types = [t for t in b.ast.kind("type") if not in_tests(t.get("p"))]

    def sites(simples, fqcns):
        out = []
        for s in simples:
            for t in annotated(types, s):
                out.append(_site(t, "@%s on %s" % (s, t.get("n", "?"))))
        return sorted(set(out + _annotated_classes(fqcns)))

    conf = sites(BOOT_CONFIG_ANNOS, BOOT_CONFIG_FQ)
    auto = sites(BOOT_AUTO_ANNOS, BOOT_AUTO_FQ)
    missing = [w for w, s in (("a Boot configuration class", conf),
                              ("auto-configuration", auto)) if not s]
    detail = "%d configuration site(s), %d auto-configuration site(s)" \
        % (len(conf), len(auto))
    if missing:
        detail += "; missing " + " and ".join(missing)
    elif conf:
        detail += ": " + "; ".join(sorted(set(conf + auto))[:4])
    return g.add("boot-configuration-class", not missing, detail, 2.5,
                 {"configuration": conf[:50], "auto": auto[:50]})


def boot_application_launch():
    """This project's own code runs the application.

    Two routes, and they are two instruments rather than two spellings:

      R1  production source writes `SpringApplication` or
          `SpringApplicationBuilder` as a type -- a `new`, a field, a
          parameter, a `.class` literal.
      R2  a production class file references either of them. This is the
          route that catches `SpringApplication.run(App.class, args)`, which
          is a static call on a name expression and produces no `typeuse`
          record at all; the reference itself takes only R1, and a tree
          written the one-line way takes only R2.

    The gate below it, `boot-configuration-class`, says a class is *marked* as
    the application. This one says something runs it. Naming a configuration
    is not starting one, which is the same distinction `container-bootstrap`
    draws in the held region above, and it is drawn again here because Boot
    makes the marked class so easy to write and the `main` so easy to leave
    out.
    """
    sites = [_site(u, u.get("n", "?")) for u in _prod_typeuses()
             if _simple(u.get("n")) in LAUNCH_TYPES]
    sites += ["%s (%s)" % (k, ", ".join(v))
              for k, v in sorted(production_refs(LAUNCH_FQ).items())]
    return g.present("boot-application-launch", sorted(set(sites)), 1, 2.5)


boot_configuration_class()
boot_application_launch()

# ---------------------------------------------------------------------------
# The container's callbacks, and what replaced them
# ---------------------------------------------------------------------------

# The Boot types that register a servlet, a filter or a listener with the
# embedded container, by simple name for the source route.
REGISTRATION_TYPES = ("ServletRegistrationBean", "FilterRegistrationBean",
                      "ServletListenerRegistrationBean",
                      "DispatcherServletRegistrationBean",
                      "DelegatingFilterProxyRegistrationBean",
                      "ServletContextInitializer")

# And the annotations that do the same job without naming any of them, which
# is the other shape Boot supports and a correct answer to the same sentence.
REGISTRATION_ANNOS = ("WebServlet", "WebFilter", "WebListener",
                      "ServletComponentScan")

BOOT_SERVLET = ("org.springframework.boot.web.servlet",)


def servlet_registration_beans():
    """Servlets and filters are registered as beans, not by the container.

    Step 03 moved the mappings out of `web.xml` and into a
    `WebApplicationInitializer`, where the application is handed a live
    `ServletContext` and calls `addServlet`/`addFilter` on it. That is still
    the container calling the application; there is no container to hand
    anything over now, and the registrations become part of the object graph
    like everything else.

    Three routes, and the union of their sites is the count:

      R1  production source writes one of `REGISTRATION_TYPES`.
      R2  production source carries one of `REGISTRATION_ANNOS`. `@WebServlet`
          with `@ServletComponentScan` is Boot's other supported shape for
          exactly this, and a tree that took it has answered the sentence.
      R3  a production class file references `org.springframework.boot.web.
          servlet`. The bytecode route, for a registration written somewhere
          the AST scan's production cut did not reach.

    Measured: the tree this step is handed 0 sites; reference 13 (six
    `ServletRegistrationBean` and six `FilterRegistrationBean` written in
    source, plus the one class file that references the package); an
    annotation-shaped port of the same five registrations 5 by R2 alone.
    The floor is 5, one site per registration the application installs; CAL
    says why, and says what was measured to make sure 5 is reachable on the
    stingiest spelling of both shapes and not only on this one.
    """
    sites = [_site(u, u.get("n", "?")) for u in _prod_typeuses()
             if _simple(u.get("n")) in REGISTRATION_TYPES]
    sites += [_site(a, "@" + _simple(a.get("n")))
              for a in _prod_annos(REGISTRATION_ANNOS)]
    sites += ["%s (%s)" % (k, ", ".join(v))
              for k, v in sorted(production_refs(BOOT_SERVLET).items())]
    return g.present("servlet-registration-beans", sorted(set(sites)),
                     CAL["registration_sites_min"], 2.0)


def no_servlet_bootstrap_contract():
    """Nothing in this tree is started by a servlet container any more.

    The negative half of `executable-jar`. `WebApplicationInitializer` is
    Spring's adapter onto `ServletContainerInitializer`, which is the Servlet
    3.1 callback a container uses to hand control to a webapp at deploy time.
    An executable jar has no deploy time: the `main` method runs and the
    container is started by the application rather than the other way round.
    A tree that kept the initializer has two bootstrap paths, one of which
    will never fire, and `sources-survive` would not notice.

    Two routes, for `own_implements`'s reason -- the `implements` clause is
    not always where the answer is:

      R1  the resolved bytecode hierarchy of a production class names one of
          them, which catches a subclass of a base that implements it and an
          anonymous `new WebApplicationInitializer() {...}` with no clause at
          all;
      R2  a production type's own `implements`/`extends` clause names one, by
          simple name, which catches a tree that declares it and does not
          compile.

    With the own-class floor, for the reason every absence gate in this file
    carries one: "no production class implements this" is also what a tree
    whose modules did not compile reports.
    """
    universe = production_classes()
    floor, _where = CAL["own_floor"]
    if len(universe) < floor:
        return g.add("no-servlet-bootstrap-contract", False,
                     "only %d own production class(es), need >=%d before an "
                     "absence is evidence" % (len(universe), floor), 1.0,
                     {"universe": len(universe), "floor": floor})
    impls = own_implements(CAL["retired_bootstrap"])
    sites = ["%s (%s)" % (k, ", ".join(v)) for k, v in sorted(impls.items())]
    simples = {n.rsplit(".", 1)[-1] for n in CAL["retired_bootstrap"]}
    for t in b.ast.kind("type"):
        if in_tests(t.get("p")):
            continue
        for n in (t.get("implements") or []) + (t.get("extends") or []):
            if _simple(n) in simples:
                sites.append(_site(t, "%s implements %s"
                                   % (t.get("n", "?"), n)))
    return g.absent("no-servlet-bootstrap-contract", sorted(set(sites)), 1.0)


def one_log4j_provider():
    """At most one archive inside the deployable provides `org.apache.log4j`.

    Step 04's gate, widened at the rung its own docstring predicted: *"Step 06
    is where the logging stack moves, and this gate goes with it."*

    It asked for *exactly* one, and that was the right number for the tree it
    was written against. Four of this project's production classes named
    `org.apache.log4j` directly, so zero providers was a `NoClassDefFoundError`
    at startup; two was the trap, because `slf4j-log4j12` at 2.x is a Maven
    relocation to `slf4j-reload4j`, which brings a fork of log4j 1.2 carrying
    the same packages, and which of the two wins at class-load time is a
    function of classpath order.

    This is the rung that removes those four references. Asked unchanged the
    gate would report "0 archive(s) provide org.apache.log4j" against a tree
    that has done exactly what this step's instruction asks, and would fail it
    -- an inherited test its successor's own requirements made unpassable,
    which is precisely the failure a hold row hides, since
    `inherited-boot-hold` fails whole and names the sub-check only in its
    evidence.

    So the upper bound survives and the lower bound goes. What step 04 was
    really guarding is the duplicate, and that reading is still enforced here
    at full strength: two providers fail this row, and zero is now not only
    allowed but required one section down by `no-log4j-packaged`, which prices
    the same fact at 4.0 as this rung's own work. Widening here is what keeps
    the two from contradicting each other.

    The referencing classes are still recorded in the evidence. On a correct
    tree the list is empty and the archive count is zero, and a reader of the
    report should be able to see both halves of that without opening another
    row.
    """
    arcs, total = packaged_providers(CAL["logging_packages"])
    users = sorted(production_refs(CAL["logging_packages"]))
    return g.add("one-log4j-provider", len(arcs) <= 1,
                 "%d archive(s) inside %s provide org.apache.log4j (%s); %d "
                 "own production class(es) reference it"
                 % (len(arcs), ART_BASE or "NONE",
                    ", ".join("%s:%d" % kv for kv in sorted(arcs.items()))
                    or "none", len(users)),
                 1.0, {"archives": arcs, "total": total, "users": users})


servlet_registration_beans()
no_servlet_bootstrap_contract()
one_log4j_provider()

# End of the second collected region: every gate between the two rebindings
# above went into `BOOT` and is reported as one row, `inherited-boot-hold`, at
# the bottom of this file.
g = _journal


_journal, g = g, JAK

# ---------------------------------------------------------------------------
# Step 05's rung: the namespace, held as one row
# ---------------------------------------------------------------------------
#
# Step 05's nine gates carried 21.5 between them. Carried forward at those
# weights they would be more than two thirds of this rung's total, and a tree
# that had finished step 05 and touched nothing else would out-score one that
# did this step's work and let a single jakarta import slip. So they are
# collected, exactly as step 04's twelve and step 03's twenty-two are, and
# reported as one row worth 2.0 at the bottom of this file.
#
# Collected and not rewritten. `g` is rebound around the whole section so that
# every gate below writes into `JAK` instead of into the journal, and not one
# predicate, scan or threshold is touched: a hold that re-implemented its
# predecessor's question as a simpler boolean would be a different question
# wearing the same name. What follows, down to the rebinding back, is step
# 05's file verbatim.
#
# Priced with `inherited-di-hold` and `inherited-boot-hold` at 2.0 rather than
# with the lighter rows, and for the same reason those two are: it is load
# bearing for everything this rung does. The deployable that writes the audit
# log is a jakarta-spelled Spring Boot 3 application, and a candidate who
# broke the namespace while moving the logging stack has not produced a tree
# whose logging can be assessed at all.
#
# Original header follows.
#

# Nine gates, read at four depths, and the reason there are four is that this
# rung's near-misses fail at different ones. A tree can rename its imports and
# not move its platform; it can move its platform and leave a library behind
# on the old one; it can rename too much and stop compiling against the JDK.
# So the same sentence -- "the servlet API this application is written against
# is `jakarta.servlet`" -- is asked of the source, of this project's own
# bytecode, of the API inside the deployable, and of one packaged dependency
# that is itself a servlet application.
#
#   source      `jakarta-servlet-source`, `no-javax-servlet-source`,
#               `jdk-javax-intact`
#   own classes `jakarta-servlet-classes`, `no-javax-servlet-classes`
#   deployable  `jakarta-servlet-api-packaged`, `no-javax-ee-apis-packaged`,
#               `servlet-api-6`
#   dependency  `jgit-http-jakarta`
#
# Every one of the eight that is not `jdk-javax-intact` is failed by the
# predecessor tree and passed by the reference. `jdk-javax-intact` is passed
# by both and is here for the candidate in between: it is the row that refuses
# `sed -i s/javax/jakarta/g`, which is the most plausible wrong answer to a
# rung whose visible content is a package rename.


def source_type_sites(prefixes, by_file=False):
    """Where this repository's Java names a type under `prefixes`.

    The positive counterpart of `gate_lib.no_source_types`, and deliberately
    the same two halves in the same order, so that a presence count and an
    absence count over two prefixes are the same measurement: every import,
    plus every type use written with its package inline. A use of an imported
    simple name is invisible to both, by the same design -- the import that
    made it legal is not, and the bytecode gates beside these see the
    reference either way.

    `by_file` changes what one site is, and it exists because the two callers
    want different things. Raw (the default) is one site per named type, which
    is the right unit for an absence -- every place a name the step retired is
    still written -- and for a presence the step asks to leave untouched.
    `by_file=True` keys a site on the file alone, so that a file naming
    `jakarta.servlet` six times by type, or once with a wildcard, or naming
    both `jakarta.servlet` and `jakarta.servlet.http`, is one site every way
    round. That is the unit §1.1 states for the imports the candidate is
    *required* to rewrite: which files name the API is the requirement, and
    how many names each of them spends on it is the candidate's business. The
    count cannot be lowered by any import style, only by a file ceasing to
    name the API at all.

    Written here rather than added to gate_lib because gate_lib is shared
    across every step of this task: a helper added there changes the build
    context of four verifier images that are already calibrated, and this
    rung is the only one that needs it.
    """
    sites = {}
    for f in b.ast.files():
        for im in f.get("imports") or []:
            nm = im.split(" ")[0].rstrip(".*").rstrip(".")
            if not S.under(nm, prefixes):
                continue
            if by_file:
                sites[f.get("p", "?")] = nm
            else:
                sites["%s (import %s)" % (f.get("p", "?"), nm)] = nm
    for u in b.ast.typeuses(prefixes):
        if by_file:
            sites[u.get("p", "?")] = u.get("n")
        else:
            sites["%s:%s %s" % (u.get("p", "?"), u.get("line", 0),
                                u.get("n", "?"))] = u.get("n")
    return sites


# The source, both ways round. These two are the same seven files seen from
# either side, and they are two gates and not one because the failure modes
# are different and a candidate should be told which one it hit: a tree that
# added jakarta imports beside the javax ones it left behind fails the second
# and passes the first, and a tree that deleted the httpd module fails the
# first and passes the second. Neither is a tree that did this rung.
#
# They do not count in the same unit, and that asymmetry is deliberate. The
# absence is raw: every place the retired name is still written is a place the
# candidate has to go and fix, and collapsing two of them into a wildcard does
# not make either go away. The presence is by file, because that is the unit
# §1.1 states -- which files name the API is the requirement, the import style
# inside them is not -- and because the candidate has just rewritten every one
# of these imports and may reasonably have let an IDE tidy them afterwards: at
# IntelliJ's default of five, tidying this reference collapses two packages and
# loses ten of the twenty-eight raw sites without any file ceasing to name the
# API. See `jakarta_servlet_source_min` in CAL.
#
# `no-javax-servlet-source` carries no own-class floor of its own, unlike
# every bytecode absence gate in this file. It does not need one: the two
# gates directly above it in the journal are `source-parses` and
# `sources-survive`, which say there are 105 files and 132 declared types to
# have searched, and an AST absence over an empty AST is exactly what those
# two exist to refuse.


def jakarta_servlet_source():
    """Every one of the seven files that named the servlet API names jakarta.

    Written out rather than handed to `Gates.present` for the detail line
    alone. `present` prints "%d site(s), need >=%d", and `jdk-javax-intact`
    two rows down is a `present` caller counting raw import sites -- so the
    journal would carry "7 site(s), need >=7" beside "18 site(s), need >=18"
    in one word for two units. This row is files. It says so. The evidence
    keys are the ones `present` would have written, and truncated the same
    way, so anything reading the report reads both rows alike.
    """
    files = sorted(source_type_sites(("jakarta.servlet",), by_file=True))
    need = CAL["jakarta_servlet_source_min"]
    return g.add("jakarta-servlet-source", len(files) >= need,
                 ("%d file(s) name a jakarta.servlet type, need >=%d"
                  % (len(files), need))
                 + (": " + ", ".join(files[:8]) if files else ""),
                 3.0, {"count": len(files), "need": need,
                       "sites": files[:200]})


jakarta_servlet_source()
G.no_source_types(g, b, "no-javax-servlet-source", ("javax.servlet",),
                  weight=3.0)


# The JDK's own `javax.*`, which is not going anywhere. `javax.xml.parsers`,
# `javax.xml.transform`, `javax.xml.xpath` and `javax.net.ssl` are java.se
# packages: they were never part of Java EE, they have no jakarta spelling,
# and there is no artifact a candidate could add that would provide one. A
# tree that renamed them does not compile, which means this gate can only
# ever read a tree that did not -- so it is the source it reads, and the
# report it produces on a blanket-renamed tree is a build failure with this
# row failing beside it, naming the packages that should have been left alone.
#
# 2.0, priced with the leaps and not with the tripwires, and the reason is the
# tree that *recovers* from the blanket rename rather than the one that runs
# it. `sed` over the whole tree breaks the build, the candidate sees a
# compiler error in `HtmlDomUtil` and `BlindSSLSocketFactory`, and the
# cheapest way to make a compiler error about a package that does not exist go
# away is to delete the code that names it. `orphan-types-intact` catches half
# of that -- `BlindSSLSocketFactory` is one of the two types it counts methods
# on -- and nothing else in this file catches the other half, because
# `HtmlDomUtil`'s XML use is not an orphan and `sources-survive` is a count
# over 105 files that four deletions do not reach. This row is what remains,
# and the tree it refuses is one that otherwise builds, passes its tests, and
# scores everything else in the section.
#
# The pass line is the whole 18 raw sites measured rather than a fraction of
# them, and that is what §1.1 asks for: these uses stay exactly as they are,
# all of them. `own_floor`'s tidying allowance does not carry over, because
# there is no fraction of 18 that tolerates the tidying and refuses the
# deletion -- the 18 sites sit in only 7 distinct (file, package) pairs, so
# consolidating them into wildcards lands on 7, and any floor loose enough to
# admit that is also loose enough to admit a tree that deleted `HtmlDomUtil`'s
# XML use outright. See `jdk_javax_sites_min` in CAL for that arithmetic
# written out.
#
# This row counts raw sites while `jakarta-servlet-source` above it counts
# distinct files, and the asymmetry is the instruction's rather than an
# oversight. There the candidate must edit every one of the imports, so import
# style is in play and the count has to survive it, and §1.1 puts the
# requirement in files for exactly that reason. Here the candidate is told to
# leave these imports alone, so a tree that reformatted them has done
# something §1.1 did not ask for -- and it would not be an accident of an IDE
# default either: the largest single package in either file is 4 imports
# (`javax.xml.transform` and `javax.xml.xpath` in `HtmlDomUtil`), below the
# five at which organize-imports collapses one, so neither of these two files
# is tidied by opening it.
#
# The detail strings say which unit each is in: this one is `Gates.present`'s
# "N site(s)", which is the honest word for a raw import count, and the row
# above prints "N file(s)" from its own `g.add` rather than borrowing it.
g.present("jdk-javax-intact",
          source_type_sites(CAL["jdk_javax_packages"]),
          CAL["jdk_javax_sites_min"], 2.0)


# This project's own bytecode, both ways round, for `production_refs`'s
# reason: an import is a statement of intent and the constant pool is what
# the class loader will actually go looking for. A candidate who writes
# `import jakarta.servlet.*;` once and simple names thereafter has one source
# site and eleven classes' worth of references, and these two rows are where
# that tree is measured correctly.
g.present("jakarta-servlet-classes",
          production_refs(("jakarta.servlet",)),
          CAL["jakarta_servlet_own_classes_min"], 1.5)

# `own=True`, and this is the one place in this file where that choice was
# made by measurement rather than by the rule in `refs_by_class`'s docstring.
# The rule says a question about the deployable asks the whole thing; asked
# that way this gate failed the reference for three rungs. Measured over all
# 16260 classes inside step 05's reference jar, three named `javax/servlet` in
# their constant pool: two in `velocity-1.6.4.jar` and one in
# `ehcache-core-1.7.2.jar`, both 2011-era dependencies the tree still used and
# neither of which had a release that did not. They were optional servlet
# integrations inside libraries used for templating and caching; nothing
# loaded them, and nothing could, because the classes they referenced were
# absent. A gate that failed the reference for them would have been measuring
# the age of Velocity rather than the state of the migration, so steps 05 and
# 06 asked `own=True` -- no class *this project compiled* names the old API.
#
# This rung removes both archives, which invites widening
# the gate to the whole deployable -- the exception
# was written about velocity and ehcache, and velocity and ehcache are gone.
# It stays narrow, because measured that way there are two `javax/servlet`
# references still inside the reference jar: `org.apache.coyote.ajp.
# AjpProcessor` in `tomcat-embed-core`, and `org.apache.logging.log4j.util.
# Constants` in `log4j-api`, which Spring Boot's own logging starter brings
# under `log4j-to-slf4j`. Neither is removable and neither is this tree's
# doing. The exception survives its original justification; only the names
# behind it changed, from two 2011 libraries to two current ones, which is
# the ordinary condition of a servlet-era classpath rather than a fact about
# either rung.
#
# The number that makes the widening look safe -- zero `javax/servlet`
# *providers* in the reference jar -- is real, but it is the answer to a
# different question than the one this gate asks. A provider census counts
# classes whose names are under the package; this row counts classes whose
# constant pools mention it. `own=True` stays, and so does the floor, because
# an absence over own classes is also what a tree with no own classes says.
G.no_refs(g, b, "no-javax-servlet-classes", ("javax.servlet",),
          weight=1.5, own=True, floor=CAL["own_floor"])


def jakarta_servlet_api_packaged():
    """The jakarta-spelled servlet API is inside the deployable.

    `servlet-api-packaged`, held one row up, counts either spelling and says
    the API travels with the application. This one says which API it is. Same
    floor, same measurement, different prefix -- and the pair is how a held
    row keeps holding while the rung it was inherited from is superseded.
    """
    arcs, total = packaged_providers(("jakarta.servlet",))
    need = CAL["servlet_api_classes_min"]
    return g.add("jakarta-servlet-api-packaged", total >= need,
                 "%d jakarta.servlet class(es) inside %s, need >=%d%s"
                 % (total, ART_BASE or "NONE", need,
                    (", from " + ", ".join(sorted(arcs)[:4])) if arcs else ""),
                 1.5, {"count": total, "need": need, "archives": arcs})


def no_javax_ee_apis_packaged():
    """No archive inside the deployable provides a javax-spelled EE API.

    The absence that makes the presence above mean something. A tree that
    added a jakarta servlet API beside the javax one it already had passes
    `jakarta-servlet-api-packaged` and has two `HttpServlet`s inside one jar,
    one of which the loader will pick for reasons that are not in any pom.

    Reported per package and per providing archive, because the archive is
    what a candidate has to act on: `tomcat-embed-el-9.0.83.jar` names the
    starter that is still on the old line, and it names it more usefully than
    a count of 56 classes does.

    CAL["javax_ee_packages"] carries the set and the measurement. Ten
    packages here where step 06 asked nine: `javax.annotation` joins at this
    rung, because `jsr305-1.3.9.jar` provided 35 classes of it in every jar
    from the pinned tree to step 06's reference and this rung removes the
    coordinate that brought it. The predecessor tree fails this row on that
    package alone, which is this step's work showing up in an inherited gate.
    """
    arcs, total = packaged_providers(CAL["javax_ee_packages"])
    by_pkg = {}
    for c in b.art.classes():
        o = c.get("o") or ""
        if not inside_artifact(o):
            continue
        for pkg in CAL["javax_ee_packages"]:
            if S.under(c.get("n", ""), (pkg,)):
                by_pkg[pkg] = by_pkg.get(pkg, 0) + 1
    if not ART_BASE:
        return g.add("no-javax-ee-apis-packaged", False,
                     "no deployable to look inside", 4.0, {"count": 0})
    return g.add("no-javax-ee-apis-packaged", total == 0,
                 ("no javax-spelled EE API class inside %s, over %d package(s) "
                  "asked" % (ART_BASE, len(CAL["javax_ee_packages"])))
                 if total == 0 else
                 "%d javax EE API class(es) inside %s: %s, from %s"
                 % (total, ART_BASE,
                    ", ".join("%s:%d" % kv for kv in sorted(by_pkg.items())),
                    ", ".join("%s:%d" % kv for kv in sorted(arcs.items())[:4])),
                 4.0, {"count": total, "by_package": by_pkg,
                       "archives": arcs,
                       "packages": list(CAL["javax_ee_packages"])})


def servlet_api_6():
    """The packaged servlet API is Servlet 6.0, not Jakarta Servlet 5.0.

    Two sides, and the second is the whole reason this gate is not just a
    package-name check. Jakarta Servlet 5.0 is Servlet 4.0 with the packages
    renamed: same types, nothing added, nothing removed. It ships as
    `jakarta.servlet:jakarta.servlet-api:5.0.0` and inside Tomcat 10.0.x, both
    of which resolve and start under Spring Boot 2.7 as happily as under 3.x.
    A tree on Servlet 5 renamed every import in this repository and moved no
    platform at all, and every gate above this one passes it.

      * the four programmatic-registration markers are present -- step 04's
        `servlet-api-3` set in the jakarta spelling, and still what
        `ServletRegistrationBean` needs to exist;
      * none of the three types Servlet 6.0 removed is present.

    CAL["servlet_6_markers"] and CAL["servlet_6_removed"] carry the
    measurement across six real jars and two container routes. Both sides are
    read from the deployable and not from a resolved coordinate, so the gate
    does not need to know whether the API arrived inside `tomcat-embed-core`
    or as a standalone `jakarta.servlet-api` beside Jetty.
    """
    packaged = {c.get("n") for c in b.art.classes()
                if inside_artifact(c.get("o") or "")}
    missing = [n for n in CAL["servlet_6_markers"] if n not in packaged]
    stale = [n for n in CAL["servlet_6_removed"] if n in packaged]
    why = []
    if not ART_BASE:
        why.append("no deployable to look inside")
    if missing:
        why.append("%d of %d Jakarta Servlet marker type(s) missing: %s"
                   % (len(missing), len(CAL["servlet_6_markers"]),
                      ", ".join(missing)))
    if stale:
        why.append("%d type(s) Servlet 6.0 removed are still packaged, so the "
                   "API is 5.0 or older: %s" % (len(stale), ", ".join(stale)))
    return g.add("servlet-api-6", not why,
                 ("all %d marker types packaged in %s and none of the %d "
                  "types Servlet 6.0 removed"
                  % (len(CAL["servlet_6_markers"]), ART_BASE,
                     len(CAL["servlet_6_removed"])))
                 if not why else "; ".join(why),
                 2.5, {"missing": missing, "stale": stale,
                       "markers": list(CAL["servlet_6_markers"]),
                       "removed": list(CAL["servlet_6_removed"])})


def jgit_http_jakarta():
    """The packaged git HTTP server is built against `jakarta.servlet`.

    The one dependency in this tree that is itself a servlet application, and
    the one place where a candidate can finish this rung everywhere it looks
    and still serve nothing. `org.eclipse.jgit.http.server` is what answers
    every `git clone` and `git push` over HTTP in the recorded suite:
    `GitServlet` extends `HttpServlet`, `GitFilter` implements `Filter`, and
    this project's own code names neither -- it registers `GitServlet` through
    a `ServletRegistrationBean` and never mentions a servlet supertype. So a
    tree that leaves JGit on 6.x compiles cleanly, starts, serves `/ssh_info`
    and the 404 servlet, and fails every git-over-HTTP observation with a
    `NoClassDefFoundError` the candidate has no source line to look at.
    
    Asked as a property of the packaged classes and not as a version, because
    the version is only the usual way of arriving at the property.
    CAL["jgit_http_archive_stem"] and CAL["jgit_http_jakarta_min"] carry the
    measurement: 61 classes in each of 6.10.1, 7.0.0 and 7.8.0, of which 48
    name a servlet type, javax in the 6.x and jakarta in both 7.x. The
    coordinate-level pin lives in `inherited-libraries-hold` as
    `no-jgit-before-7`, where inherited hygiene belongs; this is the row that
    says the application will work.

    The module is found either by the archive its artifactId names or by the
    package its classes declare, and this gate reads the union of the two. A
    Boot `BOOT-INF/lib/org.eclipse.jgit.http.server-<version>.jar`, a
    classifier on that same filename, and a shade that lifts the classes into
    the deployable while keeping their package all answer the same question,
    because §1.4 asks for a property of the packaged HTTP server and not for
    a jar filename. A version that is not 7.8.0 is not the question either: a
    candidate who resolved 7.1 or 7.4 answers this gate identically. A shade
    that *relocates* the package is a different set of classes under a
    different name, and that is the case CAL's floor of 40 rather than 48 is
    about.
    """
    stem = CAL["jgit_http_archive_stem"]
    jak, jav, seen = {}, {}, 0
    for c in b.art.classes():
        o = c.get("o") or ""
        if not inside_artifact(o):
            continue
        by_archive = "!" in o and os.path.basename(
            o.rsplit("!", 1)[0]).startswith(stem + "-")
        by_package = S.under(c.get("n") or "", (stem,))
        if not by_archive and not by_package:
            continue
        seen += 1
        names = set(c.get("r") or []) | set(c.get("l") or []) \
            | set(c.get("a") or []) | set(c.get("i") or [])
        if c.get("s"):
            names.add(c["s"])
        if [n for n in names if S.under(n, ("jakarta.servlet",))]:
            jak[c.get("n", "?")] = 1
        bad = sorted(n for n in names if S.under(n, ("javax.servlet",)))
        if bad:
            jav[c.get("n", "?")] = bad
    need = CAL["jgit_http_jakarta_min"]
    why = []
    if not seen:
        why.append("no %s classes inside %s at all"
                   % (stem, ART_BASE or "NONE"))
    if len(jak) < need:
        why.append("%d of %d packaged %s class(es) name a jakarta.servlet "
                   "type, need >=%d" % (len(jak), seen, stem, need))
    if jav:
        why.append("%d still name javax.servlet, so this is a 6.x or older "
                   "build of the module: %s"
                   % (len(jav), ", ".join(sorted(jav)[:4])))
    return g.add("jgit-http-jakarta", not why,
                 ("%d of %d packaged %s classes name a jakarta.servlet type "
                  "and none name javax.servlet" % (len(jak), seen, stem))
                 if not why else "; ".join(why),
                 2.5, {"classes": seen, "jakarta": len(jak),
                       "javax": sorted(jav)[:20], "need": need,
                       "stem": stem})


jakarta_servlet_api_packaged()
no_javax_ee_apis_packaged()
servlet_api_6()
jgit_http_jakarta()

# End of the third collected region: every gate between the rebinding above
# and this line went into `JAK` and is reported as one row,
# `inherited-jakarta-hold`, at the bottom of this file.
g = _journal

# ---------------------------------------------------------------------------
# Step 06's rung: the logging stack
# ---------------------------------------------------------------------------
#
# Ten gates for the leap, read at four depths, and two more for what the leap
# can break.
#
#   source      `no-log4j-source`, `audit-appender-logback`,
#               `velocity-log-not-log4j`
#   own classes `no-log4j-classes`
#   build graph `no-log4j-dep`
#   deployable  `no-log4j-packaged`, `logback-packaged`,
#               `no-log4j-config-packaged`, `logging-levels-declared`,
#               `one-jcl-provider`
#   runtime     `audit-log-intact`, `logging-regression-hold`
#
# Four depths for this rung's own reason, and the reason has a name:
# `org.slf4j:log4j-over-slf4j`. It is a jar that republishes the *caller* side
# of log4j 1.2 -- `Logger`, `Category`, `Level`, `Priority`, `MDC`, `NDC` and
# little else, 30 classes in all -- as a thin facade over slf4j, so that a
# library you cannot recompile keeps linking. Measured: it ships no
# `DailyRollingFileAppender`, no `AsyncAppender`, no
# `Layout` you can extend and a `spi.LoggingEvent` stub with no
# `getTimeStamp()`, so a candidate who swaps `log4j:log4j` for it and changes
# nothing else does not get a tree that compiles at all -- `SshLog` *is* an
# appender, a layout and an error handler, and the bridge carries none of
# those. The interesting candidate is therefore the one who goes further: who
# rewrites `SshLog` enough to compile against the bridge, or shades the
# package in from somewhere, and leaves the deployable publishing
# `org.apache.log4j` all the same. The source gates cannot see that: the
# source names no log4j type. The coordinate gate cannot see it either: the
# coordinate is different, and `log4j:log4j` really is gone.
#
# So `no-log4j-packaged` asks for the absence of the *package* from the
# deployable rather than the absence of an artifact from the graph, which is
# the one question the facade answers wrongly; it is this rung's heaviest gate
# for that reason, as step 05's `no-javax-ee-apis-packaged` was for its. And
# `audit-log-intact` reads the file off the running server, so a tree that
# kept the class but lost the writing does not get there on a scan alone.
#
# Nothing in the 428 observes logging -- the audit log is read off the site
# directory by this file, not by drive.py, and the console is not read at all
# -- so `must-flip.jsonl` gains no records here and carries step 05's 123
# unchanged. A correct tree scores 428 of 428, and so does the tree this step
# is handed. Movement in the behaviour half at this rung is a defect in the
# step, not evidence about the candidate.


def no_log4j_source():
    """No source file in this repository names `org.apache.log4j`.

    Measured on the predecessor: one file, `SshLog.java`, with eight names
    under the package -- `Appender`, `AsyncAppender`, `DailyRollingFileAppender`,
    `Layout`, `Level`, `Logger`, `spi.ErrorHandler`, `spi.LoggingEvent`. Every
    other reference to logging in this tree is already slf4j and stays that
    way.

    Raw sites rather than by-file, which is `no-javax-servlet-source`'s unit
    and for its reason: every place the retired name is still written is a
    place the candidate has to go and edit, and collapsing two imports into a
    wildcard does not make either disappear.

    No own-class floor here, and none is needed: `source-parses` and
    `sources-survive` are two rows above this one in the journal and say how
    many files and declared types there were to search. An AST absence over an
    empty AST is what those two exist to refuse.
    """
    sites = source_type_sites(CAL["logging_packages"])
    return g.absent("no-log4j-source", sorted(sites), 3.0)


def audit_appender_logback():
    """`SshLog` builds its appender out of logback, not out of log4j 1.2.

    The positive half of this rung at the source, and the only gate in this
    file that asks about one named class. That is deliberate: `SshLog` is the
    reason the rung is not a dependency swap. It does not log through a
    facade -- it constructs an appender, gives it a layout, points it at a
    file, wraps it in an asynchronous appender and pushes events into it by
    hand -- and none of that has an slf4j spelling. Every line of it has to be
    rewritten against the backend directly, which is what makes this a
    migration step rather than a version bump.

    Five things are asked, and they
    are asked of the file that declares the
    logger rather than of the tree, so that a candidate who satisfies them in
    some unrelated class does not pass:

      1. the file names a logback appender that writes to a file -- which in
         logback is `FileAppender` or `RollingFileAppender`;
      2. it names logback's `AsyncAppender`, which is what the tree had and
         what keeps a slow disk out of the ssh command path;
      3. it names an encoder or a layout, because an appender with neither
         writes nothing and logback will not start one;
      4. it names no type under `org.apache.log4j` at all.

    What is deliberately *not* asked: which rolling policy, what queue size,
    whether the layout is a `LayoutWrappingEncoder` around a custom `Layout`
    or a `PatternLayoutEncoder`, and whether the events are built as
    `LoggingEvent`s or logged through a `Logger`. All of those are reachable
    routes to the same file on disk, `audit-log-intact` is what says the file
    arrived, and `logging-levels-declared` is what says the levels did. A gate
    that pinned the route would be scoring the reference solution rather than
    the requirement.

    Located by file, not by type name, because a candidate may split `MyLayout`
    out into its own file -- and if they do, the logger file still has to name
    the encoder that wraps it.
    """
    want = CAL["audit_logger_file"]
    files = [f for f in b.ast.files() if (f.get("p") or "").endswith(want)]
    if not files:
        return g.add("audit-appender-logback", False,
                     "no source file named %s was parsed; the AST scan saw "
                     "%d file(s)" % (want, len(b.ast.files())), 3.0,
                     {"looked_for": want})
    paths = {f.get("p") for f in files}
    named = set()
    for f in files:
        for im in f.get("imports") or []:
            named.add(im.split(" ")[0].rstrip(".*").rstrip("."))
    for u in b.ast.kind("typeuse"):
        if u.get("p") in paths:
            named.add(u.get("n") or "")
    simple = {n.rsplit(".", 1)[-1] for n in named}

    file_app = sorted(s for s in simple if s in CAL["logback_file_appenders"])
    async_app = "AsyncAppender" in simple
    shaping = sorted(s for s in simple
                     if s.endswith("Encoder") or s.endswith("Layout"))
    log4j = sorted(n for n in named if S.under(n, CAL["logging_packages"]))

    why = []
    if not file_app:
        why.append("names no logback file appender (one of %s)"
                   % ", ".join(sorted(CAL["logback_file_appenders"])))
    if not async_app:
        why.append("names no AsyncAppender")
    if not shaping:
        why.append("names no encoder or layout, so the appender it builds "
                   "would have nothing to format events with")
    if log4j:
        why.append("still names %s" % ", ".join(log4j[:4]))

    # The instruction requires the audit appender to
    # be configured to drop nothing. The volume floors
    # cannot measure that: logback discards INFO only once 0.8 * queueSize events are
    # queued, and a sequential replay of 193 records never reaches 205. So the
    # configuration itself is the check -- an explicit zero discarding
    # threshold, and no opt-in to dropping when the queue is full. Read from
    # the declaring file's own text, because the discarding threshold is a
    # constructor-time call and the AST scan records types, not calls.
    drop_ok, drop_why = False, "the file could not be re-read"
    for path in sorted(paths):
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except (IOError, OSError):
            continue
        zero = re.search(CAL["audit_discarding_zero_re"], text) is not None
        never = re.search(CAL["audit_never_block_re"], text) is not None
        drop_ok = zero and not never
        drop_why = ("sets no zero discarding threshold, so logback's default "
                    "silently drops records once the queue fills"
                    if not zero else
                    "enables neverBlock, which drops rather than waits")
        break
    if not drop_ok:
        why.append(drop_why)

    logback = sorted(n for n in named if S.under(n, CAL["logback_packages"]))
    return g.add("audit-appender-logback", not why,
                 ("%s builds its appender from %d logback type(s): "
                  "%s, async=%s, shaping=%s"
                  % (want, len(logback), ", ".join(file_app) or "none",
                     async_app, ", ".join(shaping) or "none"))
                 if not why else "%s %s" % (want, "; ".join(why)),
                 3.0, {"file_appenders": file_app, "async": async_app,
                       "drops_nothing": drop_ok, "shaping": shaping,
                       "log4j_names": log4j,
                       "logback_names": logback[:20],
                       "files": sorted(paths)})


def velocity_log_not_log4j():
    """Nothing in this tree still asks Velocity for a log4j log system.

    `GerritGlobalConfiguration.VelocityLifecycle.start()` sets Velocity's
    `runtime.log.logsystem.class` to the string
    `"org.apache.velocity.runtime.log.SimpleLog4JLogSystem"` and sets
    `runtime.log.logsystem.log4j.category` beside it. Neither is a type
    reference, so neither the AST's type uses nor the bytecode's symbolic
    references see them -- they are string constants, and this gate reads them
    out of the constant pool, where ArtifactScan records dotted literals.
    (A case-sensitive grep for `log4j` misses the first of the two: the class
    name spells it `Log4J`. That is how it was nearly missed.)

    Why it carries real weight for two dead strings. Measured, from Velocity
    1.6.4's own bytecode: `LogManager.createLogChute` catches `Exception`
    around the configured class and logs "Target log system for ... is not
    available (...). Falling back to next log system..." before ending at
    `SystemLogChute`. So a candidate who takes log4j off the classpath and
    leaves these two strings in place does not crash, does not lose a single
    one of the 428 observations, and has no way to find out. This gate is the
    only thing in the verifier that can see it. The answer to "does it throw?"
    came from reading `javap -c` on the shipped jar rather than from memory,
    and the weight follows the answer.

    Absence only, with no positive counterpart, because the tree has more than
    one honest destination. Velocity 1.6.4 ships `CommonsLogLogChute` (which
    reaches slf4j through the `jcl-over-slf4j` or `spring-jcl` bridge already
    on the path) and `JdkLogChute`, and has no slf4j chute of its own;
    deleting both property lines and letting `LogManager` run its own fallback
    chain is a third. Step 07 retires Velocity outright, so this rung asks
    only that the tie to log4j be cut, and does not care which way.

    Read from the deployable's own classes and from its own resources, both.
    Bytecode alone would miss a candidate who moved the setting into a
    `velocity.properties`; resources alone would miss the code as it stands.
    """
    hits = []
    for c in production_classes():
        for lit in c.get("l") or []:
            if VELOCITY_LOG4J_RE.search(lit):
                hits.append("%s: \"%s\"" % (c.get("n", "?"), lit[:100]))
    for name, text in packaged_own_resources().items():
        for ln in text.splitlines():
            if VELOCITY_LOG4J_RE.search(ln):
                hits.append("%s: %s" % (name, ln.strip()[:100]))
    return g.absent("velocity-log-not-log4j", sorted(set(hits)), 2.0)


def no_log4j_classes():
    """No class this project compiled references `org.apache.log4j`.

    The bytecode half of `no-log4j-source`, and not redundant with it: an
    import can be deleted while a fully-qualified use in the body stays, a
    generated class has no source to search, and the constant pool records the
    reference either way. Carries the own-class floor for the reason every
    absence gate in this file does -- "no class references the package" is
    also true of a tree that compiled no classes.
    """
    return G.no_refs(g, b, "no-log4j-classes", CAL["logging_packages"],
                     weight=2.0, own=True, floor=CAL["own_floor"])


def no_log4j_dep():
    """The resolved dependency graph names no log4j 1.2, under any of its
    names.

    Four coordinates, and the reason there are four rather than one is that
    log4j 1.2 has been renamed twice and forked once:

      `log4j:log4j`                    the original, 1.2.16 here
      `ch.qos.reload4j:reload4j`       a maintained fork of the same packages
      `org.slf4j:slf4j-log4j12`        the slf4j binding for it, which at 2.x
                                       is a Maven relocation *to*
      `org.slf4j:slf4j-reload4j`       the binding for the fork -- and what is
                                       actually in the predecessor's graph,
                                       because the pom asks for the first and
                                       Maven follows the relocation
      `org.slf4j:log4j-over-slf4j`     the facade, named here so that a
                                       candidate who reaches for it is told
                                       which gate refused them rather than
                                       being left to infer it from
                                       `no-log4j-packaged`

    The last of those is the only one of the five that is sometimes correct
    engineering -- it is how you keep a third-party jar you cannot recompile
    linking against an API you have retired. It is not correct here, and that
    was measured rather than assumed: of the classes inside the predecessor's
    deployable that reference `org.apache.log4j`, four are this project's own
    `SshLog` and its three inner classes, four are `slf4j-reload4j`'s own
    binding (which goes with log4j), two are Velocity's optional log4j chutes
    (loaded reflectively, and `LogManager` catches the failure -- see
    `velocity-log-not-log4j`), and one is Jackson's `SubTypeValidator`, which
    names two log4j classes as *string literals* in a deserialization
    deny-list and never loads them. Nothing on this classpath needs the API to
    be present, so nothing needs a facade to supply it.

    Asked of the resolved graph -- `mvn dependency:list` output, which is what
    `deps.txt` holds -- and not of the poms, so that a coordinate inherited
    through dependency management or arriving transitively is as visible as
    one written down. Which is the point: `jcl-over-slf4j` next door is
    transitive and nowhere in any pom.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "no-log4j-dep", b, 2.5)
    bad = sorted("%s:%s:%s" % t for t in G.deps(b)
                 if (t[0], t[1]) in CAL["log4j_coords"])
    return g.absent("no-log4j-dep", bad, 2.5)


def no_log4j_packaged():
    """Zero archives inside the deployable provide `org.apache.log4j`.

    This rung's heaviest gate, and the one `log4j-over-slf4j` fails. See this
    section's header for why that matters and why the question is asked about
    the package rather than about an artifact.

    Zero and not "at most one": step 04's `one-log4j-provider`, held one row
    down in `inherited-boot-hold`, asked for exactly one because four of this
    project's own classes named the API and zero would have been a
    `NoClassDefFoundError`. This is the rung that removes those four
    references, so this is the rung where zero becomes the right number -- and
    that gate is widened to `<= 1` here to say so. Left at exactly one, it
    would be an inherited test that its successor's own requirements made
    unpassable.

    An empty artifact path makes `inside_artifact` false for everything, so a
    build that produced no deployable would report zero providers and pass
    this gate vacuously. It is guarded the same way its step-05 counterpart
    is: the artifact name is required before the count is believed.
    """
    if not ART_BASE:
        return g.add("no-log4j-packaged", False,
                     "no deployable was produced, so there is nothing to have "
                     "searched for org.apache.log4j", 4.0, {})
    arcs, total = packaged_providers(CAL["logging_packages"])
    return g.add("no-log4j-packaged", not arcs,
                 ("no archive inside %s provides org.apache.log4j" % ART_BASE)
                 if not arcs else
                 ("%d archive(s) inside %s still provide org.apache.log4j "
                  "(%d class(es)): %s"
                  % (len(arcs), ART_BASE, total,
                     ", ".join("%s:%d" % kv for kv in sorted(arcs.items())))),
                 4.0, {"archives": arcs, "total": total})


def logback_packaged():
    """The deployable carries logback, in both of its artifacts.

    The positive counterpart of the gate above, and it asks two things because
    either alone is satisfiable by accident. The package `ch.qos.logback` is
    present in some quantity -- so a candidate who deleted log4j and shipped
    no backend at all, leaving slf4j to bind to its no-op provider and print
    "SLF4J: No SLF4J providers were found", fails here rather than only at
    `audit-log-intact`. And the two ordinary artifacts are present by name --
    `logback-classic`, which is the slf4j binding, and `logback-core`, which
    is where the appenders live. `SshLog` needs the second directly and the
    application needs the first, and a tree with only one of them is a tree
    that will not start.

    Measured on the reference: `logback-classic` and `logback-core` arrive
    together from `spring-boot-starter-logging`, which the predecessor's
    `mini-git-server-httpd/pom.xml` explicitly *excludes* from its Boot
    starter -- removing that exclusion is part of this rung's work, which is
    why this gate is worth stating separately from "some logback is here".

    Named artifacts rather than inferred ones, and matched on the jar name
    stem, so that a version bump does not move the gate. A shaded uber-jar
    that inlines logback's classes under their own package would pass the
    class count and fail the artifact check; that is the intended reading --
    this ladder's deployable is an ordinary Boot jar with an ordinary
    `BOOT-INF/lib`, and `executable-jar` one row down already says so.
    """
    if not ART_BASE:
        return g.add("logback-packaged", False,
                     "no deployable was produced", 3.0, {})
    arcs, total = packaged_providers(CAL["logback_packages"])
    stems = {re.sub(r"-\d.*$", "", a[:-4] if a.endswith(".jar") else a)
             for a in arcs}
    missing = [a for a in CAL["logback_artifacts"] if a not in stems]
    ok = total >= CAL["logback_classes_min"] and not missing
    why = []
    if total < CAL["logback_classes_min"]:
        why.append("%d class(es) under ch.qos.logback, need >=%d"
                   % (total, CAL["logback_classes_min"]))
    if missing:
        why.append("no %s archive" % " or ".join(missing))
    return g.add("logback-packaged", ok,
                 ("%d class(es) under ch.qos.logback inside %s, from %s"
                  % (total, ART_BASE, ", ".join(sorted(arcs)) or "nothing"))
                 if ok else
                 "%s (archives: %s)" % ("; ".join(why),
                                        ", ".join(sorted(arcs)) or "none"),
                 3.0, {"archives": arcs, "total": total,
                       "stems": sorted(stems), "missing": missing})


def no_log4j_config_packaged():
    """No log4j configuration file ships inside the deployable.

    `BOOT-INF/classes/log4j.properties` is 52 lines of appender and level
    configuration for a backend that is no longer on the classpath. Nothing
    reads it, nothing fails because of it, and that is exactly the problem: it
    is the file an operator would open to change a log level, and it would
    have no effect. This ladder has retired a dead descriptor at every rung
    that produced one -- step 03's `web.xml`, step 04's war plumbing -- for
    the same reason and at similar weight.

    Asked of the deployable rather than of the source tree because that is
    where the question has an answer: a `log4j.properties` left in
    `src/main/resources` *is* packaged, and one deleted from there is not, so
    the artifact answers both. Named files rather than a pattern, and
    `log4j2.*` is among them: a candidate who reads "retire log4j 1.2" as
    "upgrade to Log4j 2" has taken a route this rung does not ask for, and
    should be told so by name rather than by silence.

    Scoped to this application's own resource root rather than to the whole
    archive. A Boot jar carries
    its dependencies whole, and one of them -- `spring-boot` itself -- ships
    `org/springframework/boot/logging/log4j2/log4j2.xml` as the template it
    hands to Log4j2 users. That file is inside the deployable, is named
    `log4j2.xml`, and is not this application's configuration; a correct tree
    still contains it, because removing it would mean unpacking and rewriting
    a dependency. The universe here is therefore the same one
    `packaged_own_resources` reads: entries directly under
    `CAL["boot_classes_entry_prefix"]`, which is where this project's own
    `src/main/resources` lands and where nothing else does.
    """
    if not ART_BASE:
        return g.add("no-log4j-config-packaged", False,
                     "no deployable was produced", 1.5, {})
    root = ART_BASE + "!" + CAL["boot_classes_entry_prefix"]
    hits = sorted(o for o in (e.get("o") or "" for e in b.art.entries())
                  if o.startswith(root) and "!" not in o[len(root):]
                  and os.path.basename(o) in CAL["log4j_configs"])
    return g.absent("no-log4j-config-packaged", hits, 1.5)


def logging_levels_declared():
    """The per-logger levels the tree was configured with are still declared.

    What is being preserved, and what is not. `log4j.properties` carries a root
    level and thirteen per-logger entries. Eight of the thirteen name packages
    that are not in this deployable at all -- `org.apache.mina`,
    `org.eclipse.jetty`, `eu.medsea.mimeutil`, `org.apache.xml`,
    `httpclient.wire`, `org.apache.commons.httpclient`(`.HttpMethodBase`),
    `com.google.gerrit.server.ssh.GerritServerSession`, and `velocity` --
    because steps 01 and 02 retired those libraries. Measured by resolving
    each prefix against the classes actually inside the jar. A gate demanding
    all thirteen would fail a candidate for deleting configuration for code
    that is not there, which is the right thing to do with it, so this gate
    asks for the six that still steer something:

        (root)                                                 DEBUG
        com.google.gerrit                          150 classes INFO
        org.apache.sshd.common                     742 classes WARN
        org.apache.sshd.server                     159 classes INFO
        org.apache.sshd.common.keyprovider.FileKeyPairProvider   1 INFO
        net.sf.ehcache                             169 classes WARN

    The root at DEBUG is not a leftover, and that was checked rather than
    assumed: the reference server's console output for one scored run is
    29229 lines, 13152 of them DEBUG, with Spring's own classes logging at
    DEBUG through it. Boot's default root is INFO and would emit about a
    tenth of that. Whatever one thinks of it as a choice, it is this server's
    behaviour, and a migration step is not the place to change it.
    `logging-regression-hold` measures the same property from the other end.

    Read from the deployable's own resources, in either of the two forms
    Spring Boot 3 supports, because both are ordinary and neither is more
    correct:

      * a logback configuration -- `logback.xml` or `logback-spring.xml` --
        with `<root level="..."/>` and `<logger name="..." level="..."/>`;
      * Boot's own property form in `application.properties` or
        `application.yaml`: `logging.level.root` and `logging.level.<name>`.

    Both are parsed and the results merged, so a tree that splits them passes.
    The XML is parsed as XML rather than grepped, which is not
    pedantry: a `<logger>` inside a commented-out `<appender-ref>` block is not
    a declaration. The property form is read line-wise, which is all a
    `.properties` file needs; the YAML form is read as an indented tree
    without a YAML parser, because the verifier image has no PyYAML and adding
    one to four already-calibrated images to read six keys is not a trade this
    step should make. The YAML reader is deliberately narrow and says so in
    the evidence when it finds nothing, so that a candidate who used YAML and
    a candidate who used neither are distinguishable in the report.

    Levels are compared case-insensitively and nothing else about them is
    asked -- not the appender they attach to, not `additivity`, not the
    pattern. This gate is about the six numbers.
    """
    declared, how = packaged_levels()
    want = CAL["log_levels"]
    missing, wrong = [], []
    for name, level in sorted(want.items()):
        got = declared.get(name)
        if got is None:
            missing.append(name or "(root)")
        elif got.upper() != level.upper():
            wrong.append("%s=%s want %s" % (name or "(root)", got, level))
    ok = not missing and not wrong
    why = []
    if missing:
        why.append("%d of %d not declared: %s"
                   % (len(missing), len(want), ", ".join(missing[:6])))
    if wrong:
        why.append("%d at the wrong level: %s"
                   % (len(wrong), "; ".join(wrong[:6])))
    if not how:
        why.append("no logback configuration and no logging.level.* property "
                   "was found inside %s" % (ART_BASE or "the deployable"))
    return g.add("logging-levels-declared", ok,
                 ("all %d level(s) declared, from %s"
                  % (len(want), ", ".join(sorted(how))))
                 if ok else "; ".join(why),
                 3.0, {"declared": declared, "sources": sorted(how),
                       "missing": missing, "wrong": wrong,
                       "wanted": want})


def one_jcl_provider():
    """Exactly one archive inside the deployable provides
    `org.apache.commons.logging`.

    Measured on the predecessor: two. `spring-jcl-6.2.19.jar` arrives with
    `spring-core` and is the implementation Spring Boot expects to find, and
    `jcl-over-slf4j-2.0.18.jar` arrives as a compile-scope transitive of
    `org.apache.sshd:sshd-core:2.19.0`. Both publish
    `org.apache.commons.logging.Log` and `LogFactory`; which of the two a
    class loads is a function of classpath order, which is to say it is not
    decided. Four hundred and ninety-one classes across ten Spring modules
    call into that package, so this is not a theoretical duplicate.

    It is here at this rung and not at step 07 because it is the
    commons-logging bridge, which is part of the logging stack. Seven poms
    declare neither jar, so a search of the poms finds nothing to point at,
    but `mvn dependency:tree` answers in one line what grep cannot answer at
    all. The remedy is an `<exclusions>` block on the `sshd-core`
    dependency in `mini-git-server-sshd/pom.xml`.

    Which one survives is the candidate's business and is not asked. Excluding
    `spring-jcl` and keeping `jcl-over-slf4j` routes JCL to slf4j and is a
    defensible answer; it is not the reference's answer, and the gate counts
    rather than names. Zero is refused as firmly as two: Spring will not start
    without a JCL implementation.
    """
    if not ART_BASE:
        return g.add("one-jcl-provider", False,
                     "no deployable was produced", 1.5, {})
    arcs, total = packaged_providers(CAL["jcl_packages"])
    return g.add("one-jcl-provider", len(arcs) == 1,
                 "%d archive(s) inside %s provide org.apache.commons.logging "
                 "(%s), %d class(es)"
                 % (len(arcs), ART_BASE,
                    ", ".join("%s:%d" % kv for kv in sorted(arcs.items()))
                    or "none", total),
                 1.5, {"archives": arcs, "total": total})


def audit_log_intact():
    """The ssh audit log is still written, and its lines still parse.

    The one gate in this file that reads a running server's output. Every
    other row could be satisfied by a tree that compiles; this one cannot.

    Three sites, because the suite drives three server configurations and each
    keeps its own site directory. `site-main` takes the bulk of the traffic,
    `site-alt` a short second profile, `site-ssl` three commands. Measured on
    the reference: 195, 9 and 3 physical lines, of which 193, 9 and 3 match the
    line grammar -- the two that do not are continuation lines of one record
    whose ssh command contains an embedded newline, which the log writes
    through verbatim. So the count is of *records* and the floors carry a
    little slack under the measurement rather than sitting on it.

    The grammar is the tree's own, and it is asked because an audit log's
    format is its contract with whatever reads it:

        [2026-09-18 07:06:48,412 +0000] afe16f31 tester - 'gerrit' 0ms 49ms 1
         \\__ timestamp ______________/  \\_ session  \\ user \\ acct \\ message

    A timestamp in brackets with milliseconds and a numeric zone, an
    eight-hex-digit session id, the user name, the account id (`-` throughout
    this suite, since these sessions have no account), and then the message.
    It is reachable in logback: a `PatternLayout` of
    `[%d{yyyy-MM-dd HH:mm:ss,SSS Z}] %X{session} %X{userName} %X{accountId} %m%n`
    over MDC properties produces it, and so does a hand-written `Layout`
    wrapped in a `LayoutWrappingEncoder`, which is the shape the tree already
    has. Which route is taken is not asked.

    The file name and location are asked: `<site>/logs/sshd_log`, which is
    where `SitePaths.logs_dir` and `SshLog.LOG_NAME` put it today. An operator
    who upgrades this server should find their log where they left it.

    The counts here are taken from the files on disk at scoring time. The
    verifier also runs `probe.sh` while the servers are still up, and its
    reading is folded into the evidence beside these -- see this gate's
    `probe` key. The two agree on the reference, and they are
    both recorded because the moments are different: the probe reads before
    the harness's SIGTERM, this reads after the SIGKILL. A candidate whose
    backend loses its queue in teardown shows up as a disagreement, and a
    disagreement should be visible in the report rather than something a
    gate silently absorbs.
    """
    pat = AUDIT_LINE_RE
    sites, why, ev = {}, [], {}
    for prof, floor in sorted(CAL["audit_records_min"].items()):
        path = "/score/site-%s/logs/%s" % (prof, CAL["audit_log_name"])
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                lines = fh.read().splitlines()
        except (IOError, OSError) as e:
            sites[prof] = {"exists": False, "error": str(e)[:80],
                           "lines": 0, "records": 0, "need": floor}
            why.append("%s: no %s" % (prof, path))
            continue
        recs = [ln for ln in lines if pat.match(ln)]
        sites[prof] = {"exists": True, "lines": len(lines),
                       "records": len(recs), "need": floor,
                       "first": (recs[0][:160] if recs else
                                 (lines[0][:160] if lines else ""))}
        if len(recs) < floor:
            why.append("%s: %d record(s) matching the audit line grammar in "
                       "%d line(s), need >=%d"
                       % (prof, len(recs), len(lines), floor))
    ev["sites"] = sites
    ev["probe"] = probe_report()
    ev["grammar"] = pat.pattern
    return g.add("audit-log-intact", not why,
                 ("%s written at all three sites: %s"
                  % (CAL["audit_log_name"],
                     ", ".join("%s=%d" % (p, sites[p]["records"])
                               for p in sorted(sites))))
                 if not why else "; ".join(why), 3.0, ev)


# Three readers the gates above share. Each opens its source once and caches,
# because a gate that re-reads a 29 MB jar per question turns a verifier into
# a timeout, and because two gates disagreeing about what a file said would be
# worse than either being wrong.

_RESOURCES = {}


def packaged_own_resources():
    """{entry name: text} for this project's own resources inside the jar.

    "Own" means under the deployable's classes root -- `BOOT-INF/classes/` in
    a Spring Boot jar -- which is where `src/main/resources` lands. It
    deliberately excludes `BOOT-INF/lib/`: a dependency's own
    `logback.xml` is not this application's configuration, and a gate that
    read one would be scoring somebody else's project.

    Binary entries and anything over the cap are skipped rather than decoded,
    and the cap is per entry: the largest thing a candidate has any reason to
    put here is a few kilobytes of XML, and a static asset that happens to
    decode as text should not be able to make a gate slow.
    """
    if _RESOURCES:
        return _RESOURCES
    _RESOURCES["\0loaded"] = ""
    art = b.artifact_path
    if not art or not os.path.exists(art) or not art.endswith(".jar"):
        return _RESOURCES
    import zipfile
    root = CAL["boot_classes_entry_prefix"]
    try:
        with zipfile.ZipFile(art) as z:
            for info in z.infolist():
                n = info.filename
                if not n.startswith(root) or n.endswith("/"):
                    continue
                if n.endswith(".class") or info.file_size > 512 * 1024:
                    continue
                try:
                    _RESOURCES[n] = z.read(n).decode("utf-8")
                except Exception:                       # noqa: BLE001
                    continue
    except Exception:                                   # noqa: BLE001
        pass
    return _RESOURCES


def _levels_from_logback(text):
    """{logger name: level} declared by one logback configuration document.

    Parsed as XML, so that a declaration inside an XML comment is not a
    declaration. The root logger is keyed on the empty string, matching how
    `CAL["log_levels"]` writes it, because logback spells it `<root>` and
    Boot's property form spells it `logging.level.root` and neither spelling
    belongs in the comparison.

    `<springProfile>` and `<if>` wrappers are walked through rather than
    around: a level declared only under a profile this run does not activate
    is still a declaration a reader of the file would see, and drawing that
    line correctly would mean evaluating Boot's conditional configuration,
    which is a great deal of machinery for a distinction no candidate on this
    rung has a reason to make.
    """
    import xml.etree.ElementTree as ET
    out = {}
    try:
        root = ET.fromstring(text)
    except Exception:                                   # noqa: BLE001
        return out
    for el in root.iter():
        tag = G._local(el.tag)
        lvl = (el.get("level") or "").strip()
        if not lvl:
            continue
        if tag == "root":
            out[""] = lvl
        elif tag == "logger":
            nm = (el.get("name") or "").strip()
            if nm:
                out[nm] = lvl
    return out


def _levels_from_properties(text):
    """{logger name: level} from `logging.level.*` lines in a .properties."""
    out = {}
    for ln in text.splitlines():
        ln = ln.strip()
        if not ln or ln[0] in "#!":
            continue
        key, sep, val = ln.partition("=")
        if not sep:
            key, sep, val = ln.partition(":")
        if not sep:
            continue
        key, val = key.strip(), val.strip()
        if not key.startswith("logging.level."):
            continue
        nm = key[len("logging.level."):].strip('"').strip("'")
        out["" if nm == "root" else nm] = val
    return out


def _levels_from_yaml(text):
    """{logger name: level} from a `logging: level:` block in a .yaml.

    A deliberately small reader for a deliberately small shape. It follows
    `logging:` then `level:` by indentation and takes the `key: value` pairs
    under them, including the dotted-key form Boot also accepts
    (`logging.level.root: DEBUG`) and quoted keys, which YAML requires for a
    key containing a dot at some indentation levels. It understands nothing
    else -- no anchors, no flow mappings, no multi-document files -- and that
    is a choice with a reason: the verifier image has no YAML parser, and
    adding a dependency to four already-calibrated images so that this one
    gate can read six keys is a worse trade than a thirty-line reader whose
    limits are written down. `logging-levels-declared` reports which readers
    found anything, so a candidate whose YAML this cannot read sees that in
    the evidence rather than seeing an unexplained failure.
    """
    out = {}
    stack = []           # (indent, key)
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        line = raw.strip()
        if line.startswith("- "):
            continue
        key, sep, val = line.partition(":")
        if not sep:
            continue
        key = key.strip().strip('"').strip("'")
        val = val.strip().strip('"').strip("'")
        while stack and stack[-1][0] >= indent:
            stack.pop()
        path = [k for _, k in stack] + [key]
        flat = ".".join(path)
        if not val:
            stack.append((indent, key))
            continue
        if flat.startswith("logging.level."):
            nm = flat[len("logging.level."):]
            out["" if nm == "root" else nm] = val
    return out


def packaged_levels():
    """({logger: level}, {source file names}) merged over every form found.

    Merged rather than "first one wins", because a tree that puts its root
    level in `application.properties` and its per-logger levels in a
    `logback.xml` has declared all of them and both files are read by the
    running application. Where two sources disagree the logback file wins,
    which is Boot's own precedence and is the only ordering that could be
    called correct.
    """
    props, xmls, how = {}, {}, set()
    for name, text in packaged_own_resources().items():
        base = os.path.basename(name)
        if base in CAL["logback_configs"]:
            got = _levels_from_logback(text)
            if got:
                xmls.update(got)
                how.add(base)
        elif base in CAL["boot_property_files"]:
            got = (_levels_from_properties(text) if base.endswith(".properties")
                   else _levels_from_yaml(text))
            if got:
                props.update(got)
                how.add(base)
    merged = dict(props)
    merged.update(xmls)
    return merged, how


_PROBE = {}


def probe_report():
    """What `probe.sh` saw, parsed out of /score/log/probe.log.

    Corroboration for `audit-log-intact`, never a substitute for it: this
    returns whatever the probe wrote and judges none of it, and a missing or
    unparsable probe log returns `{"ran": False}` rather than raising. A gate
    that could be failed by its instrument going missing is a gate measuring
    the wrong thing -- the verifier's own health, not the candidate's tree.
    """
    if _PROBE:
        return _PROBE
    _PROBE["ran"] = False
    path = os.path.join(S.LOG, "probe.log")
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            raw = fh.read()
    except (IOError, OSError):
        return _PROBE
    sites = {}
    for ln in raw.splitlines():
        f = ln.split("\t")
        if f[0] == "probe.site" and len(f) >= 7:
            sites[f[1]] = {"lines": _as_int(f[3]), "bytes": _as_int(f[4]),
                           "state": f[5], "polls": _as_int(f[6])}
        elif f[0] == "probe.logsdir" and len(f) >= 3:
            sites.setdefault(f[1], {})["logsdir"] = f[2][:200]
    _PROBE.update({"ran": bool(sites), "sites": sites})
    return _PROBE


def _as_int(s):
    try:
        return int(s)
    except (TypeError, ValueError):
        return -1


# Step 06's whole rung, held as one row rather than as eleven -- the same
# collapse step 03 did to step 02, step 04 to step 03, step 05 to step 04 and
# step 06 to step 05, and for the same arithmetic. Step 06's eleven rung gates
# carry 28.5 between them; left at full weight they would be more than half of
# this file's total, and a tree that finished step 06 and touched nothing else
# would score most of a rung it has not attempted. The floor measurement --
# score the predecessor with this verifier and look for a number at or below
# 0.5 -- is what that arithmetic is for.
#
# Two of the eleven are not quite step 06's question, and both changed because
# this rung removed what they named rather than because they were weakened:
#
#   * `logging_levels_declared` reads `CAL["log_levels"]`, which lost its
#     `net.sf.ehcache` entry at this rung. Asked with six categories it would
#     fail a correct tree for deleting a logger for a library it just removed.
#   * `velocity_log_not_log4j` searches for a Velocity log-system string that
#     no longer has a Velocity to configure, and is now vacuously true. It
#     stays rather than being deleted because a hold row that quietly drops a
#     sub-check is a hold row that stopped holding, and because this file's
#     own `no-legacy-source` and `no-legacy-classes` ask a strictly stronger
#     question about the same package.
_journal, g = g, LOG
no_log4j_source()
audit_appender_logback()
velocity_log_not_log4j()
no_log4j_classes()
no_log4j_dep()
no_log4j_packaged()
logback_packaged()
no_log4j_config_packaged()
logging_levels_declared()
one_jcl_provider()
audit_log_intact()
g = _journal


# ---------------------------------------------------------------------------
# Step 07's rung, held
# ---------------------------------------------------------------------------
#
# The fourteen gates below are step 07's, verbatim, and they are collected into
# `inherited-legacy-hold` rather than journalled -- the same demotion every rung
# on this ladder performs on its predecessor. Their header is kept as it was
# written, because what they ask has not changed; only what it is worth has.
# The rebinding sits at the bottom of the region, around the call list, with a
# second one around `no-legacy-classes`, which is a module-level call rather
# than a function and so cannot be caught by the first.
#
# Step 07's own header follows.
#
# Fourteen rows, 30.0 between them, and the split between the two halves is
# the design rather than an accident of pricing. Five absence rows carry 16.0
# and nine presence rows carry 14.0 -- near even, for a step whose one-line
# summary is "delete nine dependencies".
#
# The reason is in the header and is worth restating where the gates are: at
# this rung every absence is cheap and every presence is not. Nothing in the
# 428 observations reaches a cache, a `*.zip` under the base path, or
# `SshUtil.parse`, so a tree that retires Ehcache by deleting the cache
# package, retires commons-io by deleting the startup expansion and retires
# commons-codec by deleting two orphan methods passes all five absence rows,
# builds, and reproduces every observation. The nine presence rows are the
# only thing standing between that tree and full marks, and they are priced
# accordingly.
#
# What none of them does is require the port to look like the reference's.
# `cache-bounds-applied` takes any of Caffeine's three expiry policies and
# either of its two size bounds; `cache-api-intact` floors the package's type
# count rather than naming `ProxyCache`; `zip-expansion-intact` takes either
# JDK zip API and has no opinion on how the directory is walked. The rows name
# what the rung is *for* -- a bounded cache built from configuration, a key
# parser that still parses, a startup expansion that still expands -- and stop
# there.


def pom_dependencies():
    """Every `<dependency>` written in this tree, and where it was written.

    Yields (pom path, groupId, artifactId, version, managed). `managed` says
    the declaration is inside a `<dependencyManagement>` block, which is the
    distinction the declaration gate needs and the resolved graph cannot make:
    a managed entry for a coordinate nothing depends on is invisible to
    `dependency:list` and is still a version this project pins.

    Written against `_pom_docs` rather than against the text so that a
    coordinate split across lines, or written with its elements in an unusual
    order, reads the same as the tree's own formatting. An unparsable pom
    contributes nothing here and fails `compiler-release` two rows up.
    """
    out = []
    for rel, root, _text in G._pom_docs(b):
        if root is None:
            continue
        managed = set()
        for block in root.iter():
            if G._local(block.tag) != "dependencyManagement":
                continue
            for d in block.iter():
                if G._local(d.tag) == "dependency":
                    managed.add(id(d))
        for d in root.iter():
            if G._local(d.tag) != "dependency":
                continue
            kv = {}
            for ch in d:
                kv[G._local(ch.tag)] = (ch.text or "").strip()
            out.append((rel, kv.get("groupId", ""), kv.get("artifactId", ""),
                        kv.get("version", ""), id(d) in managed))
    return out


def packaged_archives():
    """The filenames of the archives nested inside the deployable.

    Both scans are read because an archive is not obliged to contain a class:
    `packaged_providers` would miss a jar of resources, and a candidate who
    kept `velocity-1.6.4.jar` for its `velocity.properties` alone would be a
    tree that did not retire velocity.

    The deployable itself is excluded -- every entry directly inside it has
    the deployable as its archive -- so what is left is the `BOOT-INF/lib`
    census. Measured: 54 on the predecessor, 45 on the reference.
    """
    names = set()
    for rec in list(b.art.classes()) + list(b.art.entries()):
        o = rec.get("o") or ""
        if not inside_artifact(o) or "!" not in o:
            continue
        head = o.rsplit("!", 1)[0]
        if head == ART_BASE:
            continue
        base = os.path.basename(head)
        if base.endswith((".jar", ".war", ".zip")):
            names.add(base)
    return names


def sources_under(marker):
    """{relative path: text} for repository sources whose path holds `marker`.

    The three gates that need this need a *string literal* -- `"cache"`,
    `"memorylimit"`, `".zip"` -- and a literal with no dot in it is in neither
    scan: `AstScan` does not emit string literals as nodes, and the
    constant-pool view `refs()` builds keeps only dotted ones, because its job
    is finding type names reached reflectively. Reading the file is the honest
    way to ask, and the weakness is stated rather than hidden: a key that
    survives only inside a comment satisfies these rows. Each of the three
    pairs the literal with something structural -- a config-read call, a
    bytecode reference -- that a comment cannot satisfy.
    """
    out = {}
    for path in S.find_files(b.app, suffixes=(".java",)):
        rel = os.path.relpath(path, b.app).replace("\\", "/")
        if marker not in "/" + rel:
            continue
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                out[rel] = fh.read(1 << 20)
        except (IOError, OSError):
            out[rel] = ""
    return out


def methods_in(suffix):
    """The declared method names of one source file, by path suffix."""
    return sorted(m.get("sig", "").split("(")[0]
                  for m in b.ast.kind("method")
                  if (m.get("p") or "").replace("\\", "/").endswith(suffix))


def calls_in(marker):
    """The called method names under a path marker, as a set."""
    return {n.get("d") for n in b.ast.nodes("call")
            if marker in "/" + (n.get("p") or "").replace("\\", "/")}


def declared_methods(names, wanted_by_file):
    """Which (file, method) pairs in `wanted_by_file` are not declared."""
    missing = []
    for suffix, wanted in wanted_by_file:
        have = set(names(suffix))
        for w in wanted:
            if w not in have:
                missing.append("%s.%s" % (suffix.rsplit("/", 1)[-1], w))
    return missing


# --- the retirement, five ways ---------------------------------------------
#
# Five rows and not one, because they fail for different reasons and a
# candidate should be told which one they hit. A tree that deleted the
# declarations but left the imports fails `no-legacy-source` alone; one that
# rewrote the code but left a managed version fails `no-legacy-declarations`
# alone; one that did both but shaded a jar into its own build output fails
# only `no-legacy-packaged`. Each of those is a different afternoon's work.


def no_legacy_deps():
    """None of the fourteen retired coordinates is in the resolved graph.

    Asked of `dependency:list` over all seven reactor modules rather than of
    the poms, because the four transitive ones are in no pom at all: `oro` and
    `commons-collections` arrive under velocity, `antlr-runtime` under antlr,
    `commons-compress` under util-compress, and each leaves only when its
    parent does. A pom-only reading would call a tree clean that still ships
    four 2003-era jars.

    `bouncycastle:bcpg-jdk15` is here despite being `provided` and therefore
    never packaged. Scope decides what is in the deployable, not what is on
    the compile classpath, and a 2009 crypto library the tree compiles against
    is still a 2009 library this rung retires.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "no-legacy-deps", b, 4.0)
    want = set(CAL["retired_coords"])
    hits = sorted("%s:%s:%s" % t for t in G.deps(b) if (t[0], t[1]) in want)
    return g.absent("no-legacy-deps", hits, 4.0)


def no_legacy_declarations():
    """No pom declares one of the nine, or pins commons-codec at all.

    The row the resolved graph cannot replace. A `<dependencyManagement>`
    entry for a coordinate nothing currently depends on resolves to nothing,
    appears in no `dependency:list` output, and is still a 2009 version this
    project pins for whoever adds the dependency back. Measured: the
    predecessor writes eighteen declarations of the nine across three poms --
    ten in `<dependencies>` blocks and eight in the root pom's
    `<dependencyManagement>` -- plus the commons-codec pin below, for the
    nineteen sites its report lists. The reference writes none of them.

    commons-codec is asked differently and the difference is the point of the
    rung. The coordinate does not leave -- jgit 7 depends on it and brings
    1.18.0 -- so the absence available here is the *pin*, and every non-empty
    `<version>` on the coordinates in `legacy_pins` is one, whatever it says.
    `1.4` freezes the graph at 2009 and `1.18.0` freezes it at today's
    release; a range and a property this file cannot expand freeze it at
    whatever Maven picks, which is the one thing §1.2 says this tree does not
    get to decide. A candidate who deletes both declarations, as the reference
    does, has nothing to match, and so does one who keeps the sshd pom's
    version-less declaration -- that is not a pin and is not asked about.
    Anyone else is told which pin was seen, as it is written and, where a
    property expanded, as it resolved.
    """
    want = set(CAL["retired_declarations"])
    pins = set(CAL["legacy_pins"])
    props = G._pom_properties(G._pom_docs(b))
    hits = []
    for rel, gid, aid, ver, managed in pom_dependencies():
        where = rel + (" <dependencyManagement>" if managed else "")
        if (gid, aid) in want:
            hits.append("%s: %s:%s%s"
                        % (where, gid, aid, ":" + ver if ver else ""))
            continue
        if (gid, aid) in pins and ver:
            resolved = G._resolve(ver, props)
            hits.append("%s: %s:%s:%s (pin)"
                        % (where, gid, aid,
                           ver if resolved == ver
                           else "%s -> %s" % (ver, resolved)))
    return g.absent("no-legacy-declarations", sorted(set(hits)), 2.5)


def no_legacy_packaged():
    """Nothing inside the deployable is one of the retired libraries.

    Two questions in one row, because they are two ways of shipping the same
    jar and neither is the interesting one on its own. By filename, for the
    fourteen stems: a prefix match, so `commons-lang-` does not object to
    `commons-lang3-3.19.0.jar`. By package, for the thirteen the retired
    archives provide: a candidate who unpacks `ehcache-core-1.7.2.jar` into
    `target/classes` or shades it into a fat jar leaves no filename behind and
    every class.

    The vacuity guard is the archive census itself. An absence over an empty
    deployable is what a build that packaged nothing says, in the same words a
    clean tree uses, so the row fails when fewer than `packaged_archives_min`
    archives were found at all -- and says which of the three things went
    wrong rather than reporting a clean pass.
    """
    arcs = packaged_archives()
    bad_arcs = sorted(a for a in arcs
                      if a.startswith(tuple(CAL["retired_archive_stems"])))
    providers, total = packaged_providers(CAL["retired_packages"])
    floor = CAL["packaged_archives_min"]
    vacuous = len(arcs) < floor
    ok = not bad_arcs and not providers and not vacuous
    if vacuous:
        detail = ("%d archive(s) inside %s, need >=%d -- nothing to search"
                  % (len(arcs), ART_BASE or "NONE", floor))
    elif bad_arcs or providers:
        detail = ("%d retired archive(s): %s; %d class(es) under the retired "
                  "packages, from %s"
                  % (len(bad_arcs), ", ".join(bad_arcs[:8]) or "none", total,
                     ", ".join(sorted(providers)[:6]) or "none"))
    else:
        detail = ("none of %d archive(s) inside %s is retired, and none "
                  "provides any of the %d retired package(s)"
                  % (len(arcs), ART_BASE or "NONE",
                     len(CAL["retired_packages"])))
    return g.add("no-legacy-packaged", ok, detail, 4.0,
                 {"archives": len(arcs), "need": floor,
                  "retired_archives": bad_arcs,
                  "providers": providers, "provided_classes": total,
                  "packages": list(CAL["retired_packages"])})


def no_legacy_source():
    """No source file in this repository names a retired package.

    Thirteen prefixes, and `org.apache.commons.codec` is one of them even
    though the coordinate stays: the rung retires the *call sites*, and the
    two in `SshUtil` are the only ones this tree has. Five of the thirteen had
    no call site to begin with -- antlr, bouncycastle, commons-lang,
    commons-net and javax.annotation, whose declarations this rung deletes
    without touching a line of Java -- and they are in the list so that a
    candidate who introduces a use while removing the declaration is caught by
    the row that named it.

    No own-class floor here, for the reason `no-javax-servlet-source` gives:
    `source-parses` and `sources-survive` are above this in the journal and
    say there were 105 files and 132 declared types to search.
    """
    return G.no_source_types(g, b, "no-legacy-source",
                             CAL["retired_source_packages"], weight=2.5)


# The same question of the bytecode this project compiled. Own classes only:
# the deployable half is `no-legacy-packaged` above, which asks it of archives
# rather than of references, and asking both of the whole jar would fail the
# reference for `caffeine-3.2.4.jar`'s own internal references or for anything
# else a current dependency happens to name. The floor is step 04's, for step
# 04's reason -- an absence over no classes is not a measurement.
_journal, g = g, LEG
G.no_refs(g, b, "no-legacy-classes", CAL["retired_source_packages"],
          weight=3.0, own=True, floor=CAL["own_floor"])
g = _journal


# --- the port, nine ways ---------------------------------------------------


def cache_backend_packaged():
    """A current cache library resolves, and is actually inside the jar.

    Named by coordinate because §5 of the instruction names it, which is step
    06's precedent for `logback-packaged`: where the rung's sentence says
    which library, the gate says which library. Where it does not -- the
    bounds, the expiry, the shape of the port -- the rows below stay out of
    it.

    Two halves because a `<dependency>` is not a classpath. The version
    pattern accepts any 3.x, so a candidate who lets Boot's BOM choose (as the
    reference does, writing no version at all) and one who pins 3.1.8 both
    pass; the class floor is 400 against 718 measured, which is loose enough
    to survive Caffeine repackaging itself and tight enough that an empty
    stub jar does not read as the library.

    A third half because a classpath is not a port. Those two say the library
    resolves and that its classes are inside the deployable; they say nothing
    about anything in this tree reaching for it, and a `<dependency>` nobody
    imports is the cheapest way to hold this row open while the cache package
    underneath it is still Ehcache or, worse, still nothing. So at least one
    class this project compiles under `com.google.gerrit.server.cache` has to
    name `com.github.benmanes.caffeine` in its own constant pool. Asked of the
    bytecode and not of the import list, because a
    `com.github.benmanes.caffeine.cache.Cache<K, V>` written out in full in a
    parameter type -- which is how `CacheProvider` writes it -- has no import
    to find. Measured: three of the reference's ten cache files name the
    package, the predecessor's eleven name it nowhere.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "cache-backend-packaged", b, 2.0)
    hits = sorted("%s:%s:%s" % t for t in G.deps(b)
                  if t[0] == CAL["caffeine_group"]
                  and re.match(CAL["caffeine_version_re"], t[2]))
    arcs, total = packaged_providers(CAL["caffeine_packages"])
    need = CAL["caffeine_classes_min"]
    users = production_refs_in(CAL["cache_package"], CAL["caffeine_packages"])
    ok = bool(hits) and total >= need and bool(users)
    return g.add("cache-backend-packaged", ok,
                 "%s resolved (%s); %d class(es) under %s inside %s, need "
                 ">=%d; %d own class(es) under %s name it"
                 % (len(hits), ", ".join(hits) or "none", total,
                    CAL["caffeine_packages"][0], ART_BASE or "NONE", need,
                    len(users), CAL["cache_package"]),
                 2.0,
                 {"coordinates": hits, "classes": total, "need": need,
                  "archives": arcs, "cache_users": sorted(users)})


def cache_api_intact():
    """The cache package, and the interface its tenants would call.

    This is the rung's central anti-deletion row and the one most likely to be
    misread as pedantry, so: there are zero caches declared in this tree.
    `CacheModule.core` is called by nothing, `Cache.get` is called by nothing,
    and no observation in the behaviour suite touches any of it. The cheapest
    way to make every absence row in this file pass is `rm -r
    com/google/gerrit/server/cache`, and that tree builds and scores 428 of
    428. This row and the three after it are the only thing that sees it.

    A type-count floor rather than a list of class names, because the port
    changes which classes exist: thirteen types are declared under the package
    in the predecessor and thirteen in the reference, but `EvictionPolicy` and
    `ProxyEhcache` leave and a lazily-bound holder and a private failure type
    arrive. Naming the old ones would forbid the port; naming the new ones
    would require this particular port. Nine leaves room to fold the holder
    into `CacheProvider` and none to delete the package.

    The five method names are `Cache`'s own interface and are named, because
    that interface is the API the rung must not change -- it is what any
    future caller writes against, and it is spelled identically over Ehcache
    and over Caffeine.
    """
    pkg = CAL["cache_package"]
    types = sorted(t.get("n", "") for t in b.ast.kind("type")
                   if S.under(t.get("n", ""), (pkg,)))
    missing = declared_methods(methods_in, CAL["cache_api"])
    need = CAL["cache_types_min"]
    ok = not missing and len(types) >= need
    return g.add("cache-api-intact", ok,
                 "%d type(s) under %s, need >=%d; %s"
                 % (len(types), pkg, need,
                    "missing " + ", ".join(missing) if missing
                    else "Cache declares all %d method(s)"
                         % len(CAL["cache_api"][0][1])),
                 2.0,
                 {"types": types[:40], "count": len(types), "need": need,
                  "missing": missing})


def cache_declaration_dsl():
    """The way a cache is declared, still declarable.

    `CacheModule.core(name)` returning a `NamedCacheBinding` with
    `memoryLimit`, `maxAge` and `populateWith` on it is the whole tenant-facing
    surface of this subsystem -- the sentence a module writes to ask for a
    cache. The port replaces what is underneath it and must not replace it.

    `diskLimit` and `evictionPolicy` were on this interface in the predecessor
    and are deliberately not asked for. Caffeine is in-memory only and its
    admission policy is not selectable, so both knobs lost their meaning at
    this rung, and a row that required them would be requiring dead
    configuration to be kept alive for the gate's sake. This file does not do
    that anywhere, and the one place it would have been easy is here.
    """
    missing = declared_methods(methods_in, CAL["cache_dsl"])
    wanted = sum(len(w) for _f, w in CAL["cache_dsl"])
    return g.add("cache-declaration-dsl", not missing,
                 "%d of %d declaration method(s) present%s"
                 % (wanted - len(missing), wanted,
                    "; missing " + ", ".join(missing) if missing else ""),
                 1.5,
                 {"missing": missing, "wanted": wanted,
                  "files": [f for f, _w in CAL["cache_dsl"]]})


def cache_bounds_applied():
    """Whatever builds the caches still bounds them, two ways.

    A cache with no size bound is a leak and a cache with no expiry is a
    correctness bug, and the predecessor applied both -- `maxElementsInMemory`
    and `timeToLiveSeconds` on an Ehcache `CacheConfiguration`. The
    straightforward port applies both; the port that reads "Caffeine is a
    `ConcurrentHashMap` with extras" and stops there applies neither, and
    nothing in this tree would ever notice, because nothing declares a cache.

    Two sets and not two names, because Caffeine spells each bound more than
    one way and the rung has no opinion on which: `maximumSize` counts entries
    where `maximumWeight` weighs them, and the three expiry policies differ in
    when the clock starts, not in whether there is one. Asked of call names
    anywhere in the cache package rather than of `CachePool` specifically, so
    that a candidate who moves the builder into the provider passes.
    """
    marker = "/" + CAL["cache_package"].replace(".", "/") + "/"
    called = calls_in(marker)
    bounds = sorted(called & set(CAL["cache_bound_calls"]))
    expiry = sorted(called & set(CAL["cache_expiry_calls"]))
    ok = bool(bounds) and bool(expiry)
    return g.add("cache-bounds-applied", ok,
                 "size bound: %s; expiry: %s (in %d call site name(s) under "
                 "%s)" % (", ".join(bounds) or "NONE",
                          ", ".join(expiry) or "NONE", len(called),
                          CAL["cache_package"]),
                 1.5,
                 {"bounds": bounds, "expiry": expiry,
                  "bound_options": list(CAL["cache_bound_calls"]),
                  "expiry_options": list(CAL["cache_expiry_calls"])})


def cache_config_honoured():
    """And the bounds come out of configuration, not out of a constant.

    The half of the port that `cache-bounds-applied` cannot see. A candidate
    who writes `Caffeine.newBuilder().maximumSize(1024).expireAfterWrite(
    Duration.ofMinutes(5))` satisfies the row above completely and has thrown
    away the `cache.<name>.memorylimit` and `cache.<name>.maxage` keys the
    site administrator sets -- which, with no cache declared anywhere, no test
    would ever miss.

    Three halves, and the third is there because the first two are both
    satisfiable by text that does nothing. The three literals still appear in
    the package's source -- but `sources_under` reads raw file text, so a key
    that survives only inside a comment clears it. The package still makes at
    least two calls that read a JGit `Config` -- but those are AST call names,
    and `getInt` is not a rare one. So, third: at least one class this project
    compiles under the cache package has to name `org.eclipse.jgit.lib.Config`
    in its own constant pool, which is what a field, a constructor parameter
    or a call on one of them puts there and what a comment cannot. The trio is
    hard to satisfy without actually reading the keys.

    The literals are matched lower-cased because that is how `Config` compares
    section and key names and how this tree writes them. The third half is a
    hold and not a leap -- the predecessor names the type too, over Ehcache --
    which is the point: it asks that the configuration reached the builder,
    not that the builder is anyone's particular port.
    """
    marker = "/" + CAL["cache_package"].replace(".", "/") + "/"
    text = "\n".join(sources_under(marker).values()).lower()
    missing = [k for k in CAL["cache_config_keys"] if k not in text]
    reads = sorted(calls_in(marker) & set(CAL["cache_config_calls"]))
    need = CAL["cache_config_calls_min"]
    users = production_refs_in(CAL["cache_package"], CAL["cache_config_types"])
    ok = not missing and len(reads) >= need and bool(users)
    return g.add("cache-config-honoured", ok,
                 "%d of %d config key(s) still written%s; %d config read "
                 "call(s) (%s), need >=%d; %d own class(es) under %s name %s"
                 % (len(CAL["cache_config_keys"]) - len(missing),
                    len(CAL["cache_config_keys"]),
                    "; missing " + ", ".join(missing) if missing else "",
                    len(reads), ", ".join(reads) or "none", need,
                    len(users), CAL["cache_package"],
                    CAL["cache_config_types"][0]),
                 1.5,
                 {"missing_keys": missing, "reads": reads, "need": need,
                  "keys": list(CAL["cache_config_keys"]),
                  "config_users": sorted(users),
                  "config_types": list(CAL["cache_config_types"])})


def sshutil_methods_intact():
    """The key parser is ported, not deleted.

    Step 02 measured this file and left it alone: nothing in this tree calls
    `SshUtil.parse` or `SshUtil.toOpenSshPublicKey`, so no observation in the
    behaviour suite reaches either, and `sources-survive` counts a file whose
    bodies have all been emptied. They hold this rung's only two commons-codec
    call sites. "Delete both methods" is therefore a complete answer to
    `no-legacy-source`, `no-legacy-classes` and `no-legacy-deps` at once, and
    it leaves a server that can no longer read an RFC 4716 public key.

    Three declared methods is what the pinned tree, the predecessor and the
    reference each declare -- `parse`, `toOpenSshPublicKey` and the nested
    `parseOpenSSHKey` -- so the floor is an equality in practice and a floor
    in form, which is the direction that cannot be gamed. Beside it, the
    compiled class must name a JDK decoder: that is what separates a port from
    a deletion, and it is measured rather than assumed -- the predecessor's
    class names `org.apache.commons.codec` and not `java.util.Base64`, the
    reference's the other way round.
    """
    declared = methods_in(CAL["sshutil_file"])
    need = CAL["sshutil_methods_min"]
    users = production_refs(CAL["sshutil_decoder_types"])
    named = sorted(u for u in users if u.endswith(CAL["sshutil_class"]))
    ok = len(declared) >= need and bool(named)
    return g.add("sshutil-methods-intact", ok,
                 "%s declares %d method(s) (%s), need >=%d; %s names %s: %s"
                 % (CAL["sshutil_file"].rsplit("/", 1)[-1], len(declared),
                    ", ".join(declared) or "none", need,
                    CAL["sshutil_class"].rsplit(".", 1)[-1],
                    CAL["sshutil_decoder_types"][0],
                    "yes" if named else "NO"),
                 1.5,
                 {"declared": declared, "need": need,
                  "decoder_users": sorted(users),
                  "decoder_types": list(CAL["sshutil_decoder_types"])})


def zip_expansion_intact():
    """The startup zip expansion is ported, not deleted.

    Same near-miss, different file. `LocalDiskRepositoryManager` expands every
    `*.zip` under the base path before the first repository is opened; no
    fixture in the behaviour suite ships a zip, so deleting the expansion
    costs nothing and removes this rung's commons-io and util-compress call
    sites in one edit.

    A declared-method floor is the wrong instrument here and the measurement
    is why: the predecessor does the expansion in four library calls and
    declares eleven methods, the reference does it in four private helpers and
    declares fifteen, and a tree that deletes the expansion outright lands
    back at eleven -- indistinguishable from the tree this step is handed. So
    the row asks the two things that are true of a tree that still expands
    zips and false of one that does not: the compiled class names a JDK zip
    API, and the source still says which files it is looking for.

    Either JDK zip API counts. `java.util.zip` is what the reference uses;
    the zip filesystem provider reached through `java.nio.file.FileSystems` is
    the other way to do it and is not a worse one.
    """
    users = production_refs(CAL["zip_api_packages"])
    named = sorted(u for u in users if u.endswith(CAL["zip_scan_class"]))
    texts = sources_under("/" + CAL["zip_file"])
    suffix = re.compile(CAL["zip_suffix_re"], re.IGNORECASE)
    says = sorted(f for f, t in texts.items() if suffix.search(t))
    ok = bool(named) and bool(says)
    return g.add("zip-expansion-intact", ok,
                 "%s names %s: %s; source names a zip suffix: %s"
                 % (CAL["zip_scan_class"].rsplit(".", 1)[-1],
                    "/".join(CAL["zip_api_packages"]),
                    ", ".join(named) or "NO", "yes" if says else "NO"),
                 1.5,
                 {"zip_api_users": sorted(users), "names_suffix": says,
                  "packages": list(CAL["zip_api_packages"]),
                  "files_read": sorted(texts)})


def args4j_current():
    """The one retirement that is a version, and the module that uses it.

    args4j 2.0.12 (2009) to 2.37 is nearly not a port: the four annotations
    and two types the CLI module uses are spelled identically in both
    releases, and the coordinate change is one line in the root pom. It is in
    this file for the same reason the other eight coordinates are -- a 2009
    jar in `BOOT-INF/lib` is a 2009 jar -- and with the same anti-deletion
    floor, because "retire args4j" also has a cheap wrong answer: delete the
    option parsing. Thirteen own classes reference `org.kohsuke.args4j` in the
    predecessor and thirteen in the reference.

    What this row does *not* ask is the second half of that upgrade, and the
    omission is deliberate. From 2.0.16 args4j appends `(default: false)` to
    every boolean option in the help text it generates, which rewrites nine of
    the 428 recorded observations -- `gerrit --help`, `gerrit -h`,
    `git --help` and six more. A tree that takes the version and not the
    consequence loses those nine, measured, in the behaviour half of the
    reward. Restating it as a gate here would price the same mistake twice,
    and would price it in a file the candidate cannot read.

    The version pattern accepts 2.33 and later rather than 2.37 exactly.
    Pinning the reference's own version would be pinning the day this file was
    written; 2.33 is the boundary that matters, being the first release built
    for a JDK this ladder targets.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "args4j-current", b, 1.5)
    hits = sorted("%s:%s:%s" % t for t in G.deps(b)
                  if t[1] == "args4j"
                  and re.match(CAL["args4j_version_re"], t[2]))
    users = production_refs(CAL["args4j_packages"])
    need = CAL["args4j_classes_min"]
    ok = bool(hits) and len(users) >= need
    return g.add("args4j-current", ok,
                 "%s; %d own class(es) reference %s, need >=%d"
                 % (", ".join(hits) or "no args4j at a current version",
                    len(users), CAL["args4j_packages"][0], need),
                 1.5,
                 {"coordinates": hits, "users": sorted(users)[:40],
                  "count": len(users), "need": need})


def no_retired_log_categories():
    """The logging configuration stops naming libraries that are gone.

    The complementary half of the widening `logging-levels-declared` took at
    this rung. That gate asks for five categories where step 06 asked for six,
    because a correct tree deletes the `net.sf.ehcache` logger along with
    Ehcache -- and a gate that only ever loosens is a gate a candidate can
    satisfy by doing nothing. This one asks the other direction: neither
    retired category may still be declared.

    Together the pair keeps the inherited row passable without letting it go
    slack: the inherited row is re-measured against this rung's reference
    instead of being carried forward, and the category it lost becomes a row
    of its own.
    """
    levels, how = packaged_levels()
    bad = sorted(name for name in levels
                 if S.under(name, CAL["retired_log_categories"]))
    return g.add("no-retired-log-categories", not bad,
                 "%d retired categor(y|ies) still declared%s; %d logger(s) "
                 "read from %s"
                 % (len(bad), ": " + ", ".join(bad) if bad else "",
                    len(levels), ", ".join(sorted(how)) or "no config found"),
                 1.0,
                 {"declared": bad, "loggers": sorted(levels),
                  "sources": sorted(how),
                  "retired": list(CAL["retired_log_categories"])})


_journal, g = g, LEG
no_legacy_deps()
no_legacy_declarations()
no_legacy_packaged()
no_legacy_source()
cache_backend_packaged()
cache_api_intact()
cache_declaration_dsl()
cache_bounds_applied()
cache_config_honoured()
sshutil_methods_intact()
zip_expansion_intact()
args4j_current()
no_retired_log_categories()
g = _journal


# ---------------------------------------------------------------------------
# Step 08's rung: the work comes off the pools
# ---------------------------------------------------------------------------
#
# Thirteen rows, 33.5 between them, and the split is the one step 07 used: ten
# rows for the leap and three for what the leap is most likely to take with it.
# What is different here is where the evidence comes from. Every rung below
# this one could be settled by reading the tree -- a coordinate is in the graph
# or it is not, an import says `javax` or it says `jakarta`. This one cannot.
# "The work runs on virtual threads" is a statement about a thread that exists
# for as long as a request is in flight and is gone by the time the suite ends,
# and no scan of the source or the jar can see it.
#
# So five of the ten leap rows are live. `probe.sh` puts four `git-upload-pack`
# ssh sessions and two `git-upload-pack` POSTs into the server and holds them
# there -- each one blocked reading a request body that never arrives -- and
# takes a pair of thread dumps while they are stuck: `Thread.dump_to_file
# -format=json`, which lists virtual and platform threads alike, and
# `Thread.print`, which on JDK 21 lists only the platform ones. A thread in the
# first and not the second is virtual. That subtraction is `gate_lib.threads_in`
# and it is the whole instrument.
#
# What the rows ask of it is *which* threads, and they ask by stack frame
# rather than by name. Names were the obvious reading and they are unusable on
# this tree, measured rather than assumed:
#
#   * JGit's `InterruptTimer` names a platform helper after the command it is
#     timing -- `SSH git-upload-pack '/demo.git' (tester)-Timer` -- so a row
#     counting platform threads whose name mentions a command finds one on a
#     correct tree;
#   * `BaseCommand$TaskThunk.run` renames whatever thread it is running on to
#     the command line for the command's duration and puts the old name back
#     afterwards. On the tree this rung starts from there is therefore no
#     thread named `SSH-Interactive-Worker-1` to find *while a command is
#     running*, even though the command is running on exactly that pool.
#
# A stack frame is not renameable. `on_stack` reads the dump's frame strings as
# an AND of ORs -- for a command, some frame under `com.google.gerrit.sshd.`
# *and* some frame under `org.eclipse.jgit.transport.`; for a request, the
# servlet entry point *and* some frame under `com.google.gerrit.httpd.` --
# which is the difference between "this thread belongs to the ssh subsystem"
# and "this thread is executing a git command right now". Measured on both
# reference trees at the same point in the same load:
#
#     reference    42 threads, 6 virtual: 2 running commands, 2 parked on the
#                  queue's permit, 2 serving requests. Command frames: 2
#                  virtual, 0 platform. Request frames: 2 virtual, 0 platform.
#     predecessor  41 threads, 0 virtual. Command frames: 0 virtual, 2 platform
#                  (`SSH-Interactive-Worker-1`, `-2`). Request frames: 0
#                  virtual, 2 platform (`http-nio-8080-exec-2`, `-3`).
#
# An exact mirror image, which is what makes these rows worth their weight: the
# same four numbers separate the two trees in both directions, so each row has
# a positive form and a negative form. `total_min` is what keeps the negative
# ones honest -- a dead sshd has no platform threads running commands either,
# so those rows also require that some thread of either kind was, before an
# absence is allowed to mean anything.
#
# The five structural rows are the other half, and they exist because the live
# rows alone would let a candidate pass by making the *first* thread virtual and
# leaving every pool in place behind it. They ask what the tree says rather than
# what it did: no type extends a JDK thread pool or calls one of the pooling
# factories, no type implements a `ThreadFactory`, no `new Thread` outside the
# shutdown hook, no `setPriority`, and something in the tree declares virtual
# execution by name. Each is a fact about the predecessor's source that a
# correct answer has to remove.
#
# What none of the ten does is require the port to look like the reference's.
# `virtual-execution-declared` takes Boot's property, four JDK or Spring calls
# or two Spring packages, and says which it found; `no-platform-thread-pools`
# names three pool types and five factories and deliberately leaves
# `newSingleThreadScheduledExecutor` out of both lists, because a virtual-thread
# scheduler still needs one platform thread to do its timing. The rows name what
# the rung is *for* -- the work is not on a pooled platform thread, and nothing
# in the tree still builds one -- and stop there.
#
# The three hold rows, and why each is a rung-08 risk rather than a generic one:
#
#   `queue-api-intact`           the registry is 300 lines of bookkeeping around
#                                a JDK pool -- `getTasks`, `getTask(int)`, the
#                                six-state model, `ProjectTask`. Replacing the
#                                pool is replacing the thing it wraps, and the
#                                cheapest answer deletes the wrapper with it.
#                                Nothing in the 428 reads the registry, so this
#                                row is the only thing that would notice.
#   `concurrency-knobs-read`     `sshd.threads` and `sshd.batchThreads` are read
#                                out of `gerrit.config` to size the two queues.
#                                "Virtual threads are unlimited" is a true
#                                sentence that ends with those keys deleted.
#   `command-concurrency-bounded` the same sentence, measured on the running
#                                server rather than in the source: the probe
#                                offers four commands at once into a fixture
#                                configured for two.


def _class_names(c):
    """Every type name one scanned class record names, in one set.

    Supertype, interfaces, annotations, the constant pool and the loadable
    constants -- the same union `production_refs` takes, pulled out as a
    helper because this rung's pool row asks the question over an explicit
    name list rather than over a package prefix.
    """
    names = set(c.get("r") or []) | set(c.get("l") or []) \
        | set(c.get("a") or []) | set(c.get("i") or [])
    if c.get("s"):
        names.add(c["s"])
    return names


_CALLS_BY_FILE = {}


def _calls_by_file():
    """{relative source path: set of called method names}, production only."""
    if not _CALLS_BY_FILE:
        for n in _prod_calls():
            rel = (n.get("p") or "").replace("\\", "/")
            _CALLS_BY_FILE.setdefault(rel, set()).add(n.get("d"))
    return _CALLS_BY_FILE


def _arity(args):
    """How many parameters a written argument list holds.

    Counted at depth zero over `<` and `>`, because a comma inside a generic
    argument does not separate parameters: `addAllTo(List<Task<?>>)` is one
    and `createQueue(int,String)` is two. This is why `queue-api-intact`
    matches methods on name and arity rather than on the signature string --
    `List<Task<?>>` and `java.util.List<Task<?>>` are the same parameter and a
    string comparison would say otherwise.
    """
    depth, n = 0, 1
    for ch in args:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "," and depth == 0:
            n += 1
    return n


def _method_index():
    """{declaring type: {(method name, arity)}} over production sources."""
    out = {}
    for m in b.ast.kind("method"):
        if in_tests(m.get("p")):
            continue
        name, _, rest = (m.get("sig") or "").partition("(")
        args = rest.rstrip(")").strip()
        out.setdefault(m.get("owner") or "", set()).add(
            (name, 0 if not args else _arity(args)))
    return out


def _source_text(rel):
    """The text of one scanned source file, by its scan-relative path."""
    try:
        with open(os.path.join(b.app, (rel or "").replace("\\", "/")),
                  "r", encoding="utf-8", errors="replace") as fh:
            return fh.read(1 << 20)
    except (IOError, OSError):
        return ""


def _code_only(text):
    """Java source with its comments and string literals removed.

    Crude on purpose -- a character scanner, not a lexer -- but it is exact
    about the three things that matter here: `//` to end of line, `/* */`
    across lines, and `"..."` / `\'...\'` with backslash escapes. Anything it
    cannot classify it keeps, because dropping code would be the expensive
    mistake and keeping a comment is only the cheap one.

    Line numbers survive: a block comment is replaced by the newlines it
    spanned, not deleted, because every caller here arrives with a line number
    from a scan of the *original* text and a javadoc is usually the longest
    run of lines in a Java file.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        d = text[i + 1] if i + 1 < n else ""
        if c == "/" and d == "/":
            i = text.find("\n", i)
            if i < 0:
                break
        elif c == "/" and d == "*":
            j = text.find("*/", i + 2)
            end = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, end))
            i = end
        elif c in "\"'":
            q, i = c, i + 1
            while i < n and text[i] != q:
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _enum_constant_words(t):
    """The identifiers in an enum declaration's constant list, and nothing else.

    Neither scan records an enum's constants -- the AST emits no field nodes,
    and the class file carries the names only as constant-pool UTF8 entries
    that `ArtifactScan` filters out (its `lit` keeps dotted strings, and
    `SLEEPING` has no dot). So this is a source-text question, and the answer
    is only worth having if it is asked of the right piece of text.

    From the type record's own line, the body opens at the first `{` after the
    `enum <Name>` header, and the constant list runs to the first `;` at that
    depth, or to the closing brace if the enum declares nothing else.
    Constants with arguments and constants with bodies both stay inside it,
    because the scan tracks brace depth and a `;` inside a constant's body is
    deeper than the list. Comments and string literals are gone before the
    scan starts, so a javadoc that names a state the tree no longer has
    satisfies nothing.

    The header, and not the record's line, is where the search starts, because
    a type record's `line` is the line the declaration begins on and a
    declaration begins at its first annotation. `@SuppressWarnings({"x"})` on
    the line above `enum State` would otherwise hand the first `{` of the
    annotation's array to the reader, and a correct tree would lose all six
    constants for having annotated the enum. If the header cannot be found --
    a record whose line points past it -- the old first-`{` rule is what is
    left, which is the behaviour this replaces rather than a new failure.
    """
    text = _code_only(_source_text(t.get("p")))
    if not text:
        return set()
    lines = text.splitlines()
    start = max(0, int(t.get("line") or 1) - 1)
    body = "\n".join(lines[start:])
    simple = re.split(r"[.$]", (t.get("n") or ""))[-1]
    if simple:
        m = re.search(r"\benum\s+%s\b" % re.escape(simple), body)
        if m:
            body = body[m.end():]
    i = body.find("{")
    if i < 0:
        return set()
    depth, kept = 0, []
    for ch in body[i:]:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth <= 0:
                break
        elif ch == ";" and depth == 1:
            break
        kept.append(ch)
    return set(re.findall(r"[A-Za-z_$][A-Za-z0-9_$]*", "".join(kept)))


def _flat_settings(base, text):
    """{flat key: value} for one properties or yaml resource.

    The same two narrow readers `_levels_from_properties` and
    `_levels_from_yaml` are built on, without their `logging.level.` filter,
    because this rung asks about one key in the same two file forms. Its
    limits are theirs and are stated there: no anchors, no flow mappings, no
    multi-document files. A candidate whose configuration this cannot read
    still has two other routes to `virtual-execution-declared`, and the row
    reports which route it took.
    """
    out = {}
    if base.endswith(".properties"):
        for ln in text.splitlines():
            ln = ln.strip()
            if not ln or ln[0] in "#!":
                continue
            key, sep, val = ln.partition("=")
            if not sep:
                key, sep, val = ln.partition(":")
            if sep:
                out[key.strip().strip('"').strip("'")] = val.strip()
        return out
    stack = []                                  # (indent, key)
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        line = raw.strip()
        if line.startswith("- "):
            continue
        key, sep, val = line.partition(":")
        if not sep:
            continue
        key = key.strip().strip('"').strip("'")
        val = val.strip().strip('"').strip("'")
        while stack and stack[-1][0] >= indent:
            stack.pop()
        flat = ".".join([k for _, k in stack] + [key])
        if not val:
            stack.append((indent, key))
            continue
        out[flat] = val
    return out


def _declared_settings():
    """{flat key: value} merged over this project's own packaged resources."""
    out = {}
    for name, text in packaged_own_resources().items():
        base = os.path.basename(name)
        if base.endswith((".properties", ".yml", ".yaml")):
            out.update(_flat_settings(base, text))
    return out


def commands_on_virtual_threads():
    """An ssh command was executing, and on a virtual thread.

    The headline row. `minimum=1` and not 2 deliberately: two is what the
    fixture's `sshd.threads = 4`/`batchThreads = 2` allows to run at once and
    what both references do, but a candidate is allowed to size the queues
    differently as long as the knobs are read, and `command-concurrency-bounded`
    is where the count is priced. This row asks the one thing that cannot be
    negotiated -- that the thread with `UploadPack` on its stack is not a
    platform thread.
    """
    return G.threads_by_frames(g, b, "commands-on-virtual-threads",
                               CAL["cmd_frames"], kind="virtual", minimum=1,
                               total_min=1, weight=4.0,
                               doing="running an ssh command")


def no_platform_command_threads():
    """...and no platform thread was executing one beside it.

    The mirror of the row above, and not implied by it: a tree that runs the
    interactive queue on virtual threads and leaves the batch queue on platform
    ones passes `commands-on-virtual-threads` and fails this.

    `total_min=1` is what stops this being free. An absence over a server that
    never ran the command -- because sshd did not start, because the probe's
    keys were rejected, because the fixture has no repositories -- is not
    evidence of anything, and without the floor it would read exactly like the
    correct tree.
    """
    return G.threads_by_frames(g, b, "no-platform-command-threads",
                               CAL["cmd_frames"], kind="platform", maximum=0,
                               total_min=1, weight=3.0,
                               doing="running an ssh command")


def requests_on_virtual_threads():
    """An http request was in flight, and on a virtual thread.

    The second half of the rung: the servlet container has its own pool and it
    is not reached from the queue code at all. On Boot it moves with one
    property; the point of asking it live is that the property can be set in a
    file nothing loads, and that a candidate can equally reach it by handing
    Tomcat's protocol handler a virtual-thread executor. Both answers look the
    same from here, which is the intent.
    """
    return G.threads_by_frames(g, b, "requests-on-virtual-threads",
                               CAL["req_frames"], kind="virtual", minimum=1,
                               total_min=1, weight=3.5,
                               doing="serving an http request")


def no_platform_request_threads():
    """...and `http-nio-*-exec-N` is not still doing it underneath.

    Measured: the predecessor has exactly two of these at dump time and the
    reference has none. Tomcat's acceptor and poller threads stay platform on
    both sides and are not counted here -- neither has a servlet frame on its
    stack, which is the whole reason these rows are written in frames.
    """
    return G.threads_by_frames(g, b, "no-platform-request-threads",
                               CAL["req_frames"], kind="platform", maximum=0,
                               total_min=1, weight=3.0,
                               doing="serving an http request")


def virtual_threads_live():
    """The mix, once, without reference to what any thread was doing.

    Weaker than the four rows above it and kept for what it says when they
    fail: `4 virtual / 38 platform` and `0 virtual / 41 platform` are different
    diagnoses, and a candidate reading the report should not have to infer the
    second from four absence rows. The floor is four because the reference has
    six -- two commands, two tasks parked on the queue's permit, two request
    handlers -- and because two of the six are a consequence of the semaphore
    this particular solution uses, which no instruction requires.
    """
    return G.virtual_threads(g, b, "virtual-threads-live",
                             minimum=CAL["virtual_threads_min"], weight=2.0)


def no_platform_thread_pools():
    """No type in this project's own code is a JDK thread pool any more.

    Three readings of the same question, merged into one row because they are
    one question asked where each can see what the others cannot:

      bytecode   a class whose supertype or constant pool names
                 `ThreadPoolExecutor`, `ScheduledThreadPoolExecutor` or
                 `ForkJoinPool`. This is the one the predecessor fails:
                 `WorkQueue$Executor extends ScheduledThreadPoolExecutor`.
      source     the same names written as types, which catches a field or a
                 local the compiler happened to erase.
      calls      `Executors.newFixedThreadPool` and its four siblings, which
                 leave no trace of the pool class in the caller at all -- the
                 implementation type lives inside `Executors`.

    The vacuity floor is the one `G.no_refs` uses and is here for the same
    reason: an absence over an empty universe is not an absence. A tree whose
    build produced no loose classes has nothing for the bytecode half to read,
    and this row says so rather than passing.
    """
    hits = {}
    pools = set(CAL["pool_types"])
    simples = set(_simple(p) for p in pools)
    classes = production_classes()
    for c in classes:
        for n in sorted(_class_names(c) & pools):
            hits["%s references %s" % (c.get("n", "?"), _simple(n))] = n
    for u in _prod_typeuses():
        if _simple(u.get("n") or "") in simples:
            hits[_site(u, u.get("n"))] = u.get("n")
    for n in _prod_calls():
        if n.get("d") in CAL["pool_factories"]:
            hits[_site(n, n.get("d"))] = n.get("d")
    floor, origin = CAL["own_floor"]
    if len(classes) < floor:
        return g.add("no-platform-thread-pools", False,
                     "VACUOUS: %d own class(es) under %s, need >=%d before an "
                     "absence here means anything"
                     % (len(classes), origin, floor), 3.5,
                     {"universe": len(classes), "floor": floor})
    return g.absent("no-platform-thread-pools", hits, 3.5)


def no_thread_factories():
    """Nothing in this tree builds threads to hand to a pool.

    Asked as `implements` and not as a reference, and the distinction is the
    whole gate. `Thread.ofVirtual().factory()` *returns* a `ThreadFactory` and
    is a perfectly good virtual-thread answer, so a row that forbade the name
    would fail it; what the predecessor does, three times, is write one --
    an anonymous class that names a thread, sets its priority and hands it to
    a pool. `own_implements` reads the resolved hierarchy out of the class
    file, which is the only place an anonymous implementation is recorded at
    all: there is no `implements` clause in the source to find.

    `setThreadFactory` is in the same row because it is the other end of the
    same wire and exists on nothing but a pool.
    """
    hits = {}
    for cls, ifaces in own_implements(CAL["thread_factory_types"]).items():
        hits["%s implements %s"
             % (cls, ", ".join(_simple(i) for i in ifaces))] = cls
    for n in _prod_calls():
        if n.get("d") in CAL["factory_calls"]:
            hits[_site(n, n.get("d"))] = n.get("d")
    return g.absent("no-thread-factories", hits, 2.5)


def no_worker_thread_construction():
    """No `new Thread` outside the one that exists to stop the server.

    The predecessor has two. `MiniGitServer` builds one for
    `Runtime.addShutdownHook`, which is the JVM's own API, takes a `Thread` by
    signature and cannot be given a virtual one to any effect -- a shutdown
    hook that is not joined is a shutdown hook that did not run. That one is
    excused, by the file it is in calling `addShutdownHook` rather than by its
    path, so a candidate who moves the hook keeps the exemption and a candidate
    who starts a worker in `MiniGitServer` does not.

    The other is `BaseCommand.startThread`: one platform thread per admin
    command, for the lifetime of the command. That is the site this rung exists
    to move, and it is also the one the live rows cannot see, because no
    recorded observation runs an admin command.

    Subclasses are asked for beside constructor calls, because `new Thread(r)`
    is not the only way to write one and the scanner only tags the literal
    form: a correct tree may write its
    shutdown hook as `private static class ShutdownHook extends Thread` and
    then `new ShutdownHook()`, which emits no `new-thread` node at all. Left at
    the constructor alone, a candidate could keep the admin command's platform
    thread by naming it -- `class CommandThread extends Thread` -- and no row
    here would see it, because no recorded observation runs an admin command.
    The same file-scoped exemption covers both forms, so a subclassed hook
    and a lambda are each excused by the file that installs them.

    `Thread.ofPlatform()` is refused with them. It is the third spelling, and
    the only one a tree at release 17 could not have written -- so it is also
    the one a candidate arrives at *after* doing this rung's toolchain half and
    reading that the other two are gone. A `Thread.ofPlatform().start(tt)` in
    `BaseCommand` would be the 2009 model in 2021 syntax, and the live rows
    cannot see it, because no recorded observation runs an admin command.

    No vacuity floor of its own: `source-parses` and `sources-survive` are two
    rows above this one in the journal and say how many files there were to
    search.
    """
    calls = _calls_by_file()
    sites = []
    for n in b.ast.nodes("new-thread"):
        rel = (n.get("p") or "").replace("\\", "/")
        if in_tests(rel):
            continue
        if CAL["shutdown_hook_call"] in calls.get(rel, ()):
            continue
        sites.append("%s:%s" % (rel, n.get("line")))
    for t in b.ast.kind("type"):
        rel = (t.get("p") or "").replace("\\", "/")
        if in_tests(rel):
            continue
        if CAL["shutdown_hook_call"] in calls.get(rel, ()):
            continue
        if "Thread" in [_simple(x) for x in (t.get("extends") or [])]:
            sites.append("%s:%s (%s extends Thread)"
                         % (rel, t.get("line"), t.get("n")))
    for n in _prod_calls():
        rel = (n.get("p") or "").replace("\\", "/")
        if CAL["shutdown_hook_call"] in calls.get(rel, ()):
            continue
        if n.get("d") in CAL["platform_builder_calls"]:
            sites.append("%s:%s (%s)" % (rel, n.get("line"), n.get("d")))
    return g.absent("no-worker-thread-construction", sorted(sites), 2.5)


def no_thread_priority():
    """Nothing sets a thread's priority.

    Two sites in the predecessor, both `Thread.MIN_PRIORITY` on a batch queue,
    both no-ops on a virtual thread: `Thread.setPriority` is specified to do
    nothing there, and `getPriority` always answers `NORM_PRIORITY`. A tree
    that carries the call across is a tree that still believes it is scheduling
    its own workers, which is the belief this rung retires. Priced at 1.5
    because it is the one structural row a candidate can pass by deleting two
    lines -- but it is also the row that catches the port that kept the old
    provider classes and changed only what they construct.
    """
    sites = sorted(_site(n, n.get("d")) for n in _prod_calls()
                   if n.get("d") in CAL["priority_calls"])
    return g.absent("no-thread-priority", sites, 1.5)


def virtual_execution_declared():
    """Somewhere, this tree says out loud that execution is virtual.

    The positive half of the structural rows, and the one with the most routes
    to it, so it is written as an either/or over three and reports which one it
    found:

      property   `spring.threads.virtual.enabled` set to a value Spring's own
                 converter reads as true, in any properties or yaml resource
                 the deployable ships. This is Boot's switch and it moves the
                 servlet container and every `TaskExecutor` Boot owns.
      api        a production call to `Thread.ofVirtual`,
                 `Thread.startVirtualThread`,
                 `Executors.newVirtualThreadPerTaskExecutor` or
                 `setVirtualThreads` -- the JDK route and Spring's builder
                 route respectively.
      type       a production reference to Spring's task or scheduling
                 packages, which is where `SimpleAsyncTaskScheduler` and
                 `SimpleAsyncTaskExecutor` live.

    Any one of the three satisfies it. What fails it is the tree that has none
    of them, which is the tree that answered the absence rows by deleting the
    queues and running everything on the caller's thread -- and which the four
    live rows then fail as well, for the more direct reason that the caller's
    thread is an sshd session thread and platform.

    The property is read from the packaged resources rather than from the
    source tree, which is the same choice `logging-levels-declared` makes: a
    property in a file that is not on the classpath of the thing that runs is
    not a setting, and `BOOT-INF/classes/` is where this project's own
    `src/main/resources` lands.
    """
    found, where = [], {}
    val = _declared_settings().get(CAL["virtual_property"])
    if val is not None and val.strip().strip('"').strip("'").lower() in (
            "true", "on", "yes", "1"):
        found.append("property")
        where["property"] = ["%s = %s" % (CAL["virtual_property"], val[:40])]
    api = sorted(_site(n, n.get("d")) for n in _prod_calls()
                 if n.get("d") in CAL["virtual_calls"])
    if api:
        found.append("api")
        where["api"] = api[:4]
    types = sorted(production_refs(CAL["task_api"]))
    if types:
        found.append("type")
        where["type"] = types[:4]
    return g.add("virtual-execution-declared", bool(found),
                 ("virtual execution declared by %s" % ", ".join(found))
                 if found else
                 ("no %s property, no call to %s, and no reference to %s"
                  % (CAL["virtual_property"],
                     "/".join(CAL["virtual_calls"]),
                     " or ".join(CAL["task_api"]))),
                 2.0, {"routes": found, "sites": where})


def queue_api_intact():
    """The task registry the queues exist to feed is still whole.

    `WorkQueue` is not a pool. It is a pool plus a registry: every task gets an
    id, a state and a place in `getTasks()`, `ProjectTask` carries the project
    a background task belongs to, and `CancelableRunnable` is how a queued task
    learns it was dropped before it ran. None of that is observable in the 428
    -- nothing in this fixture lists tasks -- and all of it lives in the class
    this rung rewrites, which is exactly the shape step 07 met in the cache
    package and answered the same way.

    Asked by declaration rather than by behaviour, in the tree's own terms.
    Types are found by simple name and then scoped: nested under the type named
    `WorkQueue`, or a top-level sibling in its package. That scoping is not
    decoration -- `Lifecycle` is also the simple name of two unrelated nested
    classes in this tree, `LocalDiskRepositoryManager.Lifecycle` and
    `CachePool.Lifecycle`, and an unscoped lookup would let either satisfy the
    row. A candidate who moves the file keeps the row; one who deletes the
    registry does not.

    What `CAL["queue_api"]` deliberately leaves out is set down beside it.
    """
    types = {}
    for t in b.ast.kind("type"):
        if in_tests(t.get("p")):
            continue
        types.setdefault(_simple(t.get("n") or ""), []).append(t)
    roots = types.get("WorkQueue") or []
    if not roots:
        return g.add("queue-api-intact", False,
                     "no type named WorkQueue is declared in this tree", 2.0,
                     {"missing": ["WorkQueue"]})
    root = sorted(r.get("n") or "" for r in roots)[0]
    pkg = root.rsplit(".", 1)[0]

    def scoped(simple):
        out = []
        for t in types.get(simple, ()):
            n = t.get("n") or ""
            if n == root or n.startswith(root + ".") \
                    or n.rsplit(".", 1)[0] == pkg:
                out.append(t)
        return out

    sigs = _method_index()
    missing = []
    for simple, kind, supers, methods in CAL["queue_api"]:
        found = scoped(simple)
        if not found:
            missing.append("type %s" % simple)
            continue
        t = found[0]
        if kind and t.get("kindOf") != kind:
            missing.append("%s is a %s, not a %s"
                           % (simple, t.get("kindOf"), kind))
        written = set(_simple(x) for x in
                      (t.get("implements") or []) + (t.get("extends") or []))
        for want in supers:
            if want not in written:
                missing.append("%s no longer names %s" % (simple, want))
        declared = sigs.get(t.get("n") or "", set())
        for name, arity in methods:
            if (name, arity) not in declared:
                missing.append("%s.%s/%d" % (simple, name, arity))

    state = scoped("State")
    words = _enum_constant_words(state[0]) if state else set()
    for want in CAL["queue_states"]:
        if want not in words:
            missing.append("State.%s" % want)

    return g.add("queue-api-intact", not missing,
                 ("the registry under %s is intact: %d type(s), the six-state "
                  "model and every method asked for"
                  % (root, len(CAL["queue_api"])))
                 if not missing else
                 "%d part(s) of the task registry are gone: %s"
                 % (len(missing), ", ".join(missing[:10])),
                 2.0, {"root": root, "missing": missing})


def concurrency_knobs_read():
    """The queue widths still come out of `gerrit.config`.

    `sshd.threads`, `sshd.batchThreads` and `sshd.streamThreads` are what a
    site administrator has to size a server with, and "virtual threads are
    unlimited, so the numbers are meaningless" is a sentence a candidate can
    believe on the way to deleting them. It is also half true, which is why
    this row asks only that the keys are still read and not what is done with
    the answer: the reference uses `threads - batchThreads` as a permit count
    rather than as a pool size, and that is a different thing to do with the
    same number.

    A string literal with no dot in it is in neither scan (see
    `sources_under`), so this reads the source text -- and pairs each key with
    a config-read call in the same file, because a key surviving in a comment
    is not a key being read.
    """
    texts = sources_under("/src/main/java/")
    calls = _calls_by_file()
    reads = set(CAL["config_read_calls"])
    found, missing = {}, []
    for key in CAL["concurrency_keys"]:
        lit = '"%s"' % key
        hits = sorted(rel for rel, text in texts.items()
                      if lit in text and set(calls.get(rel, ())) & reads)
        found[key] = hits[:3]
        if not hits:
            missing.append(key)
    return g.add("concurrency-knobs-read", not missing,
                 ("all %d queue-sizing key(s) are still read from the "
                  "configuration: %s"
                  % (len(CAL["concurrency_keys"]),
                     ", ".join("%s (%s)" % (k, v[0].rsplit("/", 1)[-1])
                               for k, v in sorted(found.items()) if v)))
                 if not missing else
                 "%d queue-sizing key(s) are no longer read beside a config "
                 "call: %s" % (len(missing), ", ".join(missing)),
                 1.5, {"found": found, "missing": missing})


def _thread_print(print_path):
    """{tid: {"name", "state", "stack"}} from the Thread.print half of the
    probe's dump pair.

    Thread.print lists platform threads only: a header line, a
    `java.lang.Thread.State: WAITING (parking)` line, then one `at <frame>`
    line per frame. A thread it does not list is virtual and is judged by its
    JSON record's top frame instead (see `_io_held`).

    A platform thread's state and stack are both read from here. Taking the
    stack from threads.json and the state from this file would mix two
    snapshots -- this one is taken after the first -- so a session thread
    running this project's code in one and parked idle in JDK frames in the
    other would read as an i/o thread parked in project code.
    """
    out, cur = {}, None
    for ln in G._lines(print_path):
        m = G.PLATFORM_LINE.match(ln)
        if m:
            cur = {"name": m.group("name"), "state": None, "stack": []}
            out[m.group("tid")] = cur
            continue
        if cur is None:
            continue
        s = ln.strip()
        if s.startswith("java.lang.Thread.State:"):
            cur["state"] = s.split(":", 1)[1].split()[0]
        elif s.startswith("at "):
            cur["stack"].append(s[3:])
    return out


_PARKED_TOPS = ("java.base/jdk.internal.misc.Unsafe.park",
                "java.base/java.lang.VirtualThread.park",
                "java.base/java.lang.Object.wait",
                "java.base/java.lang.Thread.sleep",
                "java.base/java.lang.VirtualThread.sleep")


_WAITING_STATES = ("WAITING", "TIMED_WAITING", "BLOCKED")


def _io_held(recs, printed):
    """The SSH i/o threads parked inside this project's code (CAL["io_frames"]).

    A platform thread is judged on its Thread.print entry alone -- frames and
    state from one snapshot -- and is held when that state is a waiting one. A
    virtual thread is judged on its JSON record, held when its top frame is a
    park, a wait or a sleep.
    """
    held = [{"name": p["name"], "tid": tid, "stack": p["stack"]}
            for tid, p in sorted(printed.items())
            if p["state"] in _WAITING_STATES
            and G.on_stack(p, CAL["io_frames"])]
    for r in recs:
        if r["tid"] in printed:
            continue
        stack = r.get("stack") or [""]
        if G.on_stack(r, CAL["io_frames"]) and stack[0].startswith(_PARKED_TOPS):
            held.append(r)
    return held


def command_concurrency_bounded():
    """Four commands were offered at once. At most two were running, and the
    two that were not are waiting on their own threads.

    The live half of the row above, and the one row in this section that fails
    the answer everybody reaches for: virtual threads are cheap, so run them
    all. They are cheap; the repository is not. `git-upload-pack` forks a pack
    builder and holds a repository open, and the reason this server queues
    commands instead of accepting them is the machine underneath it, which did
    not change when the threads did.

    The fixture configures `sshd.threads = 4` and `sshd.batchThreads = 2`,
    leaving two for interactive work, and `probe.sh` offers four
    `git-upload-pack` sessions into it. Measured at the dump: two running on
    both trees -- two virtual on the reference, two platform on the
    predecessor -- and the other two waiting, which is what a bound looks like.
    Unbounded looks like four.

    Counted over both kinds of thread together, because the bound is the
    property being asked about and it is the same bound whether the two
    runners are virtual or platform. That is also what makes this a hold row:
    the tree this step is handed passes it, and so does a correct answer.

    Where the excess waits is the same sentence's last bullet and is asked in
    the same row: no SSH i/o thread may be parked inside this project's code
    (CAL["io_frames"]). `SimpleAsyncTaskScheduler.setConcurrencyLimit`, or a
    permit taken in `submit` before the task is handed over, bounds the work
    correctly by count and passes everything else on this rung; it does so by
    stalling the session thread that submitted the command, which is what the
    instruction rules out. The predecessor's excess waits in its executor's
    queue and holds no thread, so it still passes this row.
    """
    recs = G.threads_in(os.path.join(b.out, "threads.json"),
                        os.path.join(b.out, "threads.txt"))
    if not recs:
        return g.add("command-concurrency-bounded", False,
                     "no usable thread dump", 2.5, {})
    hits = [r for r in recs if G.on_stack(r, CAL["cmd_frames"])]
    lo, hi = CAL["inflight_commands"]
    # The second half of the same sentence (section 1.4, third bullet): the
    # commands the bound holds back wait on their own threads, never on the
    # SSH i/o thread that submitted them. See CAL["io_frames"].
    io_held = _io_held(recs, _thread_print(os.path.join(b.out, "threads.txt")))
    ok = lo <= len(hits) <= hi and not io_held
    detail = ("%d command(s) in flight with %d offered, allowed %d-%d"
              % (len(hits), CAL["commands_offered"], lo, hi))
    detail += ("; %d ssh i/o thread(s) waiting inside this project's code"
               % len(io_held))
    return g.add("command-concurrency-bounded", ok, detail,
                 2.5, {"inflight": len(hits),
                       "offered": CAL["commands_offered"],
                       "min": lo, "max": hi,
                       "names": sorted(r["name"] for r in hits)[:8],
                       "io_held": sorted(r["name"] for r in io_held)[:8]})


# Step 08's rung, held. The thirteen definitions above are that step's,
# verbatim -- every docstring, every calibration constant, every live thread
# dump -- and only the thirteen *calls* are wrapped, because a collector that
# re-asked a predecessor's question in a cheaper form would be a different
# question wearing the same name.
#
# Wrapping the calls rather than the definitions is forced here and was a
# choice at the five rungs below. Step 08's gates are functions and its
# collapsed predecessors' were module-level statements, so there is nothing
# between `def` and `return` for a rebinding to intercept: the `g` each
# function body closes over is the one bound when the body *runs*. Rebinding
# around the call site is therefore the same mechanism, applied at the only
# point where it works.
#
# Five of the thirteen take live thread dumps out of the running server, so
# this hold costs what step 08's rung cost -- `probe.sh` still offers its six
# units of work and `Thread.dump_to_file` still runs twice. That is the price
# of asking the question rather than assuming the answer, and it is the
# question this rung most needs asked: nothing below rewrites concurrency, but
# `BaseCommand.startThread` is one of the twelve files this rung's own leap
# rows push a candidate into, and the tree that turns its `CommandRunnable`
# callback into a lambda while dropping `Thread.ofVirtual` is exactly the tree
# a scan-only hold would wave through.
_journal, g = g, VIRT

commands_on_virtual_threads()
no_platform_command_threads()
requests_on_virtual_threads()
no_platform_request_threads()
virtual_threads_live()
no_platform_thread_pools()
no_thread_factories()
no_worker_thread_construction()
no_thread_priority()
virtual_execution_declared()
queue_api_intact()
concurrency_knobs_read()
command_concurrency_bounded()

g = _journal


# ---------------------------------------------------------------------------
# Step 09's rung -- the idiom step
# ---------------------------------------------------------------------------
#
# Step 08 moved the toolchain to 21 because it needed the release that
# finalised virtual threads. This rung is what the rest of that release buys,
# and it is the only rung in this ladder whose whole subject is code the
# compiler already accepts. Nothing below changes a dependency, a descriptor,
# a bean or a thread; every row here is about a construct the JDK has
# superseded and the tree still writes.
#
# The four things it asks for, each measured on the tree this step is handed
# (`floor` below) and on this step's reference solution (`ref`):
#
#   the date and time API      floor: 18 written uses of `java.util.Date`,
#                              `Calendar`, `SimpleDateFormat` and `TimeZone`
#                              across 4 files, and 7 own classes referencing
#                              one of them in bytecode. ref: 0 and 0, with 13
#                              `java.time` uses in the same 4 files.
#
#   pattern matching for       floor: 25 `instanceof` tests followed by a cast
#   instanceof                 to the tested type, 0 binding patterns.
#                              ref: 7 and 18.
#
#   functional interfaces      floor: 11 anonymous classes, 20 lambdas and
#   written as lambdas         method references. ref: 6 and 23.
#
#   generic signatures the     floor: 8 raw uses of args4j's `Setter`,
#   project owns               `OptionHandler` and `OptionHandlerFactory`, at
#                              4 files. ref: 2, both of them in the one
#                              override whose signature args4j fixes.
#
# and one row that is not an idiom at all but is what makes the other nine
# stick: the build is configured to reject its own warnings. `-Xlint:all` with
# `-Werror` in the compiler plugin's `<compilerArgs>`, which is the difference
# between a tree that happens to be clean today and a tree that cannot stop
# being clean.
#
# That row is also the reason this rung's own work is knowable. Measured: the
# tree this step is handed compiles under `-Xlint:all` with exactly 21
# warnings, and every one of them is one of the four subjects above --
#
#     8  found raw type: org.kohsuke.args4j.spi.Setter / OptionHandler
#     4  implicit cast from int to short in compound assignment is possibly
#        lossy                                              (IdGenerator)
#     2  PersonIdent(String,String,Date,TimeZone) has been deprecated
#     2  isAdministrator() has been deprecated
#     2  serializable class has no definition of serialVersionUID /
#        non-transient instance field of a serializable class  (ProjectServlet)
#     1  java.security.AccessController has been deprecated and marked for
#        removal                                             (HostPlatform)
#     1  URL(String) has been deprecated                     (NoShell)
#     1  CmdLineException(String,Throwable) has been deprecated
#
# -- so "compiles clean under -Xlint:all -Werror" is not a separate errand
# bolted onto this rung, it *is* this rung, stated in the compiler's own
# words. The reference reaches 0 warnings and keeps six suppression arguments
# at five sites, down from ten at ten.
#
# One warning in that list is invisible on the tree as it stands and is worth
# naming, because it is the reason `suppressions` moves out of the floor block
# and becomes a leap row here. `SshInfoServlet` carries
# `@SuppressWarnings("serial")` and so contributes nothing to the 21;
# `ProjectServlet`, which has the identical defect, carries no annotation and
# contributes 2. "Suppressed findings are findings" is the whole
# distance between those two files, and a rung that asked for a warning-free
# build without also tightening the suppression cap would be asking a
# candidate to close the gap by annotating the second file.
#
# What this rung deliberately does not ask for. Records, sealed hierarchies,
# text blocks and switch expressions are the headline features of the releases
# between 17 and 21 and none of them appears below, because the tree has no
# site for them: measured with this file's own AstScan, 0 text blocks, 0
# record declarations, 0 switch expressions of either arrow or colon form, and
# no type hierarchy with a closed set of implementations that is not already
# an enum. A row asking for a record would be asking a candidate to invent a
# data carrier so that a gate could find one, and this ladder does not write
# rows that reward inventing work.
# ---------------------------------------------------------------------------


# `[WARNING] /app/<module>/src/main/java/.../Foo.java:[29,30] <text>` -- the
# shape maven-compiler-plugin gives a javac diagnostic it anchors to a source
# file. Anchored to `/app` because that is where the candidate's tree is
# mounted and a dependency's warning could never carry that prefix.
JAVAC_WARNING_RE = re.compile(r"^\[WARNING\]\s+/app/\S+\.java:\[\d+,\d+\]")


def _simple(name):
    """The last segment of a type name as AstScan recorded it.

    AstScan writes a type use under the name in the source text, so an
    imported `Date` is `Date` and an inline `java.util.Date` is the whole
    thing. Every gate below that reads type uses matches on the tail, for the
    same reason `annotated` does.
    """
    return (name or "").rsplit(".", 1)[-1]


def no_legacy_date_api():
    """`java.util.Date` and its family are gone from this project's bytecode.

    Asked of the artifact and not of the source, which is the opposite of the
    choice `no-owned-stringbuffer` makes two rows down, and the difference
    between the two is worth stating because it is the same question asked of
    two APIs with different boundaries.

    Nothing outside this project forces a `Date` on it. The four callers all
    reach the legacy API on their own account -- `SshLog` formats the audit
    timestamp, `IdentifiedUser` builds a jgit `PersonIdent`, and the two admin
    commands render times into their output -- and in every case the modern
    counterpart exists on the same library: jgit has taken
    `PersonIdent(String, String, Instant, ZoneId)` since 6.x, which is the
    version step 07 moved this tree to. So a complete answer leaves no trace
    in the constant pool, the constant pool is the strictest instrument
    available, and it is the one used.

    It is strictly stronger than the source scan here. A `Date` that arrives
    through an erased signature or a descriptor is invisible to AstScan and is
    in `refs`, and `own=True` keeps the question to classes this project
    compiled, so jgit's own use of `Date` inside its jar is correctly not this
    project's problem.

    Measured: floor 7 own classes naming one of these -- `Date` in four,
    `SimpleDateFormat` in three, `TimeZone` in two, `Calendar` in one --
    against 0 in the reference. `own_floor` guards the vacuous reading, which
    for an absence row over own bytecode is the tree that compiled nothing.
    """
    return G.no_refs(g, b, "no-legacy-date-api", CAL["legacy_date_types"],
                     weight=5.0, own=True, floor=CAL["own_floor"])


def java_time_in_use():
    """...and `java.time` took over the work, rather than the work leaving.

    The anti-deletion half, and at this rung it is load-bearing in a way the
    equivalent row at other rungs is not: none of the 428 recorded
    observations reads a rendered date. `show-queue` is not a registered
    command in this fixture at all, `show-connections` appears eight times and
    every one of them is its help text, and no HTTP response in the suite
    carries a timestamp. A candidate who answers `no-legacy-date-api` by
    deleting the three renderings scores 428 of 428.

    Two things stop that tree, and only one of them is this row. The other is
    inherited: `audit-log-intact`, inside `inherited-logging-hold`, matches
    every line of `sshd_log` against a grammar that begins
    `[yyyy-MM-dd HH:mm:ss,SSS +0000]`, so the audit timestamp is pinned to its
    wire format by a row that was already here. What this row adds is the two
    sites that grammar does not cover -- the ref-log identity and the admin
    commands' output -- and it adds them as a count rather than as a list of
    class names, for the reason `uses_refs_beside` gives at length: a list of
    the classes that did the work in the tree this was calibrated against is a
    fact about that tree, and it refuses the candidate who moves the same work
    into a class of their own.

    The floor is 6 and the measurement is 7, and the tree this step is handed
    already sits at 4. Those four are steps 07 and 08's doing --
    `CachePool$Factory` and `WorkQueue$Executor` hold `Duration`s, `SshDaemon`
    an `Instant` -- so this row asks for two more own classes naming
    `java.time` than arrive with one, against a reference that adds three. The
    slack is one unit and it buys the candidate who folds the two admin
    commands' formatting into a single helper; it costs the row nothing,
    because the tree that deletes all three renderings lands at 4 and the tree
    that keeps one lands at 5.
    """
    return G.uses_refs(g, b, "java-time-in-use", ("java.time",),
                       minimum=CAL["java_time_min"], weight=2.5, own=True)


def no_owned_stringbuffer():
    """No `.java` in this project declares a `StringBuffer`.

    A source row, where the one above it is a bytecode row, and the reason is
    the boundary rather than a preference. `HttpServletRequest.getRequestURL()`
    returns a `StringBuffer` -- that is jakarta's signature, not this
    project's choice -- so a class that calls it has `java.lang.StringBuffer`
    in the descriptor of the call site whatever it does with the result.
    Measured: the reference still references `StringBuffer` from
    `RequireSslFilter` and `HttpCanonicalWebUrlProvider` in bytecode, and
    names it in no source file at all. A bytecode row here would fail a
    correct tree, and the
    question to ask is the one the source can answer: does this project's own
    code *declare* a synchronised builder it never needed?

    Both call sites answer it the same way, by handing the servlet API's
    return value straight to `new StringBuilder(...)` and never naming the
    old type. The third site, `SshLog.MyLayout.doLayout`, builds its line from
    nothing and simply starts from a `StringBuilder`.

    Floor: 7 written uses across those three files. Reference: 0.
    """
    sites = {}
    for u in b.ast.typeuses():
        if _simple(u.get("n")) == "StringBuffer":
            sites["%s:%s" % (u.get("p", "?"), u.get("line", 0))] = u
    for f in b.ast.files():
        for im in f.get("imports") or []:
            if im.split(" ")[0].rstrip(".*").rstrip(".").endswith("StringBuffer"):
                sites["%s (import)" % f.get("p", "?")] = im
    return g.absent("no-owned-stringbuffer", sites, 2.0)


def callbacks_are_lambdas():
    """Anonymous implementations of functional interfaces became lambdas.

    `converted` rather than a bare count, for the reason its docstring gives:
    a present-only row is satisfied by four lambdas in a throwaway class while
    every anonymous class in the tree stays where it is. The falling half is
    what cannot be faked.

    The five sites are all callbacks and all of them read better without the
    ceremony: `CommandFactoryProvider`'s `CommandFactory`, the three
    `BaseCommand.startThread` bodies in `AdminShowConnections`, `ShowQueue`
    and `AdminKill`, and `HostPlatform`'s `PrivilegedAction`, which this rung
    retires outright rather than converting -- see `no-deprecated-for-removal`
    below.

    Measured: floor 11 anonymous classes against 20 lambdas and method
    references on one predecessor and 14 against 21 on the other; reference 6
    against 23. The thresholds are `<=9` and `>=22` and both predecessors fail
    both halves, which is what makes this a leap row rather than a hold. The
    CAL note above argues 9 rather than 7 from the residue the two chains
    actually hand down, which is not the same number.

    Why 6 and not 0 on the reference: an anonymous class that implements more
    than one method, or that keeps state between calls, is not a lambda and
    rewriting it as one is not possible. `WorkQueue`'s `ThreadFactory` is gone
    at step 08; what remains at this rung is six bodies that genuinely declare
    more than they could express as an expression, and a row demanding zero
    would be asking for a tree that cannot exist. On the other chain the same
    sentence lands on eight.

    One trap this row does not fall into and one it deliberately keeps.
    `startThread` is step 08's method and this rung rewrites its three callers;
    a candidate who instead deletes the callers passes this row on the falling
    half and fails `inherited-virtual-hold`, because two of the three commands
    it would have deleted are what `command-concurrency-bounded` counts. The
    one it keeps: method references count alongside lambdas, because
    `this::display` and `() -> display()` retire the same construct and
    picking between them is taste.

    The third condition is the CAL note above: both counts are whole-tree
    counts, and a tree that lifts four anonymous bodies into named classes and
    writes two lambdas where no callback was moves both counts exactly as far
    as a conversion does. So the two single-abstract-method command callbacks
    are also counted as classes wherever they are declared, and at most one
    may still be one. The instruction says this in the same words: converting
    means the callback stops being a class.
    """
    sam = tuple(CAL["command_callback_types"])
    left = {}
    for n in b.ast.nodes("anonymous"):
        if _simple(n.get("d")) in sam:
            left["%s:%s" % (n.get("p", "?"), n.get("line", 0))] = n.get("d")
    for ty in b.ast.kind("type"):
        if ty.get("kindOf") != "class":
            continue
        for sup in (ty.get("implements") or []) + (ty.get("extends") or []):
            if _simple(sup) in sam:
                left[ty.get("n", "?")] = sup
                break
    cap = CAL["command_callback_classes_max"]
    base = _Collector()
    G.converted(base, b, "callbacks-are-lambdas",
                ("lambda", "method-ref"), "anonymous",
                CAL["functional_min"], CAL["anonymous_max"], weight=3.5)
    counts = base.results[0]
    ok_classes = len(left) <= cap
    detail = counts.detail
    if ok_classes:
        detail += "; callback classes %d (allow <=%d)" % (len(left), cap)
    else:
        detail += ("; still a class: "
                   + ", ".join("%s (%s)" % kv
                               for kv in sorted(left.items())[:8]))
    ev = dict(counts.evidence)
    ev["callback_classes"] = sorted(left)
    ev["callback_classes_limit"] = cap
    return g.add("callbacks-are-lambdas", counts.ok and ok_classes,
                 detail, 3.5, ev)


def pattern_instanceof():
    """`instanceof` that tests and then casts became a binding pattern.

    The largest single population this rung moves: 25 sites in 12 files, each
    of them the three-line shape Java 16 made one line --

        if (x instanceof Foo) {          if (x instanceof Foo foo) {
          Foo foo = (Foo) x;               ...
          ...

    -- and every one of them in code this project owns, because the construct
    predates generics in this tree and nothing since has had a reason to open
    those files.

    `converted` again, and here the falling half carries most of the weight:
    the pattern form cannot be written without also removing the cast that
    made the old form necessary, so `instanceof-plain <= 9` is very nearly the
    whole claim and `instanceof-pattern >= 16` is what stops a tree passing it
    by deleting the tests.

    Measured: floor 25 plain / 0 pattern, reference 7 plain / 18 pattern, and
    the row asks 9 and 16 -- two units of slack under the reference's 18, one
    unit over its 7, which is deliberately looser on the rising half because a
    candidate who reaches the same place with fewer, larger patterns has done
    the work.

    Why the reference keeps 7. An `instanceof` whose result is a boolean and
    not a gateway to a cast has nothing to bind: `return o instanceof
    SshSession;` is already the shortest thing it can be. Those are what is
    left, and they are why this row is `converted` with a cap rather than
    `no_nodes` with zero.

    And why this rung does not have a `casts-at-most` row, which is the
    obvious companion. The cast count falls from
    52 to 33 across this port, which looks like a clean signal until the
    `IdGenerator` fix lands: closing javac's four "implicit cast from int to
    short in compound assignment is possibly lossy" warnings *adds* four
    explicit `(short)` casts to a file that had none. A row that counted casts
    would price that fix as a regression. The construct this rung is actually
    retiring is the cast that an `instanceof` immediately above it made
    redundant, and that is the construct this row counts.
    """
    return G.converted(g, b, "pattern-instanceof", "instanceof-pattern",
                       "instanceof-plain", CAL["instanceof_pattern_min"],
                       CAL["instanceof_plain_max"], weight=4.5)


def no_raw_cli_types():
    """The args4j boundary is generic in this project's own declarations.

    args4j's `OptionHandler<T>` and `Setter<T>` have been generic since 2.0
    and this tree writes them raw in four files, one of which is its own:

        public interface OptionHandlerFactory<T> {
          OptionHandler create(CmdLineParser p, OptionDef o, Setter setter);
        }

    -- a factory that is itself parameterised and then throws the parameter
    away in the one signature it declares. Fixing it is the row: `create`
    becomes `OptionHandler<T> create(..., Setter<T> setter)`, and the two
    handler classes that implement it, `SocketAddressHandler` and
    `SubcommandHandler`, take a `Setter<SocketAddress>` and a `Setter<String>`
    in place of a raw one. That is a small API redesign rather than a
    search-and-replace: `OptionHandler`'s own constructor wants a
    `Setter<? super T>`, so the parameter has to be threaded rather than
    merely written.

    What is left after it, and cannot be removed. args4j declares the hook
    `protected OptionHandler createOptionHandler(OptionDef, Setter)` with both
    names raw, and an override that names a type argument stops being an
    override. That is the library's rawness and this project cannot refuse it;
    the reference carries the tree's one remaining
    `@SuppressWarnings({"rawtypes", "unchecked"})` there, with a comment
    saying so, and `suppressions` below prices it.

    The instrument is AstScan's `targs` flag on every written type name, and
    the allowance is a list of file names rather than a count, because the
    thing being allowed is a specific override and not a budget. Two entries:

      CmdLineParser.java      the override above.
      OptionHandlerUtil.java  `ResolvableType.forClassWithGenerics(
                              OptionHandlerFactory.class, valueType)`. A class
                              literal of a generic type is raw by language
                              rule -- `OptionHandlerFactory<T>.class` does not
                              exist -- and AstScan cannot tell a class literal
                              from a raw type. javac agrees it is not a
                              finding: it is the one site in this group that
                              `-Xlint:rawtypes` does not warn about.

    Matched on the file's base name and not its path, so that a candidate who
    moves either class to another package keeps the allowance.

    `raw_cli_seen_min` is the vacuity guard. An absence row over a named type
    set reads identically on a tree that fixed every site and on a tree that
    deleted the `util-cli` module, so the row also asserts that the type names
    were seen at all -- 17 uses in both control trees, floored at 12.

    Measured: floor 8 disallowed raw uses at 4 files, reference 0.
    """
    seen, sites = 0, {}
    allow = set(CAL["raw_cli_allow"])
    for u in b.ast.typeuses():
        if _simple(u.get("n")) not in CAL["raw_cli_types"]:
            continue
        seen += 1
        p = (u.get("p") or "?").replace("\\", "/")
        if u.get("targs") or p.rsplit("/", 1)[-1] in allow:
            continue
        sites["%s:%s %s" % (p, u.get("line", 0), _simple(u.get("n")))] = u
    if seen < CAL["raw_cli_seen_min"]:
        return g.add("no-raw-cli-types", False,
                     "VACUOUS: %d use(s) of %s in the tree, need >=%d before "
                     "an absence here means anything"
                     % (seen, "/".join(CAL["raw_cli_types"]),
                        CAL["raw_cli_seen_min"]),
                     3.5, {"count": 0, "seen": seen,
                           "floor": CAL["raw_cli_seen_min"], "sites": []})
    return g.absent("no-raw-cli-types", sites, 3.5)


def no_deprecated_for_removal():
    """The security-manager API JEP 411 deprecated for removal is gone.

    One site: `HostPlatform.computeWin32` reads `os.name` through
    `AccessController.doPrivileged(new PrivilegedAction<String>() {...})`, a
    2011 idiom for a permission check that no longer exists. On JDK 21 both
    names are `@Deprecated(forRemoval = true)` and javac says so without being
    asked -- it is the single warning the tree this step is handed emits under
    the default lint set, and the only one of the 21 that a candidate cannot
    miss.

    Deprecated-for-removal is asked as its own row, separately from the
    build-warning rows below, because the two say different things. A warning
    is a finding about today's compiler; `forRemoval` is a statement that the
    API will stop existing, and a tree carrying one will not build on some
    future JDK whatever its lint settings say. It is also the one item on the
    list with a wrong fix available -- `@SuppressWarnings("removal")` silences
    it and changes nothing -- which is why the row reads the bytecode rather
    than the warning stream, and why the suppression cap below is tight enough
    that spending an argument here costs something.

    The whole JEP 411 set is named rather than the two types this tree
    happens to use, because the row is about a class of API and a candidate
    who replaces `doPrivileged` with a `SecurityManager` check has not
    answered it. `own=True` and the same `own_floor` guard as every other
    absence row over own bytecode.

    Floor: `AccessController` and `PrivilegedAction`, both in `HostPlatform`.
    Reference: none; `computeWin32` reads the property directly, which is what
    the `doPrivileged` wrapper has done since the security manager stopped
    being installed.
    """
    return G.no_refs(g, b, "no-deprecated-for-removal", CAL["jep411"],
                     weight=2.0, own=True, floor=CAL["own_floor"])


def lint_is_enforced():
    """The build tells javac to report everything, and to fail on it.

    The heaviest row in this rung and the only one that is about the build
    rather than the code, because it is the row that makes the rest
    permanent. Every other gate here is a census of a tree at one moment;
    this one asks whether the tree can go back.

    What it requires, read out of the poms rather than grepped for:

      * `-Xlint:all` (or bare `-Xlint`, which means the same thing) among the
        arguments handed to maven-compiler-plugin, and
      * `-Werror`, or the plugin's own `<failOnWarning>true</failOnWarning>`,
        which adds `-Werror` to the command line itself.

    Both read from `<compilerArgs>` under any `maven-compiler-plugin`
    `<configuration>` in any pom in the reactor, `<build>` and
    `<pluginManagement>` alike, with `${...}` resolved against the merged
    property map the same way `compiler-release` resolves the release.
    `<compilerArguments>` is deliberately not read: it is a Map element whose
    *tag names* are the flags, and a colon is not legal in an XML tag, so it
    cannot express `-Xlint:all` at all.

    Four narrowings, each of which a tree could otherwise pass through:

      * **`<profile>` does not count.** A `maven-compiler-plugin` block inside
        a profile is configuration that applies when that profile is active,
        and nothing here can know whether it is. A tree that puts `-Werror`
        in a profile it never activates has configured nothing; the row walks
        a parent map and ignores any plugin under a `<profile>` ancestor.
      * **`maven.compiler.failOnWarning` counts.** It is the plugin's own
        property for the `<failOnWarning>` parameter, so a tree that sets it
        in `<properties>` really does fail on warnings, and refusing it would
        fail a correct tree for choosing the other of two documented spellings.
      * **`-nowarn` and `-Xlint:none` disqualify outright**, wherever they
        appear in the argument list, because javac takes the last word and a
        tree carrying both has switched the reporting off.
      * **`<compilerArgument>` counts only beside `<fork>true</fork>`.** It is
        the plugin's fork-only spelling, and this reactor does not fork:
        measured, `-Dmaven.compiler.compilerArgument=-Xlint:all` over this
        tree produces the one warning javac emits with no `-Xlint` at all,
        not the twenty-one `-Xlint:all` finds. Crediting an argument the
        compiler never receives would pass a tree whose build does not
        actually fail on a warning -- and would contradict the instruction,
        which tells the candidate the same thing. With `<fork>true</fork>` in
        the same plugin the argument really is passed, so that pairing counts.

    Where the two halves are read: the parent pom, and only there. The
    instruction makes this one setting in the parent pom, applying to every
    module by inheritance, so the flags count from the reactor's root pom --
    its `<build>` or its `<pluginManagement>`, outside any profile -- and
    `maven.compiler.failOnWarning` from that pom's own `<properties>`.
    Searching every pom instead would leave a tree that set the flags in one
    warning-free leaf to the row after it, which reads the warnings the
    verifier's own build printed -- a transcript the tree's
    `.mvn/maven.config` could otherwise quiet. Maven does not read `.mvn/`
    here (helpers.sh, maven_inputs_reset), and a leaf that carries the flags
    is named in the detail so a candidate can see where they belong. The pair
    is the claim; neither half is.

    And the parent is not the whole of it:
    every module must compile under the pair as the scored build actually
    configured it. A module's own `<compilerArgs>` replaces the inherited list
    unless it appends, so one innocuous argument in a leaf pom takes that
    module out of `-Xlint:all -Werror` while the parent still carries both.
    The verifier's `package` run records each maven-compiler-plugin
    execution's effective configuration (compiler.jsonl), and each one that
    compiles is held to the same rules as the parent; see the comment at the
    check.

    The silencing check reads more widely than the flags do: every pom,
    profiles included, and `<showWarnings>false</showWarnings>` or
    `maven.compiler.showWarnings=false`, which maven-compiler-plugin turns
    into `-nowarn` on javac's command line.

    A note on an instrument that does not work here, because it is the
    obvious one to reach for:
    `-Dmaven.compiler.compilerArgument=-Xlint:all` on the command line is
    silently ignored by maven-compiler-plugin 3.13.0, which is the version
    this reactor resolves. `<compilerArgs>` in the pom is the only route, and
    that is also why the verifier reads the pom rather than forcing the flag
    itself: it has no way to force it.

    Floor: no `<compilerArgs>` anywhere, both halves absent. Reference: both,
    in the parent's `<build>`, inherited by all seven modules.
    """
    docs = G._pom_docs(b)
    props = G._pom_properties(docs)
    # `args` and `fail_on_warning` are the parent pom's; `seen` is every
    # argument in every pom, profiles included, for the silencing check; and
    # `elsewhere` names the other poms that carry compiler flags.
    args, fail_on_warning, where = [], [], []
    seen, quiet, elsewhere = [], [], []
    for rel, root, _text in docs:
        if root is None:
            continue
        top = rel == "pom.xml"
        parent = {}
        for el in root.iter():
            for kid in el:
                parent[kid] = el
        def under_profile(el):
            while el is not None:
                if G._local(el.tag) == "profile":
                    return True
                el = parent.get(el)
            return False
        for el in root.iter():
            if G._local(el.tag) != "plugin":
                continue
            aid = "".join(kid.text or "" for kid in el
                          if G._local(kid.tag) == "artifactId").strip()
            if aid != "maven-compiler-plugin":
                continue
            profiled = under_profile(el)
            counts = top and not profiled
            # `<compilerArgument>` is the plugin's fork-only spelling and this
            # reactor does not fork, so it only counts beside `<fork>true</>`.
            forked = any(
                G._local(s.tag) == "fork"
                and G._resolve(s.text or "", props).strip().lower() == "true"
                for s in el.iter())
            for sub in el.iter():
                tag = G._local(sub.tag)
                txt = G._resolve(sub.text or "", props)
                if tag == "compilerArgument" and not forked:
                    continue
                if tag in ("arg", "compilerArgument") and txt:
                    seen.extend(txt.split())
                    if counts:
                        args.extend(txt.split())
                        where.append(rel)
                    elif not profiled:
                        elsewhere.append(rel)
                elif tag == "failOnWarning" and txt.strip().lower() == "true":
                    if counts:
                        fail_on_warning.append(rel)
                        where.append(rel)
                    elif not profiled:
                        elsewhere.append(rel)
                elif tag == "showWarnings" and txt.strip().lower() == "false":
                    quiet.append("<showWarnings>false</showWarnings> in %s"
                                 % rel)
        # The plugin's own property spelling of `<failOnWarning>`, set in the
        # parent pom's own `<properties>` -- not a profile's, not a child's.
        if top:
            for el in root:
                if G._local(el.tag) != "properties":
                    continue
                for kid in el:
                    if (G._local(kid.tag) == "maven.compiler.failOnWarning"
                            and G._resolve(kid.text or "", props)
                            .strip().lower() == "true"):
                        fail_on_warning.append("pom.xml <properties>")
                        where.append(rel)
    # The property spelling of `<showWarnings>`, wherever it is set.
    if any((v or "").strip().lower() == "false"
           for v in (props.get("maven.compiler.showWarnings") or ())):
        quiet.append("maven.compiler.showWarnings=false")
    # `-Xlint` alone and `-Xlint:all` both enable every category. A list may
    # also switch categories back off -- `-Xlint:all,-deprecation` is legal and
    # is how a tree passes this row while disabling exactly the categories its
    # 21 warnings fall into -- so any negated key disqualifies the argument.
    lint = []
    for a in args:
        if a == "-Xlint":
            lint.append(a)
        elif a.startswith("-Xlint:"):
            keys = a.split(":", 1)[1].split(",")
            if "all" in keys and not any(k.startswith("-") for k in keys):
                lint.append(a)
    werror = [a for a in args if a == "-Werror"]
    # A category switched off in an argument of its own is switched off just
    # the same: `<arg>-Xlint:all</arg><arg>-Xlint:-serial</arg>` reaches javac
    # as both, and the second wins for `serial`. So any `-Xlint:` argument that
    # negates a key, or names `none`, silences the row wherever it sits.
    silenced = [a for a in seen
                if a in ("-nowarn", "-Xlint:none")
                or (a.startswith("-Xlint:")
                    and any(k.startswith("-") or k == "none"
                            for k in a.split(":", 1)[1].split(",")))] + quiet
    parent_ok = bool(lint) and bool(werror or fail_on_warning) and not silenced

    # Every module, as the scored build compiled it.
    # The parent pom carrying the pair is the instruction's spelling of the
    # setting; that every module compiles under it is the claim, and the poms
    # read above cannot show it. A module's own `<compilerArgs>` replaces the
    # inherited list rather than adding to it -- Maven merges list children
    # element by element, so a leaf that says only `<arg>-parameters</arg>`
    # compiles with exactly that, and its warnings neither print nor fail the
    # build -- unless it asks to append (`combine.children="append"`). So the
    # verifier's own `package` run logs, before any mojo runs, each
    # maven-compiler-plugin execution's effective configuration, inheritance
    # already merged (helpers.sh guard_report, build.sh; compiler.jsonl), and
    # every execution that compiles must carry the pair itself, under the same
    # rules as the parent: `-Xlint:all` with no category negated, `-Werror` or
    # failOnWarning, `<compilerArgument>` only when forked, and nothing that
    # switches reporting back off (`-nowarn`, `-Xlint:none`, a negated key,
    # showWarnings false, another compilerId, an `@argfile` javac would read
    # options from). Every module whose packaging is not `pom` must have a
    # main compile that runs, unless its own pom skips main compilation
    # (`skipMain` / `maven.main.skip`, a test-only module). No record is a
    # failure, never a pass.
    import json
    records, recorded = [], True
    try:
        with open(os.path.join(S.OUT, "compiler.jsonl"), "r",
                  encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                if ln.strip():
                    records.append(json.loads(ln))
    except (IOError, OSError, ValueError):
        recorded = False
    keys = [(r.get("project"), r.get("execution")) for r in records]
    if not records or len(keys) != len(set(keys)):
        recorded = False
    module_short, compiles, checked, main_skipped = [], {}, 0, set()

    def _eff(rec, name, prop):
        v = rec.get(name)
        if v is None:
            v = (rec.get("properties") or {}).get(prop)
        return (v or "").strip().lower()

    for rec in records if recorded else ():
        proj = str(rec.get("project"))
        goals = rec.get("goals") or []
        main, test = "compile" in goals, "testCompile" in goals
        if rec.get("packaging") != "pom":
            compiles.setdefault(proj, False)
        if not (main or test):
            continue
        if (rec.get("phase") or "").strip() == "none":
            continue
        if main and not test and _eff(rec, "skipMain", "maven.main.skip") == "true":
            # The pom's own switch for a module with no main code (a test-only
            # module); its test compile is still held to the pair. A module
            # with main code that skipped compiling it ships
            # no classes, which the artifact and source rows see.
            main_skipped.add(proj)
            continue
        if test and not main and _eff(rec, "skip", "maven.test.skip") == "true":
            continue
        if main:
            compiles[proj] = True
        checked += 1
        eargs = []
        for a in rec.get("compilerArgs") or []:
            eargs.extend(str(a).split())
        if (_eff(rec, "fork", "maven.compiler.fork") == "true"
                and rec.get("compilerArgument")):
            eargs.extend(str(rec.get("compilerArgument")).split())
        e_lint = [a for a in eargs if a == "-Xlint" or (
            a.startswith("-Xlint:")
            and "all" in a.split(":", 1)[1].split(",")
            and not any(k.startswith("-") for k in a.split(":", 1)[1].split(",")))]
        e_werror = ("-Werror" in eargs
                    or _eff(rec, "failOnWarning",
                            "maven.compiler.failOnWarning") == "true")
        e_quiet = [a for a in eargs
                   if a in ("-nowarn", "-Xlint:none") or a.startswith("@")
                   or (a.startswith("-Xlint:")
                       and any(k.startswith("-") or k == "none"
                               for k in a.split(":", 1)[1].split(",")))]
        if _eff(rec, "showWarnings", "maven.compiler.showWarnings") == "false":
            e_quiet.append("showWarnings=false")
        cid = _eff(rec, "compilerId", "maven.compiler.compilerId")
        if cid not in ("", "javac"):
            e_quiet.append("compilerId=" + cid)
        if not e_lint or not e_werror or e_quiet:
            module_short.append(
                "%s %s: -Xlint:all %s, -Werror %s%s (compilerArgs %s)"
                % (proj.split(":")[-1], rec.get("execution"),
                   "yes" if e_lint else "NO", "yes" if e_werror else "NO",
                   (", off by %s" % " ".join(e_quiet)) if e_quiet else "",
                   " ".join(eargs) or "none"))
    module_short.extend("%s: no main compile runs" % p.split(":")[-1]
                        for p, c in sorted(compiles.items())
                        if not c and p not in main_skipped)
    modules_ok = recorded and checked > 0 and not module_short
    ok = parent_ok and modules_ok
    others = sorted(set(elsewhere))
    return g.add("lint-is-enforced", ok,
                 "in the parent pom: -Xlint:all %s, -Werror %s%s "
                 "(%d compiler argument(s) there%s); %s"
                 % ("yes" if lint else "NO",
                    "yes" if werror else
                    ("via <failOnWarning>" if fail_on_warning else "NO"),
                    (", but %s switches it back off" % " ".join(silenced))
                    if silenced else "",
                    len(args),
                    ("; compiler flags also in %s" % ", ".join(others))
                    if others else "",
                    ("the scored build's effective compiler configuration "
                     "was not recorded" if not recorded else
                     ("every module compiles under both (%d compiler "
                      "execution(s) in %d module(s))"
                      % (checked, len(compiles))) if modules_ok else
                     ("but not every module compiles under both: %s"
                      % "; ".join(module_short[:6])))),
                 5.0,
                 {"args": args, "lint": lint, "werror": werror,
                  "silenced": silenced,
                  "fail_on_warning": sorted(set(fail_on_warning)),
                  "poms": sorted(set(where)), "elsewhere": others,
                  "parent_ok": parent_ok, "modules_ok": modules_ok,
                  "recorded": recorded, "executions_checked": checked,
                  "modules_short": module_short[:40]})


def warning_free_build():
    """...and the build the verifier ran printed no warning about any source.

    The other half of the pair, and the half that is measured rather than
    declared. It reads `/score/log/build.log` -- the transcript of the
    `package` this verifier's own Maven ran, on this verifier's own JDK 21 --
    and counts the lines javac anchors to a file:

        [WARNING] /app/<module>/src/main/java/.../HostPlatform.java:[29,30] ...

    Only those. A Maven `[WARNING]` about a plugin version or a missing
    descriptor is not a statement about the candidate's Java, and a row that
    counted them would fail a correct tree for a property of the build
    environment.

    What this catches that `lint-is-enforced` does not: the tree that sets
    `-Xlint:all` without `-Werror`. That build succeeds and prints all 21
    warnings, so the pom-side row passes on the lint half, fails on the
    `-Werror` half, and this row fails outright with the 21 lines in its
    evidence -- which is the single most useful thing this rung's report can
    hand a candidate who has done half of it.

    What `lint-is-enforced` catches that this does not: the tree that fixes
    every warning and configures nothing. It prints no warnings, passes here,
    and fails there -- correctly, because nothing stops the next commit
    reintroducing all 21.

    And the interaction that is not a hole. With `-Werror` genuinely in force,
    a warning is an error, the build fails, `build-and-artifact` fails, there
    is no deployable and `behaviour` is 0 of 428 -- so `reward = migration *
    behaviour` is exactly zero and this row's verdict is irrelevant. That is
    the intended shape: a candidate who turns on `-Werror` without doing the
    work does not get a partial score, and the instruction says so in as many
    words.

    Measured: the tree this step is handed prints exactly one such line under
    the default lint set and 21 under `-Xlint:all`; the reference prints none
    of either, and its full build transcript contains no `[WARNING]` at all.
    """
    try:
        with open(CAL["build_log"], "r", encoding="utf-8",
                  errors="replace") as fh:
            lines = fh.read().splitlines()
    except (IOError, OSError) as e:
        return g.add("warning-free-build", False,
                     "no build transcript at %s: %s"
                     % (CAL["build_log"], str(e)[:80]), 2.0,
                     {"count": 0, "sites": []})
    hits = [ln.strip() for ln in lines if JAVAC_WARNING_RE.match(ln)]
    ok = b.build_rc == 0 and not hits
    return g.add("warning-free-build", ok,
                 ("package rc=%s, %d javac warning(s) in %d transcript line(s)"
                  % (b.build_rc, len(hits), len(lines)))
                 + ("" if not hits else ": " + "; ".join(
                     h[len("[WARNING] /app/"):130] for h in hits[:6])),
                 2.0,
                 {"build_rc": b.build_rc, "count": len(hits),
                  "transcript_lines": len(lines),
                  "sites": [h[:200] for h in hits[:200]]})


# Step 09's thirteen, collected rather than journalled -- the ten below, plus
# the three release rows rebound up in the floor block. One row,
# `inherited-java21-hold`, at 2.5. The definitions above are step 09's
# verbatim and the calls below are in step 09's order; only the binding of `g`
# differs, which is what makes this a hold of the same question rather than a
# cheaper restatement of it.
_journal, g = g, IDIOM

no_legacy_date_api()
java_time_in_use()
no_owned_stringbuffer()
callbacks_are_lambdas()
pattern_instanceof()
no_raw_cli_types()
no_deprecated_for_removal()

# `suppressions`, moved down from the floor block above and re-priced. Every
# rung from 01 to 08 asked "may not grow" against a cap of 31 that no tree in
# this ladder has come within ten of; this rung drops it to 8, which is below
# the ten arguments the tree this step is handed carries, and the row starts
# discriminating.
#
# 8 and not 6, which is what the reference leaves. The composition matters
# more than the count here and the slack is sized to it. Of the reference's
# six, two are `@SuppressWarnings("deprecation")` on `BaseCommand.startThread`
# and `DispatchCommand.start`, both of them covering calls to this project's
# *own* deprecated `CurrentUser.isAdministrator()` -- a method the 428
# observations exercise and which this rung has no licence to remove. A
# candidate who instead threads a non-deprecated accessor through both call
# sites needs none of them and lands at 4; one who suppresses at a coarser
# granularity -- the class rather than the method -- lands at 6 by a different
# route. Two units of slack cover the spread without reaching 10.
#
# Counted by argument and not by site, which is `suppressions_at_most`'s own
# rule and is what makes the reference's
# `@SuppressWarnings({"rawtypes", "unchecked"})` in `CmdLineParser` cost two
# rather than one. Consolidating annotations does not reduce the number; that
# is the point of counting this way, and it is why the floor's ten sites and
# the reference's five are not the numbers this row reads.
G.suppressions_at_most(g, b, CAL["suppressions_max"], weight=2.5)
lint_is_enforced()
warning_free_build()
g = _journal

# `lint-is-enforced` is inside that hold, and it is the one row in it whose
# placement is worth arguing. It is the row that makes every other idiom row
# stick -- `-Xlint:all -Werror` is the difference between a tree that happens
# to be clean and a tree that cannot stop being -- and folding it into a hold
# drops what step 09 priced at 5.0 to a thirteenth share of 2.5.
#
# It is folded anyway, and deliberately. The alternative is to keep it at the
# top level, where it would outweigh every row this rung actually advances,
# and the ladder's rule is that a rung prices its own leap: a candidate at
# this rung is handed a tree whose poms already carry those two compiler
# arguments and has no work to do to keep them. What it protects against is
# deletion, and deletion is exactly what a hold row catches -- the hold fails
# whole, so a tree that strips `-Werror` to get a warning past
# `warning-free-build` loses the row and everything else in it. That is a
# harsher answer than step 09 gave, not a softer one.


# ---------------------------------------------------------------------------
# Step 10's rung -- the platform step
# ---------------------------------------------------------------------------
#
# Eleven rows, 31.5 between them, in three groups.
#
#   Five read the resolved dependency graph and the deployable and say which
#   platform this is: `boot-4`, `framework-7`, `servlet-api-6-1`,
#   `modular-starters`, `junit-6`.
#
#   Five read the source and say whether the tree is written the way Boot 4
#   expects to read it: `nullness-marked`, `nullable-returns`,
#   `jspecify-only-nullness`, `implicit-injection`, `serial-annotated`.
#
#   One runs the migration tool the platform's own authors publish and reads
#   what it still wants to change: `rewrite-clean`.
#
# None of the eleven is observable, and that is worth saying here rather than
# leaving to be discovered. All 428 observations pass on both control trees;
# `must-flip.jsonl` is step 09's file unchanged, 123 records, because this rung
# adds no flip of its own. `behaviour` cannot tell a Boot 3.5 tree from a Boot
# 4.1 one, since the server does the same thing on both, and that is what a
# platform rung is: the whole of the discrimination is in this file.
#
# The five source rows are not tidying. Boot 4 ships JSpecify-annotated
# signatures throughout and its own build is `@NullMarked`; a tree that states
# no nullness gets no checking from a platform designed to give it, and one
# that states two vocabularies gets a checker reading two dialects as one. The
# rest is the same argument: `-parameters` and constructor injection without
# `@Autowired` are how Boot 4 expects to find what it binds.


def _main_ast(kind_):
    """AST records of `kind_` whose file is under a `src/main/java` root.

    The leading `/` is prepended before the test so that a single-module tree
    whose sources sit at `src/main/java/...` -- no module directory in front
    of them -- is matched the same way this seven-module reactor is. A
    candidate is allowed to have flattened the reactor on the way here.
    """
    out = []
    for r in b.ast.kind(kind_):
        if "/src/main/java/" in "/" + (r.get("p") or "").replace("\\", "/"):
            out.append(r)
    return out


def _has_anno(rec, simple_name):
    """Whether `rec` carries `@simple_name`, however the source spelled it.

    AstScan records an annotation under the name in the source text, so
    `@NullMarked` is `NullMarked` and `@org.jspecify.annotations.NullMarked`
    is the whole dotted string. Comparing the last segment accepts both and
    accepts nothing else -- `@MyNullMarked` has a different last segment.
    """
    for a in rec.get("annos") or []:
        if a.rsplit(".", 1)[-1] == simple_name:
            return True
    return False


def _grouped_deps(group):
    """Sorted unique (group, artifact, version) triples for one groupId."""
    return sorted(set(t for t in G.deps(b) if t[0] == group))


def _declared_deps():
    """Every `<dependency>` written in a pom, as (pom, group, artifact, version).

    The resolved graph and the written poms answer different questions and
    this rung asks both. `G.deps` says what ends up on the classpath and
    cannot see who asked for it or whether a version was named; this says what
    the reactor's own text declares and cannot see anything that arrives
    through management or transitively. Two clauses of section 1 -- "the
    reactor names no JUnit version anywhere" and "jspecify is declared, and
    not with a version" -- are statements about the text, and reading them off
    the graph would answer a different question.

    `version` is the element's text with properties expanded where they
    expand, or None when the element is absent -- which is the case this rung
    is actually about. `<dependencyManagement>` entries are included: a
    managed version is a version the reactor named.
    """
    out = []
    for rel, root, _text in G._pom_docs(b):
        if root is None:
            continue
        for el in root.iter():
            if G._local(el.tag) != "dependency":
                continue
            f = {}
            for kid in el:
                f[G._local(kid.tag)] = (kid.text or "").strip()
            ver = f.get("version")
            if ver:
                ver = G._resolve(ver, _props())
            out.append((rel, f.get("groupId", ""), f.get("artifactId", ""),
                        ver or None))
    return out


def _keeps_parameter_names():
    """Where, if anywhere, the reactor asks javac to keep parameter names.

    Three spellings, all of which the maven-compiler-plugin reads and all of
    which end in `-parameters` on the command line: the `maven.compiler.
    parameters` property, a `<parameters>true</parameters>` element in the
    plugin's own configuration, and `-parameters` written out among its
    `<compilerArgs>`. Returns the list of places found, so the detail can say
    which one the tree used rather than only that it did.

    Read the same way `lint-is-enforced` reads its arguments -- off the pom
    text, plugin by plugin -- and for the same reason: there is no artifact
    that says "this class file has parameter names" without opening every
    class file and asking whether the names look synthesized, and a build that
    is configured to keep them is what section 1.6 asks for.
    """
    found = []
    for k, vals in _props().items():
        if k == "maven.compiler.parameters" and any(
                v.strip().lower() == "true" for v in vals):
            found.append("property maven.compiler.parameters")
    for rel, root, _text in G._pom_docs(b):
        if root is None:
            continue
        for el in root.iter():
            if G._local(el.tag) != "plugin":
                continue
            art = "".join(kid.text or "" for kid in el
                          if G._local(kid.tag) == "artifactId").strip()
            if art != "maven-compiler-plugin":
                continue
            for sub in el.iter():
                tag = G._local(sub.tag)
                txt = (sub.text or "").strip()
                if tag == "parameters" and txt.lower() == "true":
                    found.append("%s: <parameters>" % rel)
                elif tag in ("arg", "compilerArgument") and \
                        "-parameters" in txt.split():
                    found.append("%s: <compilerArgs>" % rel)
    return sorted(set(found))


_PROPS = {}


def _props():
    """The reactor's merged properties, read once."""
    if not _PROPS:
        _PROPS.update(G._pom_properties(G._pom_docs(b)))
    return _PROPS


def _managed_pins():
    """(pinned, declared) dependencies on artifacts the platform manages.

    A `<dependency>` element that is not inside a `<dependencyManagement>`
    block, anywhere in any reactor pom, whose groupId is or sits under one of
    CAL's `managed_groups`. `pinned` are the ones carrying a `<version>`;
    `declared` are all of them, which is what the anti-vacuity floor counts.

    `<dependencyManagement>` is excluded and has to be: importing a BOM *is*
    naming a version, and it is the one place the reactor is supposed to.
    Plugin subtrees are excluded for the opposite reason -- no BOM here
    manages a plugin version, so a plugin without one is a build that does
    not reproduce. That exclusion is the whole `<plugin>` subtree, including
    the `<dependencies>` a plugin puts on its own classpath: the reactor pins
    `maven-compiler-plugin` and `maven-surefire-plugin` deliberately, and a
    dependency handed to one of them is part of that pin rather than a
    version the application resolves.

    Walked with the ancestry carried explicitly rather than with
    `root.iter()`, because the whole question is which block the element is
    in and `iter()` throws that away.
    """
    pinned, declared = [], []
    for rel, root, _text in G._pom_docs(b):
        if root is None:
            continue
        stack = [(root, False)]
        while stack:
            el, mgmt = stack.pop()
            for kid in el:
                tag = G._local(kid.tag)
                if tag == "dependencyManagement":
                    stack.append((kid, True))
                    continue
                if tag == "plugin":
                    continue
                if tag != "dependency":
                    stack.append((kid, mgmt))
                    continue
                if mgmt:
                    continue
                f = {}
                for gk in kid:
                    f[G._local(gk.tag)] = (gk.text or "").strip()
                grp, art = f.get("groupId", ""), f.get("artifactId", "")
                if not any(grp == m or grp.startswith(m + ".")
                           for m in CAL["managed_groups"]):
                    continue
                declared.append("%s: %s:%s" % (rel, grp, art))
                ver = f.get("version")
                if ver:
                    pinned.append("%s: %s:%s:%s"
                                  % (rel, grp, art, G._resolve(ver, _props())))
    return sorted(pinned), sorted(declared)


def boot_4():
    """Every resolved `org.springframework.boot` coordinate is on the 4 line.

    `spring-boot-dep`, inside `inherited-boot-hold`, says Boot is what
    supplies the framework, and at this rung it accepts 3 or 4 -- widened so
    the floor keeps that hold, for the reason CAL's `boot_version_re` sets
    out. This is the row that says which, and it is where the floor loses what
    the widening gave back.

    Both halves, because either alone passes a tree that has not moved.
    "Nothing off the 4 line" is true of a tree that resolves no Boot at all;
    "something on the 4 line" is true of a tree that added
    `spring-boot:4.1.1` beside six 3.5.16 artifacts, which is a classpath that
    does not start. The count floor is the third, and it is the *floor tree's*
    count rather than the reference's -- CAL says why that is the honest
    number here.

    And a fourth clause, about where the version comes from rather than what
    it is. Section 1.1 says Boot supplies the versions, and a tree can satisfy
    every clause above while writing `<version>4.0.8</version>` into a
    `<dependency>` on a Boot starter -- which resolves, builds, starts, serves
    every observation, and is one artifact that the next platform bump leaves
    behind. `_managed_pins` is the measurement and
    `managed_unversioned_min` is its floor. This clause lives on this row
    rather than on one of its own because it is the same requirement seen from
    the other side: the predecessor tree passes it, and a row its predecessor
    already passes is a row about the rung below.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "boot-4", b, 3.0)
    coords = _grouped_deps("org.springframework.boot")
    off = [c for c in coords if not re.match(CAL["boot4_version_re"], c[2])]
    on = [c for c in coords if re.match(CAL["boot4_version_re"], c[2])]
    enough = len(coords) >= CAL["boot4_artifacts_min"]
    why = []
    if not coords:
        why.append("no org.springframework.boot coordinate resolves")
    elif not on:
        why.append("%d Boot coordinate(s) resolve and none is on the 4 line "
                   "(versions: %s)"
                   % (len(coords), ", ".join(sorted({c[2] for c in coords}))))
    if off:
        why.append("%d coordinate(s) off the 4 line: %s"
                   % (len(off), ", ".join("%s:%s" % (c[1], c[2])
                                          for c in off[:6])))
    if coords and not enough:
        why.append("only %d Boot coordinate(s) resolve, need >=%d for the "
                   "absence to mean anything"
                   % (len(coords), CAL["boot4_artifacts_min"]))
    pinned, declared = _managed_pins()
    if pinned:
        why.append("%d dependenc(y/ies) outside dependencyManagement name a "
                   "version the platform manages: %s"
                   % (len(pinned), "; ".join(pinned[:6])))
    if len(declared) < CAL["managed_unversioned_min"]:
        why.append("only %d dependenc(y/ies) on platform-managed artifacts "
                   "are declared at all, need >=%d before that absence means "
                   "anything" % (len(declared), CAL["managed_unversioned_min"]))
    return g.add("boot-4", not why,
                 ("%d org.springframework.boot coordinate(s), all on the 4 "
                  "line (%s), and %d platform-managed dependenc(y/ies) "
                  "declared with no version of their own"
                  % (len(coords),
                     ", ".join(sorted({c[2] for c in coords})),
                     len(declared)))
                 if not why else "; ".join(why),
                 3.0,
                 {"count": len(coords),
                  "versions": sorted({c[2] for c in coords}),
                  "off_line": ["%s:%s" % (c[1], c[2]) for c in off],
                  "need": CAL["boot4_artifacts_min"],
                  "pinned": pinned[:20], "declared": declared[:20],
                  "declared_need": CAL["managed_unversioned_min"]})


def framework_7():
    """Every resolved `org.springframework` coordinate is on the 7 line.

    `boot-4`'s question one layer down, and the reason it is asked separately
    is that the two can disagree. Boot's dependency management names a
    framework version and a candidate can override it -- a
    `<spring-framework.version>` property is the documented way to do exactly
    that -- so a tree can resolve Boot 4.1.1 and Framework 6.2.19 together.
    It does not work, it is not what anyone means to do, and the failure is a
    `NoSuchMethodError` at startup rather than at compile time.

    The groupId is matched exactly and not as a prefix, for the reason
    `no-spring-off-6-or-7` gives: `org.springframework.boot`,
    `org.springframework.security` and `org.springframework.data` have their
    own version lines and have never tracked the framework's.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "framework-7", b, 1.5)
    coords = _grouped_deps("org.springframework")
    off = [c for c in coords if not re.match(CAL["spring7_version_re"], c[2])]
    on = [c for c in coords if re.match(CAL["spring7_version_re"], c[2])]
    enough = len(coords) >= CAL["spring7_artifacts_min"]
    why = []
    if not on:
        why.append("no org.springframework coordinate is on the 7 line "
                   "(%d resolve, versions: %s)"
                   % (len(coords),
                      ", ".join(sorted({c[2] for c in coords})) or "none"))
    if off:
        why.append("%d coordinate(s) off the 7 line: %s"
                   % (len(off), ", ".join("%s:%s" % (c[1], c[2])
                                          for c in off[:6])))
    if coords and not enough:
        why.append("only %d framework coordinate(s) resolve, need >=%d"
                   % (len(coords), CAL["spring7_artifacts_min"]))
    return g.add("framework-7", not why,
                 ("%d org.springframework coordinate(s), all on the 7 line "
                  "(%s)" % (len(coords),
                            ", ".join(sorted({c[2] for c in coords}))))
                 if not why else "; ".join(why),
                 1.5,
                 {"count": len(coords),
                  "versions": sorted({c[2] for c in coords}),
                  "off_line": ["%s:%s" % (c[1], c[2]) for c in off],
                  "need": CAL["spring7_artifacts_min"]})


def servlet_api_6_1():
    """The packaged servlet API is Servlet 6.1, the Jakarta EE 11 one.

    `servlet-api-6` above is step 05's row, asked of the same deployable: four
    Jakarta markers present and the three types Servlet 6.0 removed absent. It
    passes on Servlet 6.0 and on Servlet 6.1 alike, which is right for the
    rung that wrote it and is not enough here -- Jakarta EE 11 is Servlet 6.1,
    and a tree can move Boot and leave a `<tomcat.version>` pinned at 10.1 in
    a property, which resolves, starts, and serves every one of the 428
    observations.

    One marker type and no negative half, which is the measurement and not a
    shortcut: CAL's `servlet_6_1_marker` records the four jars this was read
    off and records that nothing is removed between 6.0 and 6.1 that a gate
    could read. `HttpSession$Accessor` is a real 6.1 addition, the supported
    way to hold a session across threads, and it is in both 6.1 jars and
    neither 6.0 one.

    Read out of the deployable rather than off a coordinate, like every other
    row in this file that can be: the API arrives inside `tomcat-embed-core`
    here and could arrive as a standalone jar beside Jetty, and the question
    is the same either way.

    Four clauses, because a positive row on one type is a row one type
    answers. The marker has to be packaged; the archive providing it has to
    hold a servlet API's worth of `jakarta.servlet` classes rather than be a
    hand-written copy of one interface; no archive may provide
    `jakarta.servlet.http.HttpSession` without also providing the marker,
    which is what a 6.0 jar left on the classpath beside a 6.1 one looks
    like; and no two archives may each carry a servlet API's worth of them,
    which is what two 6.1 copies look like and which the third clause cannot
    see, because both of them carry the marker. Section 1.3 says one servlet
    API and not two, and two copies of the same generation are still two.
    One provider is what a correct tree has however it got there: Tomcat 11
    carries the API itself, and the Jetty and Undertow routes take it as a
    standalone `jakarta.servlet-api` jar with no second copy beside it. See
    `servlet_6_1_anchor` and `servlet_6_1_provider_min` in CAL.
    """
    packaged = {}
    for c in b.art.classes():
        o = c.get("o") or ""
        if not inside_artifact(o):
            continue
        packaged.setdefault(c.get("n") or "", set()).add(
            os.path.basename(o.rsplit("!", 1)[0]))
    arcs, total = packaged_providers(("jakarta.servlet",))
    marker = CAL["servlet_6_1_marker"]
    anchor = CAL["servlet_6_1_anchor"]
    own_min = CAL["servlet_6_1_provider_min"]
    holders = sorted(packaged.get(marker, ()))
    why = []
    if not ART_BASE:
        why.append("no deployable to look inside")
    elif total < CAL["servlet_api_classes_min"]:
        why.append("only %d jakarta.servlet class(es) packaged, need >=%d "
                   "before an absence means anything"
                   % (total, CAL["servlet_api_classes_min"]))
    elif not holders:
        why.append("%s is not packaged, so the servlet API in %s is 6.0 or "
                   "older (%d jakarta.servlet classes from %s)"
                   % (marker, ART_BASE, total,
                      ", ".join(sorted(arcs)) or "nowhere"))
    else:
        if not [a for a in holders if arcs.get(a, 0) >= own_min]:
            why.append("%s is packaged, but the archive(s) carrying it hold "
                       "only %s jakarta.servlet class(es) of their own and "
                       ">=%d is a servlet API rather than a stub: %s"
                       % (marker,
                          "/".join(str(arcs.get(a, 0)) for a in holders),
                          own_min, ", ".join(holders)))
        apis = sorted(a for a, n in arcs.items() if n >= own_min)
        if len(apis) > 1:
            why.append("%d archives each carry a servlet API's worth of "
                       "jakarta.servlet classes (%s) -- the deployable "
                       "packages one servlet API, not two"
                       % (len(apis),
                          ", ".join("%s: %d" % (a, arcs[a]) for a in apis)))
        stale = sorted(a for a in packaged.get(anchor, ())
                       if a not in packaged[marker])
        if stale:
            why.append("%s is also provided by %s, which carr%s no %s -- a "
                       "pre-6.1 servlet API is packaged beside the 6.1 one"
                       % (anchor, ", ".join(stale),
                          "ies" if len(stale) == 1 else "y", marker))
    return g.add("servlet-api-6-1", not why,
                 ("%s packaged in %s from %s, so the API is Servlet 6.1 (%d "
                  "jakarta.servlet classes from %s)"
                  % (marker, ART_BASE, ", ".join(holders), total,
                     ", ".join(sorted(arcs))))
                 if not why else "; ".join(why),
                 2.0,
                 {"marker": marker, "holders": holders,
                  "anchor_providers": sorted(packaged.get(anchor, ())),
                  "servlet_classes": total, "providers": arcs,
                  "need": CAL["servlet_api_classes_min"],
                  "provider_min": own_min})


def modular_starters():
    """The web layer comes from Boot 4's modules, not the retired aggregate.

    Boot 4 broke `spring-boot-starter-web` into modules and deprecated the
    aggregate in favour of `spring-boot-starter-webmvc`; the 4.1.1 pom of the
    aggregate says so in its own `<description>`. It still publishes and still
    resolves, which is exactly what makes this a row: a candidate can move
    every version in the reactor, pass `boot-4` and `framework-7`, build, and
    serve all 428 observations while still depending on an artifact upstream
    has marked as going away. That is a migration step declined, and nothing
    above this row notices it.

    Three clauses, all read off the resolved graph with no pom parsing:

      * the aggregate does not resolve. Boot 4's `spring-boot-starter-web`
        pulls `spring-boot-starter-jackson`, `spring-boot-starter-tomcat`,
        `spring-boot-http-converter` and `spring-boot-webmvc` -- and never
        `spring-boot-starter-webmvc` -- so its presence in the graph is its
        own declaration and not a shadow of something else;
      * at least one of the modular web artifacts does resolve, so this is not
        passed by deleting the web layer wholesale;
      * a servlet container still resolves, which is the same guard from the
        other side. Any of Tomcat, Jetty or Undertow satisfies it: the row is
        about the starter rename, not about which container a candidate picked.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "modular-starters", b, 1.5)
    arts = sorted(set(t[1] for t in G.deps(b)))
    aggregate = CAL["aggregate_web_starter"] in arts
    modular = [a for a in CAL["modular_web_starters"] if a in arts]
    container = [a for a in CAL["container_artifacts"] if a in arts]
    why = []
    if aggregate:
        why.append("%s still resolves; Boot 4 deprecates it in favour of %s"
                   % (CAL["aggregate_web_starter"],
                      CAL["modular_web_starters"][0]))
    if not modular:
        why.append("none of %s resolves, so nothing supplies the web layer"
                   % ", ".join(CAL["modular_web_starters"]))
    if not container:
        why.append("no servlet container resolves (looked for %s)"
                   % ", ".join(CAL["container_artifacts"][:4]))
    return g.add("modular-starters", not why,
                 ("web layer from %s, container from %s, and no %s"
                  % (", ".join(modular), ", ".join(container),
                     CAL["aggregate_web_starter"]))
                 if not why else "; ".join(why),
                 1.5,
                 {"aggregate_present": aggregate, "modular": modular,
                  "container": container})


def junit_6():
    """The tests run on JUnit 6, engine and platform together.

    Two groupIds because JUnit is two things: `org.junit.jupiter` is the
    programming model the tests are written against and `org.junit.platform`
    is what discovers and runs them. Their versions were independent through
    all of JUnit 5 -- jupiter 5.10.2 alongside platform 1.10.2 -- and JUnit 6
    is where they become one number, so a tree half-moved fails on the half it
    left behind.

    No pom in the reference names a JUnit version. It arrives from
    `spring-boot-dependencies`, which manages JUnit since Boot 4, and that is
    why step 09's reference imported `junit-bom` ahead of it and this one has
    deleted the import.

    Both halves are here because the resolved graph alone does not say what
    section 1.1 asks. A tree that deletes the import and lets the platform
    name the version, and a tree that keeps the import and pins it forward to
    6.0.3, resolve the same graph -- and the second one has kept the thing
    the first one got rid of, which is a second place where a JUnit version
    has to be right. The graph clause says JUnit moved; the text clause says
    the reactor stopped naming it. The text clause reads the poms rather than
    the graph for the reason `_declared_deps` gives.

    "Names a version" is either a `<version>` on a dependency under one of the
    two JUnit groupIds or on `junit-bom` itself -- managed or not, since a
    managed version is still a version the reactor named -- or a property
    whose name mentions JUnit and ends in `version`. An unused property still
    counts: this rung is about the reactor no longer holding an opinion, and a
    property left behind is an opinion someone will wire back up.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "junit-6", b, 3.0)
    coords = sorted(set(t for t in G.deps(b) if t[0] in CAL["junit_groups"]))
    off = [c for c in coords if not re.match(CAL["junit6_version_re"], c[2])]
    why = []
    if len(coords) < CAL["junit6_artifacts_min"]:
        why.append("only %d JUnit coordinate(s) resolve under %s, need >=%d"
                   % (len(coords), " and ".join(CAL["junit_groups"]),
                      CAL["junit6_artifacts_min"]))
    if off:
        why.append("%d coordinate(s) not on the 6 line: %s"
                   % (len(off), ", ".join("%s:%s" % (c[1], c[2])
                                          for c in off[:6])))
    named = ["%s: %s:%s:%s" % (rel, grp, art, ver)
             for rel, grp, art, ver in _declared_deps()
             if ver and (grp in CAL["junit_groups"]
                         or art == CAL["junit_bom_artifact"])]
    named += ["%s = %s" % (k, ", ".join(sorted(v)))
              for k, v in sorted(_props().items())
              if re.search(CAL["junit_version_prop_re"], k)]
    if named:
        why.append("%d place(s) in the reactor name a JUnit version: %s"
                   % (len(named), "; ".join(named[:6])))
    return g.add("junit-6", not why,
                 ("%d JUnit coordinate(s), all on the 6 line (%s), and no pom "
                  "names a JUnit version"
                  % (len(coords), ", ".join(sorted({c[2] for c in coords}))))
                 if not why else "; ".join(why),
                 3.0,
                 {"count": len(coords),
                  "versions": sorted({c[2] for c in coords}),
                  "off_line": ["%s:%s" % (c[1], c[2]) for c in off],
                  "named_versions": named[:20]})


def nullness_marked():
    """Every package of main source states that its types are non-null.

    `@NullMarked` is the JSpecify declaration that unannotated type uses in
    this scope mean "not null", which is what turns `@Nullable` from a comment
    into a statement a tool can check. Boot 4's own signatures carry it, and a
    tree that does not carry its own gets nothing from theirs.

    Two ways to be covered, and both count:

      * a `package-info.java` carrying the annotation -- one file per package,
        which is the form the reference takes and the form the framework's own
        build takes;
      * every *top-level* type in the package carrying it, which is what a
        candidate who dislikes `package-info.java` will reach for instead.

    Top-level and not every type, and that distinction is the reason this
    function does its own filtering rather than counting `type` records:
    JSpecify scopes are lexical, so a nested class inside a `@NullMarked`
    outer class is already covered, and a row that demanded the annotation on
    all 105 type declarations would be failing a tree that is entirely
    correct. A type is top-level when its simple name is the file's base name,
    which is what the language guarantees for a public type and what every
    type in either control tree satisfies.

    The universe is packages that declare at least one top-level type in main
    source -- 18 in both control trees. A `package-info.java` with nothing
    beside it declares no types and is not asked to state their nullness.
    """
    files = _main_ast("file")
    types = _main_ast("type")
    pkg_of = {(f.get("p") or "").replace("\\", "/"): f.get("pkg") or ""
              for f in files}
    top_by_pkg = {}
    for t in types:
        rel = (t.get("p") or "").replace("\\", "/")
        base = os.path.basename(rel)[:-5]
        if t.get("n", "").rsplit(".", 1)[-1] != base:
            continue
        top_by_pkg.setdefault(pkg_of.get(rel, ""), []).append(t)
    marked_pkg_info = set()
    for f in files:
        rel = (f.get("p") or "").replace("\\", "/")
        if os.path.basename(rel) != "package-info.java":
            continue
        for a in f.get("pkgannos") or []:
            if a.rsplit(".", 1)[-1] == "NullMarked":
                marked_pkg_info.add(f.get("pkg") or "")
    bare = []
    for pkg, ts in sorted(top_by_pkg.items()):
        if pkg in marked_pkg_info:
            continue
        if all(_has_anno(t, "NullMarked") for t in ts):
            continue
        bare.append(pkg)
    why = []
    if len(top_by_pkg) < CAL["nullmarked_packages_min"]:
        why.append("only %d package(s) of main source found, need >=%d before "
                   "this row means anything"
                   % (len(top_by_pkg), CAL["nullmarked_packages_min"]))
    if bare:
        why.append("%d of %d package(s) state no nullness: %s"
                   % (len(bare), len(top_by_pkg), ", ".join(bare[:6])))
    return g.add("nullness-marked", not why,
                 ("all %d package(s) of main source are @NullMarked (%d by "
                  "package-info.java)"
                  % (len(top_by_pkg), len(marked_pkg_info)))
                 if not why else "; ".join(why),
                 5.5,
                 {"packages": len(top_by_pkg), "bare": bare,
                  "by_package_info": sorted(marked_pkg_info),
                  "need": CAL["nullmarked_packages_min"]})


def nullable_returns():
    """Every method that returns null says so, and not many more than those.

    Under `@NullMarked`, a method whose declared return type carries no
    annotation is a method that promises never to return null. This tree has
    eighteen that do return it on the `-app` chain and seventeen on the
    `-war` one, and until this rung nothing said so -- which is harmless
    while nothing checks and is a false promise the moment something does.

    `nulls` is the AST scan's count of `return null;` statements whose nearest
    enclosing executable is this declaration, so a `return null` inside a
    lambda or an anonymous class belongs to that lambda's own record and not
    to the method containing it. A method with `nulls > 0` and no `@Nullable`
    is the failure.

    The cap is the other half, and it is what stops the row being passed by
    annotating everything. "No null-returning method lacks `@Nullable`" is
    satisfied by a tree that puts the annotation on every method it declares
    -- five hundred and forty-nine of them on the `-app` chain, five hundred
    and sixty-five on the `-war` one -- and such a tree has said nothing at
    all: it has merely turned the default off everywhere, which is the same
    thing as not declaring nullness. CAL sizes the cap against both
    predecessor trees.

    The third answer this row cannot refuse is deletion. Section 1.5 offers
    two answers, annotate or stop returning null, and the reference takes the
    second one on `getAcceptedIssuers` without costing a method: 549 before
    and 549 after, 18 null-returners down to 17. Deleting the eighteen
    instead would also satisfy both clauses, and no floor here can say so.
    A count of methods would have to sit under both predecessors -- 549 and
    565 -- and 18 deletions from 549 lands on 531, so any floor low enough
    to be safe on the `-app` chain is too low to catch the same deletion on
    the `-war` one. What does refuse it is the rest of the file: a deleted
    method that any of the 428 observations reaches fails `build-and-artifact`
    or `tests-ship-and-pass` and takes `behaviour` to zero with it, and
    `sources-survive` and `orphan-types-intact` hold the files and the types.
    What is left unguarded is a private null-returner nothing calls. Known,
    and left: an absolute floor that bites on one predecessor and not the
    other would buy less than the false failure it risks on a row worth 6.5.
    

    What this cannot see is a method that returns a null that arrived from
    somewhere else -- `return map.get(k);` -- which needs dataflow the scan
    does not do. The row is a floor on honesty about the obvious cases, not a
    proof of null-safety, and the instruction asks for what the row can check.
    """
    ms = _main_ast("method")
    returns_null = [m for m in ms if (m.get("nulls") or 0) > 0]
    annotated_ = [m for m in ms if _has_anno(m, "Nullable")]
    silent = [m for m in returns_null if not _has_anno(m, "Nullable")]
    why = []
    if silent:
        why.append("%d method(s) return null and are not @Nullable: %s"
                   % (len(silent),
                      "; ".join("%s.%s" % ((m.get("owner") or "?")
                                           .rsplit(".", 1)[-1], m.get("sig"))
                                for m in silent[:6])))
    if len(annotated_) > CAL["nullable_methods_max"]:
        why.append("%d method(s) carry @Nullable, more than the %d this tree "
                   "can need; blanket annotation states nothing"
                   % (len(annotated_), CAL["nullable_methods_max"]))
    return g.add("nullable-returns", not why,
                 ("%d of %d main method(s) return null and all %d are "
                  "@Nullable (%d annotated in total)"
                  % (len(returns_null), len(ms), len(returns_null),
                     len(annotated_)))
                 if not why else "; ".join(why),
                 6.5,
                 {"methods": len(ms), "return_null": len(returns_null),
                  "annotated": len(annotated_),
                  "cap": CAL["nullable_methods_max"],
                  "silent": ["%s.%s" % (m.get("owner"), m.get("sig"))
                             for m in silent[:200]]})


def jspecify_only_nullness():
    """The nullness vocabulary is JSpecify, and it is the only one.

    Three clauses. The coordinate is declared and resolves and the
    annotations are actually imported, which is the positive half; and no
    other nullness vocabulary appears anywhere in the source, which is the
    negative half.

    That it *resolves* settles nothing on its own, and the row does not
    pretend otherwise: `org.jspecify:jspecify` resolves on *both* control
    trees -- 1.0.1 on the reference, 1.0.0 on the floor, where it arrives
    transitively through `spring-core:6.2.19`. Spring Framework 7 states its
    own signatures' nullness in JSpecify, so every tree at this rung already
    has the jar.

    That it is *declared*, versionless, is the clause section 1.4 states and
    is read off the poms rather than the graph, for the reason
    `_declared_deps` gives: a transitive jar and a declared dependency resolve
    identically and are not the same build. A tree that writes the annotations
    against a jar nobody asked for is one framework dependency change away
    from not compiling, and a tree that declares it with a version of its own
    has taken back the one thing the platform is here to manage -- the same
    mistake as the `junit-bom` this rung deletes, in the same pom. The two
    halves have different shapes on purpose. *That* it is declared is a floor
    -- one module is enough, and the reference declares it once in the parent
    where every module inherits it. That it is declared *without a version*
    is a census, because "do not give it a version" is false of a reactor the
    moment one pom does, and a versionless use beside a version the reactor
    pinned itself is the platform managing nothing.

    The import clause is what discriminates on source: 29 files on the
    reference, none on the floor.

    The negative half is a guard against a plausible wrong answer rather than
    a finding about either control tree, and should be read as one: both trees
    import none of the fifteen prefixes in CAL, because the tree this step is
    handed states nullness nowhere at all. It is here because a candidate
    asked for `@Nullable` has a dozen of them to reach for -- `javax.annotation`
    from JSR-305, IntelliJ's, SpotBugs', the Checker Framework's, and
    `org.springframework.lang`'s own, which Framework 7.0 deprecated. Two
    vocabularies in one tree are two dialects that a checker reads as one, and
    under `-Xlint:all -Werror` a deprecated one is a compile error too.

    Imports and fully-qualified type uses both, because `@javax.annotation.Nullable`
    written out at the use site needs no import at all.
    """
    if b.deps_rc != 0:
        return G._deps_unusable(g, "jspecify-only-nullness", b, 3.0)
    grp, art = CAL["jspecify_coord"]
    coords = [t for t in G.deps(b) if t[0] == grp and t[1] == art]
    importers, foreign = [], []
    for f in b.ast.kind("file"):
        rel = (f.get("p") or "").replace("\\", "/")
        for im in f.get("imports") or []:
            name = im.split(" ")[0]
            if name.startswith(CAL["jspecify_package"] + "."):
                importers.append(rel)
                continue
            if name.endswith(".*"):
                pkg = name[:-2]
                if any(p == pkg or p.startswith(pkg + ".")
                       for p in CAL["foreign_nullness_prefixes"]):
                    foreign.append("%s: %s" % (rel, name))
            elif S.under(name, CAL["foreign_nullness_prefixes"]):
                foreign.append("%s: %s" % (rel, name))
    for u in b.ast.typeuses(CAL["foreign_nullness_prefixes"]):
        foreign.append("%s: %s" % ((u.get("p") or "").replace("\\", "/"),
                                   u.get("n")))
    # An annotation written out in full is an AnnotationExpr, not a type use,
    # so the scan above does not see `@javax.annotation.Nullable` with no
    # import. The declaration records carry each annotation's name as written:
    # read those too, on types, methods and fields and on the package
    # declarations. A parameter's or a
    # type argument's annotation is in neither record and is not seen.
    for kind, key in (("type", "annos"), ("method", "annos"),
                      ("field", "annos"), ("file", "pkgannos")):
        for r in b.ast.kind(kind):
            for a in r.get(key) or []:
                if "." in a and S.under(a, CAL["foreign_nullness_prefixes"]):
                    foreign.append("%s: @%s" % (
                        (r.get("p") or "").replace("\\", "/"), a))
    declared = [(rel, v) for rel, gg, aa, v in _declared_deps()
                if gg == grp and aa == art]
    why = []
    if not coords:
        why.append("%s:%s does not resolve" % (grp, art))
    if not declared:
        why.append("no pom declares %s:%s" % (grp, art))
    elif any(v for _rel, v in declared):
        why.append("%s:%s is declared with a version of its own in %s (%s)"
                   % (grp, art,
                      ", ".join(sorted({r for r, v in declared if v})),
                      ", ".join(sorted({v for _r, v in declared if v}))))
    if not importers:
        why.append("no source file imports %s.*" % CAL["jspecify_package"])
    if foreign:
        why.append("%d nullness annotation(s) from another vocabulary: %s"
                   % (len(foreign), "; ".join(sorted(set(foreign))[:6])))
    return g.add("jspecify-only-nullness", not why,
                 ("%s:%s is declared without a version and resolves at %s, "
                  "%d file(s) import it, and no other "
                  "nullness vocabulary appears"
                  % (grp, art, ", ".join(sorted({c[2] for c in coords})),
                     len(set(importers))))
                 if not why else "; ".join(why),
                 3.0,
                 {"coords": ["%s:%s:%s" % c for c in coords],
                  "declared": ["%s%s" % (rel, ":" + v if v else "")
                               for rel, v in declared],
                  "importers": len(set(importers)),
                  "foreign": sorted(set(foreign))[:200]})


def implicit_injection():
    """No sole constructor asks for injection; the container infers it.

    Spring has injected a type's only constructor without `@Autowired` since
    4.3, and Boot 4's documentation writes every example that way. The
    annotation on a sole constructor is a vestige of Guice, where it was
    required -- which is literally where this tree's came from: step 03
    rewrote `@Inject` to `@Autowired` one file at a time and kept the
    annotation because keeping it was the smaller change at that rung.

    Sole and not every constructor, because a type with two constructors must
    still say which one the container should use, and there is no such type in
    either control tree. A row that demanded the annotation's removal
    everywhere would be asking for an ambiguity.

    Paired with a floor on how many sole constructors take an argument at all,
    because "none of them is annotated" is true of a tree with no injection
    left -- see CAL. Measured: the floor carries the annotation on 31 of its
    sole constructors, the reference on none, and the constructors themselves
    are the same ones.

    And with the build flag that makes the inference resolve by name rather
    than by type and position, which is the second half of section 1.6 and
    belongs on this row rather than a row of its own: taking the annotation
    off is what asks the container to work the wiring out, and dropping
    parameter names is what leaves it working it out from less. One row, two
    clauses, because a tree with either half alone has not done what the
    section asks. Neither control tree is ambiguous here: the floor names the
    flag nowhere, the reference names it once, in the parent.
    """
    ms = _main_ast("method")
    by_owner = {}
    for m in ms:
        if (m.get("sig") or "").startswith("<init>("):
            by_owner.setdefault(m.get("owner"), []).append(m)
    sole = {o: v[0] for o, v in by_owner.items() if len(v) == 1}
    with_params = [o for o, m in sole.items() if m.get("sig") != "<init>()"]
    annotated_ = sorted(o for o, m in sole.items()
                        if _has_anno(m, "Autowired") or _has_anno(m, "Inject"))
    why = []
    if len(with_params) < CAL["sole_ctor_types_min"]:
        why.append("only %d type(s) have a single constructor taking an "
                   "argument, need >=%d before an absence means anything"
                   % (len(with_params), CAL["sole_ctor_types_min"]))
    if annotated_:
        why.append("%d sole constructor(s) still annotated: %s"
                   % (len(annotated_),
                      ", ".join(o.rsplit(".", 1)[-1] for o in annotated_[:8])))
    params = _keeps_parameter_names()
    if not params:
        why.append("no pom asks the compiler to keep parameter names "
                   "(maven.compiler.parameters, <parameters>, or -parameters "
                   "among the compiler arguments)")
    return g.add("implicit-injection", not why,
                 ("%d type(s) with one constructor, %d of them taking "
                  "arguments, none annotated, and the build keeps parameter "
                  "names (%s)"
                  % (len(sole), len(with_params), params[0]))
                 if not why else "; ".join(why),
                 1.5,
                 {"sole": len(sole), "with_params": len(with_params),
                  "annotated": annotated_, "parameters": params,
                  "need": CAL["sole_ctor_types_min"]})


def serial_annotated():
    """Every `serialVersionUID` is `@Serial`.

    `@Serial` is Java 14's way of asking the compiler to check that a
    serialization member is declared the way the runtime will look for it --
    the right name, the right type, the right modifiers. Without it a
    `private static long serialVersionUID` compiles, ships, and is silently
    ignored, and the class gets a computed UID that changes with its shape.

    This is the smallest row at this rung and it is priced accordingly: twelve
    fields, one annotation each, and nothing about it is hard. It is here
    because the release that made it possible is the one step 09 moved to, the
    tree has carried the fields since 2008, and a rung about writing code the
    way the current platform expects is the rung where it gets asked.

    Fields and not classes: `serialVersionUID` is the field, and `@Serial` goes
    on it. CAL's floor is what stops a tree that deleted them all from passing.
    """
    fs = [f for f in _main_ast("field") if f.get("n") == "serialVersionUID"]
    bare = [f for f in fs if not _has_anno(f, "Serial")]
    why = []
    if len(fs) < CAL["serial_fields_min"]:
        why.append("only %d serialVersionUID field(s), need >=%d"
                   % (len(fs), CAL["serial_fields_min"]))
    if bare:
        why.append("%d of %d serialVersionUID field(s) are not @Serial: %s"
                   % (len(bare), len(fs),
                      ", ".join((f.get("owner") or "?").rsplit(".", 1)[-1]
                                for f in bare[:8])))
    return g.add("serial-annotated", not why,
                 "all %d serialVersionUID field(s) are @Serial" % len(fs)
                 if not why else "; ".join(why),
                 1.5,
                 {"fields": len(fs),
                  "bare": [(f.get("owner") or "?") for f in bare[:200]],
                  "need": CAL["serial_fields_min"]})


def rewrite_clean():
    """OpenRewrite's own Boot 4 recipe finds nothing left to change.

    The one row in this file whose instrument is somebody else's. scan-extra.sh
    runs `org.openrewrite.recipe:rewrite-spring`'s
    `UpgradeSpringBoot_4_0` over a copy of the candidate's tree in dryRun mode
    and leaves the patch it would have applied at CAL["rewrite_patch"]; this
    reads it. Nothing is applied to `/app` and nothing the recipe does can
    reach a gate above this one -- see that script's header.

    Why a row at all, when eleven others already read the platform: this is the
    only check here that the platform's *own authors* wrote, and it knows
    things no gate in this file does -- the property renames, the removed
    autoconfiguration classes, the `@ConfigurationProperties` shapes that
    changed. A tree that passes every row above and this one has been read by
    the migration tool and found finished.

    Two rules about what counts.

      * It fails closed. `rewrite.rc != 0` -- or no rc at all -- is a failure,
        not a pass. An empty patch means "the recipe found nothing" and it also
        means "the recipe never ran", and those must not score the same.
      * A hunk that changes only a `<version>` element or a `<somethingVersion>`
        property is not a finding. Those are resolved from live Central
        metadata when the recipe runs, so counting them would fail a correct
        tree on the day a plugin releases -- a gate whose answer depends on the
        date is a clock. Measured on the reference: 754 bytes, one hunk,
        two changed elements -- `maven-compiler-plugin` 3.13.0 -> 3.16.0 and
        `maven-surefire-plugin` 3.2.5 -> 3.6.0, both of them `<version>`
        elements of plugins this reactor pins deliberately. That is four diff
        lines, a `-` and a `+` for each, and four is what the detail reports:
        the count in the evidence is of patch lines, not of elements.

    Every changed line in a `.java` file is a finding regardless: there is no
    version number in Java source, so nothing in the exemption applies there.
    """
    try:
        with open(CAL["rewrite_rc"], "r", encoding="utf-8") as fh:
            rc = fh.read().strip()
    except (IOError, OSError):
        rc = None
    if rc != "0":
        return g.add("rewrite-clean", False,
                     "the recipe did not run to completion (rewrite.rc=%s), "
                     "so an empty patch would mean nothing" % (rc or "absent"),
                     1.0, {"rc": rc, "findings": None})
    try:
        with open(CAL["rewrite_patch"], "r", encoding="utf-8",
                  errors="replace") as fh:
            patch = fh.read(1 << 22)
    except (IOError, OSError) as e:
        return g.add("rewrite-clean", False,
                     "rewrite.rc=0 but %s is unreadable: %s"
                     % (CAL["rewrite_patch"], str(e)[:80]),
                     1.0, {"rc": rc, "findings": None})
    version_only = re.compile(CAL["rewrite_version_only_re"])
    cur, findings, ignored = "", [], 0
    for ln in patch.split("\n"):
        if ln.startswith("diff --git "):
            cur = ln.rsplit(" b/", 1)[-1]
            continue
        if ln.startswith(("+++", "---", "@@", "index ", "new file",
                          "deleted file", "similarity ", "rename ")):
            continue
        if not ln or ln[0] not in "+-":
            continue
        body = ln[1:].strip()
        if not cur.endswith(".java") and version_only.match(body):
            ignored += 1
            continue
        findings.append("%s: %s" % (cur or "?", ln[:120]))
    return g.add("rewrite-clean", not findings,
                 ("UpgradeSpringBoot_4_0 has nothing left to change (%d byte "
                  "patch, %d version-only line(s) ignored)"
                  % (len(patch), ignored))
                 if not findings else
                 ("the recipe would still change %d line(s): %s"
                  % (len(findings), "; ".join(findings[:6]))),
                 1.0,
                 {"rc": rc, "patch_bytes": len(patch),
                  "ignored_version_lines": ignored,
                  "findings": findings[:200],
                  "files": sorted({f.split(":", 1)[0] for f in findings})[:40]})


boot_4()
framework_7()
servlet_api_6_1()
modular_starters()
junit_6()
nullness_marked()
nullable_returns()
jspecify_only_nullness()
implicit_injection()
serial_annotated()
rewrite_clean()


# ---------------------------------------------------------------------------
# What this rung threatens, in one row
# ---------------------------------------------------------------------------
#
# Three checks, 1.0 between them, inherited from step 06 and kept at the top
# level rather than folded into `inherited-logging-hold` with the rest of that
# rung's work. The eleven gates in that hold ask whether the logging migration
# happened; these three ask whether the running server still behaves as it did
# afterwards, which is a different question and one this rung can newly answer
# wrong -- `no-legacy-packaged` deletes fourteen archives from the deployable,
# and an over-eager reading of it takes the binding or the facade with them.
#
# They are priced like a hold rather than like a gate because that is what
# they are: they measure what the rung must not disturb, not what it advances,
# and the tree this step is handed passes all three.
#
# `audit-log-intact` is not here: a row fails whole,
# so everything in it is priced at the row's weight, and 1.0 is not a price for
# destroying the one feature step 06 existed to carry across. At this rung it
# sits inside `inherited-logging-hold` with the other ten, which is where a
# finished rung's own work goes once the next rung starts.


def logging_regression():
    """Three properties this rung can break without failing anything above."""
    h = _Collector()

    # The root level, measured from the other end. `logging-levels-declared`
    # reads what the configuration says; this reads what the server did. A
    # candidate who ships a logback.xml declaring root=DEBUG in a location
    # Boot does not load -- `src/main/resources/config/logback.xml`, say --
    # passes the declaration and fails this, and the pair is what makes the
    # requirement about the running server rather than about a file.
    #
    # Read by the level token, not by the line format and not by sheer
    # volume; `CAL["console_debug_lines_min"]` carries the measurement and the
    # argument for why counting lines instead would have passed a server that
    # had switched DEBUG off.
    counts, short = {}, []
    width = CAL["console_level_scan_chars"]
    for prof, floor in sorted(CAL["console_debug_lines_min"].items()):
        path = os.path.join(S.LOG, "server-%s.log" % prof)
        n = 0
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                for ln in fh:
                    if DEBUG_TOKEN_RE.search(ln[:width]):
                        n += 1
        except (IOError, OSError):
            n = -1
        counts[prof] = n
        if n < floor:
            short.append("%s=%d need >=%d" % (prof, n, floor))
    h.add("console-verbosity-preserved", not short,
          ("the three servers wrote %s DEBUG console line(s), which is the "
           "inherited root level still in force"
           % ", ".join("%s:%d" % (p, counts[p]) for p in sorted(counts)))
          if not short else
          "console output fell below the inherited root level: %s"
          % "; ".join(short),
          1.0, {"debug_lines": counts, "note": "console DEBUG %s"
                % ", ".join("%s:%d" % (p, counts[p]) for p in sorted(counts))})

    # The slf4j call sites the tree already had. Step 06 floored this at its
    # own before-count of fourteen; this rung re-measures it at thirteen,
    # because `CachePool` logged about a disk store that Caffeine does not
    # have. `CAL["slf4j_classes_min"]` carries that measurement and the
    # argument for why lowering it does not blunt it. What the floor still
    # catches is the candidate who "unifies logging" by routing the existing
    # call sites through logback's API directly -- which works, and which
    # makes a dozen files depend on a backend they had no reason to know
    # about.
    G.uses_refs(h, b, "slf4j-facade-intact", CAL["slf4j_packages"],
                minimum=CAL["slf4j_classes_min"], own=True)

    # And the binding underneath them. Exactly one slf4j provider: zero is
    # slf4j printing "No SLF4J providers were found" and discarding every log
    # line the application writes, two is a coin toss at class-load time.
    # Counted by service registration rather than by package, because
    # `logback-classic` registers `LogbackServiceProvider` under its own
    # `ch.qos.logback` namespace and publishes nothing under `org.slf4j` --
    # a package count would fall from two archives to one across this rung
    # and read as the rung's own work rather than as a regression. The
    # predecessor registers one (`slf4j-reload4j`), the reference registers
    # one (`logback-classic`), and this says the count did not move while the
    # identity did.
    svc = CAL["slf4j_provider_service"]
    binders = sorted({os.path.basename((e.get("o") or "").rsplit("!", 1)[0])
                      for e in b.art.entries()
                      if inside_artifact(e.get("o"))
                      and os.path.basename(e.get("o", "")) == svc})
    h.add("one-slf4j-binding", len(binders) == 1,
          "%d archive(s) inside %s register %s (%s)"
          % (len(binders), ART_BASE or "NONE", svc,
             ", ".join(binders) or "none"),
          1.0, {"archives": binders,
                "note": "slf4j binding %s" % (", ".join(binders) or "none")})
    return h.results



# ---------------------------------------------------------------------------
# What the tree arrived with, in six gates
# ---------------------------------------------------------------------------
#
# Sixty-five inherited checks, reduced to six rows carrying 7.5 between them.
#
# Sixty-five rows would be sixty-five weights, and at the ones steps 01 to 04
# gave them that is more than three times this rung's own work -- a candidate
# could leave the whole application on `javax.servlet` and still out-score one
# who moved it and let a single inherited idiom slip. What the weight has to
# buy instead is a tripwire: regressing an inherited property is a real and
# visible loss, and no pile of them is worth more than the step actually asks
# for.
#
# Six rows and not one, because the sixty-five are six kinds of thing and a
# single all-or-nothing row would price them as though they were one. Each row
# stays all-or-nothing within itself, which puts the burden on the detail --
# which is why the sub-checks are run as real gate_lib calls into a collector
# rather than reimplemented as booleans: each keeps its original step's
# predicate, scan and threshold, and the failure names which properties
# regressed.
#
#   inherited-properties-hold  1.0   ten facts about the build: the parent pom,
#                                    the test framework four ways, the mocking
#                                    library two ways. None of them moves when
#                                    a candidate renames a package.
#   inherited-idioms-hold      0.5   the five pre-Java-7 conversions. These are
#                                    the ones this rung's own work can nudge --
#                                    a file being edited import by import is a
#                                    file whose `try` blocks are being read --
#                                    and the measured direction of travel is
#                                    down on every old-form counter, so half a
#                                    gate is what a stray plain `try` costs.
#   inherited-libraries-hold   1.0   step 02's three library moves, in
#                                    fourteen checks. Two of the fourteen move
#                                    with this rung rather than being held
#                                    against it; see that function's docstring.
#   inherited-servlet-hold     1.0   step 02's servlet API coordinate, in two
#                                    checks -- three fewer than step 04 held,
#                                    because this is the rung that inverts
#                                    them. See that function's docstring.
#   inherited-di-hold          2.0   step 03's whole container rung, in
#                                    twenty-two checks, collected at the point
#                                    each is defined rather than re-run here.
#                                    Twice the lighter rows because it is the
#                                    subject the whole application is built out
#                                    of: the Guice retirement, the Spring
#                                    context, the Java configuration and the
#                                    `web.xml` deletion are what step 03 *was*.
#                                    It is also, at this rung, the row most
#                                    exposed to a half-finished rename: step
#                                    03's `container-scope` and `java-
#                                    configuration` read a context that a tree
#                                    with two servlet APIs on its path will
#                                    fail to start, and `spring-6-or-7` in it
#                                    carries the framework line that makes the
#                                    jakarta spelling possible at all.
#   inherited-boot-hold        2.0   step 04's whole deployable rung, in twelve
#                                    checks, collected the same way. Priced
#                                    with `inherited-di-hold` for the same
#                                    reason and with the same exposure: the
#                                    executable jar is what the 428 recorded
#                                    observations are served out of, and a
#                                    namespace migration that stops it being
#                                    produced has undone the step before it.
#                                    Two of its twelve are widened rather than
#                                    held verbatim; see that region's header.
#
# The thresholds are steps 01's, 02's, 03's and 04's, copied into CAL rather
# than imported. Each step's score.py is standalone -- it runs in its own
# image, with only this directory mounted -- and an import across steps would
# make this file unreadable without a file that is not there.

# ArtifactScan relativizes every origin against the target/ root it walked, so
# the test output reads "test-classes/Foo.class" with no leading slash. Step
# 01's floor, verbatim: an absence gate over test code has to be told the
# universe it must have searched, or a tree whose tests did not compile passes
# it by having nothing to find.
TESTS = (len(CAL["test_classes"]), "test-classes/")


def inherited_properties():
    """Step 01's ten properties that are facts about the build."""
    h = _Collector()

    G.no_parent(h, b, "no-oss-parent", ("org.sonatype.oss:oss-parent",))

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
    """Step 01's five pre-Java-7 conversions, on step 02's caps."""
    h = _Collector()
    INH = CAL["inherited"]

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


def inherited_libraries():
    """Step 02's three library moves, in fourteen checks.

    The two conjunctions step 02 had to write by hand are written by hand here
    too, and for the same reasons: `com.madgag:org.eclipse.jgit` carries
    upstream's artifactId under this project's own groupId, so neither half
    alone names the fork and a gate on the groupId reports the six reactor
    modules as the thing to remove; and a stale SSHD is an artifact at a
    version, which no prefix expresses at all.

    Two of the fourteen carry a version pin that moves at this rung:
    `no-jgit-before-7` and `jgit-upstream-7plus` were `...-5-13` in steps 02
    to 04. CAL["jgit_version_re"] gives the measurement -- there is no 6.x of
    `org.eclipse.jgit.http.server` that names `jakarta.servlet` and no 7.x
    that names `javax.servlet` -- and the consequence for this row is that
    the predecessor tree fails it. That is this rung's work showing up in a
    held row rather than a regression, and it is the same thing step 04's
    file did to step 02's `servlet-api-3`. The gate that does the real work
    here is `jgit-http-jakarta` in this rung's own section, which asks the
    property the version is only a way of reaching.
    """
    h = _Collector()

    def absent(name, pred):
        if b.deps_rc != 0:
            return G._deps_unusable(h, name, b, 1.0)
        return h.absent(name, sorted("%s:%s:%s" % t for t in G.deps(b)
                                     if pred(*t)), 1.0)

    def present(name, pred, minimum=1):
        if b.deps_rc != 0:
            return G._deps_unusable(h, name, b, 1.0)
        return h.present(name, sorted("%s:%s:%s" % t for t in G.deps(b)
                                      if pred(*t)), minimum, 1.0)

    absent("no-jgit-fork",
           lambda gid, aid, ver: (aid.startswith("org.eclipse.jgit")
                                  and gid != "org.eclipse.jgit"))
    absent("no-jgit-before-7",
           lambda gid, aid, ver: (gid == "org.eclipse.jgit"
                                  and not re.match(CAL["jgit_version_re"], ver)))
    G.dep_present(h, b, "jgit-upstream-7plus", group_prefixes=JGIT, minimum=2,
                  version_re=CAL["jgit_version_re"])
    G.uses_refs(h, b, "jgit-classes", JGIT, minimum=CAL["jgit_classes_min"],
                own=True)

    absent("no-sshd-0x",
           lambda gid, aid, ver: (gid.startswith(SSHD)
                                  and not re.match(CAL["sshd_version_re"], ver)))
    G.dep_present(h, b, "sshd-2x", group_prefixes=SSHD, minimum=1,
                  version_re=CAL["sshd_version_re"])
    present("sshd-core-2x",
            lambda gid, aid, ver: (gid.startswith(SSHD) and aid == "sshd-core"
                                   and re.match(CAL["sshd_version_re"], ver)
                                   is not None))
    G.uses_refs(h, b, "sshd-classes", SSHD, minimum=CAL["sshd_classes_min"],
                own=True)

    G.dep_absent(h, b, "no-mina-dep", group_prefixes=MINA)
    G.no_refs(h, b, "no-mina-classes", MINA, own=True, floor=CAL["own_floor"])

    G.dep_absent(h, b, "no-ssh-client-libs", group_prefixes=JSCH + SSHJ,
                 artifact_prefixes=("jsch", "sshj"))
    G.no_refs(h, b, "no-ssh-client-classes", JSCH + SSHJ, own=True,
              floor=CAL["own_floor"])
    G.no_source_types(h, b, "no-ssh-client-source", JSCH + SSHJ)

    G.dep_absent(h, b, "no-bc-jdk16", artifact_prefixes=("bcprov-jdk16",))
    return h.results


def inherited_servlet():
    """Step 02's servlet API: which coordinate, in two checks.

    Step 02 asked seven questions about the servlet API. Step 04 retired two
    of them when the embedded container became the source of the API, and
    kept five. This rung retires three more, and the reasoning belongs in
    this file rather than in a note somewhere:

      `servlet-api-3` asked that four Servlet 3.0+ marker types be packaged
      inside the deployable, in the `javax` spelling. The capability it was
      about is still required and is asked harder: `servlet-api-6`, in this
      rung's own section, wants the four markers in the jakarta spelling
      *and* the absence of the three types Servlet 6.0 removed, because
      Jakarta Servlet 5.0 has all four markers and is Servlet 4.0 renamed.
      Asked in step 02's spelling this check reports "4 of 4 marker types
      missing" against a tree serving 428 of 428 recorded requests -- which
      is measured, not projected: that is what it did on the first probe run
      of this rung's reference.

      `no-jakarta-servlet-source` and `no-jakarta-servlet-classes` were step
      04's guard against a candidate reaching forward past the rung it was
      on. This is that rung. Both invert, both carry real weight now, and
      both are `jakarta-servlet-source` / `no-javax-servlet-source` and their
      bytecode partners in the section below.

    What is left is the question step 02 was really asking under all seven:
    *which* servlet API coordinate this reactor declares. The answer at this
    rung, in the reference, is none at all -- `tomcat-embed-core` brings the
    API transitively and no pom names a servlet artifact. That is why both
    survivors are absence checks and why there is deliberately no positive
    coordinate check beside them: `jakarta.servlet:jakarta.servlet-api` is
    what a Jetty route resolves and what a Tomcat route does not, so a gate
    requiring it would fail a correct tree on a route §1 licenses.

      `no-tomcat-servlet-api` -- the standalone Tomcat servlet API jars step
      02 cleared out. `tomcat-embed-core` is not one of them and must not
      become one: it is the embedded container this task now runs on.

      `no-javax-servlet-dep` -- step 02's `servlet-api-provided` inverted
      twice. Step 02 required `javax.servlet:javax.servlet-api:3.1` at
      `provided` scope; step 04 retired that because there is no host to
      declare it against; here the coordinate is not merely unnecessary but
      wrong, and a tree that still declares it has a second, javax-spelled
      servlet API on the compile path beside the jakarta one the container
      supplies. Which of the two `HttpServlet` a class compiles against is
      then a function of declaration order.

    `no-pre-3-1-web-xml` is still not among them: step 03's `no-web-xml`, held
    one row up inside `inherited-di-hold`, is the stronger question, and a
    tree that passes it passes step 02's for having no descriptor at all.
    """
    h = _Collector()

    if b.deps_rc != 0:
        G._deps_unusable(h, "no-tomcat-servlet-api", b, 1.0)
        G._deps_unusable(h, "no-javax-servlet-dep", b, 1.0)
        return h.results

    h.absent("no-tomcat-servlet-api",
             sorted("%s:%s:%s" % t for t in G.deps(b)
                    if t[0] == "org.apache.tomcat"
                    and t[1] in ("servlet-api", "tomcat-servlet-api")), 1.0)
    h.absent("no-javax-servlet-dep",
             sorted("%s:%s:%s" % t for t in G.deps(b)
                    if t[0].startswith("javax.servlet")
                    or t[1].startswith("javax.servlet")), 1.0)
    return h.results


def held(name, checks, weight, noun="inherited"):
    """One row out of a collector: the count, the names, and the sub-gates.

    A sub-check that passed can still have something a reader of this row
    needs, and until this round it had no way to say it: the success text was
    `all N inherited checks hold` and nothing else, and `_Collector.add` does
    not journal sub-checks, so the only witness was `evidence.sub_gates` in
    `report.json`. The convention is that a sub-check may put one short string
    under `"note"` in its evidence, and every passing check's note is appended
    here. Capped like the regressed names are, for the same reason.

    The case this exists for is `servlet-api-packaged`, which this rung
    deliberately widened to count either spelling -- so a jar carrying both
    APIs passes it, and without the note `inherited-boot-hold` would say that
    it holds and nothing more about a deployable with two `HttpServlet`s in
    it. `no-javax-ee-apis-packaged` is still the row that fails that tree; the
    note is what makes the hold row stop being silent about why.
    """
    regressed = [r for r in checks if not r.ok]
    notes = [(r.evidence or {}).get("note") for r in checks if r.ok]
    notes = [n for n in notes if n]
    detail = ("%d of %d %s check(s) regressed: %s"
              % (len(regressed), len(checks), noun,
                 "; ".join("%s (%s)" % (r.name, r.detail)
                           for r in regressed[:6]))
              if regressed else
              "all %d %s checks hold" % (len(checks), noun))
    if notes:
        detail += "; " + "; ".join(notes[:4])
        if len(notes) > 4:
            detail += "; and %d further note(s)" % (len(notes) - 4)
    return g.add(name, not regressed, detail, weight,
                 {"checked": len(checks),
                  "regressed": [r.name for r in regressed],
                  "notes": notes,
                  "sub_gates": [r.as_dict() for r in checks]})


held("inherited-properties-hold", inherited_properties(), 1.0)
held("inherited-idioms-hold", inherited_idioms(), 0.5)
held("inherited-libraries-hold", inherited_libraries(), 1.0)
held("inherited-servlet-hold", inherited_servlet(), 1.0)
held("inherited-boot-hold", BOOT.results, 2.0)
held("inherited-jakarta-hold", JAK.results, 2.0)
held("inherited-logging-hold", LOG.results, 2.0)
# Step 07's fourteen, collected one rung later by the same rebinding.
held("inherited-legacy-hold", LEG.results, 2.0)
# Step 08's thirteen, collected one rung later by the same rebinding. Five of
# them took live thread dumps out of the running server on the way here, which
# is why this row can say "all 13 inherited checks hold" about a property no
# scan could see.
held("inherited-virtual-hold", VIRT.results, 2.0)
# The one collector that was filled as this file ran rather than by a function
# called here: `DI` collected step 03's twenty-two container gates at the point
# each is defined, up in the region between `_journal, g = g, DI` and
# `g = _journal`. Reported here with the other six.
held("inherited-di-hold", DI.results, 2.0)
# Step 09's thirteen: its ten idiom rows plus the three release rows that sat
# in the top-level floor block from step 01 to step 09. 36.5 of step 09's 59.5
# collapsed into 2.5 -- the largest single fold on this ladder, and the reason
# this rung's floor is 25.5 of 57.0 rather than most of the total.
held("inherited-java21-hold", IDIOM.results, 2.5)

# And the one row in this group that is not inherited at all: three properties
# this rung can break, collected by `logging_regression()` above. It is
# reported here rather than beside its own section because it is priced and
# read like the seven above it -- see that function's header.
held("logging-regression-hold", logging_regression(), 1.0, noun="regression")

print("%.6f" % S.finish(g, b.behaviour_pair(), {"calibration": CAL}))

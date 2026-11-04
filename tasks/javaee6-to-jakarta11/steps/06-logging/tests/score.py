"""Step 06 -- the logging stack: log4j 1.2.16 out, logback in behind the
slf4j facade the tree already had, with the audit log and the level map
arriving on the other side unchanged.

    reward = migration * behaviour

`behaviour` is the fraction of the 428 recorded observations the built artifact
reproduces, computed by run_behaviour.sh. Everything in *this* file is the
other factor.

The rung in one sentence: this tree logs through slf4j almost everywhere and
through log4j 1.2 in exactly one place, and that one place is the reason the
last end-of-life library on the classpath cannot simply be deleted.

Why it is a rung and not a dependency swap. Thirteen of this project's classes
already use slf4j, and for them the backend is an implementation detail --
swap the binding jar and they are done. `SshLog` is not one of them. It does
not log through a facade at all: it constructs a `DailyRollingFileAppender`,
gives it a hand-written `Layout`, points it at `<site>/logs/sshd_log`, wraps
it in an `AsyncAppender`, and pushes `LoggingEvent`s into it with the session
id, user name and timing set as properties. None of that has an slf4j
spelling, because slf4j is a facade for *emitting* records and this is code
that *configures a backend*. Every line of it has to be rewritten against
logback directly. That is the work, and it is why "replace the jar" is the
plausible wrong answer rather than the right one.

The near-miss this rung exists to refuse has a name: `org.slf4j:log4j-over-
slf4j`. It republishes `org.apache.log4j.Logger`, `.Level`, `.Layout` and the
appender classes as a facade over slf4j, so a candidate can swap
`log4j:log4j` for it and leave `SshLog.java` byte for byte untouched. The tree
compiles. It starts. It answers all 428 recorded observations. And the audit
log is never written again, because in that facade `addAppender` is a no-op
and `DailyRollingFileAppender` is an empty stub. Nothing in the source, in the
coordinates, or in the behaviour half can see it. `no-log4j-packaged` asks
about the *package* inside the deployable rather than about an artifact in the
graph, which is the one question the facade answers wrongly, and it is the
heaviest gate here for that reason.

What must survive, and is gated accordingly:

    the audit log            `<site>/logs/sshd_log`, same path, same line
                             grammar, still written -- `audit-log-intact`
    the level map            the root at DEBUG and the five per-logger levels
                             that still steer code inside the deployable --
                             `logging-levels-declared`, and its runtime
                             counterpart in `logging-regression-hold`
    the slf4j call sites     the fourteen classes that already log through
                             the facade keep logging through the facade

What must not survive: every remaining tie to log4j 1.2, including the two
string constants in `GerritGlobalConfiguration` that name a Velocity log
system. Those are not type references and no type-based gate sees them.
Measured from Velocity 1.6.4's own bytecode, `LogManager.createLogChute`
catches the failure and falls back silently, so leaving them costs a candidate
nothing anywhere else in this verifier. `velocity-log-not-log4j` is the only
row that can see it.

Eight of the thirteen per-logger levels in `log4j.properties` name libraries
steps 01 and 02 retired -- mina, jetty, mimeutil, commons-httpclient -- and
resolve to zero classes inside the deployable. This file asks for the six that
still steer something and says so, because a gate demanding all thirteen would
penalise a candidate for deleting configuration for code that is not there.

Nothing in the 428 observes logging: the audit log is read off the site
directory by this file, not by drive.py, and the console is not read at all.
So `must-flip.jsonl` gains no records at this rung and carries step 05's 123
unchanged. A correct tree scores 428 of 428, and so does the tree this step is
handed. Movement in the behaviour half here is a defect in the step.

What this file does *not* re-litigate: steps 01-05's gates are still evaluated
here and reported as seven rows. One is widened rather than inherited, and it
is the one this rung retires the spelling of: step 04's `one-log4j-provider`
asked for exactly one archive providing `org.apache.log4j`, which is the right
number for a tree whose own classes name the API and the wrong number for a
tree that has finished this step. Asked unchanged it would fail a correct
candidate -- an inherited test its successor's own requirements made
unpassable, which is known trap 9 and the thing a hold row is most likely to
hide.

Calibration, as in every step: each number is next to the measurement it came
from, and each was derived from the predecessor tree and this step's reference
solution rather than chosen to make a run pass. NOTES §11.27 records both.
"""


import os
import re

import gate_lib as G
import score_lib as S

# ---------------------------------------------------------------------------
# Calibration. "baseline" is step 02's solved tree -- what /app actually holds
# when this step starts, since the ladder is cumulative -- and "reference" is
# this step's own solution. Both measured with this verifier's own ArtifactScan
# and AstScan (NOTES §8.3.2).
# ---------------------------------------------------------------------------

CAL = {
    # Inherited from steps 01 and 02 and re-asserted here. Known trap 9 is a
    # step-local mean hiding an inherited test its own requirements made
    # unpassable, and this rung moves the toolchain out from under the whole
    # reactor -- so the assertion is made rather than assumed. The five test
    # classes and the nineteen tests are step 01's and are unchanged: this
    # rung touches no test source, and measured on the reference it still runs
    # 19 of 19 with 0 failures under JDK 17.
    "tests_min": 19,
    "test_classes": ("ConfigUtilTest", "SitePathsTest", "BasicSerializationTest",
                     "IdGeneratorTest", "SocketUtilTest"),

    # The toolchain, and the two independent ways of asking about it that
    # step 01 established: what the build was told (`maven.compiler.release`,
    # or `source`/`target`, which this ladder forbade at step 01) and what
    # came out (the class-file major version of every class the reactor
    # compiled). 17 and 61.
    #
    # This is the one number in this file a candidate cannot set by editing a
    # pom alone. The agent image installs three JDKs -- 11, 17 and 21 -- and
    # `update-alternatives` points at 11, so `mvn package` with
    # `release=17` under the default toolchain fails in javac with
    # "release version 17 not supported". Selecting 17 was step 05's work;
    # it is restated here because this rung moves the logging stack under the
    # whole reactor and an inherited toolchain is not assumed to survive.
    #
    # `class-release` is asked in `exactly` mode, which is step 01's shape and
    # is kept for step 01's reason: a tree may not overshoot. `release` fixes
    # the class-file version independently of the JDK that runs javac, so a
    # candidate who builds under JDK 21 with `release=17` still emits major 61
    # and passes -- which is correct, because the rung is about what the code
    # targets, not about which javac was on the path. What `exactly` refuses is
    # `release=21`, which emits 65. That is step 09, not this one, and a rung
    # that let a candidate take two at once would be scoring step 09 here.
    #
    # Measured at this rung: the tree this step is handed already compiles
    # 155 classes at major 61 with `release=17` -- step 05 moved it -- and the
    # reference compiles 155 at major 61 too. Both sides pass; the row is a
    # hold, not a leap.
    "release": "17",
    "class_major": 61,

    # Source census. This rung is source-neutral in the units this gate
    # counts: it renames imports and types inside files that already exist and
    # it creates and deletes nothing. Measured -- the tree this step is handed
    # has 110 files / 137 declared types, the reference has 110 / 137, and the
    # diff between them is 28 `javax.servlet` references becoming
    # `jakarta.servlet` across seven files plus three properties in the root
    # pom.
    #
    # So the floors are step 04's, unchanged at 105 / 132, and that is a
    # decision rather than an oversight. Step 04 derived them from its own
    # enumeration -- the two files that rung retires, less one unit of slack --
    # and they sit five files and five types below what a correct step-04 tree
    # holds. Re-anchoring them here to 110 / 137 would fail every candidate
    # who reached step 04's requirements by writing fewer classes than the
    # reference did, which step 04 explicitly licensed and which this rung has
    # no opinion about. A floor that tightens on a rung that deletes nothing
    # is a floor measuring the previous rung's taste.
    #
    # What the floor is for is unchanged: the tree that answers this rung by
    # deleting the httpd module rather than renaming its imports -- which is
    # where all 28 of the references live. That tree loses the module's 11
    # files and lands at 99, below the floor, in addition to failing every
    # HTTP observation in the behaviour suite.
    #
    # `sources_survive` counts what `AstScan` emitted over the whole
    # repository: every `.java` file under `/app` and every type declared in
    # one, test sources and nested types included. There is no main-only cut
    # on either number, so the counts above are whole-tree counts.
    # Round-1 migration review, 2026-10-05: 98 / 120 rather than the 105 /
    # 132 derived above. Those were the enumeration of the *reference's* step
    # 03 tree, but a tree that passes step 03 may legitimately hand over as
    # few as step 03's own floors, 100 files / 124 types (its configuration
    # classes are "a matter of taste"), and this rung licenses two deletions
    # and four types on top. A floor above that fails a correct chain for a
    # shape an earlier rung allowed. Logged as a DEVIATION in NOTES-authoring.
    # Round-2 review, 2026-10-05: the same arithmetic carried through the
    # rungs after step 04, which round 1 stopped short of. This rung's own
    # reference ports `SshLog` from four declared types to two (110 / 135
    # against the 110 / 137 it is handed), so a chain at the step-05 floor that
    # makes the same port hands over 98 / 118, and 120 would fail it.
    # Round-3 review, 2026-10-05: 117 rather than 118. This rung also licenses
    # a `PatternLayout` over MDC in place of `SshLog.MyLayout` ("the gate does
    # not ask which", the audit-record grammar below), and that port leaves `SshLog`
    # one type, not two -- 98 / 117 from the same chain. The deletion this
    # floor was written against (the httpd module, 11 files) is stopped by
    # every HTTP observation in the behaviour suite, not by this census.
    "source_files_min": 98,
    "source_types_min": 117,

    # Step 02's floor under the floor, verbatim, and for the same reason:
    # nothing in the pinned tree calls `CookieBase64` or `BlindSSLSocketFactory`
    # at all, so no recorded observation can depend on either, and
    # `sources-survive` counts a file whose methods have all been deleted.
    # Four and thirteen is what the unported 2011 tree declares and what both
    # this step's baseline and its reference still declare, because neither
    # class is injected into anything and this rung has no reason to open
    # either file.
    "orphan_types": (("com/google/gerrit/httpd/CookieBase64.java", 4),
                     ("com/google/gerrit/util/ssl/BlindSSLSocketFactory.java",
                      13)),

    # Suppressions may not grow. 31 is step 01's cap and has been every step's
    # since; the tree this step is handed sits at 18 and so does the
    # reference, because renaming an import introduces no cast. The cap stays
    # at 31 rather than dropping to 18 for the reason step 02 gives: driving
    # this number down is step 09's rung, and a candidate who needs one
    # @SuppressWarnings("removal") to get past a javac note on this rung has
    # not regressed anything.
    "suppressions_max": 31,

    # The universe every absence gate below has to have searched. 157 own
    # classes in the tree this step is handed and 157 in the reference: this
    # rung compiles the same types under a different toolchain. 130 is step
    # 04's floor and stays, for step 04's reason -- it is the count below
    # which "no class references com.google.inject" stops meaning anything --
    # and it is not re-anchored to 157 because a candidate is allowed to have
    # arrived here with fewer classes than the reference (see the source
    # census above, which makes the same argument in the same units).
    # Origins carry no leading slash (own_classes).
    "own_floor": (130, "classes/"),

    # The 6.x line, which is what step 03's `spring-5-3` becomes here. 5.3 was
    # the last Spring Framework train on `javax.servlet` and 6.0 is the first
    # jakarta-only one, so the gate that pinned this tree to 5.3 for three
    # rungs is exactly the gate this rung retires -- asked unchanged it fails
    # a correct tree, which is known trap 9 in its purest form.
    #
    # The whole major and not `^6\.2\.`: unlike 5.3, where the line boundary
    # carried the servlet namespace, every 6.x is jakarta and there is nothing
    # this rung wants that 6.0 does not have. Measured on the reference: 6.2.19,
    # which is what Boot 3.5.16 manages.
    #
    # There is deliberately no upper bound either. Spring 7 exists and is step
    # 10's; a candidate who reaches it early fails `spring-6` and that is the
    # right answer, so the regex is anchored at 6 rather than left open.
    "spring_version_re": r"^6\.",

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
    # What a floor of 9 does not refuse, said plainly because an earlier
    # version of this comment claimed the opposite: it does not refuse the
    # decorative tree. Nine is reached by annotating the seven surviving
    # qualifiers and writing one `@Configuration` class with a context
    # reference in it, and a tree that did exactly that and then wired the
    # object graph by hand behind a project-local registry passes this gate.
    # This count is a lower bound on *contact with the container* and was
    # never a measurement of how much of the graph moved.
    #
    # What refuses that tree is the conjunction, not this number, and round 3
    # measured it rather than asserting it: built for real, the hand-wired
    # tree took 38.5 of 43.5 and passed this gate with 55. To reach a
    # decorative pass now a candidate must also write three distinct
    # container types as types rather than as annotations
    # (`container-api-used`), a class that stands a context up
    # (`container-bootstrap`), an implementation of Spring's `Scope` and the
    # call that registers it (`container-scope`), three components on the
    # container's own lifecycle contracts (`container-lifecycle`) and fifteen
    # bean definitions in a tree that has a context to define them into
    # (`bean-definitions`) -- all of it dead code that nothing reads, which is
    # more work than porting the wiring and leaves a tree whose own classes
    # describe a container it never asked to build anything. Re-measured
    # after that change, the same tree took 34.5 of 45.5; re-measured again
    # after round 4 spliced `qualifier-annotations` in, which it passes
    # because the seven marks are exactly the decoration it does do, it takes
    # 35.5 of 46.5.
    #
    # And the honest limit: no gate in this file can prove that a context was
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
    # no `@Bean` method anywhere. The count is reported in the evidence for the
    # author's benefit and gated on by nothing.
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
    # The two widenings close false negatives that the first version of this
    # gate had and the reference itself tripped over. `FactoryBean` is a
    # producer class by any reading and CAL recommends it eight entries up.
    # Inheritance matters because a producer whose subclass is the bean is
    # still one class per produced value: this reference's
    # `HttpCanonicalWebUrlProvider` extends the `Supplier` that
    # `CanonicalWebUrlProvider` implements and declares no clause of its own,
    # and the old gate did not see it.
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
    # What it does not refuse, written down here because round 3 measured it
    # and the honest thing is to say so: ten empty
    # `class DummyN implements Supplier<String>` types under `src/main/java`
    # pass this gate. Intersecting with the types a configuration actually
    # declares as beans was tried and rejected -- this reference's own
    # producers carry no stereotype and reach the container through `@Import`
    # and through `new` inside a `@Bean` body, so the intersection tight
    # enough to refuse the dummies is the one that pins this reference's
    # exact shape, and a gate only the reference passes is worse than the one
    # it replaces. So this stays a census of a construct, at 1.0, and it is
    # *not* part of what refuses a tree that only looks Spring-shaped. The
    # five floors below are.
    #
    # Production source only, both the direct pass and the subclass closure:
    # `b.ast.kind("type")` scans `src/test/java` too, and a producer written
    # in a fixture is a fixture.
    "provider_classes_min": 10,

    # §1.3's second bullet keeps seven of the eight Guice binding annotations
    # and retires the eighth. The seven are named here by the names §1.3
    # itself prints, which leaks nothing: the instruction lists all seven in
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
    # Five floors added after a review pointed out what the gates above do not
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
    # candidate. The round-2 versions of the first four had one route each,
    # and NOTES §8.3.15 records what each of them let through.
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
    #          five routes above were all written against a tree that builds
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
    #    descriptor and creates nothing. The round-2 version of this gate
    #    counted any constant-pool reference to any of nine types, three of
    #    which were interfaces, with a floor of 1 -- so a tree whose object
    #    graph is built with `new` behind a project-local registry passed it
    #    on the strength of a `@Scope` attribute. Naming a context is not
    #    creating one.
    #
    #    `bootstrap_types` therefore serves A2 and A3 only, and is matched
    #    with `_under`, which is exact-FQN-or-below. It keeps the interfaces
    #    (harmless: an interface is never a superclass, and a string constant
    #    naming one is not a way in that costs anything) and gains Spring's
    #    four initializer base classes, which the round-2 list did not have.
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
        # application is not a web one, and it was already here.
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
    #          a stereotype. The owner requirement is Spring's own rule, not
    #          an invention: a `@Bean` method on a class the container never
    #          receives declares nothing. The round-2 version counted `@Bean`
    #          methods wherever they were written.
    #      R2  a type carrying a stereotype -- but only when production source
    #          switches component scanning on somewhere: a `@ComponentScan`,
    #          a `ClassPathBeanDefinitionScanner`, or a `scan(...)` call in a
    #          file that also names a context implementation. Fifteen
    #          `@Named` classes and no scan is fifteen annotations, and the
    #          round-2 version passed that tree outright against a floor
    #          of 15.
    #      R3  a functional registration site: a call to `registerBean`,
    #          `registerBeanDefinition` or `registerSingleton`, or a use of
    #          one of the definition builders and readers. This is the route
    #          the round-2 version had no answer for at all: a correct Spring
    #          5 port that builds a `GenericApplicationContext` and calls
    #          `registerBean` forty times scored 0.
    #      R4  a class named in an `@Import`. Each one is a definition the
    #          importing class hands to the container, and `@Import` is how a
    #          port that keeps its configuration in several classes assembles
    #          them.
    #
    #    The honest limit, because it is the one thing this gate cannot see:
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
    #          `InitializingBean` and `DisposableBean`, and -- added in round
    #          3 -- `ApplicationListener` and `SmartInitializingSingleton`.
    #          An `ApplicationListener<ContextRefreshedEvent>` that starts the
    #          SSH daemon when the context comes up is doing exactly the work
    #          `LifecycleManager` did, and the round-2 list did not have it.
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
    #    The honest limit, stated here and in the docstring because it is not
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
    #    argument. Nothing measured that until round 3; §1.5's promise that
    #    the surviving factories are counted was a promise nothing kept.
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
    # with this verifier's own AstScan (NOTES §8.3.2): diamond 34,
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
    # cannot move to 3.x and keep them. `spring_version_re` (`^6\.`) says the
    # same thing one layer down and is asked inside `inherited-di-hold`.
    #
    # Any 3.x passes rather than 3.5.16 exactly, for `spring_version_re`'s
    # reason: a release inside the line is the same line.
    #
    # Measured: the reference resolves six coordinates, all 3.5.16
    # (spring-boot, spring-boot-autoconfigure, spring-boot-starter,
    # spring-boot-starter-json, spring-boot-starter-tomcat,
    # spring-boot-starter-web); the predecessor tree resolves the same six at
    # 2.7.18 and so fails this pin, which is the point of it.
    "boot_version_re": r"^3\.",

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
    # Step 04 does leave the choice open -- a step-04 blind run took the Jetty
    # route and scored 428/428 there -- so a tree can arrive at this rung on
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
    # only on the container the observations were recorded from. The
    # measurement is written up in TRAJECTORY-step05-round1.md.
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
    # 3 was the wrong number: §1.4 says *every* servlet and filter the
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
    # types instead: seven is the figure the ticket itself printed, so a gate
    # that asked for six would be waving through a file the instruction says
    # must name the API. Flooring a measurement at its own value is a hidden
    # line and the revision before this one did exactly that -- 10 pairs on a
    # reference that has 10 pairs; flooring a published requirement at its own
    # value is not a line at all, it is the requirement.
    #
    # The unit is the file for the same reason, and a raw site floor could not
    # be set honestly at all. `RequireSslFilter` and `RequestCleanupFilter`
    # each import 6 types from `jakarta.servlet`, and IntelliJ's
    # organize-imports collapses a package to `.*` at 5 by default: run over
    # this reference it turns 6+6 into 1+1 and reports **18** raw sites, under
    # the 20 an earlier revision of this key asked for, and a full collapse of
    # all 7 files reports **10**. So the candidate who did this rung exactly
    # right and then let the IDE tidy the imports it had just made them
    # rewrite lost a 3.0 gate. The file count is **7** under all three -- 28
    # raw sites, 18 after a default tidy, 10 after a full collapse -- which is
    # §1.1's "whether you write one import per type or collapse a package to a
    # wildcard is your business; which files name the API is not", measured.
    #
    # A (file, package) pair count is 10 on this reference and survives the
    # wildcard too, and this key was that for one revision. It is not the unit
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
    # `javax.annotation` is deliberately NOT in this set, and the reason was
    # measured rather than reasoned: `com.google.code.findbugs:jsr305:1.3.9`
    # packages 35 classes under `javax.annotation` -- `@Nullable`,
    # `@Nonnull`, `@CheckReturnValue` and the rest of JSR-305 -- and it is
    # inside both the predecessor jar and the reference jar, because
    # `mini-git-server-server` and `mini-git-server-httpd` each declare it
    # outright and the parent manages its version. It is not something JGit
    # drags in: `org.eclipse.jgit-7.8.0`'s own pom names no jsr305 at all, so
    # dropping the coordinate would mean deleting a declared dependency two
    # modules compile against. JSR-305 is not an EE API, has no
    # jakarta spelling and never will have one. A set that included
    # `javax.annotation` would fail the correct reference, which is the
    # failure mode this file's own trap list calls a gate whose finding is an
    # artefact of its instrument.
    #
    # Measured inside the deployable, by package and providing archive:
    #
    #   predecessor  javax.servlet 91 (tomcat-embed-core-9.0.83)
    #                javax.el 56 (tomcat-embed-el-9.0.83)
    #                javax.websocket 56 (tomcat-embed-websocket-9.0.83)
    #                javax.security.auth.message 34 (tomcat-embed-core-9.0.83)
    #                javax.annotation 50 (jsr305 35 + jakarta.annotation-api
    #                                     1.3.5's 15 javax-packaged classes)
    #   reference    all five javax packages above: 0
    #                javax.annotation 35, jsr305 alone
    #                jakarta.servlet 88, jakarta.el 52,
    #                jakarta.websocket 55, jakarta.annotation 17,
    #                jakarta.security.auth.message 34
    #
    # So the predecessor fails on four packages and the reference is clean on
    # all of them, with `javax.annotation` the one line that would have
    # inverted the result. The set is wider than the four the two trees
    # differ on: `javax.persistence`, `javax.validation`, `javax.transaction`,
    # `javax.inject` and `javax.enterprise` are zero in both trees and are
    # named anyway, because a candidate who reaches for a jakarta API this
    # project does not currently use should not be able to bring its javax
    # twin along beside it.
    "javax_ee_packages": ("javax.servlet", "javax.el", "javax.websocket",
                          "javax.security.auth.message", "javax.persistence",
                          "javax.validation", "javax.transaction",
                          "javax.inject", "javax.enterprise"),

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
    # Round-1 migration review, 2026-10-05: 15 rather than 18. 18 is the
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
    # one scored run of it. NOTES §11.27 records each measurement beside
    # the reasoning it fed.
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
    # deployable. Thirteen were declared; eight name libraries steps 01 and 02
    # retired and resolve to zero classes in the jar. The root is keyed on the
    # empty string because logback spells it `<root>` and Boot's property form
    # spells it `logging.level.root`, and neither spelling belongs in a
    # comparison. Class counts, measured: com.google.gerrit 150,
    # org.apache.sshd.common 742, org.apache.sshd.server 159,
    # FileKeyPairProvider 1, net.sf.ehcache 169.
    "log_levels": {
        "": "DEBUG",
        "com.google.gerrit": "INFO",
        "org.apache.sshd.common": "WARN",
        "org.apache.sshd.server": "INFO",
        "org.apache.sshd.common.keyprovider.FileKeyPairProvider": "INFO",
        "net.sf.ehcache": "WARN",
    },

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
    # Round-1 review: the two spellings that make "this appender drops
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
    # Volume alone was the first draft of this and was wrong. Measured on the
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
    # convert. Measured: fourteen own production classes reference
    # `org.slf4j`, from thirteen source files -- `WorkQueue$1` is the
    # fourteenth. `SshLog` is not among them before this rung and is expected
    # to join them after it, so the floor is the before-count and the gate is
    # a floor rather than an equality.
    "slf4j_classes_min": 14,

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

# Step 03's whole rung, held as one row here rather than as twenty-two, and
# step 04's whole rung, held as one row rather than as twelve. The gates
# between each pair of rebindings below are those steps', unchanged, and `g`
# is rebound around them so that each one writes into its collector instead of
# into the journal. Unchanged is the point: a hold that re-implemented its
# predecessor's question as a simpler boolean would be a different question
# wearing the same name.
#
# `BOOT` is new at this rung and is the rule rather than an exception: step 03
# collapsed step 02's rung, step 04 collapsed step 03's, and a step that left
# its predecessor's rung at full weight would be the anomaly. It is also what
# keeps the arithmetic honest. Step 04's twelve rung gates carry 20.5 between
# them; carried forward whole they would make a tree that had finished step 04
# and touched nothing else score most of this rung's total, and the floor
# measurement -- score step 04's finished tree with this verifier -- is the
# number that says whether a rung is testing its own leap.
DI = _Collector()
BOOT = _Collector()
JAK = _Collector()


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
# How the 50.5 is split, in three columns and not two
#
# NOTE (round-2 review): the paragraphs below were carried over from step 05
# and describe *that* rung's 40.5 over 24 gates. They are kept for the shape
# of the argument, which is unchanged; the numbers that govern THIS rung are
# 50.5 over 28 gates -- 25.5 for the log4j-to-logback leap, 3.0 for
# `audit-log-intact`, 1.0 for `logging-regression-hold`, 11.5 for the nine
# common gates and 9.5 for the seven holds -- and the measured floor is
# 25.0/50.5 = 0.495050 at 428/428 behaviour, not 0.305238. Where a sentence
# below says 40.5, 24 gates, `jdk-javax-intact` or 0.305238, read it as step
# 05's accounting quoted for comparison.
#
# Same accounting as steps 03's and 04's, and this is the rung where the third
# column finally has something in it for the reason the column exists. Step
# 03's column (b) held four gates that the predecessor passed because they
# refuse overshoot rather than reward progress; step 04's was empty and said
# so. Here it holds one row, `jdk-javax-intact`, which a correct tree and the
# handed tree both pass and which exists entirely to refuse a particular wrong
# answer. A rung whose visible content is a string rename needs at least one
# gate that fires on the rename being done too widely, and that gate is by
# construction one the starting tree already satisfies.
#
# Measured the same way, by running the predecessor tree -- step 04's finished
# one, untouched, which is exactly what `/app` holds when this step starts --
# and partitioning the journal by whether each gate passed and by which
# section of this file defines it.
#
#   (a) 19.5   rung gates the predecessor FAILS, over 8 gates. The
#              discriminating leap. `no-javax-ee-apis-packaged` 4.0,
#              `jakarta-servlet-source` 3.0,
#              `no-javax-servlet-source` 3.0, `servlet-api-6` 2.5,
#              `jgit-http-jakarta` 2.5,
#              `jakarta-servlet-classes` 1.5, `no-javax-servlet-classes` 1.5,
#              `jakarta-servlet-api-packaged` 1.5.
#
#   (b)  2.0   rung gates the predecessor PASSES: `jdk-javax-intact` 2.0,
#              alone. Eighteen source sites under `javax.xml` and `javax.net`
#              that have no jakarta spelling and never will, present in both
#              trees, asked out loud so that the tree in between them is
#              refused. See its own comment for why it is priced with the
#              leaps rather than with the tripwires.
#
#   (c) 19.0   inherited gates, over 15, of which the predecessor passes 11.0
#              over 10 and fails 8.0 over 5. The five it fails are the most
#              informative rows in this file and none of them is a
#              regression: `class-release` 2.0 and `compiler-release` 1.0
#              fail because this rung's `release` is 17 and step 04's tree is
#              Java 11 -- the same tree, the same build, a different question
#              about the output. `inherited-libraries-hold` 1.0 fails on
#              `no-jgit-before-7` and `jgit-upstream-7plus`,
#              `inherited-boot-hold` 2.0 on `spring-boot-dep`'s `^3\.`, and
#              `inherited-di-hold` 2.0 on `spring-6` and `no-spring-off-6`.
#              In each case a check
#              inherited from an earlier step carries a version pin that this
#              rung moves, which is the same thing step 04's file did to step
#              02's `servlet-api-3` and is recorded there in the same words.
#              Everything else inherited passes: the floor (`source-parses`,
#              `sources-survive`, `orphan-types-intact`,
#              `build-and-artifact`), step 01's remaining re-assertions
#              (`no-jdk-internals`, `tests-ship-and-pass`, `suppressions`)
#              and three of the six hold rows.
#
#   ---- 40.5 over 24 gates.
#
# Measured, not projected: step 04's finished tree with its `target/` removed,
# which is what `/app` holds when this step starts, reports **13.0 of 40.5 =
# 0.320988** under this file, 407 of 428 behaviour, for a reward of
# **0.305238**. The reference reports 40.5 of 40.5 and 428 of 428, for
# 1.000000. Both runs are `mgs-verif-step05:p4`, the same image, and the logs
# are `/work/logs-s05/p4-s04floor` and `/work/logs-s05/p4-s05ref`.
#
# The floor rule asks for strictly below 0.5 and it lands at 0.305238. No
# weight here was set by looking at that arithmetic; each is set against the
# others in this file. The reprice of `no-javax-ee-apis-packaged` from 2.0 to
# 4.0 moved the floor down rather than up, because the predecessor is one of
# the trees that row refuses: its 13.0 of passing weight did not change and
# the denominator grew.
#
# What is different about this floor is worth saying, because it is the first
# one in this task where the migration number is doing the whole job. At steps
# 03 and 04 the predecessor produced no deployable the verifier could start,
# so behaviour was 0 of 428 and the reward was zero however the gates came
# out. Here the predecessor builds, starts, binds, and answers 407 of the 428
# recorded observations -- it is a working git server, and the twenty-one it
# misses are the ones this rung's own recording changed. So 0.305238 is a
# real number rather than a zero with an explanation, and the distance between
# it and 1.0 is exactly the work §1.1 to §1.4 name.
#
# The organising question for column (a) is which rows a tree can satisfy
# without having moved platform. It is the sharpest it has been at any rung,
# because a rename is the one migration that can be faked convincingly: `sed
# -i 's/javax\.servlet/jakarta.servlet/g'` over `src/` satisfies the two
# source rows outright, and if it also happens to compile -- which it will
# not, but a candidate who then adds `jakarta.servlet:jakarta.servlet-api` by
# hand can make it -- it satisfies the two bytecode rows and one of the
# packaged rows as well. That tree has 7.5 of the section's 21.5 and is
# running Spring 5.3 on Tomcat 9 with two servlet APIs on its classpath. The
# rows that refuse it are the ones that read the platform rather than the
# spelling, and they are the four worth defending individually:
#
#   `jakarta-servlet-source` 3.0 and `no-javax-servlet-source` 3.0 are the
#   heaviest of the source rows, and they are two rows and not one for the
#   reason their own comment gives: the failure modes are different and a
#   candidate should be told which one was hit. Together they are 6.0 of
#   21.5, which is right for a milestone whose title is the rename -- a tree
#   that has not done it has not done the step, whatever else it moved. They
#   are priced above the two 2.5 platform rows and not below because those
#   rows are reachable by a version bump and this one is not: the servlet
#   imports across seven files are the only part of this rung that is edits
#   to this repository's own source.
#
#   `servlet-api-6` 2.5. The row that separates Jakarta Servlet 5.0 from
#   Servlet 6.0, and therefore the row that separates "renamed the imports
#   and stayed on Boot 2.7" from "moved the platform". Its own docstring
#   carries the measurement. It is priced level with `jgit-http-jakarta` and
#   below the source pair because a tree that reaches it has usually reached
#   it by having done §1.2, which is itself gated in the held rows.
#
#   `jgit-http-jakarta` 2.5. The one row in this file that is about a
#   dependency's own compilation rather than about this project's, and the
#   only row whose failure is invisible from inside the repository: this
#   tree's source names no JGit servlet type, so a tree that leaves JGit on
#   6.x compiles, starts, serves `/ssh_info`, and fails every git-over-HTTP
#   observation with a `NoClassDefFoundError` naming a class the candidate
#   has no source line for. Priced with `servlet-api-6` because it is the
#   same kind of claim -- a packaged artifact is on the new namespace -- and
#   because between them they are what the behaviour half of the reward is
#   actually measuring when it moves from 407 to 428.
#
#   `no-javax-ee-apis-packaged` 4.0, the heaviest row in the rung, and the
#   only weight in this file that was raised rather than argued down. It is
#   the jar-level statement of the whole rung: not "the source says jakarta"
#   and not "this project's classes say jakarta", but that the deployable
#   carries one servlet namespace and it is the new one. It is the row that
#   catches the additive migration -- a candidate who adds the jakarta API
#   beside the javax one it already had passes `jakarta-servlet-api-packaged`
#   and ships two `HttpServlet`s in one archive, and which of them a class
#   loads is classpath order, which is to say it is not decided at all.
#
#   It was 2.0 for a round, priced as a consequence of the platform rows
#   because moving to Boot 3 removes the javax APIs without anyone asking.
#   That reasoning prices the row against the tree that reaches it honestly,
#   which is the wrong tree to price a gate against: the tree this row exists
#   for is the one that ships both, and at 2.0 that tree took 36.5 of 38.5
#   for ~0.95 migration while §1.3 names it as this rung's disaster by name.
#   5% is not what a deployable with two `HttpServlet` classes in it is worth.
#   At 4.0 it costs ~0.10, which is still generous for a tree whose behaviour
#   is decided by classloader order, and the price is paid only by a tree that
#   did the rename everywhere except in the one place the application actually
#   runs from. Nothing else in this file moved to pay for it; the total went
#   from 38.5 to 40.5 and the floor fell from 0.321095 to 0.305238, because
#   the predecessor is one of the trees this row refuses.
#
# The three rows at 1.5 come down together for one reason: each of
# `jakarta-servlet-classes`, `no-javax-servlet-classes` and
# `jakarta-servlet-api-packaged` is a second reading of a fact another row has
# already asked for. The bytecode pair corroborates the source pair, and
# `jakarta-servlet-api-packaged` corroborates `servlet-api-6`, which cannot
# pass without it. Corroboration is worth real weight here, because the two
# readings genuinely come apart -- a file that imports the new package and
# then uses an inherited simple name compiles against whichever API won, and
# the constant pool is the only witness to which -- but a row that is usually
# implied by a heavier row beside it should not be priced with it.
#
# `no-javax-servlet-classes` is 1.5 and not more for a second reason, which is
# that it is the weakest of the four as an instrument: it is `own=True`, so it
# reads only this project's compiled output and not the jar, because the jar
# legitimately contains three `javax/servlet` references from velocity-1.6.4
# and ehcache-core-1.7.2 that no release of either removes. The gate's own
# comment carries that measurement. What covers the jar instead is
# `no-javax-ee-apis-packaged`, which is why that one is 4.0.
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
# touches every constructor in the tree, and the cheapest way to make a test
# that no longer compiles stop failing is to stop shipping it.
# ---------------------------------------------------------------------------

G.class_release(g, b, CAL["class_major"], weight=2.0, mode="exactly")
G.compiler_release(g, b, CAL["release"], weight=1.0)
G.jdk_internals(g, b, weight=1.0)
G.tests_ship_and_pass(g, b, CAL["tests_min"], CAL["test_classes"], weight=2.0)
G.suppressions_at_most(g, b, CAL["suppressions_max"], weight=1.0)


# ---------------------------------------------------------------------------
# Step 03's rung, held (start of the collected region)
# ---------------------------------------------------------------------------
#
# Everything from here to the matching `g = _journal` below is step 03's
# file, verbatim, less its `no-spring-boot` gate -- which this step exists
# to make unpassable, and which is trap 9 in its purest form: an inherited
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
# a fifth spelling of the same cut is how the three gates round 3 found
# counting the test tree came to be counting it.
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

    The distinction this draws is the one round 3's Blocker turned on. A tree
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
    `AnnotationExpr`. Two consequences, both verified on this reference
    rather than assumed (NOTES 8.3.15):

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
          three on its own, which was measured before it was fixed
          (NOTES 8.3.15).
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
    never through a fully-qualified spelling) and 21 by U3. Baseline 0; the
    hand-wired tree of NOTES 8.3.15 zero on all three routes, with 55 of its
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
    Round 2 asked each of those three on its own, and the hand-wired tree of
    NOTES 8.3.15 -- whose graph is built with `new` behind a project-local
    registry -- took 4.0 of them on dead annotations while failing every gate
    that asked for the container itself.

    Five routes. The first three are the ones the gate used to hold inline;
    the last two are this rung's, because the first three were all written
    against a tree whose context is built by naming a context class, and an
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

    A4 and A5 are the fix for a real hole, and it is worth saying what the
    hole was. Before them, an entry point that wrote `SpringApplication`,
    `SpringApplicationBuilder` and a held `ConfigurableApplicationContext` --
    which is every shape §1.7 licenses -- cleared Half A only by accident, on
    the strength of an unrelated `main` elsewhere in the tree that still said
    `new AnnotationConfigApplicationContext(...)`. A port that tidied that
    `main` away, as this rung invites, would have failed four gates for
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

    The detail text of a passing tree is `Gates.present`'s exactly, so the
    counts round 3 was asked not to move do not move. A tree that has the
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
# Measured: the reference resolves eight, all 6.2.19 -- `spring-aop`,
# `spring-beans`, `spring-context`, `spring-core`, `spring-expression`,
# `spring-jcl`, `spring-web`, `spring-webmvc` -- so both names and the count.
# The tree this step is handed resolves the same eight artifacts at 5.3.31,
# which is what Boot 2.7.18 manages; it fails this gate on the version and on
# nothing else, which is the point of asking presence and line together in one
# row rather than counting artifacts here and versions somewhere else.
G.dep_present(g, b, "spring-6", group_prefixes=SPRING, minimum=3, weight=2.0,
              version_re=CAL["spring_version_re"],
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
dep_gate_absent("no-spring-off-6",
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
# What it does not ask, stated here because round 2's comment claimed it did:
# a constant-pool reference is not use. ASM cannot separate an annotation
# descriptor from a type in the path of an instruction -- both land in `r` --
# so a tree that annotates 55 hand-wired classes `@Configuration`, `@Bean` and
# `@Named` and builds its object graph with `new` passes this gate honestly,
# on 55 classes. That is the correct answer to the question this gate asks and
# the wrong answer to the rung, which is why `container-api-used` below asks
# the narrower question over the AST, where annotations and written types are
# different records. This gate stays as it is: a cheap, stable floor that no
# port of §1.3 can miss, whose limit is now written down.
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
    the old container under a new dependency, not a new container." Until
    round 3 nothing measured it. `spring-classes` counts constant-pool
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
    files, 0 U2, 21 U3); the hand-wired tree of NOTES 8.3.15 zero on all three
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

    Production source only. A `@Configuration` class under `src/test/java` is
    an ordinary thing for a Spring test to need, and this gate is worth 2.0
    against a floor of 1 -- so before round 3 a tree that left the production
    graph wired by hand and wrote one test fixture took the whole 2.0.

    Two independent routes have to meet, which is round 3's change here. The
    floor of 1 made the annotation half free: one `@Configuration` class on a
    hand-wired tree took the whole 2.0, and a `@Configuration` class no
    container ever receives configures nothing. So the count is now read
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

    The honest limit, and it is a real one: this counts a type with the right
    `implements` clause, and nothing more. Ten empty
    `class DummyN implements Supplier<String>` types under `src/main/java`
    pass it. The obvious tightening -- intersect with the types some
    configuration actually declares as beans -- was tried and rejected,
    because it does not hold on a correct port: this reference's own
    producers carry no stereotype and reach the container through `@Import`
    and through `new` inside a `@Bean` body, so an intersection tight enough
    to refuse the dummies would have pinned the reference's exact shape, and
    a gate only the reference passes is worse than the one it replaces. So
    this gate is a census of a construct, it is worth 1.0, and it is not part
    of what refuses a decorative tree -- `container-api-used`,
    `container-bootstrap` and `bean-definitions` are. The CAL block says the
    same thing, and NOTES §8.3.15 records the measurement.

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
# the CAL block for each floor's derivation and for the honest limit: none of
# them can prove a context was ever refreshed.
# ---------------------------------------------------------------------------


def container_bootstrap():
    """Something in this project stands a context up, and something drives it.

    `spring-6` says the container resolves and `spring-classes` says this
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

    A4 and A5 are why this gate no longer depends on an unrelated `main`.
    An application that starts its own container writes the thing that builds
    the context rather than the context; the three routes above it were all
    written against a tree where those are the same sentence, and in this rung
    they are not.

    Naming a context interface is deliberately *not* a route.
    `@Scope(WebApplicationContext.SCOPE_REQUEST)` names one and creates
    nothing; so does a method declared
    `install(ServletContext, WebApplicationContext)`. The round-2 version of
    this gate counted any constant-pool reference to any of nine types, three
    of them interfaces, with a floor of 1 -- and so the hand-wired tree passed
    it on the strength of a `@Scope` attribute (NOTES §8.3.15).

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

    Weight 2.0, raised from 1.0 in round 3 and on merits rather than to make
    a number come out: this is the positive half of `no-guice-classes`, which
    is worth 2.0 for deleting the old container, and the two ask the same
    question from the two sides.

    The honest limit: a context that is built and refreshed on a code path
    nothing reaches passes this. Whether the container is the thing actually
    serving the requests is a behavioural question, and the 428 observations
    are where it is asked.

    Measured: the hand-wired tree A=0 B=0; reference A=4 B=13; the tree this
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
    until round 2 nothing asked where they went -- and the round-2 answer
    counted `@Bean` methods and stereotyped types wherever they were written,
    which §1.4 does not say. It says the container is configured "from Java,
    not from XML". It does not say "from annotations".

    Four routes, summed, because four shapes are all real ports of a `bind`:

      R1  a `@Bean` method whose *owner type* carries `@Configuration` or a
          stereotype. The owner requirement is Spring's own rule: a `@Bean`
          method on a class no container ever receives declares nothing.
      R2  a type carrying a stereotype -- but only when production source
          switches scanning on: a `@ComponentScan`, a
          `ClassPathBeanDefinitionScanner`, or a `scan(...)` call in a file
          that also names a context implementation. Fifteen `@Named` classes
          and no scan is fifteen annotations, and the round-2 gate passed
          that tree outright against this same floor of 15.
      R3  a functional registration: a call to `registerBean`,
          `registerBeanDefinition` or `registerSingleton`, or a use of one of
          the definition builders and readers. This is the route the round-2
          gate had no answer for at all -- a correct Spring 5 port that
          builds a `GenericApplicationContext` and calls `registerBean` forty
          times scored 0 on it.
      R4  a class named in an `@Import`. Each is a definition the importing
          class hands to the container, and `@Import` is how a port that
          keeps its configuration in several classes assembles them.

    `@Named` counts among the stereotypes for CONTAINER_API's reason: 5.3
    reads it as one natively. An R1 method, an R2 type, an R3 call site and
    an R4 imported class cannot be the same record, so the sum
    double-counts nothing; R4 is deduplicated on the imported name, so a
    class imported by two configurations is one definition.

    The honest limit: switching scanning on and writing `@Bean` methods are
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
          `InitializingBean` and `DisposableBean`, and -- added in round 3 --
          `ApplicationListener` and `SmartInitializingSingleton`. An
          `ApplicationListener<ContextRefreshedEvent>` that starts the daemon
          when the context comes up is doing exactly what `LifecycleManager`
          did, and the round-2 list did not have it.
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

    The unit is one managed object, not one site, and round 3 rewrote the
    keys to make that true rather than claimed:

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
        object named twice, and the round-2 keys counted it twice. The return
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

    The honest limit, which is not fixable from a static scan and is written
    down rather than papered over: a `new Scope(){}` whose six methods all
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
    where the caller already holds every argument. §1.5 promised the
    survivors were counted. Until round 3 nothing counted them, and an
    instruction that promises a gate nothing measures is worse than no
    sentence at all.

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

    The honest limit: a single `@Scope("prototype")` on a class nothing asks
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
# Parsed and not grepped (rule 11), and matched on the root element rather than
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

    A split in this row's own detail was not enough for that, because `held`
    reports the collector and not its members: on the success path its whole
    message was the count of checks that held. So when the javax count is
    non-zero this sets `evidence["note"]`, which `held` appends to the hold
    row's message -- naming the count and the archives it is in, which is
    what a candidate has to act on. A correct tree has javax 0 and sets no
    note, so the hold row reads exactly as it did before.
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

# Three coordinates at least, and `spring-boot` among them by name, on the 3.x
# line -- `boot_version_re` is this step's, not step 04's. CAL says why each of
# those three numbers is what it is. Asked of the
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
    which is known trap 9 and precisely the failure a hold row hides, since
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
# that way this gate fails the reference. Measured over all 16260 classes
# inside the reference jar, three still name `javax/servlet` in their constant
# pool: two in `velocity-1.6.4.jar` and one in `ehcache-core-1.7.2.jar`, both
# 2011-era dependencies this project still uses and neither of which has a
# release that does not. They are optional servlet integrations inside
# libraries used here for templating and caching; nothing loads them, and
# nothing can, because the classes they reference are absent. A gate that
# failed the reference for them would be measuring the age of Velocity rather
# than the state of this migration. What the rung requires, and what this row
# asks, is that no class *this project compiled* names the old API.
#
# The deployable is not left unasked: `no-javax-ee-apis-packaged` below reads
# every archive inside the jar for classes that *provide* the javax APIs,
# which is the question that matters about a dependency -- a stale servlet API
# on the path is a second `HttpServlet` for the loader to choose between,
# whereas an unreachable reference to one is a dead branch.
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

    CAL["javax_ee_packages"] carries the set and the measurement, including
    the one package that is deliberately not in it -- `javax.annotation`,
    which `jsr305-1.3.9.jar` provides 35 classes of in both the predecessor
    jar and the reference jar, and which has no jakarta spelling because
    JSR-305 is not an EE API.
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
# library you cannot recompile keeps linking. Measured (authoring calibration
# 2): it ships no `DailyRollingFileAppender`, no `AsyncAppender`, no
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

    Five things are asked (the fifth added by the round-1 review), and they
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

    # Round-1 review, blocker: the instruction requires the audit appender to
    # be configured to drop nothing, and nothing measured it. The volume floors
    # cannot: logback discards INFO only once 0.8 * queueSize events are
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
    that gate is widened to `<= 1` here to say so. Trap 9 in the authoring
    notes is an inherited test that its successor's own requirements made
    unpassable; this pair is what avoiding it looks like.

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
    archive, which the first draft of this gate was not. A Boot jar carries
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
    The XML is parsed as XML rather than grepped, which is rule 11 and not
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

    It is here at this rung and not at step 07 -- where the authoring notes
    first parked it -- because it is the commons-logging bridge, which is part
    of the logging stack, and because the remedy is now nameable. It was
    parked only because it was not: seven poms declare neither jar, and a
    search of the poms therefore found nothing to point at. `mvn
    dependency:tree` answers in one line what grep could not answer at all,
    and a gate whose remedy cannot be named is a gate that should not be
    written. The remedy is an `<exclusions>` block on the `sshd-core`
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
    `probe` key and NOTES §11.26. The two agree on the reference, and they are
    both recorded because the moments are different: the probe reads before
    the harness's SIGTERM, this reads after the SIGKILL. A candidate whose
    backend loses its queue in teardown shows up as a disagreement, and a
    disagreement is something the author should see rather than something a
    gate should silently absorb.
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


# ---------------------------------------------------------------------------
# What this rung threatens, in one row
# ---------------------------------------------------------------------------
#
# Three checks, 1.0 between them, and they are not inherited: nothing before
# this step asked any of them, because before this step nothing could break
# them. They are priced like a hold rather than like a gate because that is
# what they are -- they measure what the rung must not disturb, not what it
# advances, and the tree this step is handed passes all three.
#
# `audit-log-intact` was in this row in the first draft of this file and was
# taken out of it. A row fails whole, so everything in it is priced at the
# row's weight, and 1.0 out of this step's 50.5 is not a price for destroying
# the one feature the rung exists to carry across. It is a top-level gate at
# 3.0 now, and what is left here is genuinely secondary.


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

    # The slf4j call sites the tree already had. Fourteen own production
    # classes reference `org.slf4j` before this rung and none of them is
    # `SshLog`; after it there is one more, because `SshLog` gets a logger
    # like everything else. The floor is the before count, so it catches the
    # candidate who "unifies logging" by routing the existing call sites
    # through logback's API directly -- which works, and which makes fourteen
    # files depend on a backend they had no reason to know about.
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
#                                    fail to start, and `spring-6` inside it
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
# The one collector that was filled as this file ran rather than by a function
# called here: `DI` collected step 03's twenty-two container gates at the point
# each is defined, up in the region between `_journal, g = g, DI` and
# `g = _journal`. Reported last, with the other four.
held("inherited-di-hold", DI.results, 2.0)

# And the one row in this group that is not inherited at all: three properties
# this rung can break, collected by `logging_regression()` above. It is
# reported here rather than beside its own section because it is priced and
# read like the six above it -- see that function's header.
held("logging-regression-hold", logging_regression(), 1.0, noun="regression")

print("%.6f" % S.finish(g, b.behaviour_pair(), {"calibration": CAL}))

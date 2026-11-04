"""Step 03 -- Guice 2.0 out, Spring Framework 5.3 Java configuration in. Still
a WAR, still `javax.servlet`, still JDK 11.

    reward = migration * behaviour

`behaviour` is the fraction of the 428 recorded observations the built artifact
reproduces, computed by run_behaviour.sh. Everything in *this* file is the
other factor.

The rung in one sentence: every object this server builds is built by a
container, and this is the step where the container changes.

What makes it a rung and not a library swap: Guice 2.0 is not a dependency this
tree uses, it is the shape the tree is written in. Fifteen `AbstractModule`
subclasses say what is bound to what; an assisted-inject `FactoryModule`
generates one factory implementation at startup; two `ServletModule`s declare
the filter and servlet mappings that `web.xml` hands to `GuiceFilter`; a
hand-rolled `LifecycleManager` walks a list of `LifecycleListener`s up and back
down again; and a project-local `SshScope` implements Guice's `Scope` interface
so that a per-connection object can be injected into a singleton. Each of those
five is a different construct with a different answer on the other side, and
four of the five have no Guice type left in them when they are answered:

    Guice 2.0                          Spring Framework 5.3
    -----------------------------      ---------------------------------
    AbstractModule.configure()         @Configuration + @Bean
    @Inject                            constructor injection
    Provider<T>                        ObjectFactory<T> / ObjectProvider<T>
    binding annotation                 @Qualifier meta-annotation
    FactoryModule / assisted inject    a factory bean that calls createBean
    ServletModule                      programmatic registration, no web.xml
    Scope + ServletScopes.REQUEST      @Scope("request")
    LifecycleManager                   SmartLifecycle

`WEB-INF/web.xml` goes with the `ServletModule`s: the only three things in it
are `GuiceFilter`, its `/*` mapping and the Guice context listener, and when
those go there is nothing left for the file to say. A Servlet 3.1 container
finds the application's own registrations without it, which is the capability
step 02 bought and this step is the first to spend.

The version is 5.3 and not 6.x for the same reason step 02's JGit was 5.13:
5.3 is the last Spring Framework line on `javax.servlet`, and this tree's
classpath is `javax.servlet` until step 05 says otherwise. Spring Boot is not
here yet either -- that is step 04 -- so a candidate who reaches for it has
skipped a rung rather than cleared this one, and the gate below says so.

Calibration, as in every step: each number is next to the measurement it came
from, and each was derived from the baseline census and the reference solution
rather than chosen to make a run pass. NOTES §8.3 records both.
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
    # unpassable, and this rung rewrites the constructor of very nearly every
    # class in the tree -- so the assertion is made rather than assumed.
    "tests_min": 19,
    "test_classes": ("ConfigUtilTest", "SitePathsTest", "BasicSerializationTest",
                     "IdGeneratorTest", "SocketUtilTest"),
    "release": "11",
    "class_major": 55,

    # Source census: baseline 117 files / 151 declared types, reference
    # 108 / 137. This is the first rung whose reference tree is *smaller* than
    # its baseline, and the whole of the difference is Guice wiring: seventeen
    # files go, and twenty-five declared types with them, because a module
    # class is a file whose entire job is to name bindings and a `@Bean` method
    # is not a file at all.
    #
    # The floors are the baseline less exactly what §1.2 enumerates, which is
    # the only derivation that cannot fail a candidate for doing what the
    # instruction says:
    #
    #   files   117 - 17 = 100
    #   types   151 - 25 = 126, less two units of slack -> 124
    #
    # The seventeen are the fifteen `AbstractModule` subclasses (twelve of them
    # alone in a file, three nested inside the class they configure), the
    # `FactoryModule` base and the assisted-inject factory interface it
    # generated, the `CommandName` binding annotation and the
    # `Commands.NestedCommandNameImpl` that instantiated it, the two one-line
    # `Provider` adapters that existed only to be named in a `SshScope`
    # binding, and the three files of the `com.google.gerrit.lifecycle`
    # package. Every one of them is Guice's, and none of them has a Spring
    # counterpart that is also a file.
    #
    # The two units of slack on the type floor are for the candidate who writes
    # one of the surviving one-method adapters as a lambda where the reference
    # wrote a named class -- removing a declared type without removing any
    # work. The file floor gets none, because §1.5 is explicit that a class
    # which produces a value for injection stays a class, and a candidate who
    # folds the twelve stand-alone provider classes into `@Bean` method bodies
    # would reach 96. That tree is more idiomatic Spring than the reference and
    # is still not what this step asked for: the step is a port of a wiring
    # graph, and re-deriving the graph from scratch is a different exercise
    # whose result the 428 observations cannot speak for.
    "source_files_min": 100,
    "source_types_min": 124,

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

    # Suppressions may not grow. 31 is step 01's cap and step 02's; the
    # baseline sits at 22 and this reference at 18, because four of them were
    # suppressing unchecked casts in Guice `TypeLiteral` and `Key` plumbing
    # that has no counterpart on the other side. The cap stays at 31 rather
    # than dropping to 18 for the reason step 02 gives: driving this number
    # down is step 09's rung, and a candidate who needs one
    # @SuppressWarnings("unchecked") to get a generic `ObjectProvider` past
    # javac has not regressed anything.
    "suppressions_max": 31,

    # The universe every absence gate below has to have searched. 184 own
    # classes in the baseline, 157 in the reference -- the drop is the same
    # seventeen files, plus the synthetic classes Guice's module DSL produced
    # for them. 130 is the floor at which "no class references
    # com.google.inject" starts to mean something other than "there are no
    # classes". Step 02's 150 would have been above the reference's own count
    # by the end of this rung, which is the shape of a floor that expires.
    # Origins carry no leading slash (own_classes).
    "own_floor": (130, "classes/"),

    # The 5.3 line specifically, for the reason the module docstring gives:
    # 5.3 is the last Spring Framework train on `javax.servlet`, 6.x is
    # jakarta-only, and this tree's servlet API is `javax` until step 05. Any
    # 5.3.x passes rather than 5.3.39 exactly, because a patch release inside
    # the line is the same line.
    "spring_version_re": r"^5\.3\.",

    # Own classes naming the container's API at all, counted over
    # `org.springframework` and `javax.inject` together: 0 in the baseline, 72
    # in the reference (72 under `org.springframework` alone -- this reference
    # happens to spell every qualifier Spring's way, and the broader set costs
    # it nothing).
    #
    # The two prefixes are one question and not two. Spring 5.3 reads the
    # JSR-330 annotations natively: `javax.inject.Qualifier` on an annotation
    # makes it a qualifier the container recognises on its own, which is
    # exactly what §1.3 asks of the seven that survive, and
    # `javax.inject.Provider` is a container factory interface the same way
    # `ObjectFactory` is. A port written that way is a Spring port -- those
    # annotations do something here only because Spring is the container
    # reading them -- so a gate counting `org.springframework` alone would
    # fail a correct tree for having chosen the standard spelling over the
    # proprietary one. The gate keeps the name `spring-classes` because Spring
    # is still the container either way.
    #
    # `jakarta.inject` is not a third, for the reason CONTAINER_API gives:
    # 5.3 does not read it. An import the container ignores is not contact
    # with the container.
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
    #       `javax.inject.Qualifier` -- both of them inside this gate's prefix
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
    # Spring 5.3 recognises a custom qualifier annotation on its own when the
    # annotation is meta-annotated with its own `@Qualifier` or with JSR-330's
    # -- it reads `javax.inject` natively, for CONTAINER_API's reason. A third
    # route exists in Spring, `CustomAutowireConfigurer`, and it is
    # deliberately not here: §1.3 rules it out in the same sentence, "because
    # the annotation itself is marked as one and not because the container was
    # separately told about it".
    "qualifier_marks": (
        "org.springframework.beans.factory.annotation.Qualifier",
        "javax.inject.Qualifier"),

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
    #
    #    Half B -- something in this tree drives that context -- three routes:
    #
    #      B1  a production call to one of `context_verbs`, the methods only a
    #          context or a bean factory has;
    #      B2  a production declaration of one of `context_template_methods`,
    #          which exist to be overridden and mean nothing anywhere else;
    #      B3  a production use of one of `definition_carriers`, the types
    #          that carry definitions into a context or a context into the
    #          servlet container.
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
    #    Measured: baseline A=0 B=0 (fails both halves); reference
    #    A=17 B=18, by routes A1 and B1+B3 -- A2, A3 and B2 are the routes
    #    this reference does not take and a different correct port does.
    "context_impls": (
        # Instantiable contexts, matched on the written simple name. Every
        # concrete `ApplicationContext` Spring 5.3 ships that a candidate
        # could reasonably build; the abstract bases are in `bootstrap_types`
        # below, where they are matched as superclasses instead.
        "AnnotationConfigApplicationContext",
        "AnnotationConfigWebApplicationContext",
        "GenericApplicationContext",
        "GenericWebApplicationContext",
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
    #      L2  a `@PostConstruct` or `@PreDestroy` in the JSR-250 spelling 5.3
    #          reads natively, or a Spring `@EventListener` -- the annotation
    #          form of L1's first addition, and the one a candidate who does
    #          not want a marker interface writes.
    #      L3  a `@Bean` that sets `initMethod` or `destroyMethod`.
    #
    #    Only the `javax.annotation` spelling of the JSR-250 pair counts, for
    #    CONTAINER_API's reason: 5.3's `CommonAnnotationBeanPostProcessor`
    #    reads `javax.annotation` and the `jakarta` package arrives with
    #    Spring 6. `@EventListener` is Spring's own and has no such twin.
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

    # Step 02's own thresholds, for the library tripwire. Same copying rule.
    "jgit_version_re": r"^5\.13\.",
    "jgit_classes_min": 25,
    "sshd_version_re": r"^2\.",
    "sshd_present_re": r"^2\.19\.",
    "sshd_classes_min": 1,
    "servlet_version_re": r"^3\.1\.",
}

GUICE = ("com.google.inject",)
LIFECYCLE = ("com.google.gerrit.lifecycle",)
SPRING = ("org.springframework",)

# The container's API by both the names a correct port of this tree may spell
# it under. `SPRING` is the groupId question -- what resolves, and on which
# line -- and stays narrow. `CONTAINER_API` is the bytecode question -- does
# this project's own code reach for the container -- and a port that marks the
# seven surviving binding annotations with `javax.inject.Qualifier` and writes
# its provider classes as `javax.inject.Provider` has reached for it exactly
# as much as one that used Spring's own names. See CAL["spring_classes_min"].
#
# `jakarta.inject` is deliberately not here. Spring 5.3 reads the JSR-330
# annotations from `javax.inject` and only from there -- support for the
# `jakarta.inject` spelling arrives in Spring 6 -- so a class importing
# `jakarta.inject.Inject` on this rung is a class the container will not
# inject. Counting it would let an import that does nothing raise the score of
# a tree the container ignores, which is the opposite of what this gate is
# for. Step 05 is where the `jakarta` spelling starts to mean something, and
# that step's file is where it should appear.
CONTAINER_API = SPRING + ("javax.inject",)

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
# How the 46.5 is split, in three columns and not two
#
# The obvious decomposition is "this rung's own leap" against "the floor and
# the inheritance", and it is wrong here: four of the gates that belong to
# this rung by subject -- `no-spring-off-5-3`, `no-spring-boot`,
# `no-xml-bean-definitions` and `no-profile-selection` -- are passed by a tree
# with no Spring in it at all. They are worth having and they are not
# discriminating weight, and a comment that counted them as the leap would be
# describing an instrument nobody built. So the split is measured three ways,
# by running the predecessor tree -- step 02's finished one, untouched, which
# is exactly what `/app` holds when this step starts -- and partitioning the
# journal by whether each gate passed and by which section of this file
# defines it.
#
#   (a) 28.0   rung gates the predecessor FAILS, over 19 gates. The
#              discriminating leap. `no-guice-dep` 2.0, `no-guice-classes`
#              2.0, `no-guice-source` 2.0, `no-aopalliance-dep` 1.0,
#              `no-lifecycle-classes` 1.0, `no-lifecycle-source` 1.0,
#              `spring-5-3` 2.0, `spring-classes` 2.0, `container-api-used`
#              2.0, `java-configuration` 2.0, `qualifier-annotations` 1.0,
#              `provider-classes` 1.0, `container-bootstrap` 2.0,
#              `bean-definitions` 1.0, `container-lifecycle` 1.0,
#              `container-scope` 1.0, `assisted-construction` 1.0,
#              `no-web-xml` 2.0, `no-web-xml-packaged` 1.0.
#
#   (b)  3.5   rung gates the predecessor PASSES vacuously, over 4. One-
#              directional gates that refuse overshoot rather than reward
#              progress: a tree with no Spring has no Spring off the 5.3 line,
#              no Boot artifact, no `<beans>` document and no `@Profile`.
#              `no-spring-off-5-3` 1.0, `no-spring-boot` 1.0,
#              `no-xml-bean-definitions` 1.0, `no-profile-selection` 0.5.
#
#   (c) 15.0   inherited gates, over 13, all of them passed by the
#              predecessor, because that is what inherited means. The floor at
#              the top of this file (`source-parses`, `sources-survive`,
#              `orphan-types-intact`, `build-and-artifact`), step 01's
#              re-assertions (`class-release`, `compiler-release`,
#              `no-jdk-internals`, `tests-ship-and-pass`, `suppressions`) and
#              the four `inherited-*-hold` collectors.
#
#   ---- 46.5 over 36 gates.
#
# So the predecessor's migration score is (b)+(c) over the total -- 18.5/46.5
# = 0.397849 -- and below the 0.5 the floor rule requires. It lands there
# because the work this rung names is 28.0 of 46.5 and the predecessor has
# done none of it, not because any gate was weighted to put it there: the
# column-(b) four were in the set before the arithmetic was looked at, and
# every gate added since has landed in column (a), which moves the floor's
# denominator alone and can only push it down. Round 3 added
# `assisted-construction` at 1.0 and raised `container-bootstrap` from 1.0 to
# 2.0, taking the total from 41.5 to 43.5 and the floor from 0.445783 to
# 0.425287; round 3's second pass added `container-api-used` at 2.0, taking
# them to 45.5 and 0.406593; round 4 added `qualifier-annotations` at 1.0,
# taking them to 46.5 and 0.397849.
#
# The four weights that were raised when this file was written each stand on
# their own and not on the arithmetic. `no-guice-source` went to 2.0 because
# eliminating 184 import sites is the same size of work as clearing the
# coordinate and the classpath, which are 2.0 each. `java-configuration` went
# to 2.0 because it is the positive half of `no-web-xml`, which is 2.0: one
# says the XML is gone and the other says something took its place. The two
# lifecycle gates went to 1.0 each because 16 own classes and 12 import sites
# converted onto the container's own lifecycle is not a half-point of work.
# None of the four was revisited to reach a target; the numbers above are what
# the floor run reports.
#
# `container-bootstrap` is 2.0 and the other four container gates are 1.0
# each. The 2.0 is the one weight round 3 changed, and it stands on the same
# footing as the other three raises: it is the positive half of
# `no-guice-classes`, which is 2.0 for deleting the old container, and the
# two ask the same question from the two sides -- one says the container that
# was there is gone, the other says a container is there now. It is also the
# only gate in the file that a tree wiring itself by hand behind a
# project-local registry cannot reach at all, which is the tree §1.4 exists to
# refuse; the others it can reach in part. The remaining four ask a single
# yes-or-no question each about one container feature, which is the same size
# of question as `provider-classes` and half the size of
# `java-configuration`, whose subject is the whole of the wiring.
#
# `container-api-used` is 2.0, and it is the last of this file's 2.0s to be
# justified because it is the newest. It is the positive half of
# `no-guice-source`, which is 2.0: that gate says 184 import sites naming the
# old container are gone, and this one says the project's own source now
# writes types belonging to the new one. It is also the only gate that reads
# §1.1's last bullet directly -- the tree cannot come out with Spring in
# every pom and nothing of its own naming it -- and that bullet is the
# subject of the whole rung rather than one feature of it, which is the same
# footing `java-configuration` stands 2.0 on.
#
# The direction of the change matters more than its size: raising a
# column-(a) weight and adding a column-(a) gate both move the floor *down*,
# so neither can be a way of fitting the instrument to the floor rule. The
# floor was 0.445783 before round 3, 0.425287 after its first pass, 0.406593
# after its second and is 0.397849 now, and the rule asks for strictly below
# 0.5. Measured, not projected: `/work/seed-step03-ref` reports 18.5/46.5.
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

    `javax.inject` counts beside `org.springframework` for CONTAINER_API's
    reason, and it is load-bearing here in the other direction too: a port
    that writes its producers as `javax.inject.Provider` has reached for a
    contract only a container honours and 5.3 honours natively, and refusing
    it would be a gate against §1.3's own list. A port that writes them as
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

    Three routes, unchanged from the gate that used to hold them inline:

      A1  production source writes the name of a context implementation
          (`context_impls`) as a type: `new AnnotationConfigWebApplicationContext()`,
          a field declared `GenericApplicationContext ctx`, a
          `GenericWebApplicationContext.class` literal.
      A2  an own production class's bytecode *superclass* is one of
          `bootstrap_types` -- the route for a port that only extends
          Spring's own machinery and names no context type in its own source.
      A3  an own production class carries the dotted name of one of them as a
          string constant, which is how a `contextClass` init-param is
          spelled.

    Naming a context *interface* is deliberately not a route, and
    `@Scope(WebApplicationContext.SCOPE_REQUEST)` is the case that decides
    it: that attribute names a context and creates nothing, and it emits no
    `typeuse` at all, which is what makes the cut possible without a new
    scanner record.

    Returns {site: route}.  Measured: reference 17, all by A1; baseline 0;
    the hand-wired tree 0.
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
# of the 5.3 line, so requiring it would add a row to the failure message and
# nothing to the question. §1.1 names the same two, which is the condition on
# a gate asking for an artifact by name -- it has to be something the
# instruction told the candidate to resolve.
#
# Measured: the reference resolves both (seven artifacts, all 5.3.39); step
# 02's finished tree resolves no `org.springframework` artifact at all, so the
# floor fails this gate on the count and on both names.
G.dep_present(g, b, "spring-5-3", group_prefixes=SPRING, minimum=3, weight=2.0,
              version_re=CAL["spring_version_re"],
              require_artifacts=("spring-context", "spring-web"))

# The other half of that presence, and the half that makes it a rung: a 6.x
# resolved beside the 5.3s would satisfy the gate above and would take this
# tree off `javax.servlet`, which §1.7 and step 02's whole servlet section say
# is where it stays until step 05.
#
# Asked of the `org.springframework` groupId exactly rather than of the prefix,
# because the prefix is also `org.springframework.boot`,
# `org.springframework.security` and `org.springframework.data`, whose version
# numbers are their own and have never tracked the framework's. Boot is asked
# separately below; the other two are not in this tree and no sentence in the
# instruction brings them in, so a gate that failed them would be asking for a
# removal the candidate was never told to make.
dep_gate_absent("no-spring-off-5-3",
                lambda gid, aid, ver: (gid == "org.springframework"
                                       and not re.match(CAL["spring_version_re"],
                                                        ver)), 1.0)

# Spring Boot is step 04. It is the single most likely thing for a candidate to
# reach for here -- "port this to Spring" returns a Boot application from any
# search -- and it would answer this rung's question and the next one's at the
# same time, which is exactly what a ladder is for and exactly what this gate
# refuses. §1 says not yet in words; this says it in a number.
#
# Both fields, because Boot's own artifacts carry the `spring-boot` artifactId
# under the `org.springframework.boot` groupId and its starters are published
# by third parties under groupIds of their own.
G.dep_absent(g, b, "no-spring-boot",
             group_prefixes=("org.springframework.boot",),
             artifact_prefixes=("spring-boot",), weight=1.0)


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
# Counted over `org.springframework` and `javax.inject` together -- that is
# what `CONTAINER_API` holds, and `jakarta.inject` is deliberately not in it:
# 5.3's `AutowiredAnnotationBeanPostProcessor` reads the `javax` spelling and
# the `jakarta` one arrives with Spring 6. A port that marks the seven
# surviving binding annotations with `javax.inject.Qualifier` rather than
# Spring's own, and writes its provider classes as `javax.inject.Provider`
# rather than `ObjectFactory`, has done what §1.3 asks: Spring 5.3 reads those
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
    files and types together. `javax.inject.Provider` and
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

    `spring-5-3` says the container resolves and `spring-classes` says this
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

    Naming a context interface is deliberately *not* a route.
    `@Scope(WebApplicationContext.SCOPE_REQUEST)` names one and creates
    nothing; so does a method declared
    `install(ServletContext, WebApplicationContext)`. The round-2 version of
    this gate counted any constant-pool reference to any of nine types, three
    of them interfaces, with a floor of 1 -- and so the hand-wired tree passed
    it on the strength of a `@Scope` attribute (NOTES §8.3.15).

    **Half B -- something in this tree drives that context.** Three routes:

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

    Weight 2.0, raised from 1.0 in round 3 and on merits rather than to make
    a number come out: this is the positive half of `no-guice-classes`, which
    is worth 2.0 for deleting the old container, and the two ask the same
    question from the two sides.

    The honest limit: a context that is built and refreshed on a code path
    nothing reaches passes this. Whether the container is the thing actually
    serving the requests is a behavioural question, and the 428 observations
    are where it is asked.

    Measured: baseline A=0 B=0; reference A=17 B=18.
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
      L2  `@PostConstruct` or `@PreDestroy` in the JSR-250 spelling 5.3 reads
          natively, or Spring's own `@EventListener`, which is the annotation
          form of L1's first addition and what a candidate who wants no
          marker interface writes.
      L3  a `@Bean` that sets `initMethod` or `destroyMethod`.

    Only the `javax.annotation` spelling of the JSR-250 pair counts, for
    CONTAINER_API's reason: 5.3's `CommonAnnotationBeanPostProcessor` reads
    `javax.annotation`, and the `jakarta` package arrives with Spring 6. The
    annotation is matched on its simple name, so the import list is what tells
    the two apart -- unless the use site wrote the name out in full, which
    carries its own package.

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

    def reads_javax(path, simple, written):
        if written.startswith("javax.annotation."):
            return True
        ims = jsr250.get(path) or []
        return ("javax.annotation." + simple) in ims or "javax.annotation" in ims

    for simple in CAL["lifecycle_annos"] + CAL["lifecycle_event_annos"]:
        for r in annotated(b.ast.kind("type"), simple) \
                + annotated(b.ast.kind("method"), simple):
            if in_tests(r.get("p")):
                continue
            written = next((a for a in (r.get("annos") or [])
                            if a == simple or a.endswith("." + simple)), simple)
            if simple in CAL["lifecycle_annos"] \
                    and not reads_javax(r.get("p"), simple, written):
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

# ---------------------------------------------------------------------------
# What the tree arrived with, in four gates
# ---------------------------------------------------------------------------
#
# Thirty inherited checks, reduced to four rows carrying 3.5 between them.
#
# Thirty rows would be thirty weights, and at the ones steps 01 and 02 gave
# them that is more than this rung's own work is worth put together -- a
# candidate could lose the whole of the Guice move and still out-score one who
# did it and let a single inherited idiom slip. What the weight has to buy
# instead is a tripwire: regressing an inherited property is a real and visible
# loss, and no pile of them is worth more than the step actually asks for.
#
# Four rows and not one, because the thirty are four kinds of thing and a
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
#                                    a candidate ports a constructor.
#   inherited-idioms-hold      0.5   the five pre-Java-7 conversions. These are
#                                    the ones this rung's own work can nudge,
#                                    and the measured direction of travel is
#                                    down on every old-form counter, so half a
#                                    gate is what a stray plain `try` costs.
#   inherited-libraries-hold   1.0   step 02's three library moves, in twelve
#                                    checks. A candidate who rewrites every pom
#                                    in the tree to add Spring is one careless
#                                    paste away from putting the JGit fork back.
#   inherited-servlet-hold     1.0   step 02's servlet API, in seven checks:
#                                    the 3.1 coordinate at `provided` scope,
#                                    nothing servlet-shaped packaged, and
#                                    `javax` rather than `jakarta` three ways.
#                                    This rung deletes `web.xml` and adds
#                                    programmatic registration, which is the
#                                    most servlet-API-shaped work any step
#                                    between 02 and 05 does.
#
# The thresholds are steps 01's and 02's, copied into CAL rather than imported.
# Each step's score.py is standalone -- it runs in its own image, with only
# this directory mounted -- and an import across steps would make this file
# unreadable without a file that is not there.

# ArtifactScan relativizes every origin against the target/ root it walked, so
# the test output reads "test-classes/Foo.class" with no leading slash. Step
# 01's floor, verbatim: an absence gate over test code has to be told the
# universe it must have searched, or a tree whose tests did not compile passes
# it by having nothing to find.
TESTS = (len(CAL["test_classes"]), "test-classes/")


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
    """Step 02's three library moves, in twelve checks.

    The two conjunctions step 02 had to write by hand are written by hand here
    too, and for the same reasons: `com.madgag:org.eclipse.jgit` carries
    upstream's artifactId under this project's own groupId, so neither half
    alone names the fork and a gate on the groupId reports the six reactor
    modules as the thing to remove; and a stale SSHD is an artifact at a
    version, which no prefix expresses at all.
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
    absent("no-jgit-off-5-13",
           lambda gid, aid, ver: (gid == "org.eclipse.jgit"
                                  and not re.match(CAL["jgit_version_re"], ver)))
    G.dep_present(h, b, "jgit-upstream-5-13", group_prefixes=JGIT, minimum=2,
                  version_re=CAL["jgit_version_re"])
    G.uses_refs(h, b, "jgit-classes", JGIT, minimum=CAL["jgit_classes_min"],
                own=True)

    absent("no-sshd-0x",
           lambda gid, aid, ver: (gid.startswith(SSHD)
                                  and not re.match(CAL["sshd_version_re"], ver)))
    G.dep_present(h, b, "sshd-2x", group_prefixes=SSHD, minimum=1,
                  version_re=CAL["sshd_present_re"])
    present("sshd-core-2-19",
            lambda gid, aid, ver: (gid.startswith(SSHD) and aid == "sshd-core"
                                   and re.match(CAL["sshd_present_re"], ver)
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
    """Step 02's servlet API: the 3.1 coordinate, its scope, and still javax.

    Seven checks. `no-pre-3-1-web-xml` is not among them: `no-web-xml` above
    asks the stronger question and a tree that passes it passes step 02's for
    having no descriptor at all.
    """
    h = _Collector()

    def servlet_api(gid, aid, ver):
        return (gid == "javax.servlet" and aid == "javax.servlet-api"
                and re.match(CAL["servlet_version_re"], ver) is not None)

    if b.deps_rc != 0:
        G._deps_unusable(h, "servlet-api-3", b, 1.0)
        G._deps_unusable(h, "servlet-api-provided", b, 1.0)
        G._deps_unusable(h, "no-tomcat-servlet-api", b, 1.0)
        G._deps_unusable(h, "no-jakarta-servlet-dep", b, 1.0)
    else:
        h.present("servlet-api-3",
                  sorted("%s:%s:%s" % t for t in G.deps(b)
                         if servlet_api(*t)), 1, 1.0)

        got = sorted(t for t in G.scoped_deps(b) if servlet_api(t[0], t[1], t[2]))
        wrong = ["%s:%s:%s:%s" % t for t in got if t[3] != "provided"]
        h.add("servlet-api-provided", bool(got) and not wrong,
              ("the Servlet 3.1 API resolves in no module at any scope"
               if not got else
               "resolved at a scope other than provided: %s" % ", ".join(wrong[:8])
               if wrong else "resolves at provided scope and at no other"),
              1.0, {"resolutions": ["%s:%s:%s:%s" % t for t in got],
                    "not_provided": wrong})

        h.absent("no-tomcat-servlet-api",
                 sorted("%s:%s:%s" % t for t in G.deps(b)
                        if t[0] == "org.apache.tomcat"
                        and t[1] in ("servlet-api", "tomcat-servlet-api")), 1.0)

        h.absent("no-jakarta-servlet-dep",
                 sorted("%s:%s:%s" % t for t in G.deps(b)
                        if t[0].startswith("jakarta.servlet")
                        or t[1].startswith("jakarta.servlet")), 1.0)

    packaged = sorted({e.get("o", "") for e in b.art.entries()
                       if SERVLET_JARS.search(e.get("o", ""))})
    h.add("servlet-api-not-packaged",
          not packaged and len(b.art.entries()) > 0,
          ("%d servlet API jar(s) inside the artifact: %s"
           % (len(packaged), ", ".join(os.path.basename(p) for p in packaged[:4]))
           if packaged else
           "no servlet API jar among %d artifact entries" % len(b.art.entries())),
          1.0, {"sites": packaged, "entries": len(b.art.entries())})

    G.no_source_types(h, b, "no-jakarta-servlet-source", ("jakarta.servlet",))
    G.no_refs(h, b, "no-jakarta-servlet-classes", ("jakarta.servlet",),
              own=False, floor=CAL["own_floor"])
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
held("inherited-libraries-hold", inherited_libraries(), 1.0)
held("inherited-servlet-hold", inherited_servlet(), 1.0)

print("%.6f" % S.finish(g, b.behaviour_pair(), {"calibration": CAL}))

# Step 3 — Spring Framework 5.3 Java configuration, and the descriptor goes with Guice

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a `javax.servlet` 3.1 WAR at Java 11,
hosting git repositories over HTTP on JGit 5.13 and over SSH on Apache MINA
SSHD 2.19.

Its object graph is still the one thing in it that nobody has touched. Every
object in this application is built by **Guice 2.0**, released in 2010: the
wiring is written as `Module` subclasses whose `configure()` bodies call a
binding DSL, the servlets are mounted by a `ServletModule` behind a `/*`
filter, and the container's own start-and-stop ordering is hand-rolled in a
project-local `LifecycleManager`. This milestone replaces that container with
**Spring Framework 5.3**, configured from Java.

It is still a WAR, still `javax.servlet`, still Java 11, still JGit 5.13 and
SSHD 2.19. Only the container changes — and everything the old container's
shape forced on the code around it.

### 1.1 Spring Framework 5.3 is the container

- Add **Spring Framework 5.3** to the reactor: `org.springframework` artifacts
  at a `5.3.x` version, enough of them to stand up an application context and
  a web application context — at least three resolve when you are done, and
  `spring-context` and `spring-web` are among them. The first is where the
  annotation-driven configuration classes §1.4 asks for live; the second is
  what makes this a servlet application with a request scope in it, which §1.3
  says it stays.
- **Every** artifact resolving under the `org.springframework` groupId is on
  `5.3.x`, in every module, at every scope. A reactor with two Spring
  generations on one classpath is not a port.
- **5.3 and not 6.x.** Spring Framework 6 requires Java 17 and is compiled
  against `jakarta.servlet`. This tree is Java 11 on `javax.servlet`, and it is
  Java 11 on `javax.servlet` when you are done: neither of those moves here,
  and a container that would move them does not fit. 5.3 is the last line that
  fits the tree you are porting; going further is not going faster.
- **Not Spring Boot.** No artifact whose groupId or artifactId names
  `spring-boot` resolves anywhere in the reactor. There is no embedded server,
  no `SpringApplication`, no auto-configuration and no starter here: this step
  produces the same deployable WAR it was handed, wired by Spring Framework
  alone. A Boot application is not what this step produces, and reaching for
  one fails the requirement this bullet just stated.
- The application's own code reaches for the container itself. Spring 4.3
  onwards injects a single-constructor class with no annotation at all, so how
  much of Spring's API your classes name by hand is your call — but the tree
  cannot come out the other side with Spring declared in every pom, resolving
  on every classpath, and nothing in the project's own compiled classes naming
  it. Wiring done by hand with `new` behind a project-local registry is the
  old container under a new dependency, not a new container.

  Annotations alone do not settle this. `@Configuration`, `@Bean` and a
  qualifier are all references to the container's API, and every one of them
  is inert unless something loads it — a tree can carry them on a graph it
  builds with `new` and has still not changed containers. What is being asked
  for is the container's types in the path of your code: written as a field,
  a parameter, a return type, a supertype, a `new`, a cast or a class
  literal, in the tree's own production sources. §1.3 and §1.4 below each
  force such types on you if you do what they ask — standing a context up is
  one, the custom scope is a second, and making that scope known to the
  container is a third — and the shape you write them in is yours.

### 1.2 Guice goes, in the poms, in the bytecode and in the source

- `com.google.inject:guice`, `com.google.inject.extensions:guice-servlet` and
  `com.google.inject.extensions:guice-assisted-inject` are managed in the root
  pom, named outright by two of the module poms and inherited by the rest of
  the reactor from there. None of the three resolves as a dependency of any
  module at any scope when you are done.
- `aopalliance:aopalliance:1.0` is declared for Guice — it is where Guice 2.0
  takes its `MethodInterceptor` from — and nothing else in this tree names it.
  It goes with Guice, as a **declared coordinate**.
- No `.java` file imports or names anything under `com.google.inject`, and no
  compiled class the build produces refers to it — including classes that
  arrive inside somebody else's jar. A Guice repackaged into `WEB-INF/lib`
  under another name is still Guice.

The classes that exist only to configure Guice have no work left once it is
gone, and they go. That is seventeen source files: the two servlet and web
modules under `com.google.gerrit.httpd`; the whole
`com.google.gerrit.lifecycle` package — its listener interface, its manager and
its module; the six modules under `com.google.gerrit.server.config`, including
the assisted-inject `FactoryModule`; the four under `com.google.gerrit.sshd`,
including the `CommandName` binding annotation §1.3 explains; and the two
under `com.google.gerrit.sshd.commands`. `WEB-INF/web.xml` goes with them, for
the reason §1.4 gives.

Nothing else does. §1.5 is the rule that governs the rest of the tree, and it
is not relaxed by this section: a class that was *configured by* a module is
not a module. It is narrowed by §1.5 in one direction, though, and the two
sections agree: a type nested inside a file that stays, whose own job was
configuring the container, is a module by this section's standard even though
its file is not listed here, and it goes the same way the listed ones go.

### 1.3 What the wiring said still has to be said

Guice's configuration is not decoration. Each construct below is carrying a
decision the application depends on, and the port is to say the same thing in
Spring's terms — not to drop it, and not to inline it into the one call site
that happens to be visible.

- **`AbstractModule.configure()` and the binding DSL.** What a module's body
  declares — which implementation stands behind an interface, which values are
  built once, which are built per use — is the same set of decisions a
  `@Configuration` class states. §1.4 requires it be stated in Java.
- **The seven binding annotations survive as qualifiers.**
  `@GerritPersonIdent`, `@RemotePeer`, `@CanonicalWebUrl`,
  `@GerritServerConfig`, `@SitePath`, `@CommandExecutor` and
  `@StreamCommandExecutor` each distinguish one of several values of a common
  type — two `Executor`s, a jgit `Config` among others, a `File`, a `String`.
  Those annotations stay, as annotations, on the injection points that carry
  them today; what changes is the container that reads them, so each becomes a
  qualifier Spring understands rather than a Guice binding annotation — one
  the container recognises on its own, because the annotation itself is
  marked as one and not because the container was separately told about it.
  `@CommandName` is the exception, and the reason is not cosmetic: it is not a
  qualifier at all but a *keyed* binding, one annotation instance per SSH
  command name, used to build a lookup table. Spring names beans with strings
  and looks them up by name, so the table is keyed on the name directly and the
  annotation has nothing left to distinguish.
- **`Provider<T>`.** A class whose whole job is to produce a value for
  injection is still a class when you are done, and it still names a producing
  interface as the thing it implements: Spring's `ObjectFactory`,
  `ObjectProvider` or `FactoryBean`, JSR-330's `javax.inject.Provider`, or
  `java.util.function.Supplier`. Port each one onto whichever of those fits it;
  do not fold its body into a configuration method and delete it, and do not
  replace fifteen of them with fifteen `@Bean` bodies. §1.5 counts them.
- **Assisted injection.** `FactoryModule.factory(Foo.Factory.class)` generates
  an implementation of a factory interface that takes the caller's arguments
  and has the container supply the rest. Spring has no such generator, so
  wherever one of those factory interfaces is still how a caller reaches an
  object, it needs an implementation that does the same thing: the caller's
  arguments passed in, the container's dependencies resolved, the object
  autowired on the way out. Where the caller already holds every argument and
  the interface existed only to have Guice generate it, it can go with the
  generator — but the object it built is still built, and still injected.
- **`ServletModule`.** The servlets and filters it mounts still have to be
  mounted, on the same URL patterns, in the same order relative to each other,
  and with their dependencies still injected. What changes is who mounts them —
  see §1.6, because this one is visible from outside.
- **Request scope.** `ServletScopes.REQUEST` and Guice's `@RequestScoped` mark
  the objects that live for one HTTP request: the current user, the remote
  peer, the request cleanup hook. Those lifetimes are unchanged. The SSH side
  has a scope of its own, implemented in this project against Guice's `Scope`
  interface, and it has the same work to do against Spring's — implemented, and
  then made known to the container under a name, because a scope the container
  has not been told about scopes nothing.
- **`LifecycleManager`.** It holds a list of listeners, starts them in the
  order they were added and stops them in the reverse order, and the WAR's
  context listener drives it. Spring's own container does exactly this for its
  beans — started at refresh, stopped at close, in a declared order — so the
  hand-rolled manager and its listener interface are replaced by the
  container's lifecycle, not carried on top of it. The components that start
  and stop today still start and stop, in an order that keeps working: the
  repository manager and the work queue before what uses them, the SSH daemon
  listening only once what it dispatches to is up, and everything closed on the
  way back down.

### 1.4 Configuration is Java, and there is no descriptor

- At least one type in the tree carries `@Configuration`. How many, and how
  they are split across the four contexts this application stands up, is a
  matter of taste; that the container is configured from Java is not.
- **The application stands its contexts up itself.** With the descriptor gone,
  nothing outside the tree builds a container for it: whatever mounts the
  servlets is also what creates the context they are served from, and it is
  one of this project's own classes that does it. Which class, and by which of
  the servlet specification's entry points, is yours to choose.
- **The bindings the modules declared are declared again.** Every
  implementation a module named for an interface, every value it produced for a
  qualifier and every object it built by hand is a bean definition on the other
  side — written as a method that returns it from a configuration class, or as
  an annotated class the container finds and instantiates for itself. Either is
  the same declaration and the choice between them is yours. The tree you hold
  declares upwards of forty of these between its modules, and a port of it
  declares a comparable number somewhere.
- **`WEB-INF/web.xml` is gone.** It exists to declare two things —
  `GuiceFilter` on `/*`, and the context listener that builds the injectors —
  and both are Guice's. No `<web-app>` deployment descriptor is present in the
  source tree in any module, under any servlet-specification namespace, and no
  `WEB-INF/web.xml` is present inside the built WAR either. Servlet 3.1 lets an
  application say all of this from Java, and the bullet above about standing the
  contexts up is where it says it.
- **No XML bean definitions.** Spring will read a `<beans>` document if you
  give it one; do not. No file in the tree is a Spring bean-definition document
  — not under `src/main/resources`, not under `src/test/resources`, not inside
  the WAR. Replacing one XML container configuration with another is not this
  milestone.
- The other files under `WEB-INF/` are not descriptors and are not affected.

### 1.5 What the tree you were handed already does, and still must

Everything it already does, it still does when you are done. Every module still
compiles through the compiler's `release` at Java 11 and every class file the
build produces is still a Java 11 class file; nothing reaches into a
JDK-internal API; the test suite still ships, still runs and still passes, with
no class that contributes a case deleted, emptied, renamed or disabled; the
number of warnings the tree suppresses with `@SuppressWarnings` does not grow,
counting each suppressed warning name separately, and counting a `@SafeVarargs` as a suppression too.

The build still runs as two commands, from a fresh copy of `/app`, under this
image's JDK 11 and Maven 3.9.16, with no profile to activate and no property to
set: the packaging step as `mvn -DskipTests package`, and the suite as a
separate `mvn test` after it. Both succeed. Packaging is run without the suite
attached so that a failing test still leaves a WAR behind, so the two have to
work as two commands and not only as one combined run. Both take their configuration from the poms and
nothing else: a `.mvn/` directory, a `.mavenrc` and a user `settings.xml` are
not read.

Everything the tree already carries holds. No module inherits from
`org.sonatype.oss:oss-parent`. No source file and no compiled class names a
JUnit 3 or JUnit 4 type; the suite is JUnit 5. EasyMock is in no module's resolved
dependencies and named by nothing. JGit resolves only from `org.eclipse.jgit`, at 5.13; everything under
`org.apache.sshd` is on 2.x and `org.apache.mina` is gone; `com.jcraft`,
`net.schmizz` and `org.bouncycastle:bcprov-jdk16` are gone from the poms, the
bytecode and the source. The servlet API is still `javax.servlet` — the
`javax.servlet:javax.servlet-api:3.1.0` dependency at `provided` scope, no
servlet API jar inside the WAR, and nothing `jakarta.servlet` arriving by any
route. The idioms the source was brought up to stay brought up: diamond rather
than explicit type arguments, try-with-resources rather than a hand-written
close in `finally`, multi-catch rather than repeated catch blocks, lambdas and
method references rather than single-method anonymous classes, and
`java.nio.file` used alongside `java.io.File` where it already is.

Apart from the seventeen files §1.2 names, the work the source you were handed
does is still being done when you are done, by the same files doing it. Every
file is still there; every job a declared type was doing is still a declared
type doing it; every behaviour a method carried is still carried by a method —
including in the corners of the tree that nothing else calls, which are the
cheapest thing to delete and the easiest to miss. A class that produces a
value for injection is a class producing a value for injection on the other
side too — ported onto the container's own factory interface, not dissolved
into a configuration method. So is a scope implementation, a factory
interface's implementation, and a component that starts and stops. Migrating by
deletion is not migrating: a type removed, or a method emptied, because that
was the cheapest way to stop something referring to the old container, is a
regression whatever it does to the rest of this document.

Two kinds of change are the port doing its job rather than deleting work, and
both are expected. The first: a type or a member inside a file that stays,
whose own job was configuring the container or speaking to it, is one of the
things §1.2 orders removed and goes the way they go even though its file stays.
That is a principle and not a licence to prune — it reaches exactly what the
new container now does for itself and nothing else, and a type or a method that
was doing the application's work rather than the old container's is not covered
by it however convenient deleting it would be. The second: a member whose name
was dictated by an interface this document is telling you to replace takes the
new interface's name instead. The method has not gone anywhere; it answers to
what the new contract calls it.

Both of those are sometimes forced and sometimes merely allowed, and where they
are merely allowed either answer is acceptable. §1.3's factory interfaces are the clearest case: where a caller still
reaches an object through one, it stays and gets an implementation, and where
the caller already holds every argument, keeping the interface and folding it
away are equally defensible. The same goes for a small adapter type that
existed only to be named in a binding, once there is no binding to name it in.
What is never allowed is the reverse reading — deleting a type or emptying a
method because that was the cheapest way to make this document's other
sentences true.

### 1.6 Behaviour: fixed, except where the container is the one deciding

**What this application's own code decides is still the contract, down to the
byte.** Every status, body and header its servlets, filters and SSH commands
compose; the `Expires`/`Pragma`/`Cache-Control` trio on `/ssh_info`; the
`Location` of its redirects; the output, error text and exit status of its SSH
commands; the files it writes under the site directory; its reading of
`etc/gerrit.config`, every section and key, with the same tolerance for values
it cannot parse. Reparenting an object graph does not move any of that, and if
something moves, it is a bug in the port.

**One thing does move, and it is not yours to hold still.** Guice 2.0's servlet
support mounts servlets *inside* a single `/*` filter and matches URLs itself.
It matches them against the raw request URI — the bytes the client sent,
before percent-decoding and before `..` is resolved — and it hands the servlet
a `getServletPath()` and `getPathInfo()` it recomputed the same way. A request
for a repository spelled with an escaped character, or reached through a path
that has to be normalized first, still begins with the prefix the mapping is
written on, so today it matches, and today it reaches the servlet. What the
servlet hands its repository resolver is the raw remainder — the escape still
escaped, the `..` still unresolved — and no repository is named that, so the
answer is the ordinary repository-not-found answer, composed by this
application and returned in band.

When the servlets are registered with the servlet container instead — which is
what §1.3 and §1.4 ask for — the container is the one matching, and a Servlet
3.1 container decodes and normalizes the path *before* it matches and before it
computes `getServletPath()` and `getPathInfo()`, exactly as its specification
requires. The resolver is then handed the name the client was always spelling,
the repository is there, and the request is served. **That is the new
contract.** Requests of that shape answer differently on the other side, and
they are meant to; nothing else does.

**Do not write a compatibility layer.** Reimplementing Guice's filter — a `/*`
filter that recomputes the path from `getRequestURI()` and dispatches by hand —
would hold those answers where they are, and it is the one thing this milestone
exists to remove. Mount the servlets with the container and take the
container's matching as it comes.

Nothing else moves with it. Everything the container decodes to the same name
it decoded to before is answered exactly as before; the not-found behaviour for
a repository that genuinely does not exist is unchanged; and no status, header
or body composed by this project's own code changes anywhere.

### 1.7 How the WAR is run — unchanged

`/app/README.md` still documents it and none of it moves: the WAR is deployed
at the context root of a `javax.servlet` container and takes the location of
its site directory from the **`gerrit.site_path`** system property, reading
`etc/gerrit.config` and the repositories under `basePath` from there. The
container, its HTTP port, the SSH addresses in the config, the JVM's default
time zone and the site directory itself all belong to whoever starts it.

Deployment is the ordinary one, and this project records its own: the WAR is
built, a stock `javax.servlet` container is started from outside this
repository, the WAR is deployed at the context root, and `gerrit.site_path`
points at a site directory. Nothing else is configured for the application.
Two things follow, and both are requirements of this milestone rather than
advice:

- **The context starts.** A WAR that deploys and whose context then fails to
  initialise has not been migrated. Every sentence in §1.6 is a statement
  about what a *running* server answers.
- **The site is found, not made.** `/app/README.md` gives the format of
  `etc/gerrit.config` and of the `basePath` it names, and says the directory
  structure is the same as that of the project this one was copied from;
  beyond those two entries a site holds everything else this project's own
  code reads out of one, the SSH host key material included. Whoever starts
  the server prepares that directory. The application has never had an
  installer and does not gain one: it does not stand a site up, and it does
  not decide for itself what a missing piece of one should have been. Which
  branch it takes when a piece is missing, and what it writes under a site
  while it runs, are part of §1.6's fixed contract like everything else — the
  code that reads a site today already answers both, and goes on answering
  them the same way.

The application gains no new way to select behaviour: no new flag, system
property, environment variable, config key, request header, marker string or
alternate entry point. In particular it does not gain a Spring profile, and no
bean is conditional on one.

**Still true of the tree you inherit, and still required.** Neither
`junit:junit` nor the JUnit vintage engine resolves on any module's test
classpath, declared or arriving behind something else, and no
`org.apache.tomcat:servlet-api` or `org.apache.tomcat:tomcat-servlet-api`
coordinate resolves anywhere, at any scope.

## 2. Read these local references

- **The Guice modules themselves** — `UrlModule` and `WebModule` under
  `mini-git-server-httpd`, `GerritGlobalModule`, `GerritRequestModule`,
  `GerritServerConfigModule`, `CanonicalWebUrlModule`, `FactoryModule` and
  `ToyGerritGlobalModule` under `mini-git-server-server`, `SshModule`,
  `CommandModule`, `ToySshModule`, `DefaultCommandModule` and
  `ToyDefaultCommandModule` under `mini-git-server-sshd`, and `CacheModule`'s
  binding DSL under `com.google.gerrit.server.cache`. Between them they are the
  complete specification of what has to be true of the new object graph.
- **`/app/mini-git-server-war/src/main/java/com/google/gerrit/httpd/WebAppInitializer.java`**
  — the four contexts, how they are parented, and what drives them.
- **`/app/mini-git-server-server/src/main/java/com/google/gerrit/lifecycle/`**
  — the manager, the listener and the module §1.3 retires, and
  `LocalDiskRepositoryManager`, `WorkQueue`, `CachePool`, `SshDaemon` and
  `SshLog` for what they start and stop.
- **`/app/mini-git-server-war/src/main/webapp/WEB-INF/web.xml`** — the
  descriptor §1.4 removes, and the two declarations that make it Guice's.
- **`/app/pom.xml` and the six module poms** — which module declares what, at
  which scope, and what the root pom manages.
- **`/app/README.md`** — how the server is started, and the layout and format
  of the site directory it is pointed at.

Spring Framework's own reference documentation for the 5.3 line is the other
half: the core container and its annotation-driven configuration, bean scopes,
the servlet-3.0 bootstrap it offers an application that has no deployment
descriptor, and the contracts by which the container starts and stops the
objects it manages. Which of the types those chapters describe you reach for is
a decision this document leaves to you; §1.4 and §1.3 say what has to be true,
not which class has to be on the line.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests and documents. Do not create a prose answer, a summary of the
  changes, or an alternate entry point in place of the work.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace and its documentation; do not search the system for
  other implementations or answers.
- Leave the tree building and passing from a clean checkout: nothing that only
  works in your shell, and nothing left half-applied.

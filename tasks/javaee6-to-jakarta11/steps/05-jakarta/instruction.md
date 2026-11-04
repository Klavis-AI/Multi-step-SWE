# Step 5 — the namespace moves: `javax.servlet` becomes `jakarta.servlet`

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a **Spring Boot 2.7 executable jar** at Java
11, with a servlet container inside the deployable, wired by **Spring Framework
5.3** from Java configuration classes, registering its own servlets and
filters, and hosting git repositories over HTTP on JGit 5.13 and over SSH on
Apache MINA SSHD 2.19. `java -jar` starts it.

Every servlet type it names is spelled `javax.servlet`.

That spelling is the last thing tying this application to Java EE. In 2017
Oracle gave Java EE to the Eclipse Foundation, and the one thing the donation
did not include was the `javax` trademark. Jakarta EE 9 is the consequence:
the same specifications, the same types, the same methods, with every package
renamed `javax.*` to `jakarta.*` and nothing else changed. There is no
compatibility mode and no bridge in the platform — a class compiled against
`javax.servlet.http.HttpServlet` and a container implementing
`jakarta.servlet` do not meet. It is one rename, it is mechanical, and it is
all-or-nothing.

This milestone performs it. When you are done the application is written
against **Jakarta Servlet 6.0**, is served by a container that implements it,
and has no `javax`-spelled enterprise API left anywhere — not in the source,
not in the bytecode, and not inside the jar.

Everything that rename drags with it comes too, because nothing in this
ecosystem lets it arrive alone: **Spring Framework 6**, **Spring Boot 3**, and
**Java 17**. The deployable is still a Boot executable jar, `java -jar` is
still the whole deployment, and every byte this application's own code
answers with is still the byte it answers with today. §1.6 names the one
place where an answer does move, and why it is expected to.

### 1.1 Every `javax.servlet` becomes `jakarta.servlet`, and only those

- **No `.java` file in this repository names a type under `javax.servlet`** —
  neither in an import nor written out inline at the point of use.
- **`jakarta.servlet` is what they name instead**, file for file: this is a
  rename of what is there, not a deletion of the code that used the old names.
  The tree you were handed writes servlet types across seven files, and in the
  migrated tree every one of those seven names the jakarta types instead.
  Whether you write one import per type or collapse a package to a wildcard is
  your business; which files name the API is not.
- **The compiled classes agree with the source.** No class this project
  compiles references a type under `javax.servlet` in its constant pool, and
  the classes that used to are the ones that now reference `jakarta.servlet`.
  Source and bytecode are asked separately because they can disagree: a file
  that imports the new package and then uses an old simple name it inherited
  from a supertype compiles against whichever one won, and the constant pool
  is what the class loader will actually go looking for.

**`javax` is not a synonym for "old".** The JDK has its own `javax.*`
packages, they were never part of Java EE, they are part of `java.se` and they
are not going anywhere. This tree uses two of them — `javax.xml.parsers`,
`javax.xml.transform` and `javax.xml.xpath` for the HTML templating, and
`javax.net.ssl` for the SSL socket factory — and **these uses stay exactly as
they are, all of them.** There is no `jakarta` spelling of them to move to; a
tree that renamed them does not compile. A blanket search and replace over
the string `javax` is the obvious way to do this milestone and the wrong one,
and this is the sentence that says so.

Nothing else in the tree changes its package. This is the servlet API's
rename and the artifacts that carry it, not a licence to rewrite imports
generally.

### 1.2 Spring Framework 6, Spring Boot 3 and Java 17

The rename is not something this application can do on its own: the framework
that wires it and the container that runs it both have to be on the same side
of it, and moving those two moves the language level.

- **Spring Framework 6 everywhere.** Every artifact under the
  `org.springframework` groupId resolves on the 6.x line, in every module and
  at every scope, with at least three of them present and `spring-context`
  and `spring-web` among them. 5.3 was the last Spring Framework train
  compiled against `javax.servlet` and 6.0 is the first `jakarta`-only one;
  the two cannot both be right, and a single 5.3 artifact left pinned in one
  module's pom puts a second, `javax`-spelled `HttpServletRequest` on that
  module's compile classpath.
- **Spring Boot 3, and every Boot artifact on the 3.x line.** At least three
  artifacts under `org.springframework.boot` resolve, `spring-boot` itself
  among them, all on 3.x. Boot 2.7's dependency management is what has been
  holding Spring Framework at 5.3 and the embedded container at a
  `javax.servlet` one; Boot 3's is what moves both. Versions still come from
  Boot's dependency management rather than from a pin at a time, exactly as
  the previous milestone required.
- **Java 17.** Every module compiles through the compiler's `release` at 17,
  and every class file the build produces is a Java 17 class file — major
  version 61, all of them, not a mixture. This is not a preference: Spring
  Framework 6 and Spring Boot 3 are themselves compiled at 17, so a build
  targeting 11 cannot read their class files at all. The build is run under
  JDK 17; this image carries 11, 17 and 21 and defaults to 11, so the
  toolchain the build asks for is a decision in the poms rather than
  something the environment makes for you.
- **The class file version is exact; the language features are optional.**
  Major 61 is the whole of what is asked for here: not 62, and not anything
  above it. A build that reads "17" as a lower bound and sets `release` to 21
  because 21 is also at least 17 has changed something this milestone did not
  ask it to change. 17 is the version named, and a class file version other
  than the one named is a difference from the requirement however far above
  it sits. What 17 makes legal is a separate question: records,
  sealed types, pattern matching and text blocks. None of them is asked for
  here and none of them is forbidden. A rewrite that arrives with them is
  held to §1.5 like any other.

### 1.3 What ships inside the deployable

The container and its API have been inside the jar since the previous
milestone. Which API is inside it is this milestone's question.

- **The Jakarta Servlet API ships inside the jar**, and in the quantity an
  API is: a package with a couple of classes in it is somebody's shim.
- **It is Servlet 6.0, not Jakarta Servlet 5.0.** Jakarta Servlet 5.0 is
  Servlet 4.0 with the packages renamed and nothing else — same types, none
  added, none removed — and it runs under Spring Boot 2.7 as happily as under
  3.x. A tree on it has renamed its imports and moved no platform. The
  discriminator is at the other end: Servlet 6.0 **removed**
  `SingleThreadModel`, `HttpSessionContext` and `HttpUtils`, which had been
  deprecated since 2001. The API inside your jar has none of those three, and
  still has the programmatic-registration types the previous milestone's
  registrations are built on — `ServletRegistration`,
  `ServletContainerInitializer`, `@WebServlet` and `AsyncContext`.
- **Tomcat.** Boot 3 has a starter for Jetty and for Undertow too, and at the
  previous milestone any of the three would have done. At this one they stop
  being interchangeable. Servlet 6.0 leaves each implementation to decide for
  itself what it will accept in a request target before it routes one, and the
  three now disagree: Jetty 12 refuses targets this application answers today, and
  Undertow answers targets it refuses today. §1.6 is a statement about what
  *this application* answers, so it holds only on the container that answers
  them that way. Keep the Tomcat that
  `spring-boot-starter-web` brings by default — Tomcat 10.1, which is Servlet
  6.0 — and if the tree you inherit carries a different one, move it back.
  What is required of the container is otherwise unchanged: it ships inside
  the deployable and it implements the API above.
- **No `javax`-spelled enterprise API is inside the jar at all.** Not
  `javax.servlet`, not `javax.el`, not `javax.websocket`, not
  `javax.security.auth.message`, and not the persistence, validation,
  transaction, injection or CDI APIs either. Two servlet APIs inside one
  archive is two `HttpServlet` classes for the loader to choose between, and
  which one a class gets is decided by classpath order rather than by
  anybody. The tree you were handed packages them; the migrated one packages
  none.

  This is about API jars, not about every string. A library that happens to
  carry an unreachable reference to a `javax` type it can no longer resolve
  is a dead branch in somebody else's code and is not your problem; an
  artifact that *provides* those packages is on your classpath and is.

### 1.4 The git HTTP server is a servlet application too

`org.eclipse.jgit.http.server` is what answers every `git clone`, `git
fetch` and `git push` this application serves over HTTP. It is not glue in
this repository — it is a dependency, and it is itself a servlet
application: `GitServlet` extends `HttpServlet`, `GitFilter` implements
`Filter`, and most of its classes name a servlet type.

This project's own code names none of those supertypes. It resolves the
artifact, registers `GitServlet` with the container, and never mentions
`HttpServlet` in connection with it. So:

- **The packaged `org.eclipse.jgit.http.server` is built against
  `jakarta.servlet`**, and none of its classes names `javax.servlet`.

Left on the old namespace it compiles, and the build succeeds, and the jar
starts, and `/ssh_info` is served, and every git operation over HTTP fails at
the first request with a `NoClassDefFoundError` naming a class that appears
nowhere in this repository. That is the most expensive way to get this
milestone wrong and the hardest to see, which is why it has a sentence of its
own.

JGit publishes the migrated module; finding which of its releases is the one
that carries it is part of the work. The version is a means — the
requirement is the property.

### 1.5 What the tree you were handed already does, and still must

Everything it already does, it still does when you are done. Nothing reaches
into a JDK-internal API; the test suite still ships, still runs and still
passes, with no class that contributes a case deleted, emptied, renamed or
disabled; the number of warnings the tree suppresses with `@SuppressWarnings`
does not grow, counting each suppressed warning name separately, and counting a `@SafeVarargs` as a suppression too.

The build still runs as two commands, from a fresh copy of `/app`, under this
image's Maven 3.9.16, with no profile to activate and no property to set: the
packaging step as `mvn -DskipTests package`, and the suite as a separate `mvn
test` after it. Both succeed. Packaging is run without the suite attached so
that a failing test still leaves a deployable behind, so the two have to work
as two commands and not only as one combined run. Both take their configuration from the poms and
nothing else: a `.mvn/` directory, a `.mavenrc` and a user `settings.xml` are
not read.

Everything the tree already carries holds. No module inherits from
`org.sonatype.oss:oss-parent`. No source file and no compiled class names a
JUnit 3 or JUnit 4 type; the suite is JUnit 5. EasyMock is in no module's resolved
dependencies and named by nothing. JGit resolves only from `org.eclipse.jgit` — §1.4 is what says
which line — and everything under `org.apache.sshd` is on 2.x with
`org.apache.mina` gone; `com.jcraft`, `net.schmizz` and
`org.bouncycastle:bcprov-jdk16` are gone from the poms, the bytecode and the
source. The servlet API still arrives with the container rather than
from a pin of its own: no pom declares a `javax.servlet` API coordinate, and
`org.apache.tomcat`'s standalone servlet-api artifacts stay out. The
container may of course bring the Jakarta API in transitively — that is the
container bringing it.

The object graph is the container's: the configuration is written in Java,
there is no XML bean definition document and no `WEB-INF/web.xml` anywhere in
the tree or inside the deployable, the four contexts and their parenting are
what they are, the request scope and the custom per-request scope are still
registered scopes, the qualifier annotations still qualify, the factory
interfaces still have implementations, and no bean is selected by a Spring
profile or conditional on one.

The deployable is the one the previous milestone produced: no module is a war and no pom names `maven-war-plugin`, no `.war` file exists under any module's `target/`, the build produces
one Spring Boot executable jar whose manifest records the Boot version it was
repackaged with, `Main-Class` is Boot's loader and `Start-Class` is a class
this project compiled that declares a `main`, `java -jar` is the entire start
command, the servlet container and the servlet API ship inside it, exactly
one artifact inside it provides `org.apache.log4j`, the application registers
its own servlets and filters, and nothing in the tree implements the
container's bootstrap contract — which now means **neither**
`org.springframework.web.WebApplicationInitializer` **nor**
`jakarta.servlet.ServletContainerInitializer`, the renamed successor to the
interface the previous milestone named.

The idioms the source was brought up to stay brought up: diamond rather than
explicit type arguments, try-with-resources rather than a hand-written close
in `finally`, multi-catch rather than repeated catch blocks, lambdas and
method references rather than single-method anonymous classes, and
`java.nio.file` used alongside `java.io.File` where it already is.

**A major version of a dependency-managing BOM is the new way to break one of
those by accident.** Boot 3 is not Boot 2.7 with bigger numbers: it drops
starters, changes defaults, and moves several of the artifacts listed above.
Adopting it is what §1.2 asks for; noticing what it moved is part of the same
job.

The work the source you were handed does is still being done when you are
done, by the same files doing it. Every file is still there; every job a
declared type was doing is still a declared type doing it; every behaviour a
method carried is still carried by a method — including in the corners of the
tree that nothing else calls, which are the cheapest thing to delete and the
easiest to miss. A class that produces a value for injection is a class
producing a value for injection on the other side too. So is a scope
implementation, a factory interface's implementation, and a component that
starts and stops. Migrating by deletion is not migrating: a type removed, or
a method emptied, because that was the cheapest way to stop something
referring to a `javax` name, is a regression whatever it does to the rest of
this document. The module that holds every servlet in this application is the
module this milestone touches most, and it does not get smaller.

**Annotations rename too, where they are Java EE's.** `@PostConstruct` and
`@PreDestroy` are JSR-250, so wherever your rewrite introduces one it is
spelled `jakarta.annotation`, because Spring 6 looks for that spelling and
nothing else and a `javax`-spelled one is a method that is simply never
called. `javax.annotation.Nullable` and the rest of JSR-305 are not an EE
API, have no jakarta spelling, and stay.

### 1.6 Behaviour: fixed down to the byte, with one documented exception

**Almost nothing observable moves in this milestone, and the one thing that
does is named below.** Every status, body and header this application's own
servlets, filters and SSH commands compose; the
`Expires`/`Pragma`/`Cache-Control` trio on `/ssh_info`; the `Location` of its
redirects; the static files it serves and their exact bytes; the output,
error text and exit status of its SSH commands; the SSH host key material it
presents and the algorithms it negotiates; the files it writes under the site
directory; its reading of `etc/gerrit.config`, every section and key, with
the same tolerance for values it cannot parse. Renaming a package does not
change what a program answers, and if something this repository composes
moves, it is a bug in the port.

Three clarifications, because more of the stack moves under you here than the
rename suggests, and one of the three is a place where an answer *does* move.

- **What this application composes is fixed; what a failure then looks like is
  not.** A request that matches no mapping answers with the status it answers
  with today. What an error page then *looks like* — the bytes of its body,
  the headers that come with it — is the container's own, and no two
  containers write the same one. That part is the chosen container's to decide
  and not yours to reproduce.
- **A newer container is still a servlet container.** It decodes and
  normalises a path before it matches and before it computes
  `getServletPath()` and `getPathInfo()`, exactly as its specification
  requires. Servlet 6.0 tightened what a container will accept in a request
  target before it gets that far, and this section observes the result rather
  than excusing it: a target this application answers today must
  still reach it, and one that never reaches it today must still be refused
  before it does. That is the whole reason §1.3 names a container instead of
  leaving the choice open. What the application answers for a request that
  does reach it does not move either.
- **The git HTTP layer answers for itself, and on the line §1.4 requires it
  has changed its mind about one thing.** Asked for a repository that does not
  exist, or handed a request body it cannot parse,
  `org.eclipse.jgit.http.server` used to answer `200 OK` carrying a pkt-line
  `ERR …` payload, whatever had actually gone wrong. The migrated line stopped
  doing that: a missing repository is a `404`, an unparseable upload-pack body
  is a `500`, and `git clone` of a path that is not a repository prints the
  client's own "not found" rather than a relayed remote error. **That is
  expected, it is the library's documented behaviour on the version this
  milestone moves to, and it is not yours to undo.** Do not wrap `GitServlet`
  to restore the old statuses, do not add a filter that rewrites them, and do
  not keep the old JGit line in order to keep them. Nothing this repository's
  own servlets, filters or SSH commands compose moves at all.

**Do not write a compatibility layer.** A shim package that re-exports
`jakarta.servlet` types under `javax.servlet` names, a bytecode-rewriting
plugin bolted into the build, a `/*` filter that adapts one API to the other
— all of them hold the answers where they are for the wrong reason, and the
old namespace is the thing this milestone exists to remove. Rename the code.

### 1.7 How the server is run

Unchanged from the previous milestone, in every particular:

```
java -Dgerrit.site_path=<site> -Dserver.port=<port> -jar <the jar>
```

and nothing else — no container installed beside it, no deployment step, no
`-cp`, no script. The site directory still comes from the `gerrit.site_path`
system property and `/app/README.md` still gives the format of
`etc/gerrit.config` and of the `basePath` it names. The HTTP port still comes
from Spring Boot's own `server.port` property and that property is still the
only thing that decides the listen port. The SSH addresses still live in
`etc/gerrit.config`. The JVM's default time zone, the site directory and
everything in it still belong to whoever starts the server, and the
application still does not stand a site up or decide for itself what a
missing piece of one should have been.

Two things follow, and both are requirements of this milestone rather than
advice:

- **The jar starts.** A deployable that `java -jar` cannot run, or that runs
  and then fails to come up, has not been migrated. Every sentence in §1.6 is
  a statement about what a *running* server answers. Half a namespace
  migration is the specific way to fail this: a context that cannot resolve a
  servlet type takes the whole application down at refresh, and the jar that
  built cleanly exits before it binds a port.
- **The application gains no new way to select behaviour.** No new flag,
  environment variable, config key, request header, marker string or
  alternate entry point, and no second way in beside the one `main`. In
  particular it does not gain a Spring profile, and no bean is conditional on
  one.

**Still true of the tree you inherit, and still required.** Guice (`com.google.inject`) and `aopalliance` resolve in no module's
dependency graph, and nothing in the source, in the compiled classes or
inside the deployable names `com.google.inject` or the old
`com.google.gerrit.lifecycle` package. Neither `junit:junit` nor the JUnit
vintage engine resolves on any module's test classpath, and the suite is
written against JUnit Jupiter (`org.junit.jupiter`). No
`org.apache.tomcat:servlet-api` or `org.apache.tomcat:tomcat-servlet-api`
coordinate resolves anywhere. The application is still a Spring Boot application in its own code: a
production class carries `@SpringBootApplication` (or
`@SpringBootConfiguration` together with `@EnableAutoConfiguration`), and
production code starts it through `SpringApplication` or
`SpringApplicationBuilder`.

## 2. Read these local references

- **`mini-git-server-httpd/src/main/java/com/google/gerrit/httpd/`** — the
  seven files that name servlet types, which are the entire source-level
  scope of §1.1: two filters, three servlets, and the two request-scoped
  providers that read the live request.
- **The servlet and filter registrations the tree already makes** — written,
  where they use Spring Boot's `ServletRegistrationBean` and
  `FilterRegistrationBean`, in terms of two types that are generic in a servlet
  type that is about to be spelled differently.
- **`/app/pom.xml`** — the three properties that decide this reactor's
  toolchain and its two dependency lines, and the `<dependencyManagement>`
  that imports Boot's BOM.
- **`mini-git-server-httpd/pom.xml`** — where the web stack and the embedded
  container come from, and the one place a servlet API could be declared and
  deliberately is not.
- **`/app/README.md`** — the site directory, its layout, and the property
  that points at it.

The Jakarta EE and Spring migration documentation is the other half. Jakarta
EE 9's release notes are the authority on which packages were renamed and
which were not; the Jakarta Servlet 6.0 specification's "Removed" section is
the authority on the three types §1.3 names; the Spring Framework 6 and
Spring Boot 3 migration guides are the authority on what those two majors
moved besides the namespace. Which tool you reach for — a rewrite recipe, an
IDE refactoring, or `sed` applied with more care than §1.1 warns against — is
a decision this document leaves to you; §1.1 to §1.4 say what has to be true,
not how the edit gets made.

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

# Step 4 — the deployable runs itself: a Spring Boot executable jar

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a `javax.servlet` 3.1 WAR at Java 11, wired
by **Spring Framework 5.3** from Java configuration classes with no deployment
descriptor, hosting git repositories over HTTP on JGit 5.13 and over SSH on
Apache MINA SSHD 2.19.

One thing about it has never been touched, and it is not in the source at all:
**who starts it.** The build produces an archive. A servlet container that is
not in this repository unpacks that archive, supplies the `javax.servlet` API
the application compiles against, finds the application's bootstrap class,
calls it, and owns everything from the first request to shutdown. Nothing
in the deployable is started through a `main`. It has never decided what port it listens on. Half
of its classpath is something it borrows at deploy time from a program nobody
here has ever named.

This milestone moves that job into the application. The build produces a
**Spring Boot 2.7 executable jar** with the servlet container inside it, and

```
java -jar <the jar the build produced>
```

is the whole deployment.

It is still `javax.servlet`, still Java 11, still Spring Framework 5.3, still
JGit 5.13 and SSHD 2.19. Only the deployable changes — and everything the
archive's shape was deciding for the code around it.

### 1.1 The build produces an executable jar, and no WAR

- **No module in the reactor is a war.** No pom declares
  `<packaging>war</packaging>` — resolved, so a packaging element written as a
  property reference that comes out `war` counts — and no pom names `maven-war-plugin` at all — not as a binding, and not as a
  managed version under `<pluginManagement>` either. When the build finishes, no `.war` file exists under any
  module's `target/`.
- **The build produces one deployable, and it is a Spring Boot executable
  jar.** Not a jar that has been made to run by some other means: one that
  Boot's own repackaging produced, whose manifest records the Boot version it
  was repackaged with. That repackaging writes two names, and they are
  different names. `Main-Class` is Boot's loader, which is what `java -jar`
  invokes. `Start-Class` is yours, which the loader then calls: it is a class
  this project compiled, and it declares a `public static void main(String[])`
  in this tree's own source.
- **`java -jar` is the entire start command**, with no classpath assembled
  beside it, no `-cp`, no launcher script and no directory of loose jars. The
  jar carries what it needs.

`src/main/webapp` is a WAR's layout and stops meaning anything here: a jar has
no `WEB-INF`, and nothing in one is on the classpath or reachable by a request
unless you put it somewhere that is. What the deployable serves out of it
today is part of §1.6 and does not change.

### 1.2 Spring Boot 2.7 supplies the server and the versions

- **Add Spring Boot 2.7.** At least three artifacts under the
  `org.springframework.boot` groupId resolve when you are done, and
  `spring-boot` itself is among them. That one is not optional: the loader in
  the repackaged jar, `SpringApplication`, and the embedded-container
  bootstrap all live in it.
- **Every Boot artifact on the 2.x line.** Boot 3 is built on Spring Framework
  6, which needs Java 17 and is compiled against `jakarta.servlet`. This tree
  is Java 11 on `javax.servlet`, and it is Java 11 on `javax.servlet` when you
  are done: a Boot that would move either does not fit. Spring Framework stays
  on 5.3 — §1.5 still requires it, and Boot 2.7's own dependency management
  is what keeps it there.
- **Versions come from Boot's dependency management, not from a pin at a
  time.** Either a pom imports a Spring Boot BOM into its
  `<dependencyManagement>` with `<scope>import</scope>`, or a module's
  `<parent>` is a Spring Boot one. Both are supported ways of adopting Boot
  and either satisfies this. What it buys is that the framework, the embedded
  container, the servlet API and the logging bridge stop being twenty-odd
  versions written out by hand in the root pom and start being one decision.
  Where Boot has no opinion — JGit, SSHD, this project's own libraries — the
  reactor's own `<dependencyManagement>` still decides and those pins stay.
- **One class is the application.** Something in this project's own source
  carries `@SpringBootConfiguration` (or the composed `@SpringBootApplication`)
  **and** has auto-configuration enabled (`@EnableAutoConfiguration`, or again
  the composed form). Both, because they are different: the first is what
  makes a class the primary source a Boot application is started from, and the
  second is what stands the embedded container up and binds its settings. A
  class with only the first is a Boot application serving nothing.
- **This project's own code starts it.** `SpringApplication` or
  `SpringApplicationBuilder` is written in this tree's production source or
  referenced from its compiled classes. Marking a class as the application is
  not running one.

Component scanning is neither required nor forbidden. §1.5's wiring is
declared explicitly today and can stay that way; whether you also scan is
yours to decide, subject to the wiring still being what §1.5 says it is.

### 1.3 The container and its API move inside the deployable

- **A servlet container ships inside the jar.** Tomcat, Jetty or Undertow —
  Boot 2.7 has a starter for each and this document does not choose. What it
  requires is that the running deployable carries one.
- **The servlet API ships with it.** `javax.servlet:javax.servlet-api` at
  `provided` scope was the correct declaration for a WAR: the container
  supplied the API at run time, so packaging a second copy meant the
  application fighting its host over the same classes. There is no host. The
  API is part of what you deploy now, and a jar without it cannot answer a
  request. It is still **Servlet 3.1 and still `javax`** — the programmatic
  registration §1.4 asks for is a Servlet 3.0-and-later capability and it has
  to really be there — and nothing `jakarta.servlet` arrives by any route, in
  the poms, the source or the bytecode.
- **The classpath is now entirely yours, including where it is wrong.** A
  dependency the servlet container happened to have on its own classpath was
  visible to the deployed application and is simply absent from a jar that
  runs by itself. Where the tree has been compiling against one version of a
  library and running against whatever the container supplied, the jar gets
  the one the build resolves — and that is the difference between a tree that
  deploys and a tree that starts. Resolve those against the code that needs
  them; do not paper over one by pinning the library back to a version the
  rest of the tree has moved past.
- **One provider of `org.apache.log4j` inside the deployable.** This
  project's own code names those classes directly, so zero is a
  `NoClassDefFoundError` on the first connection. Two is worse than one and is
  the easier mistake: there is more than one artifact in the world offering
  that package, they are forks of the same 2005 library, and which one a class
  loads is decided by classpath order rather than by anybody. Exactly one
  artifact inside the jar provides it.

### 1.4 The application registers its own servlets and filters

Today the HTTP configuration is handed a live `ServletContext` by the
container and calls `addServlet` and `addFilter` on it. There is no container
to hand one over any more: the container is a thing the application starts, and
the registrations become part of the object graph like everything else in
§1.5's wiring.

- **Every servlet and filter the application installs is registered by the
  application**, as part of its own configuration rather than through a
  callback the container makes. Spring Boot supports this two ways — a
  registration bean per servlet or filter, or the servlet annotations with a
  scan that picks them up — and either is a complete answer. Whichever you
  take, the URL patterns, the names, the relative order of the filters and the
  condition under which the SSL filter is in the chain at all come across
  unchanged; §1.6 is what says whether they did.
- **Nothing in the tree implements the container's bootstrap contract.** No
  production class implements `org.springframework.web.WebApplicationInitializer`
  or `javax.servlet.ServletContainerInitializer`, by an `implements` clause, by
  inheriting from something that does, or anonymously. Those are the
  interfaces by which a servlet container calls an application it has
  deployed, and there is nothing left to call them: a tree that keeps one has
  two bootstrap paths and only one of them can ever run. (A
  `ServletContextListener` is a different thing — an ordinary container
  callback that an application starting its own container may perfectly well
  still register — and is not covered by this.)
- **The lifecycle still runs in the order it runs in today.** §1.5's contexts
  come up in the order they are created and go down in the reverse of it, and
  the beans inside each that have something to start and stop do the same
  among themselves. That ordering is currently driven by the container's
  notification that the application is being undeployed. Whatever drives it
  instead, the order does not change, and neither does the fact that the
  server shuts its repositories, its work queue and its SSH daemon down
  cleanly.

In the tree you were handed, those sentences reach the class the servlet
container calls to start this application — the implementation of one of
those two interfaces — and it goes; a helper that existed only to feed it the
servlet paths has nothing left to do. Whatever
was nested inside them and doing the application's own work rather than the
container's carries on doing it somewhere else — §1.5 is what says so — and
nothing else in the tree is retired here.

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
attached so that a failing test still leaves a deployable behind, so the two
have to work as two commands and not only as one combined run. Both take their configuration from the poms and
nothing else: a `.mvn/` directory, a `.mavenrc` and a user `settings.xml` are
not read.

Everything the tree already carries holds. No module inherits from
`org.sonatype.oss:oss-parent`. No source file and no compiled class names a
JUnit 3 or JUnit 4 type; the suite is JUnit 5. EasyMock is in no module's resolved
dependencies and named by nothing. JGit resolves only from `org.eclipse.jgit`, at 5.13; everything under
`org.apache.sshd` is on 2.x and `org.apache.mina` is gone; `com.jcraft`,
`net.schmizz` and `org.bouncycastle:bcprov-jdk16` are gone from the poms, the
bytecode and the source. Spring Framework is on 5.3 everywhere, in every module
and at every scope. The object graph is the container's: the configuration is
written in Java, there is no XML bean definition document and no
`WEB-INF/web.xml` anywhere in the tree or inside the deployable, the four
contexts and their parenting are what they are, the request scope and the
custom per-request scope are still registered scopes, the qualifier annotations
still qualify, the factory interfaces still have implementations, and no bean
is selected by a Spring profile. The idioms the source was brought up to stay
brought up: diamond rather than explicit type arguments, try-with-resources
rather than a hand-written close in `finally`, multi-catch rather than repeated
catch blocks, lambdas and method references rather than single-method anonymous
classes, and `java.nio.file` used alongside `java.io.File` where it already is.

**A dependency-managing BOM is the new way to break one of those by accident.**
Importing one hands the decision on hundreds of artifacts to somebody else's
release notes, and several of the artifacts above are in the set it manages.
Adopting it is what §1.2 asks for; noticing what it moved is part of the same
job.

Apart from what §1.4 retires, the work the source you were handed does is still
being done when you are done, by the same files doing it. Every file is still
there; every job a declared type was doing is still a declared type doing it;
every behaviour a method carried is still carried by a method — including in
the corners of the tree that nothing else calls, which are the cheapest thing
to delete and the easiest to miss. A class that produces a value for injection
is a class producing a value for injection on the other side too. So is a scope
implementation, a factory interface's implementation, and a component that
starts and stops. Migrating by deletion is not migrating: a type removed, or a
method emptied, because that was the cheapest way to stop something referring
to the old deployment shape, is a regression whatever it does to the rest of
this document.

Two kinds of change are the port doing its job rather than deleting work, and
both are expected. The first: a type or a member inside a file that stays,
whose own job was talking to the servlet container about deployment — handing
it a servlet, being called by it at startup — is one of the things §1.4 orders
removed and goes the way they go even though its file stays. That is a
principle and not a licence to prune: it reaches exactly what the application
now does for itself and nothing else, and a type or a method that was doing the
application's work rather than the container's is not covered by it however
convenient deleting it would be. The second: a member whose name or signature
was dictated by an interface this document is telling you to stop implementing
takes the new shape's name instead. The work has not gone anywhere.

### 1.6 Behaviour: fixed, down to the byte

**Nothing observable moves in this milestone.** Every status, body and header
this application's servlets, filters and SSH commands compose; the
`Expires`/`Pragma`/`Cache-Control` trio on `/ssh_info`; the `Location` of its
redirects; the static files it serves and their exact bytes; the output, error
text and exit status of its SSH commands; the SSH host key material it presents
and the algorithms it negotiates; the files it writes under the site directory;
its reading of `etc/gerrit.config`, every section and key, with the same
tolerance for values it cannot parse. Changing how a program is started does
not change what it answers, and if something moves, it is a bug in the port.

That is a stronger requirement here than it looks, because more of the answer
than before is now yours to produce:

- **What was served from the archive's own layout is served by the
  deployable.** The two files this application publishes at the context root
  today come out of a WAR directory that a jar does not have. They are still
  published, at the same paths, with the same bytes and the same content
  types.
- **The container's default answers are part of what you ship.** How a request
  that matches no mapping is answered, and what happens to a path that needs
  decoding or normalising before it can be matched, were a stock container's
  behaviour and are now the behaviour of the container you chose and
  configured. A Servlet 3.1 container decodes and normalises a path before it
  matches and before it computes `getServletPath()` and `getPathInfo()`,
  exactly as its specification requires, and that is as true of an embedded
  one as of an external one. The answers do not move.
- **What is fixed about a failure is its status.** A request that matches no
  mapping, and a request that fails, answer with the status they answer with
  today. What an error page then *looks like* — the bytes of its body, the
  headers that come with it — is the container's own, and no two Servlet 3.1
  containers write the same one. That part is the chosen container's to
  decide and not yours to reproduce: you are not on the hook for making the
  new container's error page resemble the old one's, and you should not build
  anything whose purpose is to make it.
- **The port and the addresses the server binds still come from outside it**,
  and §1.7 says how. What binds them is now inside the deployable; what
  decides them is not.

**Do not write a compatibility layer.** Reimplementing the archive's
deployment shape inside the jar — a `/*` filter that matches paths by hand, a
hand-rolled mapping table consulted before the container's, an embedded
container configured to serve an exploded WAR directory — would hold the
answers where they are for the wrong reason, and it is the shape this
milestone exists to remove. Register with the container and take the
container's own behaviour as it comes.

### 1.7 How the server is run

`/app/README.md` documents the site directory and the property that points at
it, and neither of those moves. What moves is the start command, because the
thing being started is no longer an archive:

```
java -Dgerrit.site_path=<site> -Dserver.port=<port> -jar <the jar>
```

and nothing else — no container installed beside it, no deployment step, no
`-cp`, no script. Specifically:

- **The site directory still comes from the `gerrit.site_path` system
  property.** That name does not change. `/app/README.md` gives the format of
  `etc/gerrit.config` and of the `basePath` it names. How the application
  reads that property is yours — reading it directly is still correct, and so
  is binding it through the configuration mechanism the framework offers, as
  long as the same property name on the same command line still selects the
  same site.
- **The HTTP port comes from Spring Boot's own `server.port` property.** This
  is the one setting that changes hands. The port used to belong to whoever
  installed the container; the container is part of the deployable now, so it
  is configured the way Boot configures a server, and `-Dserver.port=<n>` on
  the command line is what sets it. The application is started with that
  property set and is expected to listen there, and this property takes over
  that job rather than joining whatever held it before: it is the only thing
  left that decides the listen port.
- **The SSH addresses stay in `etc/gerrit.config`**, read out of the site
  directory exactly as they are today.
- **The JVM's default time zone, the site directory and everything in it still
  belong to whoever starts the server.**

Two things follow, and both are requirements of this milestone rather than
advice:

- **The jar starts.** A deployable that `java -jar` cannot run, or that runs
  and then fails to come up, has not been migrated. Every sentence in §1.6 is
  a statement about what a *running* server answers, and there is no longer a
  container to get half way up without it.
- **The site is found, not made.** Beyond the two entries `/app/README.md`
  documents, a site holds everything else this project's own code reads out of
  one, the SSH host key material included. Whoever starts the server prepares
  that directory. The application has never had an installer and does not gain
  one: it does not stand a site up, and it does not decide for itself what a
  missing piece of one should have been. Which branch it takes when a piece is
  missing, and what it writes under a site while it runs, are part of §1.6's
  fixed contract like everything else — the code that reads a site today
  already answers both, and goes on answering them the same way.

Beyond the entry point §1.1 requires and the `server.port` named above, the
application gains no new way to select behaviour: no new flag, environment
variable, config key, request header, marker string or alternate entry point,
and no second way in beside the one `main`. In particular it does not gain a
Spring profile, and no bean is conditional on one.

**Still true of the tree you inherit, and still required.** Guice (`com.google.inject`) and `aopalliance` resolve in no module's
dependency graph, and nothing in the source, in the compiled classes or
inside the deployable names `com.google.inject` or the old
`com.google.gerrit.lifecycle` package. Neither `junit:junit` nor the JUnit
vintage engine resolves on any module's test classpath, and the suite is
written against JUnit Jupiter (`org.junit.jupiter`). No
`org.apache.tomcat:servlet-api` or `org.apache.tomcat:tomcat-servlet-api`
coordinate resolves anywhere.

## 2. Read these local references

- **`/app/mini-git-server-war/src/main/java/com/google/gerrit/httpd/WebAppInitializer.java`**
  — the four contexts, how they are parented, which one borrows a bean from
  its sibling and why, and what closes them. It is also the class the servlet
  container currently finds and calls, which is the half of it §1.4 retires.
- **The configuration under `mini-git-server-httpd` that sets up the servlets
  and filters today** — the two filters, the three servlets, their names, their
  URL patterns, their order, and the condition on the SSL one. Between them
  those classes are the complete specification of the URL space §1.4 asks you
  to register.
- **`/app/mini-git-server-war/src/main/webapp/`** — what the deployable
  publishes at the context root today, and the `WEB-INF` layout that goes away
  with the archive.
- **`/app/pom.xml` and the six module poms** — which module declares what, at
  which scope, which versions are written out by hand and what the root pom
  already manages, including the BOM it already imports.
- **`/app/README.md`** — the site directory, its layout, and the property that
  points at it.
- **`SshDaemon`, `WorkQueue`, `CachePool`, `LocalDiskRepositoryManager` and
  `SshLog`** — what starts and stops with a context, which is the ordering
  §1.4 says does not change.

Spring Boot's own reference documentation for the 2.7 line is the other half:
executable jars and how they are repackaged, dependency management and the
starters, embedded servlet containers and their configuration, registering
servlets and filters with one, externalised configuration, and what
auto-configuration does and does not do for an application that declares its
own beans. Which of the types those chapters describe you reach for is a
decision this document leaves to you; §1.1 to §1.4 say what has to be true, not
which class has to be on the line.

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

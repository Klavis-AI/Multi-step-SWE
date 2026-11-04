# Step 10 — the platform comes up to current

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a **Spring Boot 3 executable jar**,
compiled at **`release` 21** under `-Xlint:all -Werror` with no warning, wired
by **Spring Framework 6**, written against **Jakarta Servlet 6.0**, serving
every SSH command and every HTTP git request on a virtual thread. Nine
milestones brought a 2009 Java EE 6 WAR to that, and every one of them left it
one major version behind where the platform actually is:

| what runs it | now | current |
| --- | --- | --- |
| Spring Boot | 3.5.16, throughout | 4 |
| Spring Framework | 6.2.19, throughout | 7 |
| Jakarta Servlet | 6.0, from `tomcat-embed-core` 10.1.55 | 6.1, the Jakarta EE 11 version |
| JUnit | jupiter 5.10.2, platform 1.10.2 | 6 |

**This milestone moves the platform to the current major of each of those, and
then writes the four source conventions Spring Boot 4 is designed around.**
The second half is the half that is easy to skip and is not optional: a tree
can resolve Boot 4, Framework 7 and Servlet 6.1, build, start, and answer
every request exactly as it does today while still being written exactly
the way Boot 3 was written. Four things about this source are now behind its
platform, and this rung is where they stop being:

| convention | this tree today |
| --- | --- |
| packages declare their nullness | **none** of its packages of main source do |
| a method that returns `null` says so | **eighteen or so** methods return the null literal; **none** is `@Nullable` |
| a sole constructor is injected without asking | **31** sole constructors, in 27 source files, are `@Autowired` |
| a serialization member is marked as one | **a dozen** `serialVersionUID` fields; **none** is `@Serial` |

### 1.1 Spring Boot 4 supplies the platform

- **Every resolved `org.springframework.boot` coordinate is on the 4 line.**
  Not some of them: a graph that resolves `spring-boot:4.x` beside six
  artifacts still at 3.5.16 is a classpath that does not start, and it does not
  meet this requirement even in part. At least **six** Boot coordinates
  resolve when you are done, so that "nothing off the 4 line" is a statement
  about a real graph and not about an empty one.
- **Every resolved `org.springframework` coordinate is on the 7 line**, and at
  least **five** of them resolve. That groupId is matched exactly:
  `org.springframework.boot` has its own version line and is §1.1's first
  bullet, not this one. Boot 4 names a framework version in its dependency
  management, and a tree that overrides it — a `spring-framework.version`
  property is the documented way — resolves Boot 4 and Framework 6 together,
  builds, and dies at startup with a `NoSuchMethodError`. Do not.
- **Versions still come from Boot's dependency management.** The way to move
  this reactor's platform is the single property the parent pom already uses
  to name Boot's version; the way that is being asked against is a list of
  artifacts pinned one at a time. When you are done, **no `<dependency>`
  outside a `<dependencyManagement>` block names a `<version>` for an artifact
  the platform manages** — anything under `org.springframework`, `org.junit`,
  `org.jspecify`, `ch.qos.logback`, `org.slf4j`, `org.apache.tomcat.embed`,
  `com.fasterxml.jackson` or `jakarta`. Importing a BOM is itself naming a
  version and is the one place that is supposed to happen, so
  `<dependencyManagement>` is outside this; so are plugin versions, which no
  BOM here manages and which a reproducible build needs. At least **four**
  such dependencies are still declared, so that this is a statement about a
  reactor that lets the platform decide and not about a reactor with nothing
  left in it. The coordinates this project pins for itself — JGit, SSHD,
  args4j — are managed by nobody and keep their versions.

  One pinned version is enough to matter: it resolves, it builds, it starts,
  it serves every request, and it is the one artifact the next platform
  bump will leave behind.

  This has a consequence for JUnit that is worth reading before you hit it.
  **Boot 4's `spring-boot-dependencies` manages `org.junit.*` on the JUnit 6
  line**, and this reactor imports a `junit-bom` of its own ahead of it. That
  import is now the thing pinning JUnit to 5. When you are
  done, **every coordinate under `org.junit.jupiter` and `org.junit.platform`
  resolves on the 6 line**, with at least two of them resolving — and the
  reactor names no JUnit version anywhere. If a test stops compiling, fix the
  test; pinning the bom back to 5.x is the one move that undoes this.

### 1.2 The web layer comes from the modular starter

Boot 4 broke `spring-boot-starter-web` into modules and deprecated the
aggregate in favour of **`spring-boot-starter-webmvc`**; the 4.x pom of the
aggregate says so in its own description. It still publishes and it still
resolves, which is exactly why this is asked explicitly: a tree that moved
every version and left the old artifactId in place builds, starts, and serves
everything, while depending on an artifact the platform has marked as going
away.

When you are done: **`spring-boot-starter-web` is absent from the resolved
graph**, at least one of `spring-boot-starter-webmvc` or `spring-boot-webmvc`
is present, and **a servlet container is still resolved** — Tomcat or Jetty
(Boot 4 has no Undertow support), this rung does not care which, provided §1.9
still holds of it: how
an ambiguous or percent-encoded request target is answered is the
container's decision, and §1.9 fixes those answers too. That last clause is there because the
first one is also satisfied by deleting the web layer, and deleting it is not
what is being asked.

### 1.3 The servlet API is the Jakarta EE 11 one

Jakarta EE 11 is **Servlet 6.1**, and this application packages its servlet
API inside its own deployable — the embedded container ships it, so there is
no external container and no servlet coordinate in any pom. Moving Boot moves
the container, and moving the container moves the API: Boot 4 brings Tomcat 11,
which implements 6.1, where Boot 3 brought Tomcat 10.1, which implements 6.0.

When you are done, **`jakarta.servlet.http.HttpSession$Accessor` is among the
classes packaged inside the deployable**. That type is a real Servlet 6.1
addition — the supported way to hold a session reference across threads — and
it is named here rather than some 6.0 type being named as absent because
nothing was *removed* between 6.0 and 6.1. Inside the deployable rather than
in a pom, because that is where this application's servlet API lives: it
arrives within `tomcat-embed-core` today and would arrive as a standalone jar
beside Jetty, and the requirement is the same either way.

The type has to arrive the way a version arrives, inside an API, and not on
its own: **the archive that provides it carries a servlet API's worth of
`jakarta.servlet` classes** — at least 20, where the real ones hold eighty-odd
and this deployable already holds eighty-odd. And **the deployable packages one
servlet API and not two**, in both directions: exactly one archive inside it
carries a servlet API's worth of `jakarta.servlet` classes, and no archive may
provide `jakarta.servlet.http.HttpSession` without also providing
`HttpSession$Accessor`, which is what a 6.0 jar left on the classpath beside a
6.1 one looks like. Two servlet APIs in one deployable is a classloader
choosing which `HttpServletRequest` the program compiles against, and the
choice is in no pom — and two copies of 6.1 is the same coin toss as a 6.0
beside a 6.1.

A `tomcat.version` property left pinned at 10.1 resolves, starts and serves every request, and it is the specific mistake this section exists to
name.

### 1.4 Packages declare their nullness

**Every package of this project's main source states that its types are
non-null by default**, in the platform's own vocabulary:
**`org.jspecify.annotations.NullMarked`**, which is what Spring Framework 7's
own signatures carry. Either form counts — a `package-info.java` in the
package carrying the annotation, or the annotation on every top-level type the
package declares — and it may be written out in full or imported.

Top-level and not every type: JSpecify scopes are lexical, so a nested class
inside a marked outer class is already covered and does not need its own
annotation. A package that declares no top-level type is not asked to state
anything about types it does not have.

**`org.jspecify:jspecify` is declared in a pom and resolves**, and at least one
source file imports from `org.jspecify.annotations`. The jar is already in your
graph — Spring Framework 7 states the nullness of its own signatures in
JSpecify, so it arrives transitively whether or not anyone asked — and relying on that is
a build that breaks the next time the framework's dependencies move. Declare
it. Do not give it a version: the platform manages it, like everything else.

**There is exactly one nullness vocabulary in this tree.** No
`javax.annotation.Nullable`/`Nonnull`/`CheckForNull` (JSR-305),
no `jakarta.annotation.Nullable`, no `org.jetbrains.annotations.*`, no
`edu.umd.cs.findbugs.annotations.*`, no `androidx.annotation.*`, no
`org.checkerframework.*`, and no `org.springframework.lang.*` — that last one
being Spring's own, which **Framework 7 deprecated** in favour of JSpecify, so
under `-Xlint:all -Werror` reaching for it is a compile error as well as a
second dialect.
That covers imports and written-out uses alike: `@javax.annotation.Nullable`
spelled in full at the use site needs no import and is still a second
vocabulary.

### 1.5 A method that can return `null` says so

Under `@NullMarked`, a method whose return type carries no annotation is a
method promising never to hand back null. **Eighteen or so methods in this
tree's main source return the null literal.** When you are finished, **no
method in main source returns the null literal without being annotated
`@Nullable`**.

They are not listed here and will not be: the rule is the
requirement, and it is a rule about the tree you deliver rather than about a
list of places in the tree you were handed. A `return null;` inside a
lambda or an anonymous class belongs to that lambda or that class, not to the
method around it, and is asked of the declaration that actually contains it.

**Two answers count, and they are both real.** Annotate the method, or stop
returning null — an `Optional`, a throw, an empty collection, an empty array.
The one of them where the second answer is clearly right is
an `X509TrustManager.getAcceptedIssuers` that returns null into a JSSE
contract asking for an array; the rest are judgement, and this rung does not
have an opinion about which you pick.

**The ceiling is the other half of this, and it is not decoration.** "No
null-returning method lacks `@Nullable`" is satisfied by a tree that annotates
every method it declares, and such a tree has said nothing at all — it has
turned the default off everywhere, which is the same as never declaring
nullness. **At most 60 methods in main source carry `@Nullable`.** The
ceiling is more than three times what a tree that answers them honestly needs.
The allowance is for returns that merely forward a nullable value, which this
milestone neither asks for nor forbids.
`@Nullable` on a parameter or a field is a different statement and is not
counted here.

The rule is about the null literal: a method that writes `return null;` is the
case this milestone asks you to settle.

### 1.6 Constructor injection stops asking to be injected

Spring has injected a type's only constructor without `@Autowired` since
Framework 4.3, and Boot 4's documentation writes every example that way. This
tree's annotations are a vestige of Guice, where the equivalent was required,
and of the rung that rewrote `@Inject` to `@Autowired` one file at a time
because that was the smaller change to make then.

When you are finished, **no type that declares exactly one constructor
annotates it `@Autowired` or `@Inject`** — 31 such constructors today, spread
over 27 source files. Sole and not every constructor: a type with two constructors must
still say which one the container should use, and there is no such type here.

Two things this section is careful not to ask for, because doing either would
be worse than leaving the annotations alone:

- **`@Autowired` on a field or a setter is a different question and is not
  asked.** The SSH command classes args4j instantiates are injected through
  their fields, `CacheModule`'s `@Bean` factory methods are annotated for a
  different reason again, and an optional setter that can only be satisfied in
  a web context has to say `required = false` out loud. Leave those. Deleting
  field injection without replacing it is a server that starts with null
  collaborators.
- **Do not delete constructors to satisfy this.** At least 45 types must still
  declare a single constructor that takes at least one argument — they already
  do, and the count is here so that "none of them is annotated" cannot be
  reached by having none of them.

**And the build has to keep constructor parameter names.** Injection by
constructor resolves arguments by name where it can; a build that discards the
names leaves the container matching on type and position alone, which is less
than the annotation being deleted here used to say. When you are finished,
**the reactor asks javac to keep parameter names** — the
`maven.compiler.parameters` property, a `<parameters>` element in the compiler
plugin's configuration, or `-parameters` among its compiler arguments. Any of
the three, written in this repository's own poms wherever it governs the modules
that are being changed; a parent pom from outside the repository saying so does
not count. Dropping the annotations without this trades an explicit statement
for nothing.

### 1.7 Serialization members are marked as such

**Every field named `serialVersionUID` in main source carries `@Serial`** — a
dozen of them, none annotated today. `@Serial` is Java 14's way of asking the
compiler to check that a serialization member is declared the way the runtime
will actually look for it: the right name, the right type, the right
modifiers. Without it, a misspelled or mis-typed `serialVersionUID` compiles,
ships, is silently ignored, and the class quietly gets a computed UID that
changes every time its shape does.

This is the smallest thing this milestone asks for. It is asked here because
`@Serial` arrived in Java 14, after the release this reactor was first brought
up to, and because a rung about writing code the way the current
platform expects is where it belongs. The field is what carries the annotation,
not the class. At least 8 such fields must still exist: deleting them is not an
answer.

### 1.8 The platform's own upgrade path finds nothing left to do

Everything above is a statement about this tree that someone here decided to
make. This one is the platform's authors' own: **OpenRewrite's `rewrite-spring`
recipe `org.openrewrite.java.spring.boot4.UpgradeSpringBoot_4_0` — as published in
`rewrite-spring` 6.37.1 and run as a dry run through `rewrite-maven-plugin`
6.46.1 — has no structural change left to make to this reactor.**

That recipe knows things nothing else here does — the property renames, the
autoconfiguration classes Boot 4 removed, the `@ConfigurationProperties`
shapes that changed. A tree that satisfies everything above and this too has
been read by the migration tool and found finished.

What counts as a change:

- **a hunk touching a `.java` file — any line of it — is a finding.**
- **any other changed line, in a pom or in any other file — a `<groupId>`, an
  `<artifactId>`, a `<scope>`, a property, a configuration key — is a
  finding**, except the one kind below.
- **a hunk changing only a `<version>` element, or only a `<…version>` or
  `<…Version>` property, is ignored.** Those values are resolved from live
  Central metadata when the recipe runs, so counting them would flag a correct
  tree on the day some plugin publishes a release. This rung is not a clock.

Running it is not the same as finishing this milestone, and finishing this
milestone by hand is not the same as running it. Some of what §1.1 through §1.7
ask for this recipe already writes; some of it the recipe has never heard of,
and a tree that ran the recipe and stopped has not answered those sections at
all. In the other direction, a tree that answers every section above by hand can
still leave the recipe with something to say, because it knows renames and
removals that no section here names. Which half is which is not listed here —
reading the patch it proposes is the work.

### 1.9 Behaviour is fixed down to the byte

**Nothing observable moves in this milestone.** Every status, body and header
this application's servlets, filters and SSH commands compose; the
`Expires`/`Pragma`/`Cache-Control` trio on `/ssh_info`; the `Location` of its
redirects; the static files it serves and their exact bytes; the output, error
text and exit status of its SSH commands, including every line of help and
usage they print and every fixed-width column they align; the host key material
it presents and the algorithms it negotiates; the files it writes under the
site directory; the SSH audit log, still asynchronous, still
`yyyy-MM-dd HH:mm:ss,SSS` followed by an RFC 822 offset; its reading of
`etc/gerrit.config`, every section and key.

A major platform version is where that rule is hardest and matters most.
Servlet 6.1 tightens what a container accepts and changes some defaults;
Framework 7 removes APIs 6.2 deprecated; Boot 4 renames configuration
properties and moves autoconfiguration between artifacts. Every one of those
is a place where the server can come up, look healthy, and answer one byte
differently. **What this application is built on may change; what it answers
may not.**

The console is a diagnostic channel and not an answer: its verbosity is fixed
by §1.10, not its bytes.

### 1.10 What this milestone must not disturb

Everything the previous milestones established still holds. The reactor still
builds as two commands from a fresh copy of `/app` under this image's Maven and
`JAVA_HOME=/usr/lib/jvm/temurin-21` (the image's default JDK is still 11) —
`mvn -DskipTests package` and then `mvn test`, configured by the poms alone
(a `.mvn/` directory, a `.mavenrc` and a user `settings.xml` are not read) —
and both still succeed; the
suite still ships, still runs and still passes with no class that contributes
a case deleted, emptied, renamed or disabled; the compiler `release` is still
21 and every class file is still major 65; nothing reaches into a JDK-internal
API (`sun.*`, `jdk.internal.*`, the non-exported `com.sun.*`);
no module inherits from `oss-parent`; nothing names a JUnit 3 or 4 type or
EasyMock, and EasyMock is in no module's resolved
dependencies; the fourteen coordinates this reactor has already retired (`oro:oro`, `org.antlr:antlr`, `org.antlr:antlr-runtime`,
`commons-lang:commons-lang`, `commons-net:commons-net`,
`com.google.code.findbugs:jsr305`, `net.sf.ehcache:ehcache-core`,
`org.apache.velocity:velocity`, `org.apache.commons:commons-compress`,
`commons-io:commons-io`, `commons-collections:commons-collections`,
`com.madgag:util-compress`, `bouncycastle:bcprov-jdk15` and
`bouncycastle:bcpg-jdk15`, at any version) — no
`pom.xml` under `/app` declares one, no module's resolved dependency graph
contains one, and no class inside the deployable comes from one today — stay
retired, in every pom, in the resolved graph of every module, inside the deployable, and in every source file and compiled class, none of
which names a package any of them provides (`antlr`, `org.antlr`,
`com.madgag.compress`, `javax.annotation`, `net.sf.ehcache`,
`org.apache.commons.collections`, `org.apache.commons.compress`,
`org.apache.commons.io`, `org.apache.commons.lang`, `org.apache.commons.net`,
`org.apache.oro`, `org.apache.velocity`, `org.bouncycastle`); `commons-codec` is not one of them — JGit's own dependency graph legitimately
brings it back — and the rules about it are that no pom pins a version for it
and that no source file and no compiled class of this project names
`org.apache.commons.codec`; `com.jcraft`,
`net.schmizz`, `org.bouncycastle:bcprov-jdk16` and `org.apache.mina` stay
gone, `org.apache.sshd` stays on 2.x, and JGit stays on the line the ladder
moved it to; no pom declares a servlet API coordinate of its own; no `.java`
file and no compiled class names `javax.servlet`, the JDK's own `javax.*`
packages stay exactly as they are, and the Jakarta Servlet API and the servlet
container still ship inside the deployable; the object graph is still the
container's, written in Java with no XML bean definition document and no
`web.xml` anywhere, the four contexts and their parenting unchanged, the
scopes still registered, the qualifiers still qualifying, and **no bean
selected by or conditional on a Spring profile**; the build still produces one
Spring Boot executable jar with Boot's loader as `Main-Class` and this
project's own class as `Start-Class`, no module is a war and no pom names `maven-war-plugin`, the application
still registers its own servlets and filters, and nothing implements the
container's bootstrap contract; nothing names `org.apache.log4j`, logback is
still the backend behind slf4j, there is still exactly one slf4j binding
inside the deployable, the classes that log through slf4j still log through
slf4j, and the console keeps its established verbosity.

**The work comes off no thread it was put on.** SSH commands and HTTP git
requests still run on virtual threads, asked for unconditionally in the
build's own code or packaged configuration, nothing in this repository constructs a
pool, a thread factory, a worker thread or a thread priority, the bound on
concurrent commands is still read from `sshd.threads`, `sshd.batchThreads` and
`sshd.streamThreads` and still binds — when more commands are offered at once
than it allows, the excess waits, on its own thread and never on the SSH
session's i/o thread that submitted it — and the task registry behind
`show-queue` and `kill` still registers and still cancels.

**The source stays up at the language level the parent `pom.xml` already sets,
and the compiler stays in charge of saying so.** `-Xlint:all -Werror` remains in
the parent pom with no category negated, and the build still emits no warning:
`@NullMarked`, `@Nullable` and `@Serial` are plain annotations and add none, so
if the warning count comes off zero, something else moved. The legacy date API
stays gone and `java.time` stays in its place; no `.java` file names
`StringBuffer`;
the callbacks that became lambdas stay lambdas; the pattern `instanceof`
bindings stay bindings; the args4j boundary stays parameterised; and the
`@SuppressWarnings` budget stays where it was left, at no more than eight
suppressed warnings (each suppressed warning name counts, and so does a
`@SafeVarargs`) — **a suppression is not how
anything above is satisfied**, and reaching for one to quiet a Framework 7
deprecation is spending a budget the previous milestone deliberately did not
spend.

**The application gains no new way to select behaviour.** No new flag,
environment variable, config key, request header, marker string or alternate
entry point; no second way in beside the one `main`; no Spring profile and no
bean conditional on one.

The work the source you were handed does is still being done when you are
done, by the same files doing it. Every job a declared type was doing is still
a declared type doing it, and every behaviour a method carried is still carried
by a method. **Deleting code is not how a platform move is finished.** Three of
the requirements above carry an explicit floor — at least six Boot
coordinates, at least 45 single-constructor types, at least 8
`serialVersionUID` fields — and those floors are there because each of those
three is also satisfiable by subtraction. Count the source files and the
declared types you are handed if you care to; that census does not shrink to
make a requirement easier.

### 1.11 How the server is run

Unchanged, in every particular:

```
java -Dgerrit.site_path=<site> -Dserver.port=<port> -jar <the jar>
```

and nothing else beyond the JVM's default time zone, which, like the site
directory, belongs to whoever starts the server — no container installed
beside it, no deployment step, no
`-cp`, no script, no agent, no `--enable-preview`, and no system property that
names a configuration file of any kind. The site directory still comes from
`gerrit.site_path` and `/app/README.md` still gives the format of
`etc/gerrit.config`; the HTTP port still comes from Spring Boot's
`server.port`; the SSH addresses still live in `etc/gerrit.config`.

Two things follow, and both are requirements rather than advice:

- **The jar starts.** A deployable that `java -jar` cannot run, or that runs
  and then fails to come up, has not been migrated. A major version move is
  the likeliest place on this whole ladder for a tree to compile and then not
  start; build it, run it, and ask it for something.
- **`-Werror` fails builds.** A tree that does not compile produces no jar, and
  a milestone with no jar has delivered nothing, however good its intentions
  were.

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
`SpringApplicationBuilder`. No class of a `javax` Java EE API — `javax.servlet`, `javax.el`, `javax.websocket`,
`javax.security.auth.message`, `javax.persistence`, `javax.validation`,
`javax.transaction`, `javax.inject`, `javax.enterprise` or `javax.annotation` — is packaged inside
the deployable. Inside the deployable there is exactly one Commons Logging implementation,
and no log4j configuration file, `log4j-over-slf4j` or `reload4j`. The audit
log's asynchronous appender is still set, where `SshLog` builds it, to drop
nothing — `setDiscardingThreshold(0)`, and never `setNeverBlock(true)`. The
per-logger levels the logging configuration declares today stay declared at
the same levels. The cache layer still runs on Caffeine 3.x, with its bounds and its configuration keys still applied;
`SshUtil` still decodes with `java.util.Base64`; the repository manager
still expands zip archives with the JDK's own zip support; args4j stays at
2.33 or later; and nothing logs under the retired `net.sf.ehcache` or
`velocity` categories. The Security Manager API that JEP 411 deprecated for removal —
`AccessController`, `AccessControlContext`, `AccessControlException`,
`PrivilegedAction`, `PrivilegedExceptionAction`,
`PrivilegedActionException`, `Policy` and `SecurityManager` — stays unused,
and the Java 7 and 8 idioms stay where they are: diamond, try-with-resources,
multi-catch, lambdas and method references, and `java.nio.file` beside
`java.io.File`.

## 2. Read these local references

- **the parent `pom.xml`** — the one property that names the platform's
  version, the `junit-bom` import §1.1 is about, the `<dependencies>` block
  every module inherits, and the `maven-compiler-plugin` configuration already
  in place.
- **`mini-git-server-httpd/pom.xml`** — the web starter §1.2 renames, and the
  comment beside it explaining where `jakarta.servlet` comes from.
- **`mini-git-server-app/pom.xml`** — the module that is repackaged into the
  executable jar, and the comment naming the starter that brings the logging
  backend.
- **`mini-git-server-server/src/main/java/com/google/gerrit/server/config/SitePaths.java`**
  — an ordinary class in this tree, read for its shape rather than its
  contents: a package, a single constructor, and a method whose javadoc says
  what it hands back when there is nothing to hand back. Most of this tree's
  main source looks like this, and §1.4 through §1.7 are about that shape.
- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/BaseCommand.java`**
  — injection through fields rather than through a constructor, which is the
  shape §1.6 explicitly does not ask about and the one easiest to mistake for
  the shape it does.

Which sites those sections are about is not listed anywhere in this document;
finding them in the tree is the work.

Spring Boot's own migration documentation is the authority on what moved
between 3 and 4 and on the modular starters; Spring Framework's on what 7
removed; the Jakarta Servlet 6.1 specification on what the API gained;
JSpecify's on `@NullMarked` and `@Nullable`; the JDK's on `@Serial`;
OpenRewrite's on the recipe §1.8 names. Which order you do this in, and
whether you move the platform first and write the conventions after or the
other way round, is a decision this document leaves to you — §1.1 to §1.11 say
what has to be true, not how the edit gets made.

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

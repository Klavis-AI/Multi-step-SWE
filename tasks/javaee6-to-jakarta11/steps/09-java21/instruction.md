# Step 9 — the source comes up to the language

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a **Spring Boot 3 executable jar** at
**Java 21**, wired by **Spring Framework 6**, written against **Jakarta
Servlet 6.0**, logging through **slf4j in front of logback**, serving every
SSH command and every HTTP git request on a **virtual thread**. Eight
milestones have moved this application's platform from 2009 to the present.
They have not moved its prose.

The source is still written the way it was written for Java 5 and 6, and the
compiler that reads it has moved fifteen releases since. Ask that compiler
what it thinks — `-Xlint:all`, every category on — and this reactor answers
with **21 warnings**, in eight groups:

| count | what javac says | where |
| --- | --- | --- |
| 8 | `found raw type: org.kohsuke.args4j.spi.Setter` / `…spi.OptionHandler` | `CmdLineParser`, `OptionHandlerFactory`, `SocketAddressHandler`, `SubcommandHandler` |
| 4 | `implicit cast from int to short in compound assignment is possibly lossy` | `IdGenerator` |
| 2 | `PersonIdent(String,String,java.util.Date,java.util.TimeZone)` … `has been deprecated` | `IdentifiedUser` |
| 2 | `serializable class … has no definition of serialVersionUID`, `non-transient instance field … declared with a non-serializable type` | `ProjectServlet` |
| 2 | `isAdministrator() in com.google.gerrit.server.CurrentUser has been deprecated` | `BaseCommand`, `DispatchCommand` |
| 1 | `java.security.AccessController … has been deprecated and marked for removal` | `HostPlatform` |
| 1 | `URL(java.lang.String) in java.net.URL has been deprecated` | `NoShell` |
| 1 | `CmdLineException(String,Throwable) … has been deprecated` | `SocketAddressHandler` |

**This milestone brings the source up to the language it is compiled by, and
then makes that permanent by turning the compiler's own judgement into a
build failure.** When you are done, `mvn -DskipTests package` compiles every
module of this reactor under `-Xlint:all -Werror` and emits **no warning at
all**, and the pom says so, so that what this tree has been brought up to
cannot quietly rot back.

Getting to zero is not one edit repeated twenty-one times. Four of the eight
groups above are the visible end of a subject this rung is actually about —
raw types in the CLI layer, the legacy date API, the APIs the JDK has marked
for removal, and the `Serializable` contract this application never asked
for. The other four are ordinary defects that a compiler which had been
listened to would have caught in 2009.

### 1.1 The compiler's warnings become the build's failures

Every module of this reactor compiles under **`-Xlint:all`** with **all
warnings treated as errors**. That is one setting, in the parent pom, applying
to every module by inheritance, and it applies to `testCompile` as well as
`compile`.

Three specifics, because each of them is a way to do this that does not work:

- **`-Xlint:all` means all of it.** `-Xlint:all,-deprecation` is not
  `-Xlint:all`; neither is `-Xlint:all,-rawtypes,-serial`. Turning a category
  off to get to zero is not getting to zero. No category may be negated.
- **`<compilerArgs>` is where these go.** The
  `maven.compiler.compilerArgument` property and the
  `<compilerArgument>` element are *silently ignored* by the
  maven-compiler-plugin this reactor uses — the build succeeds, nothing is
  passed to javac, and the warnings you were trying to fail on are still
  merely printed. Configure the plugin's `<compilerArgs>` element, or its
  `<failOnWarning>`, and check the build output rather than assuming.
- **This is a compiler-plugin change, not a toolchain change.** The reactor
  stays at `release` 21 and every class file this build produces is still
  major version **65**. Do not reach past 21 to make a warning go away.

  Which JDK *runs* the build is a separate question, and this image answers it
  in a way worth knowing before you spend a command on it: three JDKs are
  installed and `JAVA_HOME`, `java` and `javac` all still point at the
  Temurin 11 this image has carried from the start, which cannot compile a `release` 21
  reactor at all. Build with `JAVA_HOME=/usr/lib/jvm/temurin-21`, which is the
  toolchain the build is run with. Running the build on 21 is not
  targeting 21.

`@SuppressWarnings` is not the mechanism §1.1 is asking for; §1.7 puts a
ceiling on it.

### 1.2 Dates and times move to `java.time`

Four files carry `java.util.Date`, `java.util.Calendar`,
`java.util.GregorianCalendar`, `java.util.TimeZone`, `java.text.DateFormat`
and `java.text.SimpleDateFormat` between them, across **18 uses**:
`IdentifiedUser`, `SshLog`, `AdminShowConnections` and `ShowQueue`. When you
are finished **no class this project compiles references any of those six
types**, in source or in bytecode, and `java.time` is what took their place —
at least **six** of this project's own classes reference it, up from the four
that already do.

The four sites are not interchangeable:

- **`IdentifiedUser`** builds JGit `PersonIdent` objects through the
  constructor that takes a `Date` and a `TimeZone`, which is the deprecation
  in the table above. JGit offers the same identity built from an `Instant`
  and a `ZoneId`. The author and committer identity this produces — name,
  email, timestamp, offset — is a git wire value and does not move.
- **`SshLog.MyLayout`** renders the audit log's timestamp by hand, through a
  shared `Calendar` guarded by a lock and a one-second cache of the formatted
  string. **The rendered bytes do not change**: `[` then
  `yyyy-MM-dd HH:mm:ss,SSS`, a space, the RFC 822 offset of the default zone,
  then `]`. What changes is what renders them. A `DateTimeFormatter` is
  immutable and needs neither the lock nor the cache; on a server whose log
  writer is now a virtual thread, that is worth having.
- **`AdminShowConnections`** and **`ShowQueue`** format ages and start times
  into fixed-width columns for a human at a terminal. Keep those tables as
  they are: the same headings, the same rule line, the same fields at the
  same widths in the same order. A connection row's session id and its two `HH:mm:ss` fields carry whatever the
  session and the clock say; everything else about the row stays exactly as it
  is now — its shape, its widths, its zero padding and every other character
  of it.

### 1.3 Callbacks that are values become lambdas

Some of this tree's callbacks are anonymous inner classes and some are already
a lambda or a method reference; count both if you want to know where you are
starting from. When you are finished it has **no more than 9** anonymous
classes and **at least 22** lambdas and method references between them.

Those numbers are deliberately not "zero" and "all of them". An anonymous
class that implements a multi-method interface, that declares a field, that
overrides a class rather than implements an interface, or that needs its own
`this`, is not a lambda and must not be mangled into one. The ones that
convert are the ones that are a single method implementing a single abstract
method; convert those, leave the rest, and do not write a new functional
interface to make a conversion possible.

Converting means the callback stops being a class. Lifting an anonymous body
out into a named one moves it from one column of that count to neither, and
the single-method command callbacks this rung is about are counted as classes
wherever they are declared: after this milestone at most one implementation
of `BaseCommand.CommandRunnable` or of sshd's `CommandFactory` is still a
class of any kind, anonymous or named. In the other direction, a lambda
written somewhere no callback ever was is not a conversion either.

### 1.4 `instanceof` carries its cast

This tree tests a reference's type and then casts it on the following line
**25 times**. When you are finished it does that **no more than 9** times and
uses **at least 16** pattern `instanceof` bindings instead.

A pattern binding replaces the cast; it does not replace the test. Where the
test guards something other than a cast, where the cast is to a different type
than the test, or where the binding's scope would not reach the use, leave it
alone. The remaining plain `instanceof` sites are the ones where that is true.

### 1.5 The CLI's parameterised types

`org.kohsuke.args4j.spi.Setter`, `org.kohsuke.args4j.spi.OptionHandler` and
this project's own `OptionHandlerFactory` are generic types, used raw at
**8 sites** in four files, each of those files carrying an
`@SuppressWarnings("unchecked")` that exists only because of them. When you are
finished, **every use of those three types outside
`mini-git-server-util-cli/.../CmdLineParser.java` and
`.../OptionHandlerUtil.java` carries its type arguments**.

Those two files are the exception because they are the reflective core: they
look a field's type up at runtime and hand it to a handler chosen at runtime,
and there is no type argument at the call site to write down. Parameterising
what can be parameterised and leaving a narrowed, argued suppression in the
one place that cannot is the outcome; deleting the type safety everywhere so
the warning stops is not.

The CLI's behaviour does not move: `--help` output, usage text, error
messages, exit statuses and the parsing of every option this server accepts
are all fixed by §1.8.

### 1.6 The APIs that are going away

Two absences, and they are absences of different things:

- **`java.security.AccessController` and the JEP 411 set.** `AccessController`,
  `AccessControlContext`, `AccessControlException`, `PrivilegedAction`,
  `PrivilegedExceptionAction`, `PrivilegedActionException`, `Policy` and
  `SecurityManager` are deprecated for removal, and a call to
  `AccessController.doPrivileged` on a JDK with no security manager to install
  does nothing that calling the code directly would not do. One class uses one
  of them. When you are finished **no class this project compiles references
  any of the eight**.
- **`StringBuffer`.** **No `.java` file in this repository names it** — not as
  a declared type, not as a constructor, not as an import. This is a source
  requirement rather than a bytecode one, and deliberately so:
  `HttpServletRequest.getRequestURL()` *returns* a `StringBuffer` and that is
  the servlet API's signature, not yours. You may still call it. Copy what it
  hands back into a `StringBuilder` and edit that — the buffer is yours alone
  the moment you receive it, and synchronising it was never buying anything.

### 1.7 Suppressions come down, and they are counted

This tree suppresses **10 warnings** with `@SuppressWarnings`, at 10 sites.
When you are finished it suppresses **no more than 8**, counting each suppressed warning name separately and a `@SafeVarargs` as one — a site that suppresses two names costs two.

That ceiling is the point of the milestone, not an afterthought to it.
`-Werror` over a tree whose every warning is suppressed is a build that
compiles clean and has learned nothing. A suppression is defensible where the
warning is about an API you do not own and cannot avoid — a deprecated method
you must still call because it is still the only way in — and is not
defensible where fixing the code is available. `@SuppressWarnings("serial")`
on a servlet is the clearest case of the second kind: a servlet inherits
`Serializable` from `HttpServlet` whether or not anyone will ever serialise
it, and declaring the `serialVersionUID` the compiler is asking for, and
marking the injected fields that are not serialisable `transient`, is both
shorter than the suppression and actually true.

Do not spend the budget you are given: eight is a ceiling, not a target.

### 1.8 Behaviour is fixed down to the byte

**Nothing observable moves in this milestone.** Every status, body and header
this application's servlets, filters and SSH commands compose; the
`Expires`/`Pragma`/`Cache-Control` trio on `/ssh_info`; the `Location` of its
redirects; the static files it serves and their exact bytes; the output, error
text and exit status of its SSH commands, including every line of help and
usage they print and every fixed-width column they align; the host key
material it presents and the
algorithms it negotiates; the files it writes under the site directory; the
SSH audit log, still asynchronous, still in the format §1.2 pins to the
character; its reading of `etc/gerrit.config`, every section and key.

This rung is the one where that rule does the most work, because every edit it
asks for is an edit to the expression of something rather than to the thing
expressed. A `DateTimeFormatter` pattern that renders `2026-9-18` where a
`SimpleDateFormat` rendered `2026-09-18` is a correct-looking rewrite of a
line that is now wrong. A pattern `instanceof` whose binding shadows a field
compiles and answers differently. A lambda that captures a loop variable where
the anonymous class captured a field is a different program. **What the source
says may change; what the server answers may not.**

The console is a diagnostic channel and not an answer: §1.9 fixes its
verbosity, not its bytes.

### 1.9 What this milestone must not disturb

Everything the previous milestones established still holds. The reactor still
builds as two commands from a fresh copy of `/app` under this image's Maven —
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
container's bootstrap contract; versions still come from Boot's dependency
management rather than from a pin at a time; nothing names `org.apache.log4j`,
logback is still the backend behind slf4j, there is still exactly one slf4j
binding inside the deployable, the classes that log through slf4j still log
through slf4j, and the console keeps its established verbosity.

**The idioms the earlier milestones brought this source up to stay brought
up.** Diamond, try-with-resources, multi-catch, the lambdas and method
references that were already here, and `java.nio.file` alongside
`java.io.File`: this rung adds to that set and takes nothing out of it. A
pattern `instanceof` written by unwinding a try-with-resources, or a lambda
arrived at by deleting a multi-catch, is a step backwards wearing this
milestone's clothes.

**The work comes off no thread it was put on.** SSH commands and HTTP git
requests still run on virtual threads, asked for unconditionally in the
build's own code or packaged configuration, nothing in this repository constructs a
pool, a thread factory, a worker thread or a thread priority, the bound on
concurrent commands is still read from `sshd.threads`, `sshd.batchThreads` and
`sshd.streamThreads` and still binds — when more commands are offered at once
than it allows, the excess waits, on its own thread and never on the SSH
session's i/o thread that submitted it — and the task registry behind
`show-queue` and `kill` still registers and still cancels. A lambda conversion inside `WorkQueue` that
drops a task from the registry, or an `instanceof` rewrite in `BaseCommand`
that changes which branch a command takes, undoes the virtual-thread work this
workspace already carries while completing this rung.

**The application gains no new way to select behaviour.** No new flag,
environment variable, config key, request header, marker string or alternate
entry point; no second way in beside the one `main`; no Spring profile and no
bean conditional on one. A build property that switches `-Werror` off is
exactly this, and it is worse than not adding `-Werror`.

The work the source you were handed does is still being done when you are
done, by the same files doing it. Every job a declared type was doing is still
a declared type doing it, and every behaviour a method carried is still
carried by a method. **Deleting code is not how a warning is fixed.** An
unused private method, a field nobody reads, a `catch` that cannot be entered
— if `-Xlint:all` complains about one of those, it is complaining about
something the eight previous milestones deliberately kept, and the answer is
not to remove it. Count the source files and the declared types you are
handed if you care to; that census does not shrink to make a warning go
away.

### 1.10 How the server is run

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
  and then fails to come up, has not been migrated.
- **`-Werror` fails builds.** A tree that does not compile produces no jar,
  and a milestone with no jar has delivered nothing, however good its
  intentions were. Turn the flag on *after* the warnings are gone, or turn it
  on first and work the list down — but do not deliver a reactor that stops at
  the first `[ERROR]`.

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
`velocity` categories.

## 2. Read these local references

- **`mini-git-server-util-cli/src/main/java/com/google/gerrit/util/cli/CmdLineParser.java`**
  and **`OptionHandlerFactory.java`** — §1.5's reflective core and the factory
  interface; four of the eight raw uses and the two suppressions that hide
  them.
- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/args4j/SocketAddressHandler.java`**
  and **`SubcommandHandler.java`** — the other two raw `Setter` uses, and the
  deprecated `CmdLineException` constructor.
- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/SshLog.java`**
  — §1.2's hardest half: `MyLayout`, the `Calendar` behind its lock, the
  one-second cache, and the exact timestamp format the audit log writes.
- **`mini-git-server-server/src/main/java/com/google/gerrit/server/IdentifiedUser.java`**
  — the two deprecated `PersonIdent` constructions and the git identity they
  produce.
- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/commands/ShowQueue.java`**
  and **`AdminShowConnections.java`** — §1.2's column-aligned output, and
  two of the anonymous callbacks §1.3 is about. Their comparators are
  already a lambda and a method reference; what is anonymous in them is the
  body each one hands to `startThread`.
- **`mini-git-server-server/src/main/java/com/google/gerrit/server/util/HostPlatform.java`**
  and **`IdGenerator.java`** — the JEP 411 call and the four lossy compound
  assignments.
- **`mini-git-server-httpd/src/main/java/com/google/gerrit/httpd/ProjectServlet.java`**
  and **`raw/SshInfoServlet.java`** — §1.7's two servlets, one warning about
  its serial contract and one suppressing the warning.
- **the parent `pom.xml`** — the `maven-compiler-plugin` block §1.1 configures,
  where `release` already lives.

The JDK's documentation is the authority on `java.time`, on pattern matching
for `instanceof`, on what `-Xlint` categories mean and on JEP 411; JGit's on
`PersonIdent`; args4j's on `Setter` and `OptionHandler`; the
maven-compiler-plugin's on `compilerArgs` and `failOnWarning`. Which
categories you fix first, and whether you reach for a record, a switch
expression or a text block along the way, is a decision this document leaves
to you — §1.1 to §1.10 say what has to be true, not how the edit gets made.

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

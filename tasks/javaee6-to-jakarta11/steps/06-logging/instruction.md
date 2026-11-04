# Step 6 — log4j 1.2 retires, and the audit log is ported to logback

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a **Spring Boot 3 executable jar** at Java
17, wired by **Spring Framework 6**, written against **Jakarta Servlet 6.0**,
serving git over HTTP on JGit and over SSH on Apache MINA SSHD. `java -jar`
starts it, and every enterprise API in it is spelled `jakarta`.

Its logging is still 2009's. Four of this project's own classes name
`org.apache.log4j`, all four declared in a single source file; one log4j 1.2
configuration file steers the whole application; and `SshLog` — the class
that writes the SSH audit trail, and the file those four classes are in — does
not merely *use* log4j: it constructs a log4j appender, gives it a log4j
layout, wraps it in a log4j asynchronous appender and pushes log4j events into
it by hand.

**log4j 1.2 has been end-of-life since August 2015.** It is not log4j 2 with
smaller numbers — it is a separate, abandoned codebase with no security
maintenance, and it is the one component of this stack with no supported
successor of its own name. The industry's answer, and the one the rest of this
tree is already half-way to, is **slf4j in front of logback**: fourteen of this
project's classes already log through slf4j and have for the whole ladder.

This milestone finishes that. When you are done **nothing in this application
— not the source, not the bytecode, not a pom, not the deployable — carries
log4j 1.2 in any form, logback is the backend behind slf4j, and the SSH audit
log is still written, still in its own format, still asynchronously, still
without dropping records.**

Retiring a logging backend is not a dependency swap, and this one in
particular is not. A facade call site (`logger.info(...)`) is portable by
construction; an *appender* is not. `SshLog` is an appender, a layout and an
error handler, and none of that has an slf4j spelling — every line of it has
to be written against the new backend directly. **The bridges do not help
here.** `log4j-over-slf4j` republishes the `org.apache.log4j` package so that
somebody else's unmaintained jar keeps working: it ships the *caller* side —
`Logger`, `Level`, `Category` — and it deliberately does not ship
`DailyRollingFileAppender`, `AsyncAppender`, or a `LoggingEvent` you can
construct and read a timestamp back out of. A tree that adopts it does not
compile, and a tree that adopts it for the parts that do compile has not
removed the package. §1.1 is written about the package for that reason.

### 1.1 log4j 1.2 is gone, in all four places it can hide

- **No `.java` file in this repository names a type under `org.apache.log4j`**
  — not in an import, and not written out inline at the point of use.
- **No class this project compiles references `org.apache.log4j`** either.
  Source and bytecode are asked separately because they can disagree: a
  deleted import with a fully-qualified use left in a method body compiles
  fine, and it is the constant pool that the class loader will act on.
- **No pom declares log4j 1.2 or a binding to it.** That means the
  `log4j:log4j` coordinate itself and equally `slf4j-log4j12`, its
  relocation `slf4j-reload4j`, and `reload4j` — the maintained fork is a
  fork of the thing being retired, and arriving at it is not arriving
  anywhere. It also means `log4j-over-slf4j`: the bridge republishes the
  package rather than removing it, so declaring it is not a way out of
  this bullet or the next one.
- **Zero archives inside the deployable provide the `org.apache.log4j`
  package.** Not one, zero. The previous milestone required *exactly one*
  provider, because four of this project's classes named the API and zero
  would have been a `NoClassDefFoundError` at the first log line. This is the
  milestone that removes those four references, so this is the milestone where
  zero becomes the right number. A jar that republishes the package under any
  artifact name is an archive providing the package.
- **No log4j configuration file is packaged with the application's own
  resources.** A configuration for a framework that is no longer there is
  dead weight that reads like live configuration, and the next person to
  edit it will believe it does something.

### 1.2 logback is what is behind slf4j now

- **The deployable carries logback**, and in both of the artifacts logback is
  published as: `logback-classic`, which is the slf4j binding, and
  `logback-core`, which is where the appenders live. §1.3 needs the second
  directly and the application needs the first; a deployable with one of them
  is a deployable that will not start.
- Deleting a backend and shipping none is not this milestone. slf4j with no
  provider on the classpath does not fail — it binds to a no-op, prints one
  line about it, and silently discards everything the application logs.

### 1.3 The SSH audit log survives the port

`SshLog` writes one record per SSH command: when it ran, which session and
which account, the command line as given, how long it took and what it exited
with. It is an audit trail — the file a site operator reads to find out who
did what — and it is the reason this milestone is a port rather than a
version bump.

- **The class that writes it builds its appender out of logback.** It names a
  logback appender that writes to a file; it names logback's asynchronous
  appender, which is what the tree already had and what keeps a slow disk out
  of the SSH command path; it names something that shapes an event into text,
  because an appender with nothing to format with writes nothing; and it names
  no log4j type at all. These are asked of the file that declares the audit
  logger, not of the tree at large — satisfying them in some other class is
  not satisfying them.
- **The log is still written, wherever the server runs.** Every server
  configuration this application supports still produces its audit file under
  the site directory, under the name it has today.
- **Its format is unchanged, down to the punctuation.** The bracketed
  timestamp with its offset, the session identifier, the account, the quoted
  command, the two durations and the exit status: an audit log's format is its
  contract with whatever reads it, and this port does not get to modernise it.
- **Its volume is unchanged.** Every command that produced a record before
  produces one now. Two of logback's asynchronous-appender settings will
  quietly take that away from you: by default it drops events below a
  threshold once its queue fills, and it can be told never to block, which
  means discarding instead. An audit log drops nothing — configure it so, and
  say so explicitly rather than relying on a default that a logback upgrade
  may change: where `SshLog` builds the asynchronous appender,
  `setDiscardingThreshold(0)`, and never `setNeverBlock(true)`.
- **It is still asynchronous.** The appender the tree has today hands the
  record off rather than writing it on the SSH command thread, and that
  property comes across with it.

### 1.4 The levels that steer this application stay declared

The configuration this tree has today does not just switch logging on. It
sets a root level and then overrides fourteen loggers individually, and five
of those overrides are load-bearing: they are what keep an SSH library's per-packet
chatter, an SSH key provider's startup noise and a cache's eviction log out of
an operator's console while leaving this application's own messages in it.

**Those levels are still declared in the deployable, at the values they have
today**: the root logger at `DEBUG`, `com.google.gerrit` at `INFO`,
`org.apache.sshd.common` at `WARN`, `org.apache.sshd.server` at `INFO`,
`org.apache.sshd.common.keyprovider.FileKeyPairProvider` at `INFO`, and
`net.sf.ehcache` at `WARN`. Whether you express them in the logging
framework's own configuration document or through the application framework's
logging properties is your business; that all six are declared, and declared
at those values, is not. A milestone that carried the appender across and
dropped the levels would have changed what this server prints while appearing
to change nothing.

### 1.5 Velocity stops asking for a log4j log system

The HTML templating engine is configured in code with a runtime log system
class, and today that class is log4j's. Point it somewhere that still exists:
**no runtime property this application sets names a log4j log system or
chute.** The engine keeps logging — this is a redirection, not a mute.

### 1.6 One `org.apache.commons.logging`, not two

**Exactly one archive inside the deployable provides the
`org.apache.commons.logging` package.** Today two do: the one Spring brings as
its own implementation, and a bridge that arrives as a compile-scope
transitive of the SSH library. Both publish `Log` and `LogFactory`, several
hundred classes across the Spring modules in this deployable call into that
package, and which of the two they get is decided by classpath order rather
than by anybody. This is part of the logging stack and it is this milestone's
to resolve — leaving both is leaving the application's most heavily used
logging API undecided. Which provider you keep is a judgement; that there is
one of them is the requirement.

### 1.7 What this milestone must not disturb

Three properties belong to this rung specifically, because before it nothing
in the ladder could break them:

- **The console keeps the verbosity it has today.** The root level in §1.4 is
  `DEBUG` and a running server prints debug-level output in quantity; a
  configuration that declares the right levels in a document the application
  framework never loads satisfies §1.4 and breaks this one. The declaration and the
  running server are asked separately on purpose.
- **Exactly one slf4j binding is inside the deployable.** Two providers is two
  answers to "where does a log line go", and slf4j picks one of them
  arbitrarily and warns.
- **The classes that log through slf4j today still log through slf4j.** The
  fourteen production classes that hold an slf4j logger keep holding one.
  "Unifying" them onto the backend's own API works, and it makes fourteen
  files depend on a backend they had no business knowing about — the facade is
  the point of having one.

And everything the previous milestones established still holds. The reactor
still builds as two commands from a fresh copy of `/app` under this image's
Maven and JDK 17 (`JAVA_HOME=/usr/lib/jvm/temurin-17`; the image defaults to
11) — `mvn -DskipTests package` and then `mvn test`, configured by the poms alone
(a `.mvn/` directory, a `.mavenrc` and a user `settings.xml` are not read) —
and both still
succeed; the suite still ships, still runs and still passes with no class that
contributes a case deleted, emptied, renamed or disabled; every module still
compiles through the compiler's `release` at 17 and every class file the build
produces is major version 61; nothing reaches into a JDK-internal API
(`sun.*`, `jdk.internal.*`, the non-exported `com.sun.*`); no module inherits from `oss-parent`; nothing
names a JUnit 3 or 4 type or EasyMock, and EasyMock is in no module's resolved
dependencies; `com.jcraft`, `net.schmizz`,
`org.bouncycastle:bcprov-jdk16` and `org.apache.mina` stay gone and
`org.apache.sshd` stays on 2.x; JGit stays on the line the previous milestone
moved it to; no pom declares a servlet API coordinate of its own; no `.java`
file and no compiled class names `javax.servlet`, the JDK's own `javax.*`
packages stay exactly as they are, and the Jakarta Servlet API and the servlet
container still ship inside the deployable; the object graph is still the
container's, written in Java with no XML bean definition document and no
`web.xml` anywhere, the four contexts and their parenting unchanged, the
scopes still registered, the qualifiers still qualifying, and no bean selected
by or conditional on a Spring profile; the build still produces one Spring
Boot executable jar with Boot's loader as `Main-Class` and this project's own
class as `Start-Class`, no module is a war and no pom names `maven-war-plugin`, the application still registers
its own servlets and filters, and nothing implements the container's bootstrap
contract; versions still come from Boot's dependency management rather than
from a pin at a time; and the idioms the source was brought up to — diamond,
try-with-resources, multi-catch, lambdas and method references, `java.nio.file`
alongside `java.io.File` — stay brought up.

The work the source you were handed does is still being done when you are
done, by the same files doing it. `SshLog` is rewritten, not removed: every
job a declared type was doing is still a declared type doing it, and every
behaviour a method carried is still carried by a method. Deleting the audit
logger is the cheapest way to stop naming `org.apache.log4j` and it is not a
migration; neither is emptying a method, stubbing a class that produces a
value for injection, or dropping a component that starts and stops. The number
of warnings the tree suppresses with `@SuppressWarnings` does not grow,
counting each suppressed warning name separately, and counting a `@SafeVarargs` as a suppression too.

### 1.8 Behaviour is fixed down to the byte

**Nothing observable moves in this milestone.** Every status, body and header
this application's servlets, filters and SSH commands compose; the
`Expires`/`Pragma`/`Cache-Control` trio on `/ssh_info`; the `Location` of its
redirects; the static files it serves and their exact bytes; the output, error
text and exit status of its SSH commands; the host key material it presents
and the algorithms it negotiates; the files it writes under the site
directory; its reading of `etc/gerrit.config`, every section and key, with the
same tolerance for values it cannot parse. Changing where diagnostics are
written does not change what a program answers, and if something this
repository composes moves, it is a bug in the port.

The console is a diagnostic channel and not an answer: §1.7 fixes its
verbosity, not its bytes. Layouts differ between frameworks in ways nothing
depends on, and the line format the console prints is yours to choose. The
audit log is the exception and §1.3 is why — it is a file this application
writes on purpose for somebody else to read, so its format is an answer and it
is fixed.

### 1.9 How the server is run

Unchanged, in every particular:

```
java -Dgerrit.site_path=<site> -Dserver.port=<port> -jar <the jar>
```

and nothing else beyond the JVM's default time zone, which, like the site
directory, belongs to whoever starts the server — no container installed
beside it, no deployment step, no
`-cp`, no script, and in particular **no system property that names a logging
configuration file**. The configuration ships inside the deployable and is
found there. The site directory still comes from `gerrit.site_path` and
`/app/README.md` still gives the format of `etc/gerrit.config`; the HTTP port
still comes from Spring Boot's `server.port` and that property is still the
only thing that decides the listen port; the SSH addresses still live in
`etc/gerrit.config`. The site directory and everything in it still belong to
whoever starts the server — including the log directory, which the application
still writes into rather than standing up.

Two things follow, and both are requirements rather than advice:

- **The jar starts.** A deployable that `java -jar` cannot run, or that runs
  and then fails to come up, has not been migrated. A logging configuration
  the backend rejects at startup is a specific way to fail this: logback
  reports a broken configuration on its own status stream and carries on with
  no appenders, which looks like a server that came up and wrote nothing.
- **The application gains no new way to select behaviour.** No new flag,
  environment variable, config key, request header, marker string or alternate
  entry point, no second way in beside the one `main`, no Spring profile, and
  no bean conditional on one.

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
`javax.transaction`, `javax.inject`, `javax.enterprise` — is packaged inside
the deployable.

## 2. Read these local references

- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/SshLog.java`** —
  the audit logger: the appender it builds, the layout that writes the record
  format §1.3 fixes, the asynchronous wrapper, and the per-record fields it
  carries from the command thread to the layout.
- **the application module's `src/main/resources/`** (`mini-git-server-app/`
  in a tree that kept the ladder's name for it) — the logging configuration
  the application ships today, which is where §1.4's six levels live.
- **`GerritGlobalConfiguration`** — the Velocity runtime properties of §1.5.
- **The one source file that imports `org.apache.log4j`** — it is `SshLog`,
  and the four classes declared in it are the whole of §1.1's source scope.
  A search for other log4j-naming files in this tree finds none; the rung is
  narrow at the source and wide everywhere else.
- **`mini-git-server-httpd/pom.xml`** — the Boot starter here carries an
  exclusion aimed at the logging stack, put there when log4j was the backend.
- **`/app/README.md`** — the site directory, its layout, and the property that
  points at it.

The logback documentation is the other half: its manual is the authority on
configuring an appender in code, on the asynchronous appender's queue
behaviour and on what its defaults discard; the slf4j documentation is the
authority on what a binding is and on which bridge does what. Which route you
take — a migration recipe, a hand port, or something else — is a decision this
document leaves to you; §1.1 to §1.7 say what has to be true, not how the edit
gets made.

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

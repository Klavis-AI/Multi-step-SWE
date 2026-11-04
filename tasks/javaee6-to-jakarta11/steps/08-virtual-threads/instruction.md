# Step 8 — the work comes off the thread pools

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a **Spring Boot 3 executable jar** at Java
17, wired by **Spring Framework 6**, written against **Jakarta Servlet 6.0**,
logging through **slf4j in front of logback**, with a dependency graph in which
nothing is older than the framework carrying it. Seven milestones have moved
this application forward and not one of them has touched the way it runs work.

The way it runs work is from 2009. Every unit of work this server does —
every SSH command, every git request over HTTP — occupies one operating-system
thread from the moment it is accepted until the moment it finishes, and
because operating-system threads are expensive, the application keeps a small
fixed number of them and makes work queue for one. `git-upload-pack` spends
almost all of its wall-clock blocked: reading the client's `want` lines off a
socket, reading objects off a disk, writing a pack back down the socket.
Blocked or running, it is holding a thread the whole time.

Three places build those threads:

- **`WorkQueue.Executor extends ScheduledThreadPoolExecutor`**, in
  `mini-git-server-server`. Every queue in the application is an instance of
  it. Its constructor installs an anonymous `ThreadFactory` that names and
  numbers the workers (`SSH-Interactive-Worker-1`, …) and, indirectly, fixes
  how many there can ever be.
- **`CommandExecutorQueueProvider` and `StreamCommandExecutorProvider`**, in
  `mini-git-server-sshd`, which size three of those queues out of
  `etc/gerrit.config` — `sshd.threads`, `sshd.batchThreads`,
  `sshd.streamThreads` — wrap each one's factory in a second anonymous
  `ThreadFactory` that drops the new thread to `Thread.MIN_PRIORITY`, and keep
  them alive from startup to shutdown whether or not anything is being asked of
  the server.
- **`BaseCommand.startThread`**, which sidesteps all of that for the commands
  that must not occupy a worker — `new Thread(tt, tt.toString()).start()`, one
  platform thread per admin command, unqueued and unbounded. It is the only
  `Thread` this application constructs that is not the launcher's shutdown
  hook.

**This milestone puts the work on virtual threads.** When you are done, the
thread that carries an SSH command is a virtual thread, the thread that carries
an HTTP git request is a virtual thread, no platform thread in this application
carries either, and nothing in this repository constructs a pool, a thread
factory or a worker thread to make that happen.

Virtual threads are a JDK 21 API on a JDK 21 runtime, so §1.6 moves the
reactor's `release` to 21. That is a consequence of this milestone, not a
separate one, and it is the only toolchain change it makes.

### 1.1 What "runs on a virtual thread" means here

It means exactly what `Thread.isVirtual()` means, as it reads while the work is
in flight rather than as the source reads:

- While an SSH `git-upload-pack` is running, the thread whose stack holds this
  project's `com.google.gerrit.sshd` frames and JGit's
  `org.eclipse.jgit.transport` frames underneath them is **virtual**.
- While a git request is being served over HTTP, the thread whose stack holds
  the servlet container's `service` frames and this project's
  `com.google.gerrit.httpd` frames underneath them is **virtual**.
- **No platform thread carries either.** A thread pool that hands its tasks to
  virtual threads and then blocks a platform thread waiting for them has moved
  nothing.
- While that work is in flight, this JVM is running **at least four** virtual
  threads. A tree that reports virtual threads which are not doing the work,
  or does the work on a platform thread while some unrelated virtual thread
  idles, does not satisfy this.

Two traps live in the names, and both are real in this tree, so do not reason
about threads by what they are called:

- JGit's `InterruptTimer` names a **platform** helper thread after the command
  it is timing — `SSH git-upload-pack '/demo.git' (tester)-Timer`. A thread
  whose name mentions a command is not evidence that the command is on it.
- `BaseCommand$TaskThunk.run` **renames whatever thread it is running on** for
  the duration of the command and puts the old name back afterwards. This is
  true before and after your change, and it means the name of the thread says
  nothing about what kind of thread it is.

The accepting side is not asked to move. Apache MINA SSHD's own i/o threads
(`sshd-SshDaemon…-nio2-thread-N`), the container's acceptor, the JVM's
compiler and GC threads, the logging appender's worker and the carrier threads
the JDK schedules virtual threads onto are all platform threads and stay that
way. What this milestone moves is the work: the command, and the request.

### 1.2 Nothing builds a pool, a factory, a worker thread or a priority

Four things must be absent from this repository's own source and classes when
you are done. Test sources are excluded from all four; everything else that
this build compiles is in scope.

- **No thread pool type.** Nothing extends, instantiates or is typed as
  `ThreadPoolExecutor`, `ScheduledThreadPoolExecutor` or `ForkJoinPool`, and
  nothing calls the `java.util.concurrent.Executors` factories that build one
  — `newFixedThreadPool`, `newCachedThreadPool`, `newScheduledThreadPool`,
  `newWorkStealingPool`, `newSingleThreadExecutor`. Replacing one pool with
  another pool is not this milestone.
- **No thread factory.** Nothing in this tree implements
  `java.util.concurrent.ThreadFactory` — named class, anonymous class or
  lambda — and nothing calls `setThreadFactory`. Calling
  `Thread.ofVirtual().factory()` and handing the result to something that
  wants a `ThreadFactory` is fine: what is forbidden is writing one, and
  configuring a pool with one.
- **No constructed worker.** The only thread this tree may build for itself
  is the one the launcher hands to `Runtime.getRuntime().addShutdownHook`,
  which takes a `Thread` by signature and cannot be given a virtual one to any
  effect. That is true of every way of writing one: no `new Thread(...)`
  anywhere else, no class of this project's own extending `Thread` anywhere
  else, and no `Thread.ofPlatform()` anywhere else either — the builder is
  new in 21 and it builds the same 2009 worker. `BaseCommand.startThread`'s
  goes.
- **No thread priority.** Nothing calls `Thread.setPriority`. A virtual thread
  has no priority to set; the two sites that lower workers to
  `Thread.MIN_PRIORITY` today are describing a scheduling policy that no
  longer exists.

Deleting the two `setPriority` calls is the correct outcome for those two
sites. Deleting the queues they configure is not — see §1.3.

### 1.3 Virtual execution is asked for, not merely permitted

Say so somewhere the build carries: enable it as a property in the packaged
application configuration (`spring.threads.virtual.enabled`), or ask for it in
code through the JDK's or Spring's API for it — `Thread.ofVirtual`,
`Thread.startVirtualThread`, `Executors.newVirtualThreadPerTaskExecutor`, or a
`org.springframework.core.task` / `org.springframework.scheduling` executor
told `setVirtualThreads(true)`. Both routes, in the two halves of the
application that need them, is the expected shape: the embedded container is
auto-configured by the property, and the SSH side builds its own queues and is
not.

Whichever route you take, it is **on, unconditionally**. Not behind a profile,
not behind a conditional bean, not behind a flag, an environment variable or a
config key that could be turned off — see §1.8.

### 1.4 The bound on concurrent commands survives the pools

`sshd.threads` and `sshd.batchThreads` look like thread-count knobs and they
are not only that. `git-upload-pack` forks a pack builder, holds a repository
open and walks its object database; `gerrit gsql` holds a database session.
The number that bounds how many of those may happen at once is there because
the machine has a finite amount of memory and disk bandwidth, not because
threads are expensive. Virtual threads make the thread free. They do not make
the pack builder free.

So, with the site's `etc/gerrit.config` exactly as it is:

- **Both keys are still read**, from the same section, by the same component,
  with the same defaults and the same arithmetic turning them into a bound.
  `sshd.streamThreads` is read too, and still sizes the stream queue.
- **The bound still binds.** When more SSH commands are offered at once than
  the configured bound allows, the excess **waits**; it does not run. A tree
  in which every offered command starts immediately has deleted the bound,
  whatever its configuration still says.
- **Waiting happens on the command's own thread, not on the caller's.** The
  thread that submits a command is the SSH session's i/o thread; if
  submission blocks, the session stops reading its socket, and a server whose
  i/o threads are all parked inside `submit` has stopped serving everyone --
  including the connections that were not asking for the bounded work. Bound
  the work after it is on its own thread, not before.

`sshd.threads` keeping its name and its meaning while no longer naming a count
of platform threads is the intended outcome. Renaming the keys, adding a new
one beside them, or dropping the batch/interactive split is not.

### 1.5 The task registry survives the executor

`WorkQueue` is two things bolted together: the executor, which this milestone
replaces, and a task registry, which it does not. The registry is what the
`kill` command looks a task up in, and what `ShowQueue` would print if anything
registered it. Nothing does: `kill` is a top-level command here, the `gerrit`
command's own subcommands are `receive-pack` and `show-connections`, and
`ShowQueue` is wired to no name at all, so it never runs. It still stays --
this rung changes what runs a task, not which types the tree declares, and an
unreachable command is not an invitation. So the only view of the registry
from outside is `kill`, and on a server with nothing running `kill` answers
`No such task` for every id it is given -- which is exactly what a server that
had thrown the registry away would answer too. The registry is spelled out here
because no amount of poking at a running server distinguishes the two, and not
because it is optional. When you are done, and regardless of what runs the
tasks underneath:

- `WorkQueue` still declares `getDefaultQueue()`, `createQueue(int, String)`,
  `getTasks()`, `getTask(int)` and `stop()`.
- `WorkQueue.Lifecycle` still exists and still declares `start()` and
  `stop()`, and the queues still stop when the context does.
- `CancelableRunnable` is still an interface, still extends `Runnable`, still
  declares `cancel()`.
- `Task` still declares `getTaskId()`, `getState()`, `cancel(boolean)`,
  `isCancelled()`, `isDone()`, `run()` and `toString()`, and still carries the
  identity, start time and delay that `show-queue` prints.
- `Task.State` is still an enum and still declares all six of `DONE`,
  `CANCELLED`, `RUNNING`, `READY`, `SLEEPING` and `OTHER`, with the same
  meanings — `getState()` still distinguishes a task that is running from one
  that is waiting its turn.
- `ProjectTask` still exists, still implements `ProjectRunnable`, and still
  declares `getProjectNameKey()`, `getRemoteName()` and
  `hasCustomizedPrint()`.

Every task that runs through a queue is still registered while it runs and
still removed when it finishes, and a task cancelled through the registry
still stops. The registry is roughly two hundred lines of this milestone's
main file and none of what it does is made obsolete by a virtual thread.

### 1.6 The toolchain moves to 21

`maven.compiler.release` moves from 17 to 21 for every module, and every class
file this build produces is major version **65**. Virtual threads are preview
API before 21: at `release=17` the tree does not compile against
`Thread.ofVirtual` at all, and `--enable-preview` is not the answer here — it
is not a supported production mode, and this migration's destination is Java 21
proper.

This is a `release` bump and nothing more: it is here because the API this
milestone is about does not exist below it. Rewriting source idiom is not part
of it — do not reach for newer language constructs because the compiler would
now accept them. Move the number, let the class files follow, and leave the
source's shape to §1.8's rule that what the tree does today it still does when
you are finished.

Which JDK *runs* the build is a separate question from what the build targets,
and this image answers it in a way worth knowing before you spend a command on
it: three JDKs are installed and `JAVA_HOME`, `java` and `javac` all still
point at the Temurin 11 this image has carried from the start, which cannot compile a
`release` 21 reactor at all. Build with `JAVA_HOME=/usr/lib/jvm/temurin-21`,
which is the toolchain the build is run with.

### 1.7 Behaviour is fixed down to the byte

**Nothing observable moves in this milestone.** Every status, body and header
this application's servlets, filters and SSH commands compose; the
`Expires`/`Pragma`/`Cache-Control` trio on `/ssh_info`; the `Location` of its
redirects; the static files it serves and their exact bytes; the output, error
text and exit status of its SSH commands, including every line of help and
usage they print; the host key material it presents and the algorithms it
negotiates; the files it writes under the site directory; the SSH audit log,
still in its own format, still asynchronous, still naming the thread it names
today; its reading of `etc/gerrit.config`, every section and key.

Changing which thread runs a program does not change what the program answers.
It does change what can go wrong while it answers: a `synchronized` block held
across a blocking call pins a virtual thread to its carrier, and a
`ThreadLocal` written by a task that used to outlive one request and now does
not will read differently. This application's request-scoped state, its
`RequestCleanup`, its `ThreadLocal`-backed scopes and its audit log all cross
this boundary. **Every one of them must still work**, and what counts is
what this server answers, not how the code that answers it is arranged.

The console is a diagnostic channel and not an answer: §1.8 fixes its
verbosity, not its bytes.

### 1.8 What this milestone must not disturb

Everything the previous milestones established still holds. The reactor still
builds as two commands from a fresh copy of `/app` under this image's Maven —
`mvn -DskipTests package` and then `mvn test`, configured by the poms alone
(a `.mvn/` directory, a `.mavenrc` and a user `settings.xml` are not read) —
and both still succeed; the
suite still ships, still runs and still passes with no class that contributes
a case deleted, emptied, renamed or disabled; nothing reaches into a
JDK-internal API (`sun.*`, `jdk.internal.*`, the non-exported
`com.sun.*`); no module inherits from `oss-parent`; nothing names
a JUnit 3 or 4 type or EasyMock, and EasyMock is in no module's resolved
dependencies; the fourteen coordinates this reactor has already retired (`oro:oro`, `org.antlr:antlr`, `org.antlr:antlr-runtime`,
`commons-lang:commons-lang`, `commons-net:commons-net`,
`com.google.code.findbugs:jsr305`, `net.sf.ehcache:ehcache-core`,
`org.apache.velocity:velocity`, `org.apache.commons:commons-compress`,
`commons-io:commons-io`, `commons-collections:commons-collections`,
`com.madgag:util-compress`, `bouncycastle:bcprov-jdk15` and
`bouncycastle:bcpg-jdk15`, at any version) — no `pom.xml` under `/app` declares one, no module's resolved
dependency graph contains one, and no class inside the deployable comes from
one today — stay retired, in every pom, in the resolved graph of every module,
inside the deployable, and in every source file and compiled class, none of
which names a package any of them provides (`antlr`, `org.antlr`,
`com.madgag.compress`, `javax.annotation`, `net.sf.ehcache`,
`org.apache.commons.collections`, `org.apache.commons.compress`,
`org.apache.commons.io`, `org.apache.commons.lang`, `org.apache.commons.net`,
`org.apache.oro`, `org.apache.velocity`, `org.bouncycastle`);
`commons-codec` is not one of them — JGit's own dependency graph legitimately
brings it back — and the rules about it are that no pom pins a version for it
and that no source file and no compiled class of this project names
`org.apache.commons.codec`; `com.jcraft`, `net.schmizz`, `org.bouncycastle:bcprov-jdk16` and
`org.apache.mina` stay gone and `org.apache.sshd` stays on 2.x; JGit stays on
the line the ladder moved it to; no pom declares a servlet API coordinate of
its own; no `.java` file and no compiled class names `javax.servlet`, the
JDK's own `javax.*` packages stay exactly as they are, and the Jakarta Servlet
API and the servlet container still ship inside the deployable; the object
graph is still the container's, written in Java with no XML bean definition
document and no `web.xml` anywhere, the four contexts and their parenting
unchanged, the scopes still registered, the qualifiers still qualifying, and
**no bean selected by or conditional on a Spring profile**; the build still
produces one Spring Boot executable jar with Boot's loader as `Main-Class` and
this project's own class as `Start-Class`, no module is a war and no pom names `maven-war-plugin`, the application
still registers its own servlets and filters, and nothing implements the
container's bootstrap contract; versions still come from Boot's dependency
management rather than from a pin at a time; the idioms the source was brought
up to — diamond, try-with-resources, multi-catch, lambdas and method
references, `java.nio.file` alongside `java.io.File` — stay brought up;
nothing names `org.apache.log4j`, logback is still the backend behind slf4j,
there is still exactly one slf4j binding inside the deployable, the classes
that log through slf4j still log through slf4j, and the console keeps its
established verbosity.

**The application gains no new way to select behaviour.** No new flag,
environment variable, config key, request header, marker string or alternate
entry point; no second way in beside the one `main`; no Spring profile and no
bean conditional on one. `spring.threads.virtual.enabled` in the packaged
configuration is not an exception to this — it is a framework property that
must be **set**, unconditionally and in the build's own resources, rather than
a switch this application reads, defaults or documents. Adding it while
leaving a way to turn it off is worse than not adding it.

The work the source you were handed does is still being done when you are
done, by the same files doing it. Every job a declared type was doing is still
a declared type doing it, and every behaviour a method carried is still carried
by a method. Emptying a method, stubbing a class that produces a value for
injection, dropping a component that starts and stops, or deleting the
registry of §1.5 or the bound of §1.4 because a virtual thread made the pool
they hung off unnecessary, is not a migration. The number of warnings the tree
suppresses with `@SuppressWarnings` does not grow, counting each suppressed warning name separately, and counting a `@SafeVarargs` as a suppression too.

### 1.9 How the server is run

Unchanged, in every particular:

```
java -Dgerrit.site_path=<site> -Dserver.port=<port> -jar <the jar>
```

and nothing else beyond the JVM's default time zone, which, like the site
directory, belongs to whoever starts the server — no container installed
beside it, no deployment step, no
`-cp`, no script, no agent, and no system property that names a configuration
file of any kind. In particular **no `--enable-preview`, no
`-Djdk.virtualThreadScheduler.*`, and no JVM flag of any kind beyond that time zone is part of how this
server is started**: whatever this milestone needs, it needs from the
code and the packaged configuration. The site directory still comes from
`gerrit.site_path` and `/app/README.md` still gives the format of
`etc/gerrit.config`; the HTTP port still comes from Spring Boot's
`server.port`; the SSH addresses still live in `etc/gerrit.config`.

Two things follow, and both are requirements rather than advice:

- **The jar starts.** A deployable that `java -jar` cannot run, or that runs
  and then fails to come up, has not been migrated.
- **It keeps serving under concurrent load.** This milestone's
  properties hold on a running server with several commands and requests in
  flight at once, not only on a tree at rest.

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

- **`mini-git-server-server/src/main/java/com/google/gerrit/server/git/WorkQueue.java`**
  — the whole of §1.5 and half of §1.2: the executor, the factory inside its
  constructor, the `decorateTask` hooks that wrap every submission in a `Task`,
  and the registry those tasks live in.
- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/CommandExecutorQueueProvider.java`**
  and **`StreamCommandExecutorProvider.java`** — §1.4's three configuration
  keys and the arithmetic over them, and two of §1.2's factories and both of
  its `setPriority` calls.
- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/BaseCommand.java`**
  — `startThread`, the `CommandRunnable`/`TaskThunk` pair it wraps work in,
  the queue it normally submits to, and the thread rename of §1.1.
- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/SshConfiguration.java`**
  — where the queues are published as beans and under what qualifiers, which
  is where a new qualifier would go if §1.2's third bullet leads you to one.
- **the class with `main` in it**, wherever the ladder left it (`MiniGitServer`
  in the module the build packages as the executable jar) — the shutdown hook
  §1.2 excuses, and the only thread this application still builds for itself.
- **that same module's `src/main/resources/`**, beside the logging
  configuration — where §1.3's property goes if you take that route.
- **`/app/README.md`** — the site directory, `etc/gerrit.config`, and the
  `sshd` section §1.4 asks you to keep reading.

The JDK's documentation is the authority on virtual threads, what pins one to
its carrier and what `Thread.ofVirtual` offers; Spring Boot's on
`spring.threads.virtual.enabled` and what it does and does not auto-configure;
Spring Framework's on the task executors and schedulers that can be told to
use virtual threads. Which route you take — a Boot property, a JDK executor, a
Spring scheduler, or something else — is a decision this document leaves to
you; §1.1 to §1.9 say what has to be true, not how the edit gets made.

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

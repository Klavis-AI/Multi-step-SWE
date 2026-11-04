# Step 7 — the 2003–2011 library stack retires

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a **Spring Boot 3 executable jar** at Java
17, wired by **Spring Framework 6**, written against **Jakarta Servlet 6.0**,
serving git over HTTP on JGit and over SSH on Apache MINA SSHD 2.x, logging
through **slf4j in front of logback**. Every framework in it is current and
every enterprise API in it is spelled `jakarta`.

Underneath that, the deployable still carries fourteen artifacts published
between 2003 and 2011. `BOOT-INF/lib` holds `oro-2.0.8.jar` (2003),
`antlr-3.2.jar` and `antlr-runtime-3.2.jar` (2009), `commons-lang-2.5.jar`,
`commons-net-2.0.jar`, `jsr305-1.3.9.jar`, `ehcache-core-1.7.2.jar`,
`velocity-1.6.4.jar`, `commons-compress-1.1.jar`, `commons-io-2.0.1.jar`,
`commons-collections-3.2.1.jar` and `util-compress-1.28.jar` — twelve of them —
and two `bouncycastle:*-jdk15:140` artifacts sit on the compile classpath at
`provided` scope. Nine of the fourteen are declared by this project's own poms
and the other five arrive underneath those nine. None has had a release under
those coordinates in well over a decade, and three of them — Velocity 1.6,
Ehcache 1.7 and `oro` — are the last things in this tree still reaching for
`commons-logging`.

`args4j-2.0.12.jar` is the same vintage and is not one of the fourteen: that
coordinate is still published, and §1.6 moves it forward rather than retiring
it.

**This milestone retires all fourteen.** When you are done, **no pom declares
any of them, none of them is anywhere in the resolved dependency graph of any
module, none of them is inside the deployable, and no source file or compiled
class in this repository names a package any of them provides** — with the
single, explicitly stated exception in §1.2.

Retirement is not deletion. Four of these libraries are load-bearing: the cache
subsystem is built on Ehcache, the SSH public-key parser decodes with
commons-codec, the repository manager expands zip archives at startup with
commons-io and `util-compress`, and every SSH command's option parsing is
args4j. **Everything those four libraries do today must still be done when you
are finished**, by the same classes doing it. Deleting a feature is the
cheapest way to stop naming a package and it is not a migration. §1.3 to §1.6
say, feature by feature, what has to survive.

### 1.1 The coordinates that go, and the four places "gone" is asked

The list, with what each one is:

| coordinate | what it is here |
|---|---|
| `net.sf.ehcache:ehcache-core` | the cache backend — §1.3 |
| `org.apache.velocity:velocity` | a template engine that renders no template in this tree |
| `commons-io:commons-io` | file listing and deletion in the startup zip expansion — §1.5 |
| `com.madgag:util-compress` | the zip expansion itself — §1.5 |
| `org.apache.commons:commons-compress` | under `util-compress` |
| `commons-lang:commons-lang` | declared, unused |
| `commons-net:commons-net` | declared, unused |
| `com.google.code.findbugs:jsr305` | declared, unused; its `javax.annotation` classes are named nowhere |
| `bouncycastle:bcpg-jdk15`, `bouncycastle:bcprov-jdk15` | the first declared `provided`, the second under it; named nowhere |
| `org.antlr:antlr`, `org.antlr:antlr-runtime` | declared, unused; under Velocity as well |
| `commons-collections:commons-collections`, `oro:oro` | under Velocity |

"Gone" is asked in four separate places, because a library can survive any one
of them:

- **No pom declares it**, in any module, and that includes a
  `<dependencyManagement>` entry with nothing depending on it. A managed
  version for an artifact nobody uses resolves to nothing, appears in no
  dependency listing, and is still a 2009 version this project pins for
  whoever adds the dependency back. The root pom holds several.
- **Nothing in the resolved graph of any module is one of them.** Five of the
  fourteen are in no pom at all — four arrive underneath Velocity and under
  `util-compress`, and `bcprov-jdk15` underneath `bcpg-jdk15` — and they leave
  only when their parent does. A pom-only reading leaves four 2003-era jars in
  the jar.
- **Nothing inside the deployable is one of them**, by filename *or* by what
  is inside it. Unpacking a jar into `target/classes`, or shading one into the
  build output, removes the filename and keeps every class.
- **No `.java` file and no class this project compiles names a package any of
  them provides** — `net.sf.ehcache`, `org.apache.velocity`,
  `org.apache.commons.io`, `com.madgag.compress`,
  `org.apache.commons.compress`, `org.apache.commons.lang`,
  `org.apache.commons.net`, `org.apache.commons.collections`,
  `org.apache.oro`, `antlr`, `org.antlr`, `org.bouncycastle`,
  `javax.annotation`. Source and bytecode are asked separately because they
  can disagree: a deleted import with a fully-qualified use left in a method
  body compiles fine, and it is the constant pool the class loader acts on.

Where a JDK API does the job, use it; where it does not, §1.3 to §1.6 name
what to use instead. Do not reach for a bridge, a shim, or a repackaged fork
of a retired library: republishing `net.sf.ehcache` under a new coordinate is
not retiring it, and neither is vendoring the two classes you still need into
this repository under your own package name.

### 1.2 commons-codec is the exception, and it is exact

`commons-codec:commons-codec` is the one coordinate of that vintage you cannot
make disappear: **JGit depends on it**, and resolving JGit's own graph brings a
current release of it back. That is the correct outcome, and a tree that ships
`commons-codec-1.18.0.jar` inside its deployable has done nothing wrong.

What this milestone asks about commons-codec is therefore narrower and is the
whole of it:

- **No pom pins it.** Wherever a `<version>` for it survives — the ladder's own
  tree pins **1.4 (2009)** in the root pom's `<dependencyManagement>` — delete
  it, and do not replace it with a newer pin. Whatever JGit resolves to is the
  version. A declaration with no version of its own is not a pin and is not
  asked about either way.
- **No source file and no class this project compiles names
  `org.apache.commons.codec`.** There are exactly two call sites, both in
  `SshUtil`, and §1.4 says what must remain true of them.

### 1.3 The cache moves to Caffeine, and the cache subsystem survives it

`com.google.gerrit.server.cache` is this project's caching subsystem: a small
`Cache` interface, a Spring module that lets another module *declare* a cache
by name, a pool that builds the declared caches from `etc/gerrit.config`, and
the glue that hands a cache out for injection before the pool has been built.
It is implemented over Ehcache 1.7.2.

Port it to **Caffeine 3.x** (`com.github.ben-manes.caffeine:caffeine`), which
is the current in-process caching library for this platform and the one Spring
itself defaults to.

Notice before you start that **this tree declares no caches.** Nothing calls
`CacheModule.core`, nothing injects a `Cache`, and no behaviour anywhere in
this application will change if the whole package stops existing. That is
precisely why it is in this milestone: it is infrastructure that a later
feature is meant to use, and the only thing standing between it and deletion
is this paragraph. **The package stays, and it stays usable.**

Specifically, when you are done:

- **`Cache` still declares `get`, `put`, `remove`, `removeAll` and
  `getTimeToLive`**, and the package still declares a comparable number of
  types to the ones it declares now. Which types is your call — see below.
- **A cache is still declared the same way.** `CacheModule.core(...)` still
  returns a binding, and that binding still offers `memoryLimit`, `maxAge` and
  `populateWith`. This is the sentence another module writes to ask for a
  cache, and the port replaces what is underneath it, not it.
- **Every cache the pool builds is still bounded, two ways**: by size and by
  age. A cache with no size bound is a memory leak and a cache with no expiry
  is a correctness bug, and both bounds are applied today.
- **The bounds still come out of `etc/gerrit.config`.** The `cache` section,
  and within it each cache's `memorylimit` and `maxage` keys, are read exactly
  as they are read now, with the same defaults and the same time-unit
  handling, and they are read here — where the caches are built, inside this
  package, not delegated out to a helper somewhere else in the tree.
  Hard-coding the numbers into the builder satisfies the previous bullet and
  throws away the site administrator's control of it.

Two things are *not* asked, because Caffeine does not have them and keeping
them alive would be keeping dead configuration alive:

- **The disk store goes.** Caffeine is in-memory only. Everything that exists
  to configure a disk store goes with it — the `disk()` declaration, the
  `diskLimit` binding, the `cache.directory` read, and the Ehcache system
  property set alongside them.
- **The eviction-policy knob goes.** Caffeine's admission policy is not
  selectable, so the enum that selected Ehcache's had nothing left to set.

Those two are the reason the type list above is a count and not a list of
names: `ProxyEhcache` implements an Ehcache interface and cannot survive the
port under any spelling, and the class that named the eviction policies has
nothing to name. Removing *those* is the port. Removing the package is not.

### 1.4 The SSH public-key parser keeps parsing

`SshUtil` turns the text of an authorized key into a `PublicKey` and back, and
its two public methods are the two call sites in this repository that use
commons-codec. **Nothing in this tree calls either of them today** — they are
this application's key-handling API, kept for the code that will — so deleting
both is another complete way to satisfy §1.1 and another thing this milestone
is not.

Both methods stay, and they still work: decode with the JDK's own
`java.util.Base64` instead. Note what the commons-codec call you are replacing
does with input it does not expect — an RFC 4716 public key is folded across
lines, and the two decoders do not agree by default about characters outside
the base64 alphabet.

### 1.5 The startup zip expansion keeps expanding

`LocalDiskRepositoryManager` walks the git base path at startup, finds every
`*.zip` file at or below it, and expands each one into a directory beside it —
this is how a site is seeded with template repositories. It does this with
`commons-io` for the walk and the cleanup and `util-compress` for the
expansion.

**It still does it when you are done**, over `java.util.zip` or the JDK's zip
filesystem provider, still finding zips at any depth, still replacing whatever
was at the destination, and still logging what it expanded. No fixture in this
repository's own test suite ships a zip file, so nothing here will tell you if
you delete this; it is named in this section for that reason.

`LocalDiskRepositoryManager` also carries an unused Velocity import, which is
the whole of Velocity's presence in it.

### 1.6 args4j moves forward, and the CLI keeps its output

`args4j` is pinned at **2.0.12 (2009)** and parses the options of every SSH
command this server exposes. Move it to **a 2.33 or later release**; the four
annotations and the two types this tree uses are spelled identically, so the
coordinate change is most of the work.

It is not all of it. **A version change is not automatically invisible**, and
this library generates the help text those commands print — which §1.8 fixes
down to the byte, as it fixes everything else observable. Run a command's help
before and after and compare it. Whatever differs is yours to put back, and
the library offers a supported way to do so; changing the annotations to
compensate is not it.

The option parsing itself stays, in all thirteen of this project's classes
that use args4j today.

### 1.7 Velocity's logging configuration, and the levels that follow

`GerritGlobalConfiguration` starts a Velocity runtime and configures it to log
through `commons-logging` under the category `velocity`. Nothing in this tree
renders a Velocity template; starting up and arranging its own logging is the
only thing the engine does here. It goes with the library, and the Spring
component that started it goes with the engine — this is the one component
whose removal is the correct outcome rather than the deletion §1.9 forbids,
and it is stated here so that it is stated rather than inferred.

The logging configuration follows the code. Whatever categories the
application's logback configuration declares today, **two of them —
`net.sf.ehcache` and `velocity` — name nothing in the deployable once this
milestone is done, and must not still be declared.** These five lines stay
exactly as they are, whatever else sits beside them:

```
root                                                          DEBUG
com.google.gerrit                                             INFO
org.apache.sshd.common                                        WARN
org.apache.sshd.server                                        INFO
org.apache.sshd.common.keyprovider.FileKeyPairProvider        INFO
```

A level line for a package with no classes behind it is a line an operator
will edit expecting it to do something.

### 1.8 Behaviour is fixed down to the byte

**Nothing observable moves in this milestone.** Every status, body and header
this application's servlets, filters and SSH commands compose; the
`Expires`/`Pragma`/`Cache-Control` trio on `/ssh_info`; the `Location` of its
redirects; the static files it serves and their exact bytes; **the output,
error text and exit status of its SSH commands, including every line of help
and usage they print**; the host key material it presents and the algorithms
it negotiates; the files it writes under the site directory; the SSH audit
log, still in its own format, still asynchronous; its reading of
`etc/gerrit.config`, every section and key, with the same tolerance for values
it cannot parse.

Replacing a library does not change what a program answers. If something this
repository composes moves, it is a bug in the port — and §1.6 exists because
one of these upgrades will move something unless you look for it.

The console is a diagnostic channel and not an answer: §1.9 fixes its
verbosity, not its bytes.

### 1.9 What this milestone must not disturb

Everything the previous milestones established still holds. The reactor still
builds as two commands from a fresh copy of `/app` under this image's Maven
and JDK 17 (`JAVA_HOME=/usr/lib/jvm/temurin-17`; the image defaults to 11) —
`mvn -DskipTests package` and then `mvn test`, configured by the poms alone
(a `.mvn/` directory, a `.mavenrc` and a user `settings.xml` are not read) —
and both still succeed; the
suite still ships, still runs and still passes with no class that contributes
a case deleted, emptied, renamed or disabled; every module still compiles
through the compiler's `release` at 17 and every class file the build produces
is major version 61; nothing reaches into a JDK-internal API
(`sun.*`, `jdk.internal.*`, the non-exported `com.sun.*`); no module inherits from `oss-parent`; nothing names a
JUnit 3 or 4 type or EasyMock, and EasyMock is in no module's resolved
dependencies; `com.jcraft`, `net.schmizz`,
`org.bouncycastle:bcprov-jdk16` and `org.apache.mina` stay gone and
`org.apache.sshd` stays on 2.x; JGit stays on the line the ladder moved it to;
no pom declares a servlet API coordinate of its own; no `.java` file and no
compiled class names `javax.servlet`, the JDK's own `javax.*` packages stay
exactly as they are, and the Jakarta Servlet API and the servlet container
still ship inside the deployable; the object graph is still the container's,
written in Java with no XML bean definition document and no `web.xml`
anywhere, the four contexts and their parenting unchanged, the scopes still
registered, the qualifiers still qualifying, and no bean selected by or
conditional on a Spring profile; the build still produces one Spring Boot
executable jar with Boot's loader as `Main-Class` and this project's own class
as `Start-Class`, no module is a war and no pom names `maven-war-plugin`, the application still registers its own
servlets and filters, and nothing implements the container's bootstrap
contract; versions still come from Boot's dependency management rather than
from a pin at a time; the idioms the source was brought up to — diamond,
try-with-resources, multi-catch, lambdas and method references,
`java.nio.file` alongside `java.io.File` — stay brought up; nothing names
`org.apache.log4j`, logback is still the backend behind slf4j, there is still
exactly one slf4j binding inside the deployable, the classes that log through
slf4j still log through slf4j, and the console keeps the verbosity §1.7's root
level gives it.

The work the source you were handed does is still being done when you are
done, by the same files doing it. Every job a declared type was doing is still
a declared type doing it, and every behaviour a method carried is still
carried by a method. Emptying a method, stubbing a class that produces a value
for injection, or dropping a component that starts and stops is not a
migration. Three removals are the correct outcome rather than that, and all
three are named above: the Ehcache proxy and the eviction-policy enum in §1.3,
and the Velocity lifecycle in §1.7. The number of warnings the tree suppresses
with `@SuppressWarnings` does not grow, counting each suppressed warning name separately, and counting a `@SafeVarargs` as a suppression too.

### 1.10 How the server is run

Unchanged, in every particular:

```
java -Dgerrit.site_path=<site> -Dserver.port=<port> -jar <the jar>
```

and nothing else beyond the JVM's default time zone, which, like the site
directory, belongs to whoever starts the server — no container installed
beside it, no deployment step, no
`-cp`, no script, and no system property that names a configuration file of
any kind. The site directory still comes from `gerrit.site_path` and
`/app/README.md` still gives the format of `etc/gerrit.config`; the HTTP port
still comes from Spring Boot's `server.port` and that property is still the
only thing that decides the listen port; the SSH addresses still live in
`etc/gerrit.config`. The site directory and everything in it still belong to
whoever starts the server.

Two things follow, and both are requirements rather than advice:

- **The jar starts.** A deployable that `java -jar` cannot run, or that runs
  and then fails to come up, has not been migrated.
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
`javax.transaction`, `javax.inject` or `javax.enterprise` — is packaged inside
the deployable; once §1.1's JSR-305 jar has gone, no `javax.annotation` class
is packaged inside it either. Inside the deployable there is exactly one Commons Logging implementation,
and no log4j configuration file, `log4j-over-slf4j` or `reload4j`. The audit
log's asynchronous appender is still set, where `SshLog` builds it, to drop
nothing — `setDiscardingThreshold(0)`, and never `setNeverBlock(true)`.
Apart from the two categories §1.7 retires, the per-logger levels the logging
configuration declares today stay declared at the same levels.

## 2. Read these local references

- **`mini-git-server-server/src/main/java/com/google/gerrit/server/cache/`** —
  the whole of §1.3. Eleven files: the `Cache` interface §1.3 fixes, the module
  and binding that declare a cache, the pool that builds them from
  configuration, and the proxy that exists because a bean has to be handed out
  before the pool is built.
- **`mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/SshUtil.java`**
  — §1.4's two methods and the two commons-codec call sites in this tree.
- **`LocalDiskRepositoryManager`** — §1.5's startup expansion, and what it
  uses from `commons-io` and `util-compress` to do it.
- **`GerritGlobalConfiguration`** — §1.7's Velocity runtime, the properties it
  sets, and the lifecycle bean that publishes it.
- **`pom.xml` and `mini-git-server-server/pom.xml`** — the managed versions
  and most of the direct declarations of §1.1; `mini-git-server-httpd/pom.xml`
  holds one more, and `mini-git-server-sshd/pom.xml` holds §1.2's
  commons-codec.
- **the application module's `src/main/resources/`** (`mini-git-server-app/`
  in a tree that kept the ladder's name for it) — §1.7's logging
  configuration.
- **`/app/README.md`** — the site directory, its layout, `etc/gerrit.config`
  and the `cache` section §1.3 asks you to keep reading.

The Caffeine documentation is the authority on how a cache is built, bounded
and populated; args4j's on what its parser generates and how that is
configured; the JDK's on `java.util.Base64` and `java.util.zip`. Which route
you take — a migration recipe, a hand port, or something else — is a decision
this document leaves to you; §1.1 to §1.9 say what has to be true, not how the
edit gets made.

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

# Step 1 — Java 11 toolchain, current Maven, JUnit 5 Jupiter

## 1. Implement this milestone

`/app` holds **mini-git-server**: a Maven reactor of six modules that builds a
Java EE 6 WAR, hosting git repositories over HTTP and over SSH. It was written
and released in 2011 for Java 6, and it does not compile on any JDK installed
here.

This milestone brings the build, the language level and the test suite up to
**Java 11**, and changes nothing about what the program does.

### 1.1 The build runs

- `mvn package`, run from `/app`, succeeds with no profile to activate and no
  property to set, and produces the deployable WAR under a module's `target/`
  directory. It is run as two commands with one added property — the packaging
  step as `mvn -DskipTests package`, the suite as a separate `mvn test` after
  it — so that a red test still leaves a WAR behind. Nothing else is added, and no profile is activated.
- `mvn test` runs the project's tests and they pass.
- Both take their configuration from the poms and nothing else: a `.mvn/`
  directory, a `.mavenrc` and a user `settings.xml` are not read when the
  build is run.
- Both work under this image's JDK 11 and Maven 3.9.16, which is also the
  toolchain the build will be run with. This milestone is Java 11 whichever JDK
  a shell happens to pick up. Java 8 and earlier are not installed and are not
  coming back.
- Everything the build needs is inside `/app` and declared in its poms. The
  build must work from a fresh copy of `/app`, run by a different user, with an
  empty `~` and its own local repository, so it may not depend on your shell, on
  files outside `/app`, or on anything you `mvn install`ed by hand.

### 1.2 The language and class-file level is Java 11

- Every module compiles at Java 11, set through the compiler's **`release`**
  (`maven.compiler.release`, or `<release>` in the compiler plugin's
  configuration) rather than through `source`/`target`. `release` is what makes
  javac compile against the Java 11 API instead of against the API of whichever
  newer JDK happens to be running.
- No module is left asking for an older `source` or `target`.
- Every class file the build produces is a Java 11 class file — main code and
  test code alike, in every module. A mixture of levels is not this milestone.
- No code reaches into a JDK-internal API (`sun.*`, `jdk.internal.*`, the
  non-exported `com.sun.*`): those were a warning on Java 6 and are a link
  error on a module-aware JDK.

### 1.3 The 2011 build is gone

- The reactor no longer inherits from the external
  `org.sonatype.oss:oss-parent` pom. Whatever the build still needs from it is
  declared in the project itself.
- The unused mocking library the parent pom declares for every module,
  `org.easymock:easymock`, is gone: absent from every module's resolved
  dependencies, and referenced by no code.

### 1.4 The tests are JUnit 5 Jupiter

- The test suite uses **JUnit 5 Jupiter** (`org.junit.jupiter.*`).
- No JUnit 3 type (`junit.framework.*`, `junit.textui.*`,
  `junit.extensions.*`) and no JUnit 4 type (`org.junit.Test`,
  `org.junit.Assert`, `org.junit.Assume`, `org.junit.Before`/`After`,
  `org.junit.Rule`, `org.junit.runner.*`, `org.junit.runners.*`, and the rest
  of the JUnit 4 API) is used anywhere in the source, or present in the classes
  the build compiles.
- Neither JUnit 3/4 (`junit:junit`) nor the vintage engine
  (`org.junit.vintage:*`) is on any module's resolved test classpath — not
  declared, and not arriving transitively behind something else. Migrating the
  API while the old jar stays on the classpath is not migrating the suite.
- Every test class the tree ships today that contributes a case survives,
  under the same class name, with all of its cases: none deleted, emptied,
  renamed, folded into another class, commented out, or disabled — and none
  skipped at run time. Adding tests is fine; the suite is a floor, not a
  ceiling.
- A test-support class that contributes no case of its own — an abstract base,
  a fixture, a helper — is governed by the classpath rule above and not by the
  survival rule. Port it if it can be ported; if the only way to keep it
  compiling is to keep a retired test dependency on the classpath, it goes out
  with that dependency. Nothing that runs may be lost either way, because it
  runs nothing.

### 1.5 The pre-Java-7 idioms give way

This is a language-level milestone and not only a compiler flag. Where the tree
spells something the way Java 6 required, spell it the way Java 7 and 8 allow:

- A `new` expression whose type arguments the compiler can infer uses the
  **diamond** (`new HashMap<>()`). The only explicit type-argument lists left
  are the ones where reifying the argument *is* the point, such as a type token
  like Guice's `TypeLiteral`.
- A `try`/`finally` whose `finally` exists to close a resource becomes
  **try-with-resources**.
- `catch` clauses that differ only in the exception type and share a body
  become one **multi-catch** clause.
- An anonymous class implementing a single method becomes a **lambda** or a
  **method reference**.
- File-system work goes through **`java.nio.file`** (`Path`, `Files`) instead
  of `java.io.File`-only calls. This is about the code that already reaches for
  the disk — what resolves the site's directory layout, what reads the SSH key
  files, what creates a repository's directory — and not about writing a `Path`
  into somewhere that never touched a file. Handing a `File` to a library whose
  API takes one is not the thing being retired; doing the work with `File`'s own
  methods is, and `Path.toFile()` at that boundary is the ordinary way through.
- Warnings are fixed, not silenced: the number of warnings the tree suppresses
  with `@SuppressWarnings` does not grow, counting each suppressed warning name separately, and counting a `@SafeVarargs` as a suppression too. Merging four annotations into one annotation with four arguments
  suppresses just as much.
- Every `.java` file in the workspace is valid Java the compiler accepts —
  including any file no module currently compiles.

Each of those is a *replacement*, so it shows up twice: the new form appears
and the old form goes away. The tree ends with fewer plain `try` statements,
fewer single-type `catch` clauses, fewer explicit type-argument lists and fewer
anonymous classes than it started with. Writing the new form somewhere the old
one never was raises no language level, and neither does deleting the file the
old one lived in. Apart from the retirement 1.4 allows, the source you were
handed is still all there when you are done.

### 1.6 Behaviour does not change

A migration changes how the code is written, not what it does. The observable
behaviour of the tree you inherit is the contract, down to the byte:

- the same HTTP status on every path the WAR serves — the ordinary ones, the
  error paths, the malformed requests and the unexpected methods alike — and,
  wherever the application answers rather than failing into the container's
  error handler, the same response body and the same response headers *this
  application sets*: the `Expires`/`Pragma`/`Cache-Control` trio its `/ssh_info`
  servlet writes, the `Location` of the redirects its SSL filter and its URL
  module send, the `Content-Encoding` its git servlets negotiate, and the
  `application/x-git-*` content types they answer with. What the servlet
  container writes on its own account — the error page, the mime type for
  everything else, the headers `HttpServlet` produces without being asked —
  belongs to whoever runs the WAR and is not yours to hold;
- the same git wire behaviour over HTTP and over SSH — the same
  advertisements, the same `upload-pack`/`receive-pack` exchanges, the same
  accept and reject decisions with the same messages;
- the same SSH command output, error text and exit status;
- the same files written under the site directory;
- the same reading of the site configuration: every section and key
  `etc/gerrit.config` honours today, parsed the same way, with the same
  tolerance for values it cannot parse.

Read the code you are about to change. Where today's behaviour looks like an
accident — an odd status code, a surprising precedence between two config
values, an error message with a typo in it — it is still the contract, and
keeping it is the work.

### 1.7 How the WAR is run — unchanged

`/app/README.md` documents this, and none of it moves in this milestone: the
WAR is deployed at the context root of a `javax.servlet` container, and it
takes the location of its site directory from the **`gerrit.site_path`** system
property, reading `etc/gerrit.config` and the repositories under `basePath`
from there. The container, its HTTP port, the SSH addresses in the config, the
JVM's default time zone and the site directory itself all belong to whoever
starts it.

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
alternate entry point.

## 2. Read these local references

- **`/app/README.md`** — how the server is started, and the layout and format
  of the site directory it is pointed at.
- **`/app/pom.xml` and the six module poms** — the build as it stands: the
  parent, the managed versions, the plugins, the compiler settings.
- **The existing test sources** under `*/src/test/java` — the suite that has to
  survive the move to Jupiter.
- **The application sources** under `*/src/main/java` — the specification of
  the behaviour that must not change. There is no separate behaviour document
  for this milestone; the tree is it.

Public documentation for the target toolchain is the other half: the
maven-compiler-plugin's `release` parameter, the JUnit 5 user guide, and the
JDK 11 release notes for what changed under the language and the JDK between 6
and 11.

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

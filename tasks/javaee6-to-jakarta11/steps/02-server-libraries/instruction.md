# Step 2 — Upstream JGit 5.13, Apache MINA SSHD 2.x, a container servlet API

## 1. Implement this milestone

`/app` holds **mini-git-server** as the previous session left it: a Maven
reactor of six modules that builds a Java EE 6 WAR, hosting git repositories
over HTTP and over SSH, now compiling and testing at Java 11.

Underneath it are three server libraries from 2010, and one of them is not even
the library it claims to be. This milestone replaces all three. The servlet API
this tree compiles against and runs on is `javax.servlet`, in the imports and
on the classpath, and it still is when you are done.

### 1.1 JGit comes from upstream, at 5.13

- The reactor depends on `com.madgag:org.eclipse.jgit` and
  `com.madgag:org.eclipse.jgit.http.server` at version
  `0.10.99.7-UNOFFICIAL-ROBERTO-RELEASE`. That is a personal fork of a 2011
  JGit snapshot, published under someone else's groupId with upstream's
  artifactIds and upstream's package names, so nothing about the code that uses
  it says which one it got. Replace both with the Eclipse Foundation's own:
  `org.eclipse.jgit:org.eclipse.jgit` and
  `org.eclipse.jgit:org.eclipse.jgit.http.server` at
  **`5.13.3.202401111512-r`**.
- When you are done, no artifact whose artifactId is one of JGit's resolves
  from any groupId other than `org.eclipse.jgit`, in any module, at any scope.
- **5.13 and not 6.x.** The release named above is the one this tree moves to;
  resolve exactly that version, not a later JGit line.
- The code that uses JGit stays and keeps using it. Thirteen years of API
  movement sit between the two versions — how a repository is opened and
  cached, how a `FileKey` is built, where the HTTP servlets and their
  resolvers live, what `PersonIdent` and `RepositoryCache` expect. Port the
  call sites.

### 1.2 The SSH server is Apache MINA SSHD 2.x

- `org.apache.sshd:sshd-core:0.5.0` becomes **2.19.0**. In 2.x the library is
  split, so `sshd-core` alone is not the whole of it: take the modules the
  code you port actually needs.
- Nothing under the `org.apache.sshd` groupId is left resolving at anything
  other than a 2.x version, anywhere in the reactor. Two SSH servers on one
  classpath is not a migration; it is a coin toss decided by classloader
  order.
- `org.apache.mina:mina-core` goes. SSHD 0.5 was built on Apache MINA proper;
  SSHD 2.x has its own I/O layer and needs none of it, so a resolved
  `mina-core` means something in the tree is still naming it. No compiled
  class refers to `org.apache.mina` either.
- Nearly the whole server-side API moved: the factory that turns a command
  line into a command, the command interface itself and how it is handed its
  streams, environment and exit callback, how a public key is authenticated,
  how host keys are provided, and how ciphers, MACs and key exchanges are
  named and built. Port them. The daemon that results starts, listens on the
  addresses the config names, and serves the same commands.

For JGit, the call sites stay where they are. Porting means changing them in
place: the classes that reach for JGit today still reach for it themselves,
not through a new wrapper, facade or adapter layer, and JGit's API is not
re-exposed under a project-local name so that the old call shapes can stay as
they were. SSHD 2.x folds many of 0.5's named types into builders and enum
constants, so how many of this project's classes still name it afterwards is
a consequence of the port rather than a requirement of it; what is required
is that the daemon and its commands are written against SSHD itself.

### 1.3 The client SSH libraries go with it

`com.jcraft:jsch` and `net.schmizz:sshj` are SSH **client** libraries sitting
inside an SSH **server**. Each is here for one small thing — rendering a host
key's fingerprint, and parsing a public key out of an `authorized_keys` line —
and SSHD 2.x does both itself.

- Neither resolves as a dependency of any module at any scope when you are
  done, no compiled class refers to `com.jcraft` or `net.schmizz`, and no
  `.java` file imports or names either package.
- `org.bouncycastle:bcprov-jdk16` arrives behind SSHD 0.5 and leaves with it.
  This is the `-jdk16` artifact specifically. The WAR's own
  `bcprov-jdk15`, declared at `provided` scope for reasons that have nothing to
  do with SSH, is a different artifact and is not part of this milestone.

### 1.4 The servlet API comes from the container

- `org.apache.tomcat:servlet-api:6.0.24` is declared at **compile** scope,
  which packages a servlet API inside the deployable. A servlet container is
  entitled to refuse a WAR that ships one. Replace it with
  `javax.servlet:javax.servlet-api:3.1.0` at **`provided`** scope — that exact
  groupId, artifactId and version, resolving at that scope.
- When you are done there is **no servlet API jar inside the built WAR** —
  not under `WEB-INF/lib`, not anywhere else in the archive, under any name.
  This is the half of the change that the pom alone cannot show you: `compile`
  and `provided` produce identical coordinates and differ only in what lands
  in the artifact. Look in the artifact.
- `WEB-INF/web.xml` declares a bare `<web-app>` with no `version` attribute,
  which tells the container to assume the oldest specification it supports.
  Either declare a version of **3.1 or newer** on it, or delete the descriptor
  entirely if the application's annotations and its
  `ServletContainerInitializer` already say everything it said. An undeclared
  version, and any declared version below 3.1, is what this milestone retires;
  both ways out of it are fine.
- Still `javax.servlet`, everywhere, in the imports and on the classpath.
  Nothing `jakarta.servlet` arrives in its place by any route: no dependency
  resolves under that coordinate, no `.java` names the package, and no class
  file inside the built WAR refers to it — including class files that arrive
  inside somebody else's jar, whatever that jar is called.

### 1.5 What the tree you were handed already does, and still must

Everything it already does, it still does when you are done. Every module still
compiles through the compiler's `release` at Java 11 and every class file the
build produces is still a Java 11 class file; nothing reaches into a
JDK-internal API; the test suite still ships, still runs and still passes, with
no class that contributes a case deleted, emptied, renamed or disabled; the
number of warnings the tree suppresses with `@SuppressWarnings` does not grow,
counting each suppressed warning name separately, and counting a `@SafeVarargs` as a suppression too.

A test-support class that contributes no case of its own — an abstract base, a
fixture, a helper — is governed by §1.1's coordinate rule and not by that
survival rule. Port it if it can be
ported; if the only way to keep it compiling is to keep a retired test
dependency resolving, it goes out with that dependency. Nothing that runs is
lost either way, because it runs nothing.

The build still runs as two commands, from a fresh copy of `/app`, under this
image's JDK 11 and Maven 3.9.16, with no profile to activate and no property to
set: the packaging step as `mvn -DskipTests package`, and the suite as a
separate `mvn test` after it. Both succeed. Packaging is run without the suite
attached so that a failing test still leaves a WAR behind, so the two have to
work as two commands and not only as one combined run. Both take their configuration from the poms and
nothing else: a `.mvn/` directory, a `.mavenrc` and a user `settings.xml` are
not read.

The rest of what the tree carries holds too. No module inherits from
`org.sonatype.oss:oss-parent`. No source file and no compiled class names a
JUnit 3 or JUnit 4 type; the suite is JUnit 5, on the test classpath and named by the test classes
themselves, and neither `junit:junit` nor the JUnit vintage engine
(`org.junit.vintage:*`) resolves on any module's test classpath, declared or
arriving behind something else. EasyMock is in no module's resolved dependencies
and named by no compiled class. The idioms the source was brought up to stay
brought up: diamond rather than explicit type arguments, try-with-resources
rather than a hand-written close in `finally`, multi-catch rather than repeated
catch blocks, lambdas and method references rather than single-method anonymous
classes, and `java.nio.file` used alongside `java.io.File` where it already is.

Apart from what §1.3 retires, the source you were handed is still all there
when you are done: its files, the types those files declare, and the methods
those types declare, including in the corners of the tree that nothing else
calls.

### 1.6 Behaviour: the application's is fixed, the libraries' is not

"Change nothing observable" would be the wrong instruction here, so read this
section carefully: it draws a line, and both sides of that line are a
requirement.

**What the application's own code decides is still the contract, down to the
byte.** The line falls where the answer is composed. A status, body or header
put together by code in this project is fixed; one produced inside an upgraded
library is the library's, and it stays the library's even where this project's
own code is what mounted that library and routed the request into it. On this
project's side of the line: the `Expires`/`Pragma`/`Cache-Control` trio its
`/ssh_info` servlet writes; the `Location` of the redirects its SSL filter and
its URL module send; the output, error text and exit status of the SSH commands
this project implements; the files it writes under the site directory; and its
reading of `etc/gerrit.config` — every section and key it honours today, parsed
the same way, with the same tolerance for values it cannot parse. That includes
the SSH daemon's algorithm keys and the four forms this project reads them in: a
bare name, which replaces the default list; a `+` name, which adds to it; a `-`
name, which removes from it; and a name no implementation knows, which is
ignored rather than fatal. Where the config names a list outright, the daemon
offers that list and nothing else, on whichever library is underneath.

**What the upgraded libraries do on their own account is not.** Thirteen years
of git and three of SSH sit between the old versions and the new ones, and
where the new library answers differently, *the new answer is the contract*.
Some of what moves:

- the newer git wire protocol advertises things the 2010 one did not, and
  peels an annotated tag further than it did;
- a condition the old server could only report by hanging up — no such
  repository, an unsatisfiable `want`, an internal failure — is reported in
  band by the newer one, which changes the HTTP status, the headers and the
  packet stream all at once;
- a request the old server turned away flat — the dumb file-access protocol it
  does not serve, an unrecognised or malformed `service` on the smart-protocol
  advertisement — is still turned away, but under the status the newer library
  picks for it rather than the one the old one picked;
- capabilities the old server did not implement, such as shallow clients and
  atomic pushes, now work, so a client that used to refuse before sending now
  sends and gets a real answer;
- deleting the branch a bare repository's `HEAD` names, which the old library
  refused because it read that branch as checked out, is carried out by the
  newer one;
- the SSH daemon's default algorithm preferences are the new library's, so a
  config key that only adds to or subtracts from the default rather than
  replacing it leaves the new library's list with that delta applied; the key
  exchanges, which this project's config does not name at all, are the new
  library's outright; and the site's own host key, unchanged on disk, is
  offered under SHA-2 signature algorithms as well as the SHA-1 one;
- messages whose wording, spelling or stream upstream has since fixed read the
  way upstream fixed them.

A feature the new SSH library provides only when the code that builds the
daemon switches it on is on this project's side of the line, not the
library's. SSH extension negotiation is one, and the 2010 daemon did not have
it: the daemon does not begin announcing it (RFC 8308 — `ext-info-s` in its
key-exchange offer, and the `server-sig-algs` that would follow). A client
signing with an RSA user key therefore still has to be allowed the SHA-1
`ssh-rsa` signature, exactly as it must be against the tree you were handed.

**Do not write a compatibility layer.** A shim that restores an old status
code, an old error string or an old refusal is wrong in both directions: it
satisfies neither the old contract, which this milestone does not keep, nor the
new one, which is what the upgraded library now answers. The right move
everywhere is the faithful port: take the new library's behaviour as it comes,
and change only what your own code decides.

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
alternate entry point.

## 2. Read these local references

- **The application sources** under `*/src/main/java` — in particular the SSH
  daemon and its commands under `mini-git-server-sshd`, and the git servlets
  under `mini-git-server-httpd`. These are the call sites that move, and they
  are also the specification of the behaviour that must not change.
- **`/app/pom.xml` and the six module poms** — the dependencies as they stand:
  which module declares what, at which scope, and what the root pom manages.
- **`/app/mini-git-server-war/src/main/webapp/WEB-INF/web.xml`** — the
  deployment descriptor §1.4 is about.
- **`/app/README.md`** — how the server is started, and the layout and format
  of the site directory it is pointed at.

Public documentation for the target libraries is the other half: JGit's own
javadoc and release notes for the 5.x line, the Apache MINA SSHD project's
documentation and its 0.x-to-2.x migration notes, and the Servlet 3.1
specification for what a `provided` container API means for packaging.

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

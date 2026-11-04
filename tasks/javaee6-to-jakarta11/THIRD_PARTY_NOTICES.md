# Third-Party Notices

## Task

`javaee6-to-jakarta11` — migrate mini-git-server, a 2011 Java EE 6 WAR hosting
git over HTTP and SSH, to Jakarta EE 11 / Spring Boot 4 on Java 21 across ten
steps while preserving its recorded behaviour.

## Upstream project

| Field | Value |
| --- | --- |
| Project | mini-git-server (a fork of Gerrit with the review functionality removed) |
| URL | <https://github.com/rtyley/mini-git-server> |
| Pin | commit `6ffc187caaf1f3cb7402ca79fdbc5905f7285453` (tree version 0.5-SNAPSHOT) |
| Bundled path | `environment/warm-src/` — the pinned working tree; `steps/01-java11-junit5-build/workdir/COPYING` carries the same license file |
| Upstream license | Apache-2.0 (`environment/warm-src/COPYING`) |

## License files

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | mini-git-server at commit `6ffc187`. Exact copy of `environment/warm-src/COPYING` (byte-identical to the `COPYING` in the step 01 workdir). |

## Modifications by the benchmark

- `environment/warm-src/` is the full upstream tree at the pin with Git metadata
  removed. Most files are byte-identical to upstream, but four source files were
  changed so the 2011 Guice wiring starts:
  - `mini-git-server-httpd/src/main/java/com/google/gerrit/httpd/WebModule.java`
    — added imports of `com.google.gerrit.server.AnonymousUser` and
    `com.google.gerrit.server.CurrentUser`, and bound
    `CurrentUser.class` to `AnonymousUser.class` in request scope.
  - `mini-git-server-server/src/main/java/com/google/gerrit/server/config/ToyGerritGlobalModule.java`
    — added `import com.google.gerrit.server.git.GitRepositoryManager;` and
    enabled the `GitRepositoryManager` to `LocalDiskRepositoryManager` binding
    that upstream leaves commented out.
  - `mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/ToySshModule.java`
    — removed the duplicate `GitRepositoryManager` binding.
  - `mini-git-server-war/src/main/java/com/google/gerrit/httpd/WebAppInitializer.java`
    — enabled `webInjector = createWebInjector();` and
    `manager.add(webInjector);`, and returned `webInjector` instead of
    `sshInjector`.
- `steps/01-java11-junit5-build/workdir/COPYING` carries the same Apache-2.0
  license file as the environment tree.
- Migration steps under `steps/` add benchmark-authored instructions, tests,
  workdirs and solutions. The step workdirs are populated from the environment
  tree at run time.

## Third-party dependencies

Maven resolves this project's dependencies at image build time into the image's
`/root/.m2/repository`; they are not vendored or redistributed in the task tree.
They include JGit `0.10.99.7-UNOFFICIAL-ROBERTO-RELEASE`, Guice, sshd-core,
Bouncy Castle (`bcprov-jdk15`/`bcpg-jdk15`), log4j, commons-io, commons-lang,
commons-net, commons-codec, ANTLR, ehcache-core, Velocity, args4j, slf4j and
JUnit. Each artifact retains its own POM license declaration. Apache Maven
3.9.16 is downloaded at image build time and is likewise not shipped in the task
tree.

## Required notices

- Apache-2.0 `COPYING` is the standard Apache License 2.0 text.
- Gerrit-derived source files carry the upstream copyright headers, including
  `Copyright (C) 2008, 2009, 2010 The Android Open Source Project`,
  `Copyright 2008 Google Inc. All rights reserved.`,
  `Copyright (C) 2008, Shawn O. Pearce <spearce@spearce.org>`, and
  `Copyright (C) 2008, Imran M Yousuf <imyousuf@smartitengineering.com>`, all
  under Apache-2.0.
- The pinned tree contains no upstream `NOTICE` file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.

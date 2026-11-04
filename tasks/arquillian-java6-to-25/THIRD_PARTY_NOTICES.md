# Third-Party Notices

## Task

`arquillian-java6-to-25` — migrate the 2011 Arquillian core (Java 6, Maven,
JUnit 4) to Java 25 across thirteen steps while preserving recorded behaviour.

## Upstream project

| Field | Value |
| --- | --- |
| Project | Arquillian Core (`arquillian_deprecated`) |
| URL | <https://github.com/arquillian/arquillian_deprecated> |
| Pin | commit `3fa78dfce2e3db871be6352ea62cc552c1fa1c57` (tree version 1.0.0-SNAPSHOT) |
| Bundled path | `environment/arquillian/` — working subset of the upstream tree: `api`, `build`, `impl-base`, `junit`, `protocols`, `spi`, `testenrichers`, `testng`, plus `pom.xml`, `readme.txt`, `license.txt` and `apl.txt` |
| Upstream license | Apache-2.0 (`environment/arquillian/license.txt`; `apl.txt` is byte-identical) |

## License files

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | Arquillian Core at the pin. Exact copy of `environment/arquillian/license.txt`. |
| LGPL-2.1 | `LGPL-2.1.txt` | Third-party artifacts in the warm Maven cache whose POMs declare the GNU Lesser General Public License (71 declarations). |
| EPL-2.0 | `EPL-2.0.txt` | Third-party artifacts in the cache declaring Eclipse Public License 2.0 (6 declarations, including the Jakarta API JARs). |

## Third-party dependency cache

`environment/m2-seed/` is an unmodified local Maven repository shipped so the
image can build offline: 279 JARs and 973 POMs (74 MB), all third-party
artifacts from Maven Central and the JBoss repositories. It is a dependency
cache, not Arquillian source.

License families declared in the POMs (973 POMs, of which 763 are parent POMs
with no `<licenses>` block): Apache-2.0 (103), LGPL (71), CDDL-1.0 (7), EPL-2.0
(6), EPL-1.0 (5), public domain (4), MIT (4), CPL-1.0 (3), MPL-1.1 (3), BSD (2),
GPL-2.0 with Classpath Exception (1), and one unnamed declaration. In addition,
157 of the 279 JARs carry their own `META-INF/LICENSE` and/or `META-INF/NOTICE`
files; the rest rely on their POM metadata. The Apache-2.0, LGPL-2.1 and
EPL-2.0 texts supplied here cover the largest families. Per-artifact license and
notice texts (including CDDL-1.0, CPL-1.0, MPL-1.1 and the EPL-1.0 artifacts)
remain with the unmodified artifacts. No artifact was modified.

## Modifications by the benchmark

- `environment/arquillian/` is a subset of the pinned upstream tree; shipped
  files are byte-for-byte upstream. Modules and files not needed by the task
  were omitted.
- No upstream source file was modified. Migration steps add benchmark-authored
  instructions, tests and solutions.
- `environment/m2-seed/` is an unmodified Maven cache.

## Required notices

- Arquillian source headers carry, among others, `Copyright 2008, Red Hat
  Middleware LLC, and individual contributors`, `Copyright 2009, Red Hat
  Middleware LLC, and individual contributors`, `Copyright 2010, Red Hat
  Middleware LLC, and individual contributors`, and `Copyright 2011 Red Hat Inc.
  and/or its affiliates and other contributors`, all under Apache-2.0.
- The pinned subset contains no upstream `NOTICE` file and no `copyright.txt`,
  although source headers refer to a `copyright.txt` in the distribution.
- Cached Maven artifacts retain their own POM license declarations and, for 157
  of 279 JARs, their own `META-INF/LICENSE`/`META-INF/NOTICE` files.

Task-authored benchmark content is licensed separately by the benchmark maintainers.

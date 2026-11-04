# Third-Party Notices — lua-vm-version-chain

**Task.** Five-step benchmark that builds a Lua implementation in Java and
carries it from Lua 5.1 to Lua 5.5, graded against the official Lua test suites
and conformance programs recorded from the PUC-Lua reference interpreter.

## 1. Upstream project

| | |
| --- | --- |
| Project | Lua |
| URL | <https://www.lua.org/> |
| Source and documentation | <https://www.lua.org/ftp/>, <https://www.lua.org/manual/> |
| Official test suites | <https://www.lua.org/tests/> |
| Licence | MIT |

Pinned releases, in step order:

| Step | Interpreter version implemented | Reference manual | Official test suite |
| --- | --- | --- | --- |
| 01-lua-5-1 | Lua 5.1.5 | Lua 5.1 manual (`/app/SPEC/lua-5.1/`) | Lua 5.1 suite (`lua5.1-tests/`) |
| 02-lua-5-2 | Lua 5.2.4 | Lua 5.2 manual (`/app/SPEC/lua-5.2/`) | Lua 5.2.2 suite (`lua-5.2.2-tests/`) |
| 03-lua-5-3 | Lua 5.3.6 | Lua 5.3 manual (`/app/SPEC/lua-5.3/`) | Lua 5.3.4 suite (`lua-5.3.4-tests/`) |
| 04-lua-5-4 | Lua 5.4.8 | Lua 5.4 manual (`/app/SPEC/lua-5.4/`) | Lua 5.4.8 suite (`lua-5.4.8-tests/`) |
| 05-lua-5-5 | Lua 5.5.1 | Lua 5.5 manual (`/app/SPEC/lua-5.5/`) | Lua 5.5.1 suite (`lua-5.5.1-tests/`) |

lua.org publishes no test suite for the exact 5.2.4 and 5.3.6 patch releases;
as directed by lua.org, the workspace uses the closest published suite of the
same minor line (5.2.2 and 5.3.4). The reference manuals are the corresponding
minor-line manuals from lua.org.

## 2. License file

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| MIT | `MIT.txt` | Exact Lua license text as published by lua.org (1,068 bytes), covering the Lua manuals, the official test suites, and the derived conformance corpus in this task |

`MIT.txt` is the current lua.org licence text and carries the line
`Copyright © 1994–2026 Lua.org, PUC-Rio.` Each bundled release carries its own
year range in its upstream `COPYRIGHT`/`README` file; those ranges are recorded
in §6.

## 3. What is bundled or derived

- `steps/*/workdir/SPEC/lua-<minor>/` — the Lua reference manual for each
  minor line: `manual.html`, `contents.html`, `readme.html`, `lua.css` and
  `manual.css`, copied from lua.org.
- `steps/*/workdir/tests/lua*-tests/` — the official Lua test suite as
  published at <https://www.lua.org/tests/>: 25 files for 5.1, 27 for 5.2.2,
  28 for 5.3.4, 33 for 5.4.8 and 34 for 5.5.1.
- `steps/*/tests/corpus.tgz` — the hidden conformance corpus. It is derived
  from the official suite files: each suite file is split into small programs,
  a small prelude is prepended, and the expected stdout, stderr and exit status
  were recorded by running the program under the PUC-Lua reference interpreter.
  The archive also contains `expected.json` and `builds.json`.
- `steps/*/workdir/tests/conformance/` — 41 task-authored example programs and
  their recorded reference output, plus the `check.py` runner.
- `steps/*/solution/src/` — the task-authored Java implementation supplied as
  the oracle. It has no third-party libraries.
- `environment/Dockerfile` and `environment/timer.sh` — task-authored image
  tooling.

## 4. Modifications made by the benchmark

- The reference manuals and the official test suite files are stored verbatim;
  only their directory names are normalised to the `SPEC/lua-<minor>/` and
  `tests/lua-<version>-tests/` layout named by the instructions.
- The hidden corpus is a derived work of the official suites. The split pieces
  carry an injected prelude that sets the suite's normal `_soft`, `_port` and
  `_nomsg` globals, wraps `assert` to count assertions, and redirects the final
  summary through `__print`. No assertion, expected value or test body is
  changed; the modifications only make each slice independently runnable and
  observable.
- The conformance programs under `tests/conformance/` and the Java interpreter
  under `solution/src/` are task-authored, not upstream Lua code.
- No upstream license file, manual or test-suite file is altered or removed.

## 5. Third-party dependencies bundled in the task

The only third-party work product stored in the task is Lua itself (manuals,
test suites and the derived corpus), under the MIT licence above. The Java
solution is required by the task contract to use no third-party libraries.

The container image is built from `eclipse-temurin:21-jdk` and installs
Node.js 24.22.0 for the agent harness. Those are image-level components, not
files stored in the task directory: the Temurin JDK is distributed under the
GPL-2.0 with the Classpath Exception, and the Node.js distribution carries its
own MIT `LICENSE` and the notices of its bundled components.

## 6. Required notices

- Lua software, per released version:

  | Release | Upstream copyright line |
  | --- | --- |
  | Lua 5.1.5 | `Copyright © 1994–2012 Lua.org, PUC-Rio.` |
  | Lua 5.2.2 | `Copyright © 1994–2013 Lua.org, PUC-Rio.` |
  | Lua 5.3.4 | `Copyright © 1994–2017 Lua.org, PUC-Rio.` |
  | Lua 5.4.8 | `Copyright © 1994–2025 Lua.org, PUC-Rio.` |
  | Lua 5.5.1 | `Copyright © 1994–2026 Lua.org, PUC-Rio.` |

- Reference manuals, per manual edition (from the footer of each
  `manual.html`):

  | Manual | Upstream copyright line |
  | --- | --- |
  | Lua 5.1 | `Copyright © 2006–2012 Lua.org, PUC-Rio.` |
  | Lua 5.2 | `Copyright © 2011–2015 Lua.org, PUC-Rio.` |
  | Lua 5.3 | `Copyright © 2015–2020 Lua.org, PUC-Rio.` |
  | Lua 5.4 | `Copyright © 2020–2025 Lua.org, PUC-Rio.` |
  | Lua 5.5 | `Copyright © 2020–2026 Lua.org, PUC-Rio.` |

- The official test suites carry no separate copyright or licence file; they
  are part of the Lua distribution and are covered by the same MIT terms.
- No trademark statement is made in the Lua licence or in the bundled manual
  and test-suite files.

Task-authored benchmark content is licensed separately by the benchmark maintainers.

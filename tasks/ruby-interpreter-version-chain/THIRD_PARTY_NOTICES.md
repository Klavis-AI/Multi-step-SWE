# Third-party notices — ruby-interpreter-version-chain

A C11 Ruby interpreter built across the CRuby release chain — 1.8.7, 2.0.0,
2.7.8, 3.1.7, 3.3.12 and 3.4.10 — graded by running conformance programs drawn
from `ruby/spec` and `ruby/ruby`'s own `test/ruby` suite and comparing the
output with the real CRuby release.

## Upstream projects

| | |
|---|---|
| Project | CRuby (`ruby/ruby`) |
| URL | https://github.com/ruby/ruby |
| Releases pinned | 1.8.7, 2.0.0, 2.7.8, 3.1.7, 3.3.12, 3.4.10 |
| Bundled | Release documentation (`steps/*/workdir/SPEC/<version>/`: `NEWS`, `README`, `man/ruby.1`, `doc/`) and conformance programs extracted from the release's `test/ruby` suite |
| Project | ruby/spec |
| URL | https://github.com/ruby/spec |
| Pin | no commit is recorded in the task tree; `LICENSE` retrieved from the repository's default branch on 2026-11-04 |
| Bundled | Conformance programs extracted from the `core/`, `language/` and `library/` specs into `steps/*/workdir/spec/cases/` |

No CRuby or ruby/spec source code is bundled: the interpreter in each step's
`solution/` is authored C, and the conformance programs under
`workdir/spec/cases/` are Ruby programs, not interpreter source.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Ruby OR GPL-2.0 | `LICENSES/Ruby-1.8.7.txt`, `LICENSES/GPL-2.0.txt` | CRuby 1.8.7 (`COPYING` and `GPL` at tag `v1_8_7`) |
| Ruby OR BSD-2-Clause | `LICENSES/Ruby.txt`, `LICENSES/BSD-2-Clause.txt` | CRuby 2.0.0, 2.7.8, 3.1.7, 3.3.12, 3.4.10 (`COPYING` and `BSDL` at tag `v3_4_10`; the Ruby license conditions are the same across these releases) |
| MIT | `LICENSES/MIT-ruby-spec.txt` | ruby/spec corpus (`LICENSE`) |

## Version-to-license map

| Step | CRuby release | Documentation path | License |
|---|---|---|---|
| `01-ruby-1-8` | 1.8.7 | `steps/01-ruby-1-8/workdir/SPEC/1.8.7/` | Ruby OR GPL-2.0 |
| `02-ruby-2-0` | 2.0.0 | `steps/02-ruby-2-0/workdir/SPEC/2.0.0/` | Ruby OR BSD-2-Clause |
| `03-ruby-2-7` | 2.7.8 | `steps/03-ruby-2-7/workdir/SPEC/2.7.8/` | Ruby OR BSD-2-Clause |
| `04-ruby-3-1` | 3.1.7 | `steps/04-ruby-3-1/workdir/SPEC/3.1.7/` | Ruby OR BSD-2-Clause |
| `05-ruby-3-3` | 3.3.12 | `steps/05-ruby-3-3/workdir/SPEC/3.3.12/` | Ruby OR BSD-2-Clause |
| `06-ruby-3-4` | 3.4.10 | `steps/06-ruby-3-4/workdir/SPEC/3.4.10/` | Ruby OR BSD-2-Clause |

CRuby 1.8.7 offers GPL version 2 as the alternative to the Ruby license;
CRuby 2.0.0 and later offer the 2-clause BSDL instead. `LICENSES/Ruby.txt` is
the `COPYING` from 3.4.10 and `LICENSES/Ruby-1.8.7.txt` is the `COPYING` from
1.8.7; the two differ only in the named alternative and formatting.

## Modifications made by the benchmark

- Release documentation is copied unchanged from the corresponding `ruby/ruby`
  tag into `steps/*/workdir/SPEC/<version>/`; only the listed files are
  included.
- Conformance programs were extracted from `ruby/spec` and `ruby/ruby`'s
  `test/ruby` suite, combined into standalone single-file programs, and stored
  under `steps/*/workdir/spec/cases/`.
- `steps/*/workdir/spec/expected.tsv` records exit status, exception class,
  warning flag, switches and the base64-encoded standard output produced by
  running each program with the real CRuby release. It is generated data, not
  upstream source.
- The task's instructions, harnesses, verifiers and authored C implementation
  are benchmark content.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| CRuby release documentation | 1.8.7, 2.0.0, 2.7.8, 3.1.7, 3.3.12, 3.4.10 | Ruby OR BSD-2-Clause (1.8.7: Ruby OR GPL-2.0) | `steps/*/workdir/SPEC/<version>/` |
| ruby/spec conformance programs | upstream default branch at retrieval | MIT | `steps/*/workdir/spec/cases/` |
| CRuby `test/ruby` conformance programs | same releases as above | Ruby OR BSD-2-Clause (1.8.7: Ruby OR GPL-2.0) | `steps/*/workdir/spec/cases/` |

## Required notices

CRuby:

> Ruby is copyrighted free software by Yukihiro Matsumoto <matz@netlab.jp>.

The 2-clause BSDL copyright line (2.0.0 and later):

> Copyright (C) 1993-2013 Yukihiro Matsumoto. All rights reserved.

ruby/spec:

> Copyright (c) 2008 Engine Yard, Inc. All rights reserved.

Ruby is a trademark of Yukihiro Matsumoto. The task is not affiliated with or
endorsed by the Ruby project.

Task-authored benchmark content is licensed separately by the benchmark maintainers.

# Ruby interpreter — CRuby 3.4.10

## 1. Implement this milestone

The interpreter in `/app` currently behaves like an older Ruby. Make it behave
like **CRuby 3.4.10**, the release whose own documents are in `/app/SPEC/3.4.10/`.

**Build and entry point.** Running `make` in `/app` — no arguments,
no targets — must produce an executable at `/app/ruby`. It is driven through
Ruby's own command-line interface, `ruby [switches] program.rb`, documented in
`/app/SPEC/3.4.10/man/ruby.1`, and must accept the switches `-W0`, `-W1`, `-W2`, `-w`, `-W:deprecated` and `-W:no-deprecated`. It reads the
named file, executes it, and writes to standard output and standard error. Its
exit status is `0` on normal completion, the value given to `Kernel#exit`, and
`1` when the program ends with an uncaught exception. An uncaught exception is
reported on standard error in CRuby's format, which names the exception's class
in parentheses at the end of a line. A program that is not valid Ruby 3.4.10 is
rejected before any of it runs, with `syntax error` on standard error and exit
status `1`. Under `-w` and `-W2` the warnings CRuby 3.4.10 emits are written to
standard error, each on a line containing `warning:`; under `-W0` none are.

**What changes.** Ruby 3.4. A bare `it` inside a block with no parameters is the
first block parameter, so `[1, 2].map { it * 2 }` is `[2, 4]` — what 3.3 warned
about now happens, and it takes precedence over a method named `it`. `Hash#inspect` changed shape: a Hash
renders as `{a: 1}` when the key is a symbol whose name needs no quoting,
`{"my key": 1}` when it does, and `{1 => 2}` — with spaces around the arrow — for
every other key. String literals in a file with no `frozen_string_literal`
comment are *chilled*: mutating one still works but emits a deprecation warning
under `-W:deprecated`, and so under `-w` and `-W2`, which turn that category on;
`-W0`, `-W1` and `-W:no-deprecated` stay silent. The library gains
`Array#fetch_values` (which raises `IndexError` for an index that is not there
unless a block supplies a default), `String#append_as_bytes`, and
`MatchData#bytebegin` and `#byteend`. `NEWS.md` lists the rest;
`/app/spec/COVERAGE.md` says which areas the interpreter must cover.

The `ruby.1` page shipped with this release still does not document the
`-W:<category>` form, so the categories `deprecated` and `experimental`, and the
`-W:no-<category>` spelling, are specified by `NEWS.md` and by this instruction
rather than by that man page.

**Performance.** This release must also execute Ruby *fast enough*. Each
benchmark program must come in under its instruction budget as counted by
`valgrind --tool=callgrind`, which counts the machine instructions the process
executes; a run under `valgrind` that takes longer than 240 seconds is over
budget. `/app/spec/perf/` holds five such programs and their budgets:
`bench/*.rb` are the programs, `budgets.tsv` has one
`name<TAB>budget<TAB>sha256-of-the-expected-output` row per program — a
benchmark meets its budget only when the interpreter prints the right answer
*and* stays under the budget — and `sh /app/spec/perf/run.sh` measures the
interpreter at `/app/ruby` against them and prints each count next to its
budget. These five are representative, not the whole requirement: a variant of
any of them with other literal constants (starting values, multipliers, moduli,
string and key literals) but the same loops and iteration counts has its own
budget and expected output, measured the same way, and must meet it too.
The budgets are CRuby 3.4's own instruction counts for the programs they belong
to, so an interpreter that dispatches every integer operation through a full
method lookup will not meet them.

`Kernel#require`, `Kernel#require_relative` and `$LOAD_PATH` must load Ruby
source files, so that a library can be used the way a gem checkout is:
`/app/gems/lib` holds the unmodified source of released, pure-Ruby gems
(`tsort`, `abbrev`, `shellwords` and `colorize`), and programs that put that
directory on `$LOAD_PATH`, `require` `tsort`, `abbrev`, `shellwords` or
`colorize`, and exercise their public APIs must run as they do under CRuby
3.4.10.

Everything the earlier corpus already covered has to keep working — but the
expected output of a program is whatever **CRuby 3.4.10** prints for it.

**Running a program.** Each program is a standalone Ruby file, run as its own
process from a scratch working directory, and must finish within ten seconds
under a 4 GB address-space limit. A program behaves correctly when its standard
output is byte-for-byte what CRuby 3.4.10 produces, its exit status matches, and
— when it ends with an uncaught exception — the exception class named in
parentheses on standard error matches (a program rejected as a syntax error is
taken as ending with `SyntaxError`, however the message is phrased). Programs in
the `warn` stratum must also agree with CRuby 3.4.10 about whether a warning is
printed: a warning where this release prints none is as wrong as a missing one.
The programs in `/app/spec/` are representative of what the interpreter must
run, not the whole of it. `/app/spec/COVERAGE.md` lists every area the
interpreter must cover, with the number of programs in each.

The interpreter must also build from source alone. With every `.o`, `.a`, `.so`
and `.d` file and **every ELF executable** anywhere under `/app` deleted — not
only `/app/ruby` — `make` in `/app` must rebuild it within 900 seconds, in a
fresh container that has the C toolchain (`gcc`, `make`, binutils and the libc
headers) and the standard shell utilities, and nothing else from this container:
a tool or library you install here, or any other tool this container has, is not
there for the Makefile. So the build has to produce the interpreter by
compiling, and a binary shipped under another name does not survive the rebuild.
The requirements below hold both before and after that rebuild.
The interpreter must be your own C: `/app/ruby` must be a native ELF
executable, no source archive or compressed file (`.tar.gz`, `.tgz`, `.tar.xz`,
`.tar.bz2`, `.zip`, `.gem`, or the same formats under any other name) may be left
in the tree, the C and C++ sources must total between 2,000
and 120,000 lines (`.c`, `.h`, `.cc`, `.cpp`, `.y`, `.def` and `.inc` count,
wherever under `/app` they are), no one string literal in them may run past
100,000 characters, and none of them may contain the strings
`Yukihiro Matsumoto`, `mruby`, `JRuby`, `Rubinius`, `TruffleRuby`,
`vm_insnhelper`, `vm_core.h` or `ruby/internal/`, which would mean an existing
Ruby implementation had been vendored in. Nor may any of them be, or contain, an
existing Ruby implementation's own source in any other form, verbatim or edited
(renamed, reformatted or re-commented); no other file in the tree, whatever it is
called, may hold one either, because a compiler reads a file of any name; and the
build may not write one anywhere: this is about the code itself, not only
those strings. The build itself has to be self-contained, so the
Makefile may not pull anything in at build time: none of `wget`, `curl`,
`git clone`, `svn`, `pip install`, `gem install`, `scp`, `rsync`, `urllib`, or a
`http://`, `https://` or `ftp://` URL may appear anywhere in it.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/3.4.10/` holds CRuby 3.4.10's own `NEWS.md` —
the list of what this release changed — together with its `README.md`, the `man/ruby.1` page that documents the
command-line interface, and the `doc/syntax/` chapters.

`doc/NEWS/NEWS-3.3.0.md` lists what 3.3 changed.

**Conformance corpus:** `/app/spec/cases/` holds Ruby programs extracted from
`ruby/spec` and from `ruby/ruby`'s own `test/ruby` suite;
`/app/spec/expected.tsv` records, for each, the exit status, the exception class
if any, whether a warning was printed, the switches to run it with, and the
standard output **CRuby 3.4.10** produces (base64) — including for the programs
that were added for earlier releases, whose expected output this release
changes. `sh /app/spec/run.sh` runs them all against `/app/ruby` and prints how
many pass; `sh /app/spec/run.sh <id>` re-runs one and shows the difference. The
programs under `cases/` accumulate as the corpus grows; `expected.tsv` is
replaced each time, because it always records what the release being implemented
produces. `/app/spec/COVERAGE.md` describes the areas covered.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents
  and corpus. `make` in `/app` must keep producing `/app/ruby`.
- Do not cheat by using online solutions or hints specific to this task.
- Everything under `/app/spec/` is read-only input.
- Leave a reproducible handoff for anyone rebuilding this tree: source that
  builds with `make`, and notes in `/app/NOTES.md` on what works and what does
  not.

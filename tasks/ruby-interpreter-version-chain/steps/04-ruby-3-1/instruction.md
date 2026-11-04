# Ruby interpreter — CRuby 3.1.7

## 1. Implement this milestone

The interpreter in `/app` currently behaves like an older Ruby. Make it behave
like **CRuby 3.1.7**, the release whose own documents are in `/app/SPEC/3.1.7/`.

**Build and entry point.** Running `make` in `/app` — no arguments,
no targets — must produce an executable at `/app/ruby`. It is driven through
Ruby's own command-line interface, `ruby [switches] program.rb`, documented in
`/app/SPEC/3.1.7/man/ruby.1`, and must accept the switches `-W0`, `-W1`, `-W2`, `-w`, `-W:deprecated` and `-W:no-deprecated`. It reads the
named file, executes it, and writes to standard output and standard error. Its
exit status is `0` on normal completion, the value given to `Kernel#exit`, and
`1` when the program ends with an uncaught exception. An uncaught exception is
reported on standard error in CRuby's format, which names the exception's class
in parentheses at the end of a line. A program that is not valid Ruby 3.1.7 is
rejected before any of it runs, with `syntax error` on standard error and exit
status `1`. Under `-w` and `-W2` the warnings CRuby 3.1.7 emits are written to
standard error, each on a line containing `warning:`; under `-W0` none are.
Those numeric levels are not the whole switch: `-W:deprecated` turns deprecation
warnings on and `-W:no-deprecated` turns them off, and both are part of this
release's command-line interface. The `ruby.1`
page shipped with this release documents only the numeric `-W` *level* form, so
the `-W:<category>` and `-W:no-<category>` spellings do not appear on it; the
interpreter must accept them all the same.

**What changes.** Ruby 3.0 separated keyword arguments from positional ones, and
3.1 built on it. What 2.7 merely warned about is now an error: a Hash passed as
the last positional argument is no longer converted into keywords, so a method
that requires a keyword raises `ArgumentError`, while a method that accepts no
keywords receives the Hash as a positional argument. `**{}` passes no argument at all.

Also from 3.0 and 3.1: `Hash#except`, `Symbol#name`, endless method definitions
(`def f = expr`), the rightward assignment and `in` boolean forms of pattern
matching outside `case`, the find pattern, `Ractor` (`Ractor.new` and `#take`, an
`ArgumentError` when the block reaches a variable of the enclosing scope,
`Ractor.shareable?`, `Ractor.make_shareable`), hash-literal value omission
(`{x:}`), anonymous block forwarding (`def f(&) … g(&)`),
`Enumerable#each_cons` and `#each_slice` returning the receiver instead of `nil`,
`Symbol#to_proc` returning a lambda, `Array#intersect?` and `Enumerable#tally`. `Struct#keyword_init?` reports whether a
Struct was defined with `keyword_init:` — `nil` when the keyword was never
passed. `Hash#index`, deprecated
since 1.9, is finally gone.

Everything the earlier corpus already covered has to keep working — but the
expected output of a program is whatever **CRuby 3.1.7** prints for it.

**Running a program.** Each program is a standalone Ruby file, run as its own
process from a scratch working directory, and must finish within ten seconds
under a 4 GB address-space limit. A program behaves correctly when its standard
output is byte-for-byte what CRuby 3.1.7 produces, its exit status matches, and
— when it ends with an uncaught exception — the exception class named in
parentheses on standard error matches (a program rejected as a syntax error is
taken as ending with `SyntaxError`, however the message is phrased). Programs in
the `warn` stratum must also agree with CRuby 3.1.7 about whether a warning is
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

**Normative semantics:** `/app/SPEC/3.1.7/` holds CRuby 3.1.7's own `NEWS.md` —
the list of what this release changed — together with its `README.md`, the `man/ruby.1` page that documents the
command-line interface, and the `doc/syntax/` chapters.
`doc/NEWS-3.0.0.md` lists what 3.0 changed.

**Conformance corpus:** `/app/spec/cases/` holds Ruby programs extracted from
`ruby/spec` and from `ruby/ruby`'s own `test/ruby` suite;
`/app/spec/expected.tsv` records, for each, the exit status, the exception class
if any, whether a warning was printed, the switches to run it with, and the
standard output **CRuby 3.1.7** produces (base64) — including for the programs
that were added for earlier releases, whose expected output this release
changes. `sh /app/spec/run.sh` runs them all against `/app/ruby` and prints how
many pass; `sh /app/spec/run.sh <id>` re-runs one and shows the difference. The
programs under `cases/` accumulate as the corpus grows; `expected.tsv` is
replaced each time, because it always records what the release being implemented
produces.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents
  and corpus. `make` in `/app` must keep producing `/app/ruby`.
- Do not cheat by using online solutions or hints specific to this task.
- Everything under `/app/spec/` is read-only input.
- Leave a reproducible handoff for the next fresh session: source that builds
  with `make`, and notes in `/app/NOTES.md` on what works and what does not.

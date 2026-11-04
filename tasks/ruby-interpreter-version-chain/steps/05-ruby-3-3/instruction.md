# Ruby interpreter — CRuby 3.3.12

## 1. Implement this milestone

The interpreter in `/app` currently behaves like an older Ruby. Make it behave
like **CRuby 3.3.12**, the release whose own documents are in `/app/SPEC/3.3.12/`.

**Build and entry point.** Running `make` in `/app` — no arguments,
no targets — must produce an executable at `/app/ruby`. It is driven through
Ruby's own command-line interface, `ruby [switches] program.rb`, documented in
`/app/SPEC/3.3.12/man/ruby.1`, and must accept the switches `-W0`, `-W1`, `-W2`, `-w`, `-W:deprecated` and `-W:no-deprecated`. It reads the
named file, executes it, and writes to standard output and standard error. Its
exit status is `0` on normal completion, the value given to `Kernel#exit`, and
`1` when the program ends with an uncaught exception. An uncaught exception is
reported on standard error in CRuby's format, which names the exception's class
in parentheses at the end of a line. A program that is not valid Ruby 3.3.12 is
rejected before any of it runs, with `syntax error` on standard error and exit
status `1`. Under `-w` and `-W2` the warnings CRuby 3.3.12 emits are written to
standard error, each on a line containing `warning:`; under `-W0` none are.

**What changes.** Ruby 3.2 and 3.3. `Data.define` builds immutable value classes
(`new` by position or keyword, `to_h`, `members`, `with`, `==`, `deconstruct`,
`deconstruct_keys`, frozen instances). `Set` is available without a `require`.
Anonymous rest and keyword-rest arguments can be forwarded (`def f(*) … g(*)`,
`def f(**) … g(**)`). `Object#taint`, `#untaint`, `#trust` and `#untrust` are
removed, and so are the `Fixnum` and `Bignum` constants. The library gains
`Integer#ceildiv`, `String#byteindex`, `#byterindex` and `#bytesplice`,
`Module#set_temporary_name`, `Range#overlap?`, `MatchData#named_captures` with
`symbolize_names:`, `Exception#detailed_message`, `Proc#parameters(lambda:)` and
`Struct#keyword_init?`. A `Struct` defined without `keyword_init:` now accepts
either positional or keyword arguments. 3.3 warns, at `-W1` and above, that a bare
`it` inside a block will refer to the first block parameter in the next release;
`-W0` silences it, and because that warning is not one of the named categories
`-W:no-deprecated` does not suppress it. `NEWS.md` and
the `doc/NEWS/NEWS-*.md` files list the rest of what these two releases changed;
`/app/spec/COVERAGE.md` says which areas the interpreter must cover.

The `ruby.1` page shipped with this release predates the `-W:<category>` form,
so the categories `deprecated` and `experimental`, and the `-W:no-<category>`
spelling, are specified by `NEWS.md` and by this instruction rather than by that
man page.

`Kernel#require`, `Kernel#require_relative` and `$LOAD_PATH` must load Ruby
source files, so that a library can be used the way a gem checkout is:
`/app/gems/lib` holds the unmodified source of released, pure-Ruby gems
(`tsort`, `abbrev`, `shellwords` and `colorize`), and programs that put that
directory on `$LOAD_PATH`, `require` `tsort`, `abbrev`, `shellwords` or
`colorize`, and exercise their public APIs must run as they do under CRuby
3.3.12.

Everything the earlier corpus already covered has to keep working — but the
expected output of a program is whatever **CRuby 3.3.12** prints for it.

**Running a program.** Each program is a standalone Ruby file, run as its own
process from a scratch working directory, and must finish within ten seconds
under a 4 GB address-space limit. A program behaves correctly when its standard
output is byte-for-byte what CRuby 3.3.12 produces, its exit status matches, and
— when it ends with an uncaught exception — the exception class named in
parentheses on standard error matches (a program rejected as a syntax error is
taken as ending with `SyntaxError`, however the message is phrased). Programs in
the `warn` stratum must also agree with CRuby 3.3.12 about whether a warning is
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

**Normative semantics:** `/app/SPEC/3.3.12/` holds CRuby 3.3.12's own `NEWS.md` —
the list of what this release changed — together with its `README.md`, the `man/ruby.1` page that documents the
command-line interface, and the `doc/syntax/` chapters.
`doc/NEWS/NEWS-3.0.0.md`, `doc/NEWS/NEWS-3.1.0.md` and `doc/NEWS/NEWS-3.2.0.md`
list what the
releases between 2.7 and 3.3 changed.

**Conformance corpus:** `/app/spec/cases/` holds Ruby programs extracted from
`ruby/spec` and from `ruby/ruby`'s own `test/ruby` suite;
`/app/spec/expected.tsv` records, for each, the exit status, the exception class
if any, whether a warning was printed, the switches to run it with, and the
standard output **CRuby 3.3.12** produces (base64) — including for the programs
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

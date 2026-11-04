# Ruby interpreter — CRuby 2.7.8

## 1. Implement this milestone

The interpreter in `/app` currently behaves like an older Ruby. Make it behave
like **CRuby 2.7.8**, the release whose own documents are in `/app/SPEC/2.7.8/`.

**Build and entry point.** Running `make` in `/app` — no arguments,
no targets — must produce an executable at `/app/ruby`. It is driven through
Ruby's own command-line interface, `ruby [switches] program.rb`, documented in
`/app/SPEC/2.7.8/man/ruby.1`, and must accept the switches `-W0`, `-W1`, `-W2`, `-w`, `-W:deprecated` and `-W:no-deprecated`. It reads the
named file, executes it, and writes to standard output and standard error. Its
exit status is `0` on normal completion, the value given to `Kernel#exit`, and
`1` when the program ends with an uncaught exception. An uncaught exception is
reported on standard error in CRuby's format, which names the exception's class
in parentheses at the end of a line. A program that is not valid Ruby 2.7.8 is
rejected before any of it runs, with `syntax error` on standard error and exit
status `1`. Under `-w` and `-W2` the warnings CRuby 2.7.8 emits are written to
standard error, each on a line containing `warning:`; under `-W0` none are.

**What changes.** The 2.1–2.7 releases. `Fixnum` and `Bignum` are no longer
separate classes: `1.class` and `(2**100).class` are both `Integer`, and the two
names are deprecated aliases of `Integer`.
The grammar gains the `# frozen_string_literal: true` magic comment (a literal
string is then frozen and mutating it raises `FrozenError`), the safe-navigation
operator `&.`, the squiggly here-document `<<~`, endless (`(1..)`) and beginless
(`(..3)`) ranges, numbered block parameters (`_1`, `_2`), `case`/`in` pattern
matching over array, hash, alternative, pin and guard patterns. The library gains `Array#dig`,
`Hash#dig`, `Comparable#clamp`, `String#match?`, `String#delete_prefix` and
`#delete_suffix`, `String#-@` and `#+@`, `Hash#transform_keys`,
`Hash#transform_values`, `Hash#to_proc`, `Integer#digits`, `Integer#bit_length`,
`Integer#pow` with a modulus, `Enumerable#sum`, `#tally`, `#filter_map`,
`#chain`, `#uniq`, `filter` as an alias of `select`, `Object#then` /
`#yield_self`, `Proc#>>` and `#<<`, and `Enumerable#to_h` with a block.

Two switches arrive with this release: `-W:deprecated` turns deprecation
warnings on and `-W:no-deprecated` turns them off. Under `-W:deprecated` this
release warns when a method that accepts keyword arguments is called with a Hash
as its last positional argument. The `ruby.1` page shipped with this release
documents only the numeric `-W` *level* form, `-W0`, `-W1` and `-W2`; the
`-W:<category>` and `-W:no-<category>` spellings above are part of this
release's command-line interface but do not appear on that page, and the
interpreter must accept them all the same.

Everything the earlier corpus already covered has to keep working — but the
expected output of a program is whatever **CRuby 2.7.8** prints for it.

**Running a program.** Each program is a standalone Ruby file, run as its own
process from a scratch working directory, and must finish within ten seconds
under a 4 GB address-space limit. A program behaves correctly when its standard
output is byte-for-byte what CRuby 2.7.8 produces, its exit status matches, and
— when it ends with an uncaught exception — the exception class named in
parentheses on standard error matches (a program rejected as a syntax error is
taken as ending with `SyntaxError`, however the message is phrased). Programs in
the `warn` stratum must also agree with CRuby 2.7.8 about whether a warning is
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

**Normative semantics:** `/app/SPEC/2.7.8/` holds CRuby 2.7.8's own `NEWS` —
the list of what this release changed — together with its `README.md`, the `man/ruby.1` page that documents the
command-line interface, and the `doc/syntax/` chapters.
`doc/NEWS-2.1.0` through `doc/NEWS-2.6.0` list what each release between
2.0.0 and 2.7 changed.

**Conformance corpus:** `/app/spec/cases/` holds Ruby programs extracted from
`ruby/spec` and from `ruby/ruby`'s own `test/ruby` suite;
`/app/spec/expected.tsv` records, for each, the exit status, the exception class
if any, whether a warning was printed, the switches to run it with, and the
standard output **CRuby 2.7.8** produces (base64) — including for the programs
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

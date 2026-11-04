# Ruby interpreter — CRuby 2.0.0

## 1. Implement this milestone

The interpreter in `/app` currently behaves like an older Ruby. Make it behave
like **CRuby 2.0.0**, the release whose own documents are in `/app/SPEC/2.0.0/`.

**Build and entry point.** Running `make` in `/app` — no arguments,
no targets — must produce an executable at `/app/ruby`. It is driven through
Ruby's own command-line interface, `ruby [switches] program.rb`, documented in
`/app/SPEC/2.0.0/man/ruby.1`, and must accept the switches `-W0`, `-W1`, `-W2` and `-w`. It reads the
named file, executes it, and writes to standard output and standard error. Its
exit status is `0` on normal completion, the value given to `Kernel#exit`, and
`1` when the program ends with an uncaught exception. An uncaught exception is
reported on standard error in CRuby's format, which names the exception's class
in parentheses at the end of a line. A program that is not valid Ruby 2.0.0 is
rejected before any of it runs, with `syntax error` on standard error and exit
status `1`. Under `-w` and `-W2` the warnings CRuby 2.0.0 emits are written to
standard error, each on a line containing `warning:`; under `-W0` none are.

**What changes.** Ruby 1.9 and 2.0 rewrote the string model and the grammar.
`String` carries an encoding and is indexed by character: `"héllo".length` is
`5`, `"abc"[0]` is `"a"`, and `bytesize`, `encoding`, `force_encoding`, `encode`,
`valid_encoding?`, `ascii_only?`, `chars`, `each_char`, `codepoints`, `getbyte`,
`byteslice` exist, together with the `Encoding` class, its `UTF_8`, `US_ASCII`
and `ASCII_8BIT` constants, and the `# encoding:` magic comment. `Array#to_s` and
`Hash#to_s` are now `inspect`. `Hash` iterates in insertion order. A block
parameter is local to its block and no longer assigns to a same-named local of
the enclosing scope. `Kernel#proc` produces a non-lambda `Proc` that does not
check arity, while `Kernel#lambda` still does. `String#each`, `Array#nitems` and
`Object#type` are gone, and `when x:` and the `{1, 2}` hash
literal are syntax errors.

The grammar gains the `{key: value}` hash literal and `"str": value` keys, the
`->(){}` lambda, `%i[]` and `%I[]`, and block-local variables (`{ |x; y| }`).
The library gains keyword arguments and `**` keyword splats (`def f(k: 1, **r)`,
`f(**h)`), `Enumerator` as a core class needing no `require` — external iteration
keeps 1.8.7's `next` and `rewind` and gains `peek` — and
`Enumerator::Lazy` (`lazy`, `force`, chained `map`/`select`/`take`/`first` over
an endless source), `Fiber` (`Fiber.new`, `#resume`, `Fiber.yield`, `#alive?`),
refinements (`Module#refine` and `using`), `Module#prepend` and
`Enumerable#each_with_object`.

Everything the earlier corpus already covered has to keep working — but the
expected output of a program is whatever **CRuby 2.0.0** prints for it, which for
many of those programs is not what 1.8.7 printed.

**Running a program.** Each program is a standalone Ruby file, run as its own
process from a scratch working directory, and must finish within ten seconds
under a 4 GB address-space limit. A program behaves correctly when its standard
output is byte-for-byte what CRuby 2.0.0 produces, its exit status matches, and
— when it ends with an uncaught exception — the exception class named in
parentheses on standard error matches (a program rejected as a syntax error is
taken as ending with `SyntaxError`, however the message is phrased). Programs in
the `warn` stratum must also agree with CRuby 2.0.0 about whether a warning is
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

**Normative semantics:** `/app/SPEC/2.0.0/` holds CRuby 2.0.0's own `NEWS` —
the list of what this release changed — together with its `README`, the `man/ruby.1` page that documents the
command-line interface, and the `doc/syntax/` chapters.
`doc/NEWS-1.9.1`, `doc/NEWS-1.9.2` and `doc/NEWS-1.9.3` list what the three
releases between 1.8.7 and 2.0.0 changed.

**Conformance corpus:** `/app/spec/cases/` holds Ruby programs extracted from
`ruby/spec` and from `ruby/ruby`'s own `test/ruby` suite;
`/app/spec/expected.tsv` records, for each, the exit status, the exception class
if any, whether a warning was printed, the switches to run it with, and the
standard output **CRuby 2.0.0** produces (base64) — including for the programs
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

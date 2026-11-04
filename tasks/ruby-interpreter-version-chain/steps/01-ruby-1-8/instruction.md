# Ruby interpreter — CRuby 1.8.7

## 1. Implement this milestone

Write a Ruby interpreter in C that behaves like **CRuby 1.8.7**, the release
whose own documents are in `/app/SPEC/1.8.7/`.

**Build and entry point.** Running `make` in `/app` — no arguments, no targets —
must produce an executable at `/app/ruby`. It is driven through Ruby's own
command-line interface, `ruby [switches] program.rb`, documented in
`/app/SPEC/1.8.7/ruby.1`, and must accept the switches `-W0`, `-W1`, `-W2` and
`-w`. That man page documents only `-w`; the numeric `-W` levels (`-W0`, `-W1`,
`-W2`) do not appear in it but are part of the interpreter's command-line
interface and must be accepted. It reads the named file, executes it, and writes to standard output and
standard error. Its exit status is `0` on normal completion, the value given to
`Kernel#exit`, and `1` when the program ends with an uncaught exception. An
uncaught exception is reported on standard error in CRuby's format, which names
the exception's class in parentheses at the end of a line —
`program.rb:3:in 'foo': undefined method 'bar' for nil (NoMethodError)`. A
program that is not valid Ruby 1.8.7 is rejected before any of it runs, with
`syntax error` on standard error and exit status `1`. Under `-w` and `-W2` the
warnings CRuby 1.8.7 emits are written to standard error, each on a line
containing `warning:`; under `-W0` no warnings are written.

**What it must run.** The Ruby 1.8.7 language and core library, as CRuby 1.8.7
implements them. `/app/spec/` is a corpus of Ruby programs with the output that
release produces for each; `/app/spec/COVERAGE.md` names every area the
interpreter must cover and how many programs are in each.

It must be **1.8.7**, not a later Ruby. In 1.8.7 a `String` is a sequence of
bytes, so `"héllo".length` is `6` and `"abc"[0]` is the Integer `97`;
`[1, 2].to_s` is `"12"`; a block parameter that shares a name with a local of
the enclosing scope assigns to that local; `Kernel#proc` and `Kernel#lambda`
both produce arity-checking procs; `1.class` is `Fixnum`. Constructs and methods
that CRuby 1.8.7 does not have must fail exactly the way 1.8.7 fails them, with
the same error.

**Running a program.** Each program is a standalone Ruby file, run as its own
process from a scratch working directory, and must finish within ten seconds
under a 4 GB address-space limit. A program behaves correctly when its standard
output is byte-for-byte what CRuby 1.8.7 produces, its exit status matches, and
— when it ends with an uncaught exception — the exception class named in
parentheses on standard error matches (a program rejected as a syntax error is
taken as ending with `SyntaxError`, however the message is phrased). Programs in
the `warn` stratum must also agree with CRuby 1.8.7 about whether a warning is
printed: a warning where this release prints none is as wrong as a missing one.
The programs in `/app/spec/` are representative of what the interpreter must
run, not the whole of it.

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

**Normative semantics:** `/app/SPEC/1.8.7/` holds CRuby 1.8.7's own `NEWS`,
`README` and the `ruby.1` man page that documents the command-line interface.

**Conformance corpus:** `/app/spec/cases/` holds Ruby programs extracted from
`ruby/spec` and from `ruby/ruby`'s own `test/ruby` suite;
`/app/spec/expected.tsv` records, for each, the exit status, the exception class
if any, whether a warning was printed, the switches to run it with, and the
standard output CRuby 1.8.7 produces (base64). `sh /app/spec/run.sh` runs them
all against `/app/ruby` and prints how many pass; `sh /app/spec/run.sh <id>`
re-runs one and shows the difference. `/app/spec/COVERAGE.md` describes the
areas covered.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents
  and corpus. `make` in `/app` must keep producing `/app/ruby`.
- Do not cheat by using online solutions or hints specific to this task.
- Everything under `/app/spec/` is read-only input.
- Leave a reproducible handoff for the next fresh session: source that builds
  with `make`, and notes in `/app/NOTES.md` on what works and what does not.

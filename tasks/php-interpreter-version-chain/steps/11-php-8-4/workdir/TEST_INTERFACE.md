# TEST_INTERFACE.md — how your interpreter is built, started and driven

The build output path, the command-line options, the `.phpt` file format and the
`EXPECTF` placeholder language described here are all php-src's own, and the runner
reproduces exactly what php-src's `run-tests.php` does to the binary it is testing.

## 1. Build

```
make            # run in /app, with no arguments
```
must produce a native executable at

```
/app/sapi/cli/php
```

That is php-src's own build-output location for the CLI SAPI. The build must work in a
clean copy of the repository: one with every object file and executable deleted, without
`/app/tests` and `/app/SPEC` — the corpus and the release notes — or any other directory
named `tests` or `SPEC` wherever it sits, and without any `*.o`, `*.a` or `*.gz` file, so
your build must not depend on any of them. In that copy, `make` with no arguments must
succeed within 600 seconds and leave an ELF executable at `sapi/cli/php`, and the
requirements on the interpreter apply to the binary built that way.

The interpreter is a C program, built with the `cc`/`gcc`, `make`, `binutils` and libc
development headers this container already has. There is no PHP anywhere on it. The
project must build in a fresh container that has only that same toolchain and `python3`:
it must not depend on any library, header or tool you install in this one.
Your `make` must build from the sources in the repository alone, and it must keep debug
information (`-g`), because rule 3 below requires the binary's debug information to show
that it was compiled from those sources.

**This container has 7 CPUs and 16 GB of memory.** `nproc`, `os.cpu_count()` and
`/proc/cpuinfo` all report the machine's CPU count instead, which is much larger, so
`make -j$(nproc)` will start far more compilers than there are CPUs and can exhaust the
container's memory. Use `make -j7`. The test runner already works this out for itself.

**The target environment is 64-bit Linux**, the same platform you build on, and the
corpus assumes it: `PHP_OS` is `"Linux"`, `PHP_INT_SIZE` is `8`, `PHP_EOL` is a single
newline, and the tests whose names end in `_64bit` expect 64-bit integer limits.

A clean build starts from scratch, in a copy of the repository without any object
files, so whatever your makefile does incrementally cannot change its result. It can
still mislead *you* while you work. If your makefile does not rebuild an
object when a header it includes changes, an edit to a struct will leave stale objects
linked against the new layout, and the result is memory corruption that looks like a
wrong answer in an unrelated test. Either declare header prerequisites or `make clean`
before you trust a result.

The interpreter must be your own C code. Three rules state what that means, and all three
must hold:

1. **No php-src sources.** No file in the repository, or written by the build, may be
   php-src's own source code, whether it is a byte-identical copy or an edited one
   (reformatted, re-commented, with its identifiers renamed), and whatever the file is
   called: a C compiler compiles a file of any name, so the rule covers every text file,
   not only `*.c`, `*.h`, `*.y`, `*.l`, `*.re`, `*.inc`, `*.cpp`, `*.cc` and `*.hpp`.
   Neither kind of copy is allowed.
2. **No embedded binary payload.** Outside `tests/` and `SPEC/`, no file other than a C
   or C++ source or header may exceed 256 KiB, and no file of any kind may carry a
   base64 or hex blob, whether as one run thousands of characters long or cut into
   many shorter pieces.
3. **The executable must be this repository, compiled.** It must be an ordinary
   dynamically linked executable, not a statically linked one; its debug information
   must resolve, by the directory and file name each compilation unit was compiled
   from, to at least eight distinct `*.c` files of the tree it was built in; and
   it must import none of `execve`, `execv`, `execl`, `execlp`, `execvp`, `system`,
   `popen`, `fork`, `vfork`, `posix_spawn`, `posix_spawnp`, `dlopen`, `dlsym`,
   `syscall`, `ptrace` or `clone`: an interpreter written in C evaluates PHP itself
   and never starts another program or loads a shared object to do it.

## 2. Command line

The runner invokes the binary exactly as `run-tests.php` does:

```
sapi/cli/php -n -d <name>=<value> ... -f <script>.php [-- <args>]
```

with standard output and standard error merged. These are php-src's CLI SAPI options
(`php --help` in any PHP release documents them), and they are the only four the runner
uses:

| option | meaning |
| --- | --- |
| `-n` | do not load a `php.ini` file |
| `-d name[=value]` | set an INI entry; repeatable. Unknown INI names are accepted and ignored, as PHP accepts them |
| `-f <file>` | parse and run `<file>` |
| `--` | everything after it becomes `$argv[1]`, `$argv[2]`, … |

`$argv[0]` is the script path and `$argc` counts it. The runner compares only the
process's merged standard output and standard error; it does not examine the exit status.
Nothing else about the command line is needed;
implementing more of the CLI SAPI (`-r` for one-liners, `-v`, reading the script from
standard input) is useful while you work but is not required.

The INI entries the runner passes before every test are the `$ini_overwrites` list from
this release's own `run-tests.php`, and they are in `run-tests-ini.txt` next to the test
pool — read it, it is the authoritative list, and it differs between releases because
each release's own `run-tests.php` does. The entries that change observable behaviour
are `error_reporting=32767` (E_ALL), `display_errors=1`, `html_errors=0`,
`date.timezone=UTC` and whichever float-precision entries that file actually lists.
Each test's own `--INI--` section is appended after them.

Every test runs with a scrubbed environment: `PATH=/nonexistent`, `TZ=UTC`, `LC_ALL=C`,
`LANG=C`, plus the CGI-style variables `run-tests.php` sets (`REQUEST_METHOD=GET`,
`SCRIPT_FILENAME`, `PATH_TRANSLATED`, …) and the test's own `--ENV--` section. Your
interpreter must not need any other program to evaluate PHP code.

## 3. The `.phpt` format

php-src's test format. A test is a sequence of `--SECTION--` lines; the sections this
runner understands are:

| section | meaning |
| --- | --- |
| `--TEST--` | title |
| `--FILE--` | the PHP script; it is written to `<name>.php` **next to the `.phpt`** and run with that directory as the working directory, so a test may `include` a neighbouring `.inc` file |
| `--EXPECT--` | expected output, compared for exact equality after both sides are trimmed of `" \t\n\r\0\x0B"` and CRLF-normalised |
| `--EXPECTF--` | expected output as a pattern (below) |
| `--EXPECTREGEX--` | expected output as a PCRE, used as-is |
| `--INI--` | extra `name=value` INI entries, one per line; `{PWD}` expands to the test's directory |
| `--ARGS--` | extra arguments, appended after `--` |
| `--ENV--` | extra environment entries, one `name=value` per line |
| `--STDIN--` | fed to the process on standard input |
| `--SKIPIF--`, `--CLEAN--`, `--CREDITS--`, `--DESCRIPTION--`, `--CONFLICTS--` | accepted and ignored |

`--SKIPIF--` is **not executed**. Every test in the pool runs and must pass.
Tests carrying `--XFAIL--` are not run; the runner reports them as XFAIL.

`EXPECTF` placeholders, with exactly php-src's meanings:

| placeholder | matches |
| --- | --- |
| `%s` | one or more characters other than CR/LF |
| `%S` | zero or more characters other than CR/LF |
| `%a` | one or more of any character |
| `%A` | zero or more of any character |
| `%w` | zero or more whitespace characters |
| `%i` | an optionally signed integer |
| `%d` | one or more digits |
| `%x` | one or more hexadecimal digits |
| `%f` | a floating-point number |
| `%c` | exactly one character |
| `%e` | the directory separator |
| `%0` | a NUL byte |
| `%r…%r` | the enclosed text is a regular expression |

Everything outside a placeholder is matched literally; the whole output must match from
start to end with `.` matching newlines.

## 4. Running the pool

```
python3 /app/tests/run-phpt.py --php /app/sapi/cli/php \
        --root /app/tests/phpt \
        /app/tests/phpt/Zend/tests        # or any file or directory
```

Useful options: `-j N` (how many tests run at once, default: the CPUs this container
may use), `--timeout SEC` (default 20), `--memory-cap-mb MB` (ceiling on one test's
resident memory, default 2048, `0` to lift it), `--list FILE` (one test path per line,
relative to `--root`), `--show-diff N` (print the expected and actual output of the
first N failures), `--json FILE` (machine-readable report). The runner exits 0 only
when every test passed.

The ceilings exist because the binary under test is an interpreter that is still being
written: a runaway allocation or a runaway loop in it should fail that one test rather
than take the container down with it. A test that goes over the memory ceiling is
killed and reported as a failure saying so.

**The tests must pass under `-j 7 --timeout 20 --memory-cap-mb 2048`** -- the
runner's own defaults in this container.
The slowest test in the pool takes about five seconds on a correct interpreter, so twenty
is a wide margin for a slow one.

The runner, the pool and the ini list define the required behaviour as supplied;
editing them does not change what is required.

# PHP 8.1 — enums, fibers, readonly properties and first-class callables

## 1. Implement this milestone

`/app` holds a PHP interpreter written in C: `make`, run with no arguments in `/app`,
builds it into `/app/sapi/cli/php`. It is meant to implement **PHP 8.0** — the corpora
already under `/app/tests/` are what that means, and whatever of it is missing is still
owed. Bring the interpreter to **PHP 8.1.31**.

`/app/SPEC/php-8.1/UPGRADING` is php-src's own account of what changed in this release,
and `/app/tests/phpt/` holds php-src's own `.phpt` tests at tag `php-8.1.31`, copied verbatim.
The tests are the specification of this milestone: make them pass.

What the release changes, and what this milestone therefore covers:

* **enumerations**: pure and backed enums, `cases()`, `from()`, `tryFrom()`, constants, methods, interfaces, `UnitEnum`/`BackedEnum`, the `Error`s for instantiating or cloning one, and how `var_dump()` prints an enum case;
* **fibers**: `Fiber::start()`, `Fiber::suspend()`, `resume()`, `throw()`, `getReturn()`, `isSuspended()`/`isRunning()`/`isTerminated()`, `Fiber::getCurrent()` and the `FiberError` cases, including suspending across an arbitrary call depth;
* **readonly properties**, with `Error: Cannot modify readonly property C::$p`;
* the **`never`** return type;
* **first-class callable syntax** `strlen(...)`, `$obj->m(...)`, `C::m(...)`;
* **intersection types** `A&B`;
* **`new` in initializers** (parameter defaults, static variables, global constants, attribute arguments);
* **explicit octal notation** `0o777`;
* **final class constants**;
* `enum_exists()`;
* the **implicit float-to-int deprecation**: converting a float with a fractional part to `int` — through a parameter, return or property type of `int` in coercive mode — emits `Deprecated: Implicit conversion from float <v> to int loses precision`, while a float whose fractional part is exactly zero converts without a diagnostic;
* the `$GLOBALS` restrictions and the interface-constant rules `UPGRADING` lists.

**Behaviour that changed must change.** `/app/tests/phpt/` has been refreshed to this
release's tag, so a test whose expected output moved now states the new behaviour and
your interpreter has to produce it; `/app/SPEC/php-8.1/UPGRADING` is php-src's own record
of which behaviours those are. Everything the release did not change must keep working —
the refreshed corpus still contains the earlier releases' tests, and they must keep passing.

The `PHP_VERSION` constant reports `8.1.31`, and `PHP_VERSION_ID` reports `80131`.

## 2. Read these local references

**Normative semantics.** `/app/SPEC/php-8.1/UPGRADING` and `/app/SPEC/php-8.1/NEWS`
(php-src's release notes for this series) and `/app/tests/phpt/` (php-src's `.phpt`
tests at tag `php-8.1.31`, copied verbatim). The PHP language reference at
`https://www.php.net/manual/en/` (local copy of its DocBook source, covering the language
reference, the appendices and the functions of the extensions under test:
`/opt/docs/php-manual/`) documents the same behaviour in prose.

**Additional files.** `/app/TEST_INTERFACE.md` — the build contract, the command-line
options your binary must accept, the `.phpt` format and the `EXPECTF` placeholder rules,
and how to run the corpus. `/app/WORKSPACE_HANDOFF.md` — the running record of this
repository; read it first and update it before you finish. `/app/tests/run-phpt.py` —
the runner.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied tests, specs and
  documents. Do not create a prose answer or an alternate entry point: the artifact
  is the binary `make` builds at `/app/sapi/cli/php`.
- The interpreter must be your own C code, and `/app/TEST_INTERFACE.md` section 1 states
  what that means: no file may be php-src source, verbatim or edited, no
  embedded binary payload is allowed, the build must keep debug information, and the
  executable must be dynamically linked, must carry debug information that
  resolves to at least eight of the repository's own `*.c` files as a clean `make`
  builds them, and must import no process-spawning or dynamic-loading symbol. There is no PHP on this container.
- Do not search for, download, copy, wrap, or invoke an online
  implementation or task-specific answer; work only from this workspace, its
  documentation and `/opt/docs`, and do not search the system for other
  implementations or answers.
- The runner, the ini list and the tests define the required behaviour as supplied;
  editing them in `/app` does not change what is required.
- The project must build when `make` runs in a copy of the repository with every
  object file and executable deleted, and `/app/sapi/cli/php` must work when driven
  exactly as `/app/TEST_INTERFACE.md` describes. Its behaviour must also hold for
  variants of the corpus's own cases, not only for the literal files, and its version
  constants and which classes and functions it defines must be those of this release.
- Run the corpus yourself as you go:
  `python3 /app/tests/run-phpt.py --php /app/sapi/cli/php --root /app/tests/phpt /app/tests/phpt/Zend/tests --show-diff 3`

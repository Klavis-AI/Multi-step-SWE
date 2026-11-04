# PHP 8.3 — typed class constants, #[\Override] and dynamic class-constant fetch

## 1. Implement this milestone

`/app` holds a PHP interpreter written in C: `make`, run with no arguments in `/app`,
builds it into `/app/sapi/cli/php`. It is meant to implement **PHP 8.2** — the corpora
already under `/app/tests/` are what that means, and whatever of it is missing is still
owed. Bring the interpreter to **PHP 8.3.16**.

`/app/SPEC/php-8.3/UPGRADING` is php-src's own account of what changed in this release,
and `/app/tests/phpt/` holds php-src's own `.phpt` tests at tag `php-8.3.16`, copied verbatim.
The tests are the specification of this milestone: make them pass.

What the release changes, and what this milestone therefore covers:

* **typed class constants**: `const int X = 1;`, with the error when an inherited constant changes the declared type;
* the **`#[\Override]`** attribute;
* **dynamic class constant fetch** `C::{$name}` and `C::{expr}`;
* **`json_validate()`**;
* **increment and decrement diagnostics**: `++` and `--` on a `bool`, and `--` on `null`, have no effect and now emit `Warning: Increment on type bool has no effect, this will change in the next major version of PHP` (and the matching `Decrement`/`null` forms); `--` on a non-numeric string emits `Deprecated: Decrement on non-numeric string has no effect and is deprecated`;
* the **`get_class()` deprecation**: calling it with no arguments emits `Deprecated: Calling get_class() without arguments is deprecated`.

**Behaviour that changed must change.** `/app/tests/phpt/` has been refreshed to this
release's tag, so a test whose expected output moved now states the new behaviour and
your interpreter has to produce it; `/app/SPEC/php-8.3/UPGRADING` is php-src's own record
of which behaviours those are. Everything the release did not change must keep working —
the refreshed corpus still contains the earlier releases' tests, and they must keep passing.

The `PHP_VERSION` constant reports `8.3.16`, and `PHP_VERSION_ID` reports `80316`.

## 2. Read these local references

**Normative semantics.** `/app/SPEC/php-8.3/UPGRADING` and `/app/SPEC/php-8.3/NEWS`
(php-src's release notes for this series) and `/app/tests/phpt/` (php-src's `.phpt`
tests at tag `php-8.3.16`, copied verbatim). The PHP language reference at
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

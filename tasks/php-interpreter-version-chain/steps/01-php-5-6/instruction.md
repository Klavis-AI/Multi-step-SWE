# PHP 5.6 — a working interpreter

## 1. Implement this milestone

Write a PHP interpreter in C. `make`, run with no arguments in `/app`, must build it
into `/app/sapi/cli/php` — php-src's own build-output path for the CLI SAPI — and the
result must behave as **PHP 5.6.40** does.

"Behave as PHP 5.6.40 does" has an exact meaning here: php-src's own `.phpt` tests for
that release are in `/app/tests/phpt/`, copied verbatim from tag `php-5.6.40` of
`https://github.com/php/php-src`. Those `.phpt` files are the specification of this
milestone. Make them pass.

The corpus covers the language and the part of the library it needs:

* the value model — integers, floats, strings, booleans, null, **copy-on-write arrays**
  and `&` references, the numeric-string rules, and every conversion and comparison rule
  between them;
* the ordered array: insertion order, integer-like string keys, the next-free-element
  rule, `list()`, `foreach` by value and by reference, the internal pointer;
* functions — default, by-reference and **variadic** parameters, `func_get_args()`,
  recursion, `static` variables, `global`;
* **closures** — `use` by value and by reference, `$this` binding, `static function`,
  `Closure::bind`/`bindTo`, `__invoke`;
* **generators** — `yield`, `yield $k => $v`, `send()`, `current()`/`key()`/`next()`/
  `valid()`/`rewind()`, generators in `foreach`, and throwing into and out of a
  generator;
* classes — properties, constants, `static`, visibility, inheritance, `abstract`,
  `final`, interfaces, **traits** with `insteadof` and `as`, late static binding,
  `instanceof`, `clone`;
* the **magic methods** `__construct` `__destruct` `__get` `__set` `__isset` `__unset`
  `__call` `__callStatic` `__toString` `__invoke` `__clone`;
* exceptions — `try`/`catch`/`finally`, the SPL exception hierarchy, uncaught-exception
  output;
* the rest of the statement and expression grammar, including `switch`, `goto`,
  heredoc and nowdoc, string interpolation in both the simple and the `{$…}` form,
  `**`, `use function`/`use const`, and constant scalar expressions;
* the core library functions the corpus calls — string, array, math, variable-handling
  and type functions, `json_encode`/`json_decode`, and the `mbstring` functions the
  corpus uses. The set is exactly the functions the tests in the pool call, and each must
  behave as php-src implements it at this tag.

Diagnostics are part of the behaviour, not decoration: `Notice:`, `Warning:`,
`Deprecated:`, `Strict Standards:`, `Catchable fatal error:`, `Parse error:` and
`Fatal error:` must carry php-src's exact text, file and line, because the tests
compare the whole of stdout and stderr.

## 2. Read these local references

**Normative semantics.** `/app/tests/phpt/` — php-src's `.phpt` tests at tag
`php-5.6.40`, which define what correct means, and `/app/SPEC/php-5.6/UPGRADING` and
`/app/SPEC/php-5.6/NEWS`, php-src's own release notes for the 5.6 series. The PHP
language reference at `https://www.php.net/manual/en/` (local copy of its DocBook source, covering the language
reference, the appendices and the functions of the extensions under test:
`/opt/docs/php-manual/`) documents the same behaviour in
prose.

**Additional files.** `/app/TEST_INTERFACE.md` — the build contract, the command-line
options your binary must accept, the `.phpt` format and the `EXPECTF` placeholder
rules, and how to run the corpus. `/app/WORKSPACE_HANDOFF.md` — the running record of
this repository; update it before you finish. `/app/tests/run-phpt.py` — the runner.

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
  constants and which classes and functions it defines must be those of PHP 5.6.40.
- Run the corpus yourself as you go:
  `python3 /app/tests/run-phpt.py --php /app/sapi/cli/php --root /app/tests/phpt /app/tests/phpt/Zend/tests --show-diff 3`

# Add custom properties and `var()` to the layout engine

## Implement this milestone

`/app` already holds a WebDriver remote end that lays out CSS 2.1 documents and
resolves `calc()`. Everything it does today must keep working.

Add **custom properties** and the **`var()`** function, as CSS Custom Properties
for Cascading Variables Level 1 defines them
(<https://www.w3.org/TR/css-variables-1/>; local copy:
`/opt/docs/w3c/CR-css-variables-1-20220616.html`).

The behaviours in scope:

| behaviour | Variables 1 |
| --- | --- |
| a declaration whose name begins with two dashes is a custom property and is retained on the element | §2 |
| `var(--x)` substitutes that value wherever a value is accepted | §3 |
| a `var()` that resolves to nothing, with no fallback, makes the declaration invalid at computed-value time | §3 |
| a `var()` inside a **shorthand** substitutes before the shorthand is parsed, so the longhands it expands to — `margin-*`, `border-*-width` — get the substituted value | §3 |
| substitution happens before the value is parsed, so `calc(var(--x) + 20px)` works | §3 |

Custom properties also inherit (§2.1), and `var(--x, <fallback>)` uses its
fallback when `--x` is not set (§3); the documents at this milestone do not
depend on either.

A custom property name is case-sensitive and is spelled as an identifier
beginning with `--`; note that CSS Syntax Level 3 §4.3.9 (local copy:
`/opt/docs/w3c/CRD-css-syntax-3-20261001.html`) makes `--` a valid
identifier start, which an engine written for CSS 2.1 will not do.

## Read these local references

* `/app/SPEC/03-selectors3-vars.md` — what this milestone covers.
* `/app/TEST_INTERFACE.md` — the WebDriver commands the remote end serves.
* `/app/WORKSPACE_HANDOFF.md` — what is already in this workspace.

Check progress with the corpus in `/app/css-corpus/`. It is
divided into named strata; **`s3-var`** is this milestone's, selected so that
the previous milestone's engine fails at least one check in each of them and a
correct engine for this one passes all of them. The earlier milestones' strata
are still there and still run, so a regression shows up as well:

```
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --list-strata
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only s3-var --show-failures 20
python3 /app/css-corpus/run.py --binary /app/build/cssdriver
```

Read the per-stratum lines, not the TOTAL: the TOTAL sums strata of very
different sizes.

## Meta requirements

* The remote end is written in **C11** and built with `cc`. The container has a
  C toolchain and no C++ compiler, so a C++ source tree will not build — here or
  in a clean build.
* It is one program, built here from this workspace's C sources: it links
  nothing beyond the C library and the compiler's own runtime, carries no other
  language's runtime and no existing layout engine, and while it runs it
  starts no other program and executes no code from anywhere else. `make` must build
  it from this workspace's own files with the C toolchain alone — it must not
  fetch anything — and must finish within six minutes.
* `make`, run in `/app`, must build `/app/build/cssdriver` from a clean copy of
  this directory, including when the workspace is copied elsewhere and
  `make -j7` is run in the copy, as an unprivileged user, in a container that
  has a C toolchain and nothing of yours.
* Keep everything the earlier milestones established working: this workspace is
  cumulative. The corpus at `/app/css-corpus/` covers all earlier
  strata as well and is the right tool for confirming regressions.
* You have 7,200 seconds of working time. `bash /app/timer.sh` reports what is
  left; the configured timeout is the one that counts.
* Do not cheat by using online solutions or hints specific to this task.

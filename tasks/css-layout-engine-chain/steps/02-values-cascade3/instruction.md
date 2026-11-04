# Add CSS Values and Units Level 3 `calc()` to the layout engine

## Implement this milestone

`/app` already holds a W3C WebDriver remote end that lays out CSS 2.1 documents.
Everything it does today must keep working exactly as it does.

Add support for **`calc()`** as CSS Values and Units Level 3 defines it
(<https://www.w3.org/TR/css-values-3/#calc-notation>; local copy:
`/opt/docs/w3c/CRD-css-values-3-20240322.html`), wherever a length is
accepted: `width`, `height`, `min-width`, `min-height`, `max-width`,
`max-height`, `margin-*`, `padding-*`, `top`, `right`, `bottom`, `left`,
`text-indent`, `font-size`, `line-height` and `vertical-align`.

The behaviours in scope:

| behaviour | Values 3 |
| --- | --- |
| `+` and `-` over lengths and percentages, resolved against the same base a bare percentage would use | §8.1 |
| `*` and `/` where at least one operand of `*`, and the right operand of `/`, is a `<number>` | §8.1 |
| a `calc()` whose operands mix a percentage and a length stays unresolved until its containing block is known | §8.1 |
| nested parentheses, and `calc()` inside `calc()` | §8.1 |

§8.1 also makes some expressions invalid — division by zero, and
multiplication where neither operand is a `<number>`; the documents at this
milestone use only valid expressions.

## Read these local references

* `/app/SPEC/02-values-cascade3.md` — what this milestone covers, and the
  rounding and comparison rules that apply to it.
* `/app/TEST_INTERFACE.md` — the WebDriver commands the remote end must serve
  and how resolved values are reported. Unchanged from the previous milestone.
* `/app/WORKSPACE_HANDOFF.md` — what is already in this workspace.

Check progress with the corpus in `/app/css-corpus/`. It is
divided into named strata; **`s2-calc`** is this milestone's, selected so that
the previous milestone's engine fails at least one check in each of them and a
correct engine for this one passes all of them. The earlier milestones' strata
are still there and still run, so a regression shows up as well:

```
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --list-strata
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only s2-calc --show-failures 20
python3 /app/css-corpus/run.py --binary /app/build/cssdriver
```

Read the per-stratum lines, not the TOTAL: the TOTAL sums strata of very
different sizes.
`s2-calc-carryover` is the one stratum that is **not** a signal about this
milestone: it holds `calc()` documents the previous engine already laid out
correctly, so it was green before `calc()` existed here and will stay green
whatever you do. `s2-calc` is the stratum that moves.

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
* Keep everything the previous milestone established working: this workspace is
  cumulative. The corpus at `/app/css-corpus/` covers all earlier
  strata as well and is the right tool for confirming regressions.
* You have 7,200 seconds of working time. `bash /app/timer.sh` reports what is
  left; the configured timeout is the one that counts.
* Do not cheat by using online solutions or hints specific to this task.

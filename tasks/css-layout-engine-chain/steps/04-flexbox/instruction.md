# Add flex layout to the layout engine

## Implement this milestone

`/app` already holds a WebDriver remote end that lays out CSS 2.1 documents,
resolves `calc()`, and substitutes `var()`. Everything it does today must keep
working.

Add **flex layout**, as CSS Flexible Box Layout Level 1 defines it
(<https://www.w3.org/TR/css-flexbox-1/>; local copy:
`/opt/docs/w3c/CRD-css-flexbox-1-20251014.html`), at its final rules — not the early
drafts, whose behaviour several of this milestone's documents deliberately
distinguish from the current level.

The behaviours in scope:

| behaviour | Flexbox 1 |
| --- | --- |
| `display: flex` and `inline-flex` generate a flex container, which establishes a flex formatting context rather than a block one | §3 |
| a flex container's in-flow children are flex items; their `float` does not apply and their margins do not collapse | §4 |
| `flex-direction` and `flex-wrap` (and the `flex-flow` shorthand) select the main and cross axes, including the reversed directions | §5.1–§5.3 |
| `flex-grow`, `flex-shrink` and `flex-basis` (and the `flex` shorthand, including `flex: none` and a bare `<number>`) determine each item's main size | §7.1–§7.3 |
| resolving flexible lengths: distribute free space, clamp, freeze, and repeat until every item is frozen | §9.7 |
| `justify-content` distributes free space on the main axis, including `space-between`, `space-around` and `space-evenly` | §8.2 |
| `align-items` and `align-self` position items on the cross axis; `stretch` applies only when the cross size is `auto` | §8.3 |
| `align-content` distributes multiple flex lines when the container wraps | §8.4 |
| `order` reorders items within a line, leaving equal values in document order | §5.4 |
| `min-width` / `min-height` take `auto` as their initial value, and on a flex item `auto` means the content-based minimum size | §4.5 |

Note that §4.5 changes the initial value of `min-width` and `min-height` from
`0` to `auto`. Outside a flex container that `auto` still resolves to `0`, and
the CSSOM resolved value is `0px`; on a flex item it is the keyword `auto` and
the used minimum is content-based. Documents at this milestone distinguish
these two readings.

## Read these local references

* `/app/SPEC/04-flexbox.md` — what this milestone covers.
* `/app/TEST_INTERFACE.md` — the WebDriver commands the remote end serves.
* `/app/WORKSPACE_HANDOFF.md` — what is already in this workspace.

Check progress with the corpus in `/app/css-corpus/`. It is
divided into named strata; **the seven `s4-…` strata** are this milestone's, selected so that
the previous milestone's engine fails at least one check in each of them and a
correct engine for this one passes all of them. The earlier milestones' strata
are still there and still run, so a regression shows up as well:

```
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --list-strata
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only s4- --show-failures 20
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

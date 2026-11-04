# Add grid layout to the layout engine

## Implement this milestone

`/app` already holds a WebDriver remote end that lays out CSS 2.1 documents,
resolves `calc()`, substitutes `var()`, and performs flex layout. Everything it
does today must keep working.

Add **grid layout**, as CSS Grid Layout Level 1 defines it
(<https://www.w3.org/TR/css-grid-1/>; local copy:
`/opt/docs/w3c/CRD-css-grid-1-20250326.html`).

The behaviours in scope:

| behaviour | Grid 1 |
| --- | --- |
| `display: grid` and `inline-grid` generate a grid container, which establishes an independent formatting context rather than a block one | §3 |
| a grid container's in-flow children are grid items; their `float` does not apply and their margins do not collapse | §6 |
| `grid-template-columns` and `grid-template-rows` define the explicit track list, from `<length>`, `<percentage>`, `auto`, `min-content`, `max-content` and `<flex>fr` | §7.2 |
| `fr` distributes the space left after fixed tracks and gaps are subtracted — not a fraction of the container | §7.2.3 |
| `grid-column` and `grid-row` place an item by line number or `span`; a missing end line is `auto`, so `grid-column: 2` spans one track | §8.3, §8.4 |
| `grid-template-areas` with `grid-area` places items by name | §7.3 |
| items with no explicit placement are auto-placed along the flow axis and wrap on the other | §8.5 |
| `grid-auto-flow: column` swaps which axis the cursor advances along | §7.7 |
| `grid-auto-rows` / `grid-auto-columns` size the implicit tracks created outside the explicit grid | §7.5 |
| `row-gap`, `column-gap` and the `gap` shorthand separate tracks without adding space at the container's edges | css-align-3 §8 (local copy: `/opt/docs/w3c/WD-css-align-3-20261008.html`) |
| `justify-items` / `align-items` and `justify-self` / `align-self` position an item inside its grid area; `stretch` applies only where the item's size is `auto` | §10.1 |
| `justify-content` / `align-content` distribute the track grid inside the content box when it is smaller | §10.3 |
| a **replaced element** used as a grid item — `<canvas>`, `<img>`, `<video>`, `<iframe>` — carries the intrinsic size its own attributes or content give it, which is what an `auto` track and an `auto` cross size resolve against | §6, §7.2 |
| `align-items: baseline` / `align-self: baseline` align an item on the first baseline of its first line box | §10.1 |
| an **absolutely positioned** child of a grid container is placed against its grid area, not against the container's padding box | §9 |

Note that with the initial `normal` distribution there is no leftover space to
distribute: `auto`-sized tracks absorb it. An empty `auto` track measures zero
and then stretches, so a two-column `auto` grid in a 200px box gives two 100px
columns, not two zero-width ones.


## Read these local references

* `/app/SPEC/05-grid1.md` — what this milestone covers.
* `/app/TEST_INTERFACE.md` — the WebDriver commands the remote end serves.
* `/app/WORKSPACE_HANDOFF.md` — what is already in this workspace.

Check progress with the corpus in `/app/css-corpus/`. It is
divided into named strata; **the six `s5-…` strata** are this milestone's, selected so that
the previous milestone's engine fails at least one check in each of them and a
correct engine for this one passes all of them. The earlier milestones' strata
are still there and still run, so a regression shows up as well:

```
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --list-strata
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only s5- --show-failures 20
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

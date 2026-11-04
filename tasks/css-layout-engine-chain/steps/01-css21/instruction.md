# A CSS 2.1 layout engine behind a WebDriver remote end

## 1. What to build

Build a **W3C WebDriver remote end** that loads HTML documents and reports the
geometry and resolved style of their elements, with a **CSS 2.1 visual formatting
model** behind it.

**Read section 2 before planning the work.** The engine is the interesting part,
but everything is reached through the interface: a remote end that does not
start, or that answers a command in the wrong shape, is useless however correct
the layout behind it is.

### The contract

* The remote end is written in **C11** and built with `cc`. The container has a
  C toolchain and no C++ compiler, so a C++ source tree will not build — here or
  in a clean build.
* It is one program, built here from this workspace's C sources: it links
  nothing beyond the C library and the compiler's own runtime, carries no other
  language's runtime and no existing layout engine, and while it runs it
  starts no other program and executes no code from anywhere else. `make` must build
  it from this workspace's own files with the C toolchain alone — it must not
  fetch anything — and must finish within six minutes.
* `make`, run in `/app`, builds the project and produces an executable at
  **`/app/build/cssdriver`**. The build must also succeed when this workspace is
  copied elsewhere and **`make -j7`** is run in the copy, as an unprivileged user,
  in a container that has a C toolchain and nothing else — so it must work from a
  clean tree and must be safe to run in parallel.
* It is started as **`/app/build/cssdriver --port=<n>`** and serves the WebDriver
  protocol on `127.0.0.1:<n>`. `--port` is the only option it is given.
* It must implement these commands of **WebDriver Level 1**
  (<https://www.w3.org/TR/webdriver1/>; local copy:
  `/opt/docs/w3c/REC-webdriver1-20180605.html`), whose request and response shapes that specification fixes:
  `GET /status`; `POST /session`; `DELETE /session/{id}`;
  `POST /session/{id}/window/rect`; `POST /session/{id}/url`;
  `POST /session/{id}/elements`; `GET /session/{id}/element/{id}/rect`;
  `GET /session/{id}/element/{id}/css/{name}`.
* The session is created with `browserName` set to **`cssdriver`**. Element
  queries locate elements by **id selector** (`#name`); the strategy name sent is
  `css selector`. The web
  element identifier is the string `element-6066-11e4-a52e-4f735466cecf`
  (§11.2). Find Elements (`POST /session/{id}/elements`) returns every element
  the selector matches, in tree order, and the **first** element of that list is
  the one whose rect and resolved values are read.
* `POST .../window/rect` sets the **viewport**; every document is measured at
  **800 × 600 CSS pixels**. `POST .../url` is given a **`file://`** URL and the
  document is parsed per the WHATWG HTML parsing specification (local copy:
  section 13.2 of `/opt/docs/whatwg/html-review-draft-2026-07.html`).
* `Get Element Rect` reports the element's **border box** in CSS pixels, with
  `x`/`y` relative to the initial containing block's origin. A rect is correct
  when **each of `x`, `y`, `width` and `height` is within 0.5 CSS pixels** of the
  reference.
* A resolved value that is a **length** is compared numerically, to the same
  0.5 CSS pixel tolerance as a rect. A keyword value (`block`, `auto`) must match
  exactly.
* `Get Element CSS Value` reports the **resolved value** as CSSOM defines it
  (<https://drafts.csswg.org/cssom/#resolved-values>; local copy of the W3C
  snapshot: `/opt/docs/w3c/WD-cssom-1-20210826.html`). **At this milestone**
  `display`, `width` and `height` are the properties in scope; the
  `What is measured` section of `/app/SPEC/01-css21.md` names them, and that is
  the list to work from. A property whose used value is not a length — `width` and
  `height` on a non-replaced inline box, for instance — resolves to its computed
  value, which is `auto`.

### The layout

Implement CSS 2.1 (<https://www.w3.org/TR/CSS21/>; local copy:
`/opt/docs/w3c/REC-CSS2-20110607/`, also as plain text in `css2.txt` there) well
enough to reproduce a browser's geometry on ordinary documents. These are the behaviours in
scope:

| behaviour | CSS 2.1 |
| --- | --- |
| block formatting contexts; block width and height resolution; auto margins | §9.4.1, §10.3.3, §10.6.3 |
| content, padding, border and margin edges; `box-sizing`; min/max clamping | §8.1, §10.4 |
| collapsing margins, collapse-through, and what prevents collapsing | §8.3.1 |
| float placement, shrink-to-fit, `clear` and clearance | §9.5, §10.3.5 |
| relative, absolute and fixed positioning; static position; over-constrained insets | §9.3, §10.1, §10.3.7, §10.6.4 |
| line box construction, white-space processing, `line-height` and half-leading, baseline alignment | §9.4.2, §10.8, §16.6 |
| `overflow` establishing a block formatting context; scrollbars consuming space | §11.1.1, §9.4.1 |

The documents also use `direction: ltr` and `direction: rtl`, and both
`box-sizing` values, throughout; those are axes across the behaviours above
rather than behaviours of their own.

Two environmental details the documents rely on, both described in
`/app/SPEC/01-css21.md`: documents style the scrolling mechanism's thickness
themselves with the `::-webkit-scrollbar` pseudo-element, and documents that do
not declare a font are laid out against the **HTML user-agent stylesheet and
default font metrics** given there. Documents containing Ahem text supply
`Ahem.ttf` beside themselves and reference it from their own `@font-face` rule.

## 2. Read these local references

**Normative semantics.** `/app/SPEC/01-css21.md` lists the exact chapters of
CSS 2.1 this milestone rests on, together with the specifications for the
interface: WebDriver Level 1, CSSOM resolved values, and WHATWG HTML parsing.
Local copies of the documents it links are under `/opt/docs/w3c/` and
`/opt/docs/whatwg/`.
Where this instruction and a specification disagree, the specification wins.

**The interface in full.** `/app/TEST_INTERFACE.md` describes how the engine is
built, started and driven, endpoint by endpoint, and states the comparison rules
and the Ahem metrics. **Read it before writing code.**

**Handoff.** `/app/WORKSPACE_HANDOFF.md` describes the cumulative workspace and
what to leave behind.

**Checking your own progress.** `/app/css-corpus/` holds documents with reference
values recorded for them, and a runner that drives your remote end over them with
the same protocol, the same tolerance and the same per-element comparison described
above and in `/app/TEST_INTERFACE.md`:

```
python3 /app/css-corpus/run.py --binary /app/build/cssdriver
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only css21-floats-clearance --show-failures 20
```

Those documents are yours to work against. They are divided into named strata,
one per behaviour family; read the per-stratum lines rather than the **TOTAL**,
which weights a stratum by its size. The corpus is representative; the engine is
expected to lay out other documents of the same kinds just as well.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents.
  `make` must succeed from a clean copy of this directory and must produce
  `/app/build/cssdriver`.
- Do not cheat by using online solutions or hints specific to this task.
- Run the local checks named above and leave the workspace in a buildable state.

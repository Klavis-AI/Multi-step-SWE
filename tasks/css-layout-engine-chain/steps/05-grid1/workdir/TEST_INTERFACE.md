# The interface this engine is driven through

This file describes **how the artifact is built, started and driven**. It does
not describe layout behaviour — the normative documents under `/app/SPEC/` do
that. Nothing here is specific to any particular test.

## 1. Build and start

* `make` in `/app` builds the project and produces the executable
  **`/app/build/cssdriver`**.
* It is started as **`/app/build/cssdriver --port=<n>`** and serves on
  `127.0.0.1:<n>`. That is the convention every WebDriver driver binary follows;
  `cssdriver` is this remote end's own name.
* `--port` is the only command-line option. Everything else — the viewport, the
  document, what is measured — is selected through the protocol below, exactly
  as a real browser driver selects it.

## 2. The protocol: W3C WebDriver Level 1

**<https://www.w3.org/TR/webdriver1/>** — a W3C Recommendation. The executable is
a WebDriver **remote end**: the role ChromeDriver, geckodriver, safaridriver and
WebKitWebDriver all implement. These are the commands it is driven with:

| Method | Path | Spec § |
| --- | --- | --- |
| `GET` | `/status` | 8.3 |
| `POST` | `/session` | 8.1 |
| `DELETE` | `/session/{sessionId}` | 8.2 |
| `POST` | `/session/{sessionId}/window/rect` | 10.7.3 |
| `POST` | `/session/{sessionId}/url` | 9.1 |
| `POST` | `/session/{sessionId}/elements` | 12.2.3 |
| `GET` | `/session/{sessionId}/element/{elementId}/rect` | 13.6 |
| `GET` | `/session/{sessionId}/element/{elementId}/css/{propertyName}` | 13.4 |

Notes that the specification fixes and that are therefore not open choices:

* every response body is `{"value": ...}`, and an error is
  `{"value":{"error":"<code>","message":"...","stacktrace":""}}` with the
  status the specification gives (§6.6 and the per-command sections);
* the **web element identifier** is the string
  `element-6066-11e4-a52e-4f735466cecf` (§11.2), so an element is returned as
  `{"element-6066-11e4-a52e-4f735466cecf": "<opaque id>"}`;
* Find Elements uses the **`css selector`** location strategy;
* `Get Element CSS Value` returns the **resolved value** as CSSOM defines it —
  <https://drafts.csswg.org/cssom/#resolved-values>. Element queries use the
  `css selector` strategy with **id selectors only**.

## 3. How it is driven

1. `POST /session`, then `POST /session/{id}/window/rect` with a width and
   height. **That rect is the viewport**: the remote end has no browser chrome,
   so the window and the viewport are the same box, and `vw`/`vh`/`vmin`/`vmax`
   resolve against it. Every document is measured at a viewport of
   **800 × 600 CSS pixels**.
   The session is created with `browserName` set to **`cssdriver`**.
2. `POST /session/{id}/url` with a **`file://`** URL naming an HTML document on
   disk. The document is parsed per the WHATWG HTML parsing specification
   (<https://html.spec.whatwg.org/multipage/parsing.html>) and laid out.
3. Elements are located with `POST /session/{id}/elements` using an id selector
   such as `#e12`, and then measured with `Get Element Rect` and
   `Get Element CSS Value`.

## 4. What "correct" means numerically

* `Get Element Rect` returns the element's **border box** in CSS pixels, with
  `x`/`y` relative to the initial containing block's origin.
* A rect is correct when **each of `x`, `y`, `width` and `height` is within
  0.5 CSS pixels** of the reference value. That tolerance is half a pixel on
  each edge and is the whole tolerance: a box a pixel out of place is wrong.
* An element counts as correct only when its rect **and** every resolved value
  read for it are correct; each measured element is one check.
* A resolved value that is a **length** is compared numerically, to the same
  0.5 CSS pixel tolerance as a rect. A keyword value (`block`, `auto`) must match
  exactly. A property that does not apply, or whose used value is not a length — `width`/`height` on a non-replaced inline box —
resolves to its computed value (`auto`).
* **Which** properties are read back is set per milestone: each milestone
  compares its own set, named in the `What is measured` section of that
  milestone's `/app/SPEC/` file. The set always contains `width` and `height`;
  beyond those it is chosen for the resolved values the milestone's behaviour
  changes, so read your own milestone's list rather than carrying an earlier
  one forward.
* The remote end is run under four limits, and one that exceeds a limit fails
  the checks it did not answer rather than being told: its process is started
  under an address-space ceiling of **4,194,304 KiB** (4 GiB); it must be
  answering `GET /status` within **60 seconds** of being started; each request
  must be answered within **10 seconds**; and a run stops starting new
  documents **1500 seconds** after it begins. Documents not reached by then
  count as failed, so a correct but slow engine fails them too. The reference
  engine finishes well inside every one of them.

## 5. Fonts

Documents that contain text use the **Ahem** test font, supplied next to each
document as `Ahem.ttf` and referenced from the document's own `@font-face` rule.
Ahem is the CSS Working Group's metric-exact test font — every glyph is one em
wide, the ascent is 0.8em and the descent 0.2em, and U+200B has zero advance —
which is what makes text layout measurable without font rasterisation. It is
described at <https://web-platform-tests.org/writing-tests/ahem.html>.

## 6. Checking your own progress

`/app/css-corpus/` holds documents with the reference values recorded for them:

```
/app/css-corpus/documents/      the HTML documents, plus Ahem.ttf
/app/css-corpus/expected.json   reference rects and resolved values
/app/css-corpus/run.py          drives your remote end over them and reports
```

Run it with `python3 /app/css-corpus/run.py --binary /app/build/cssdriver`. It
uses the same protocol, the same tolerance and the same one-check-per-element
comparison described above. These documents are yours to work against. They are
representative: the engine is expected to lay out other documents drawn from the
same sources just as well. It covers this milestone and any earlier ones.

The corpus is divided into named **strata**, one per behaviour family.
`--list-strata` prints them with their sizes; `--only` restricts the run to one
stratum, to a comma-separated list, or to a prefix:

```
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --list-strata
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only css21-
python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only css21-floats-clearance --show-failures 20
```

Four things about reading its output:

* **The per-stratum lines are the signal; the TOTAL is not.** It sums strata of
  very different sizes.
* The strata are not all the same kind of thing. The `css21-` strata are the
  original broad recording and are filtered neither way: they are a regression
  suite over ordinary documents, not a target that is guaranteed reachable
  check by check. A **milestone stratum** — one named `s<N>-…`, for the
  milestone N that introduced it — is selected twice over: the engine of the
  milestone before it fails at least one check in that stratum, and a correct
  engine for the milestone itself passes **all** of them. So a milestone
  stratum below 1.0000 is work, and at 1.0000 is done.
* A stratum named `…-carryover` is a milestone stratum's opposite: documents the
  **previous** milestone's engine already laid out correctly. Those catch a
  regression, and they tell you nothing at all about whether this milestone's
  work is right.
* The runner **refuses to run** a corpus in which a recorded document belongs
  to no stratum, rather than quietly leaving it out of the counts it reports.

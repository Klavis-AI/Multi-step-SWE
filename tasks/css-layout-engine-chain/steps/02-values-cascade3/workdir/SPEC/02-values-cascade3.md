# `calc()` (CSS Values and Units Level 3)

## What is measured

Every document is laid out at 800x600 from a `file://` URL and compared
against Chrome for Testing 153.0.8010.36. For each measured element the check is
its **border box** -- `x`, `y`, `width`, `height` from `Get Element Rect` --
and the resolved values of the milestone's declared property set — `display`,
`width`, `height`, `margin-left`, `padding-left` and `font-size`. The comparison
is per element and per property: only the properties an expectation actually
records a value for are asked for and compared, so at this milestone every
recorded resolved value is `width` or `height`, and the rest of the declared set
is read back nowhere.

A rect is correct when each of the four numbers is within **0.5 CSS pixels** of
the reference. A resolved value that is a length is compared numerically to the
same tolerance; a keyword must match exactly.

## `calc()` — §8.1

A `calc()` reduces to a sum of a length and a percentage. The two parts cannot
be combined until the containing block is known, so:

* `width: calc(50% + 10px)` on a 400px containing block is 210px.
* `margin-left: calc(1vw + 2px)` at an 800px viewport is 10px.
* `height: calc(2rem - 4px)` with a 16px root font size is 28px.
* Percentages in `calc()` resolve against the same base a bare percentage would:
  the containing block's **width** for `margin-*` and `padding-*` on all four
  sides, its **height** for vertical positions, the element's own **font-size**
  for `line-height`, and the parent's font-size for `font-size`.

`*` requires at least one `<number>` operand; `/` requires a `<number>` on the
right and it must not be zero: `calc(10px * 2px)` and `calc(10px / 0)` are
invalid. Whitespace around `+` and `-` is required by the grammar, so
`calc(1px+2px)` is invalid too; around `*` and `/` it is optional. The grammar
and the whitespace rule are exercised by the documents, and no document depends
on what an *invalid* declaration leaves behind.

## What is NOT measured at this milestone

`min()`, `max()`, `clamp()`, `round()`, `mod()` and the trigonometric and
exponential functions are **Values Level 4** and are not part of this milestone.
Apart from `calc()`, no CSS function and no cascade feature beyond CSS 2.1 is
measured here.

## Fonts

Text uses **Ahem**, supplied as `Ahem.ttf` beside each document: one em advance
per glyph, 0.8em ascent, 0.2em descent, and U+200B with zero advance.

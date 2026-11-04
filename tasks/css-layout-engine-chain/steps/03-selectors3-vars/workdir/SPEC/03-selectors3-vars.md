# Custom properties and `var()`

## What is measured

Every document is laid out at 800x600 from a `file://` URL and compared
against Chrome for Testing 153.0.8010.36. For each measured element the check is
its **border box** — `x`, `y`, `width`, `height` — and the resolved values of
the milestone's declared property set — `display`, `width`, `height`,
`margin-left`, `padding-left`, `font-size`, `color`, `background-color`,
`border-top-width`, `border-left-width`, `line-height` and `letter-spacing`.
The comparison is per element and per property: only the properties an
expectation actually records a value for are asked for and compared. At this
milestone the fixtures record `width`, `height`, `margin-left`,
`border-top-width` and `border-left-width` — which of those five a given
element is asked for varies with the element — and the other seven properties in
the declared set are read back nowhere.

That measured list is larger than the previous milestone's, and deliberately so:
`var()` substitutes into a **shorthand**, so the places where an engine without
custom properties gives a different answer are the longhands a shorthand expands
to — the border widths and the margins — at least as often as they are `width`.
`Get Element CSS Value` must therefore answer for each of the five.

A rect is correct when each of the four numbers is within **0.5 CSS pixels** of
the reference. A resolved value that is a length is compared numerically to the
same tolerance; a keyword must match exactly.

## Custom properties — Variables 1 §2

A declaration whose property name begins with `--` is a custom property. Its
name is case-sensitive. It is retained on the element it is declared on and is
**inherited**, so a descendant resolves `var(--x)` against the nearest ancestor
that declared `--x`.

An engine written for CSS 2.1 will not tokenize `--x` as an identifier at all:
CSS Syntax Level 3 §4.3.9 makes two dashes a valid identifier start, and without
that the whole declaration is dropped before the cascade ever sees it.

## `var()` — Variables 1 §3

`var(--x)` is replaced by the custom property's value **before the declaration
is parsed**. That ordering is what makes `width: calc(var(--x) + 20px)` work:
substitution happens first, then the resulting text is parsed as a value.

`var(--x, <fallback>)` uses everything after the first comma as the fallback,
including further commas and balanced parentheses. When `--x` is not set and
there is no fallback, the declaration is invalid at computed-value time and the
property keeps the value it would otherwise have — usually its initial value, so
`width` becomes `auto`.

## What is NOT measured at this milestone

`min()`, `max()`, `clamp()` and the other CSS Values **Level 4** functions;
CSS Cascade Level 4's `revert`; Level 5's `@layer`; Selectors Level 4's `:is()`,
`:where()` and `:has()`. None of those is measured here.

## Fonts

Text uses **Ahem**, supplied as `Ahem.ttf` beside each document: one em advance
per glyph, 0.8em ascent, 0.2em descent, U+200B with zero advance.

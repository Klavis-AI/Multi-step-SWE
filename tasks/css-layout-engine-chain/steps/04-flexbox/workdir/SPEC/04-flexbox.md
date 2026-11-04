# Milestone: flex layout

This milestone measures CSS Flexible Box Layout Level 1
(<https://www.w3.org/TR/css-flexbox-1/>) at its **current** rules.

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

## What the documents exercise

The documents come from the Web Platform Tests `css/css-flexbox`
directory, laid out by Chrome 153.0.8010.36 at an 800x600 viewport, and are
grouped into strata by the behaviour each one turns on:

* **flex-basis, grow and shrink** — `flex-basis` as a length, a percentage,
  `auto` or `content` (which sizes the item from its contents, as `auto` does
  when the main size is `auto`); the `flex` shorthand including `flex: none`,
  `flex: auto`, `flex: initial` and a bare `<number>`; free-space distribution
  and the freeze/clamp/repeat loop of §9.7.
* **direction and wrap** — `flex-direction` in all four values, `flex-wrap`
  including `wrap-reverse`, and the `flex-flow` shorthand.
* **main-axis alignment** — `justify-content`, including `space-between`,
  `space-around` and `space-evenly`, and auto main-axis margins, which absorb
  free space before `justify-content` sees it (§8.1).
* **cross-axis alignment** — `align-items` and `align-self` in all their values
  including `baseline` (items are aligned on the first baseline of their first
  line box), with `stretch` applying only where the item's cross size is `auto`,
  and `align-content` across multiple lines.

## Things the milestone pins that are easy to miss

* A flex container establishes a **flex formatting context**. Floats do not
  intrude into it, its items' floats do not apply, and margins do not collapse
  through it (§3, §4).
* A flex item is **blockified**: `display: inline` on a child of a flex
  container behaves as `display: block` (§4).
* `flex-basis: 0%` is not `flex-basis: auto`. A bare `<number>` in the `flex`
  shorthand sets `flex-basis` to `0%`, which is why `flex: 1` and
  `flex-grow: 1` lay out differently (§7.1).
* `align-items: stretch` changes an item's cross size only when that size is
  `auto`; an item with an explicit cross size is aligned, not stretched (§8.3).
* `order` is a reordering, not a sort key: items with equal `order` keep
  document order (§5.4).

## Resolved values

`min-width` and `min-height` take `auto` as their initial value (§4.5). The
CSSOM resolved value of that `auto` is `0px` on anything that is not a flex
item, and the keyword `auto` on a flex item. Every expectation in this
milestone was recorded from Chrome 153.0.8010.36.

## Not this milestone

`aspect-ratio`, any layout mode other than block, inline, positioned and flex
layout, writing modes, and Selectors Level 4 are not measured here; documents
needing them are not included. The intrinsic sizing
keywords (`fit-content`, `min-content`, `max-content`) are not a focus of this
milestone, but a few documents use them as box sizes, and where an element's
geometry depends on one it is measured like any other.

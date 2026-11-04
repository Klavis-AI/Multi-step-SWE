# Milestone: grid layout

This milestone measures CSS Grid Layout Level 1
(<https://www.w3.org/TR/css-grid-1/>).

The documents come from the Web Platform Tests `css/css-grid` directory, laid
out by Chrome 153.0.8010.36 at an 800x600 viewport.

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

## Things the milestone pins that are easy to miss

* A grid container establishes an **independent formatting context**: floats do
  not intrude, and margins do not collapse through it (§3).
* A grid item is **blockified**, and its `float` does not apply (§6).
* `grid-column: 2` places the item in column 2 spanning **one** track — a
  missing end line is `auto`, not a copy of the start (§8.4).
* `fr` is a flex factor over the space left after fixed tracks and gaps are
  subtracted, not a fraction of the container (§7.2.3).
* `justify-items` / `align-items` default to `stretch`, and stretch changes an
  item's size only where that size is `auto`; an item with an explicit size is
  aligned, not stretched (§10.1).
* Implicit tracks are created for items placed outside the explicit grid, and
  they are sized as `auto` (§7.5).

## Resolved values

Every expectation in this milestone was recorded from Chrome 153.0.8010.36.

## Not this milestone

`subgrid` and container queries, writing modes, Selectors Level 4, and cascade
layers are not measured here. Documents needing them are not included.

Within grid itself, **baseline alignment** and **absolutely positioned grid
children** are not the focus of this milestone, and most documents do not turn on
them. A few documents do, and where an element's geometry depends on either, it
is measured like any other: an absolutely positioned grid child is
placed against its grid area, and an item aligned with the `baseline` value of
`align-items` / `align-self` is aligned on the first baseline of its first line
box. Implicit tracks are measured for their *sizing* (`grid-auto-rows`,
`grid-auto-columns`) as well.

A **replaced element** used as a grid item — a `<canvas>`, `<img>`, `<video>` or
`<iframe>` — carries an intrinsic size from its own attributes or content, and
that size is what an `auto` track and an `auto` cross size resolve against: a
`<canvas width="100" height="100">` in an `auto` row is 100px tall, not zero.
Two of the documents turn on this.

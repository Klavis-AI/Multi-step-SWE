# Rasterization, and linearized output

Normative text: **ISO 32000-1:2008**, published at `/opt/pdfspec/ISO-32000-1-2008.txt`.
Clause numbers below are that document's own. This milestone adds no new standard: every
construct here is defined in the document the first milestone already worked from. What is
new is that the constructs must now be **obeyed** rather than reported, and that one of
them must be **produced** rather than read.

The previous milestones' clauses still apply in full and this document replaces none of
them. `/app/SPEC/pdf14.md` matters most: it defines the transparency constructs — groups,
soft masks, blend modes — as a reader must parse them, and it defines the linearized file
form of Annex F as a reader must interpret it. Both of those definitions stand. This
document says what painting them means, and what writing the second one requires.

**Two halves, two kinds of requirement.** The rasterization half must agree with a
reference implementation's pixels, so its requirements are about *agreement*; the
linearization half must satisfy a checker's verdict on a file you wrote, so its
requirements are about *construction*. `/app/TEST_INTERFACE.md` §4.1 fixes how each is
compared, and §1.6 below is the part of this document you most need to read before
starting the first half.

---

## 1. The raster model

### 1.1 The device space and the pixel grid — 8.3.2.3

Rendering maps the page's default user space onto a device space of pixels. At a
resolution of *r* dots per inch, one unit of default user space — 1/72 inch — is *r*/72
device pixels. The page's `/MediaBox` gives the region painted: its width and height in
default user space, multiplied by *r*/72 and rounded, are the raster's width and height in
pixels. A `/CropBox` is **not** applied at this milestone; no invocation asks for
it, and no corpus page carries one that differs from its `/MediaBox`.

The device *y* axis runs downward while user space's runs upward, so the transformation
from default user space to device space flips it. `/Rotate` on the page dictionary rotates
the rendered result by the multiple of 90 degrees it names, exchanging the raster's width
and height for odd multiples.

Everything the content stream then does happens in user space as the earlier milestones
define it: the current transformation matrix, the graphics state stack, path construction,
clipping, and the text matrix are unchanged here, and this document assumes them.

### 1.2 Colour and the sample format

The raster holds three 8-bit components per pixel, red first, with no alpha channel: the
page is composited onto an opaque white backdrop before it is written. `/DeviceRGB`
components are the samples' values directly, and `/DeviceGray` replicates its single
component across all three.

**`/DeviceCMYK` is not converted by the formula of clause 10.3.5** (red = 1.0 − min(1.0,
cyan + black), and likewise for green and blue). The reference converts it by multilinear
interpolation between sixteen fixed colours, one for each corner of the CMYK unit
hypercube, and that conversion is the one required. Each operand of `k` or `K` is first
truncated to a multiple of 1/65536 (`(int)(v * 65536) / 65536.0`). A corner's weight is the
product, over the four components, of the component where the corner's entry below is 1
and of one minus the component where it is 0; each of red, green and blue is the sum of
the sixteen weights times that corner's value, and each sum is then quantised to a byte by
the rule below:

| C M Y K | red | green | blue |
| --- | --- | --- | --- |
| 0 0 0 0 | 1 | 1 | 1 |
| 0 0 0 1 | 0.1373 | 0.1216 | 0.1255 |
| 0 0 1 0 | 1 | 0.9490 | 0 |
| 0 0 1 1 | 0.1098 | 0.1020 | 0 |
| 0 1 0 0 | 0.9255 | 0 | 0.5490 |
| 0 1 0 1 | 0.1412 | 0 | 0 |
| 0 1 1 0 | 0.9294 | 0.1098 | 0.1412 |
| 0 1 1 1 | 0.1333 | 0 | 0 |
| 1 0 0 0 | 0 | 0.6784 | 0.9373 |
| 1 0 0 1 | 0 | 0.0588 | 0.1412 |
| 1 0 1 0 | 0 | 0.6510 | 0.3137 |
| 1 0 1 1 | 0 | 0.0745 | 0 |
| 1 1 0 0 | 0.1804 | 0.1922 | 0.5725 |
| 1 1 0 1 | 0 | 0 | 0.0078 |
| 1 1 1 0 | 0.2118 | 0.2119 | 0.2235 |
| 1 1 1 1 | 0 | 0 | 0 |

So `0 0.9 0.2 0 k` paints (238, 30, 128), where clause 10.3.5 followed by the quantisation
below gives (255, 25, 204). Measured: over a page of 400 CMYK fills, including every CMYK
colour the corpus uses, this table reproduces the reference's bytes on all 400;
10.3.5's formula reproduces 3, and the table without the operand truncation 398.

`/CalRGB`, `/CalGray`, `/Lab`, ICC-based, `/Indexed`,
`/Separation` and `/DeviceN` spaces are converted as clauses 8.6.5 and 8.6.6 define,
through their tint transforms and alternate spaces where they have them.

The page begins white. A region no operator paints stays white, which is why a page that
renders blank is indistinguishable from a page that failed to render. Three corpus pages do
render blank — the current transformation matrix carries their only fill off the page, so a
renderer that applies it correctly paints nothing there.

**The final step — a component to a byte — is fixed here, and it is not `round(v * 255)`.**
Clause 8.6.4 defines the colour transform but says nothing about the quantisation that
follows it, and the compared value is the byte, so the rule is stated rather than left to be
inferred. A component `v` in [0,1] becomes a sample by a 16.16 fixed-point quantisation:

```
c    = (int)(v * 65536)          clamped to [0, 65535]
byte = ((c << 8) - c + 0x8000) >> 16
```

Because `c` truncates before the rounding term is added, a value lying exactly half way
between two bytes resolves **downward**: of the 255 values `(k + 0.5) / 255`, 254 map to
`k`. The single exception is `v = 0.5`, which maps to 128, because `0.5 * 65536` is exactly
representable in binary while the others land a hair below it. So `0.9` is 229 and `0.7` is
178, where a naive `round(v * 255)` would give 230 and 179.

This mapping governs fill colours, shading output and soft-mask luminosity alike. It was
measured, not derived: over an axial `/ShadingType 2` ramp it reproduces the reference
renderer on **829 of 829** interior pixels, where `round(v * 255)` reproduces 828 and misses
by one at the sample nearest a half-way point.

### 1.3 Antialiasing

Coverage is computed per pixel and used to blend the painted colour with what is beneath
it. Two independent settings exist and both are on for every render invocation: one for
glyphs and one for vector artwork — paths, fills and strokes. They are independent, and
they were measured to be: turning vector antialiasing off changed a page of shadings and
left a page of text alone.

**A filled axis-aligned rectangle is snapped, not antialiased, and every page of
the `raster`, `composite` and `damaged` groups turns on this rule.** The reference moves
each of the rectangle's four edges, in device space after the current transformation
matrix and the page's `/Rotate` have been applied, to the **nearest** pixel boundary — an
edge at device coordinate *x* becomes `floor(x + 0.5)` — and paints every pixel between
the moved edges at full coverage. Equivalently, a pixel is painted exactly when its centre
lies inside the rectangle. A rectangle spanning device *x* 10.3 to 20.7 therefore paints
columns 10 to 20, and one spanning 10.7 to 20.3 paints columns 11 to 19: an edge that
crosses into a pixel without reaching its centre leaves that pixel untouched, so painting
every pixel the rectangle touches (`floor` of the near edge to `ceil` of the far one) is
wrong wherever an edge lies past a centre. A snapped fill has no intermediate edge samples
at all. Every edge on a corpus page lies at least 0.2 device px from a pixel centre, so
which boundary is nearest is never in doubt. Measured: a page of 100 rectangles whose
edges fall at 0.05, 0.2, 0.3, 0.45, 0.49, 0.51, 0.55, 0.7, 0.8 and 0.95 of a pixel
rendered in exactly two colours, every painted pixel's centre inside a rectangle and no
other pixel painted; and on every page of the `raster`, `composite` and `damaged`
groups, pixels covered by the same set of rectangles under this rule carry one colour.

**An axis-aligned edge is necessary for a snapped result, but it is not sufficient**, and
two constructs are therefore outside the raster groups. A **clip path is not snapped
the way a fill is**: its columns antialias at quarter-pixel granularity while its rows hard
snap, so even a perfectly axis-aligned `W n` yields three or more distinct sample values. And
a **rectangle of zero extent** — a `re` with a width or height of 0 — still marks the device,
as an antialiased one-pixel line rather than nothing at all. A page in the raster,
shading or composite groups is built from fills alone, and carries neither construct.

Antialiasing is where two rasterizers most easily disagree while both being defensible,
which is precisely why `/app/TEST_INTERFACE.md` §2.1 freezes the settings and compares
exactly. The requirement is not "antialias sensibly"; it is "produce the reference's
pixels".

### 1.4 Thin lines — 8.4.3.2

A stroke whose width transforms to less than one device pixel is the case clause 8.4.3.2
addresses: a zero-width line "shall be rendered as a thin line", the thinnest the device
can render, and a device may not render it as invisible. The reference offers three
treatments of such strokes — leave them to ordinary coverage, force them solid, or adjust
the stroke's shape — and they were measured to produce **three different rasters** on
strokes of 0, 0.05, 0.1 and 0.25 pt. The render invocation always selects the first.

The same measurement is a warning about method. At 150 dpi a 0.5 pt stroke is 1.04 device
pixels — wide enough that none of the three treatments applies — and against a page whose
thinnest stroke was 0.5 pt all three produced identical output, which would have been
recorded as "this setting does nothing" by anyone who stopped there. It does something; the
input had not reached it.

### 1.5 Images — 8.9

An image XObject is painted into the unit square of user space, transformed by the current
matrix. Sample decoding, `/Decode` arrays, `/ImageMask` stencils and the filters that
carry the data are the earlier milestones' work and are unchanged. What is new is
resampling: the mapping from image samples to device pixels when the two grids do not
coincide. **No page in this milestone's corpus contains an image**, so resampling
is described here for completeness and is not exercised -- it may be implemented or
omitted, exactly as with the shading types outside 2.

### 1.6 What "matching" actually demands

This is the sentence to read twice. **Two correct rasterizers do not agree pixel for
pixel.** Sampling positions, coverage computation, rounding of geometry to the pixel grid,
glyph hinting and the order of compositing are all choices a standard-conforming
implementation may make differently. Nothing in ISO 32000-1 fixes them.

What makes exact matching possible is not that the pixels are canonical — they are not —
but that **one** implementation's choices have been frozen, published as the reference,
and measured to be reproducible. You are asked to match those choices. Where this document
describes a behaviour that the standard leaves open, it is describing the reference's
behaviour and saying so, and that is the behaviour to implement.

---

## 2. Shadings — 8.7.4.5

A shading paints a colour that varies across an area, with no explicit colour operator per
sample. It is reached two ways, and both are exercised: the `sh` operator, which paints
the shading's own geometry across the current clip, and a shading pattern set as the
current colour and used by an ordinary fill. The shading dictionary's `/ColorSpace`
governs the values its function or its data produces; `/Background` and `/BBox` apply where
present; `/AntiAlias` is a hint the reference does not treat as a requirement.

**Only type 2 is required** -- the continuous axial ramp. All seven
are described below because the shading dictionary is one interface and a reader has to
recognise every type it may meet, but the corpus contains axial ramps
only, bounded by `/Extend` rather than by a clip. Two exclusions were measured rather
than assumed: a type-3 STITCHING function across a band boundary makes the reference
interpolate where this description implies a step (one fixture differed in exactly one
column, carrying the exact mean of the two band colours), and a shading bounded by a
clip paints a one-pixel ring at alpha 32/255 that does not track the geometry.
**No corpus page contains a type-3 shading.** Types 1 and 3-7 may be implemented or
omitted.

* **Type 1, function-based — 8.7.4.5.2.** The function maps a point (*x*, *y*) in the
  shading's own space to a colour. `/Domain` bounds the parameter rectangle, and `/Matrix`
  maps that rectangle into the target space. Points outside the domain are not painted.
* **Type 2, axial — 8.7.4.5.3.** Colour varies along the axis between the two points of
  `/Coords`, parameterized by `/Domain`, constant along lines perpendicular to it.
  `/Extend` continues the terminal colours beyond each endpoint.
* **Type 3, radial — 8.7.4.5.4.** Colour varies along a family of circles interpolating
  between the two circles `/Coords` gives as centre and radius. The rendering order —
  later circles painted over earlier — is part of the appearance where the circles
  overlap, and `/Extend` continues the family outward in either direction.
* **Type 4, free-form Gouraud triangle mesh — 8.7.4.5.5.** Vertex data is packed in a
  stream as a bit field sequence: an edge flag, coordinates, and colour components, with
  the widths `/BitsPerFlag`, `/BitsPerCoordinate` and `/BitsPerComponent` give and the
  ranges `/Decode` maps them into. The edge flag decides whether a vertex starts a new
  triangle or extends the previous one. Colour is interpolated linearly across each
  triangle from its three vertices.
* **Type 5, lattice-form Gouraud triangle mesh — 8.7.4.5.6.** The same interpolation with
  the topology implied instead of flagged: vertices arrive in rows of `/VerticesPerRow`,
  and consecutive rows form the triangles.
* **Type 6, Coons patch mesh — 8.7.4.5.7.** Each patch is bounded by four cubic Bézier
  curves with twelve control points, with a colour at each of the four corners. The
  surface interior is the Coons patch those boundaries define, with colour interpolated
  bilinearly in the patch's parametric space. The edge flag lets a patch share an edge and
  two corner colours with its predecessor, in which case only the remaining control points
  and colours appear.
* **Type 7, tensor-product patch mesh — 8.7.4.5.8.** As type 6 with four additional
  internal control points, sixteen in all, giving control of the interior surface that the
  Coons form derives implicitly.

For types 4 to 7 the geometry is a bit stream, not a byte stream: fields are packed
without regard to byte boundaries, each vertex or patch beginning where the previous one
ended, except that each new record begins on a byte boundary where the standard says so. A
misread of one field width shifts everything after it, which shows up as a page of noise
rather than as a subtle error — a useful property when debugging.

---

## 3. Transparency — clause 11

`/app/SPEC/pdf14.md` defines these dictionaries and how to read them. This clause is about
what they mean on the page.

### 3.1 The compositing formula — 11.3.6, 11.4.4

A source colour is composited with the backdrop under three controls: the **constant
alpha** of the graphics state, the **blend mode**, and the **soft mask**. For an opaque
backdrop the result is the union formula of 11.3.6 reduced to an interpolation between the
backdrop colour and the blended colour, weighted by the effective alpha — the constant
alpha multiplied by the mask value at that point and by the shape of the object being
painted. Painting is otherwise in the order the content stream gives.

Stroking and filling have separate constant alphas — `/CA` and `/ca` in the extended
graphics state — and the operator decides which applies.

**Constant alpha does NOT use §1.2's quantiser, and this is the one place the renderer is
internally inconsistent.** `/ca` and `/CA` become an 8-bit alpha by rounding half **up**:

```
A = floor(255 * a + 0.5)
```

So `/ca 0.1` is alpha **26** where a fill colour of 0.1 is byte **25** — the same number,
the same page, two different rules. It was measured directly rather than inferred: painting
an opaque white backdrop and a black source at the alpha under test makes the composite
`(0*A + 255*(255-A))/255 = 255 - A`, so the observed byte yields `A` with no second rounding
in the way. Six values where the two rules disagree — 0.1, 0.3, 0.7, 0.9, 1/255 and 2.5/255 —
all follow round-half-up; nine more where they agree all match. `a = 0.5` is not
discriminating, and for the same reason it is §1.2's lone exception: `0.5 * 65536` is exactly
representable.

### 3.2 Blend modes — 11.3.5

The blend function combines the backdrop and source colours before the alpha
interpolation. The **separable** modes apply a scalar function to each colour component
independently: `Normal`, `Multiply`, `Screen`, `Overlay`, `Darken`, `Lighten`,
`ColorDodge`, `ColorBurn`, `HardLight`, `SoftLight`, `Difference` and `Exclusion`, each
defined by the formulas of table 136. The **non-separable** modes — `Hue`, `Saturation`,
`Color` and `Luminosity`, table 137 — are defined through the luminosity and saturation
functions of 11.3.5.3 and cannot be computed component by component. A mode name the file
gives that is not one of the sixteen is treated as `Normal`.

Four of these were measured rendering in this environment — `Multiply`, `Screen`,
`Difference` and `HardLight` — each producing a visibly distinct composite where two
objects overlapped, so none was silently ignored. The other twelve are required by the
standard and are stated here as the standard states them.

**The arithmetic is integer, and where it truncates is part of the answer.** Table 136 is
written over components in [0,1]; the renderer evaluates it over the **8-bit values**, with
a **truncating** division by 255 rather than a rounding one, and the alpha interpolation
that follows truncates as well:

```
out = (blend * a + backdrop * (255 - a)) / 255          integer division, truncating
```

So `Multiply` of a `0.8` source over a `0.3` backdrop is not `0.24 → 61`. The components
quantise to 204 and 76 by §1.2's rule, and `204 * 76 / 255 = 60.8` truncates to **60**
(`0x3c`) — which is the byte the reference renderer actually produces, measured on a
two-rectangle overlap before any of this was written down. An implementation that evaluates table
136 in floating point and rounds at the end is off by one on most overlaps, and every
composite case turns on exactly that byte.

One threshold is likewise exact rather than approximate: `HardLight` and `Overlay` select
their branch on `< 128`, strictly, so a component of exactly 128 takes the **high** branch.

All of the above describes compositing against an **opaque** backdrop, which is what every
page in these groups has. Where the backdrop is transparent the blend function is not
applied at all: 11.3.6 weights it by backdrop alpha, so over nothing the source colour is
delivered unblended. A source that extends beyond the object it is meant to blend with is
therefore measuring the wrong thing, and no corpus page does it.

### 3.3 Soft masks — 11.6.5

A soft mask supplies a per-point alpha from a transparency group rendered separately from
the page. `/SMask` in the extended graphics state names a dictionary whose `/G` is that
group and whose `/S` selects how the group becomes a mask:

* `/Luminosity` — the group is composited onto a fully opaque backdrop of its `/BC`
  colour, or black where `/BC` is absent, and the **luminosity** of the result is the mask
  value;
* `/Alpha` — the group is composited on a fully transparent backdrop and its accumulated
  **alpha** is the mask value, its colour discarded.

`/TR`, where present, is a transfer function applied to the mask value afterwards. The
mask is evaluated in the coordinate system in force when the extended graphics state
dictionary was set, not when the painting happens. Setting `/SMask` to `/None` removes it.

A luminosity mask over a gradient was measured producing a smooth progression across the
masked region rather than a uniform tint, which is the observable difference between a
mask that is applied and one that is parsed and dropped.

### 3.4 Transparency groups — 11.6.6

A form XObject with a `/Group` whose `/S` is `/Transparency` is composited as a unit: its
contents are rendered into their own buffer and the result composited with the backdrop
under the group's own alpha, blend mode and mask. `/I` makes the group isolated — its
contents see a transparent initial backdrop rather than the page's — and `/K` makes it
knockout, where each object composites against the group's initial backdrop rather than
against the objects painted before it. `/CS` gives the group's own colour space.

---

## 4. Why this is reproducible

Byte-exact comparison is only legitimate where the pipeline records nothing that varies,
and here it records nothing at all. The image file the reference writes contains a header,
a physical-dimensions chunk, the compressed image data and an end marker — and no
timestamp chunk, no producer string and no comment. There is nothing in it whose value
depends on when or where it was written.

That is the structural reason. The empirical check agreed with it across every axis varied:
repeated renders in one container; separate container invocations; a different mount point
and a different hostname; ten environment settings including the locale, the time zone,
the home directory, the cache directory and a fixed build epoch; one CPU against eight; a
host under heavy load from unrelated work; text, all seven shadings and the transparency
features; two pages of a 756-page production document; and 300 pages rendered once in a
batch and again as 300 separate parallel invocations, every one of the 300 identical.

The obligation this places on you is narrow and absolute: given the frozen command line,
the same input must produce the same output bytes every time, and those bytes must be the
reference's. Do not seed anything from the clock, the process, the environment or the
address space. Nothing in this milestone needs a random number.

---

## 5. Linearized output — Annex F

### 5.1 What is already defined, and what is new

`/app/SPEC/pdf14.md` §5 defines the linearized form for a reader: the eleven parts, the
parameter dictionary and its entries, the rule that `/L` must equal the file's real length
or the file is not linearized at all, the two cross-reference sections, and the hint
tables as a bit stream. **All of that still applies and is not restated here.** This
section is about producing such a file, which is a different problem: a reader may ignore
what it does not understand, and a writer may not.

### 5.2 What the writer must arrange — F.2, F.3

* **The first-page objects come first.** The objects needed to display the first page — the
  page object, its content streams, its resources, and the document-level objects a viewer
  needs before any page — are placed at the front of the file, so that a reader holding
  only the first part of the file can show the first page.
* **The parameter dictionary is the first object in the body**, with every value direct,
  nothing in the document referring to it, and the whole dictionary inside the first 1024
  bytes. Its `/L` is the total file length, `/O` the object number of the first page's
  page object, `/E` the offset of the end of the first page's objects, `/N` the number of
  pages, `/T` the offset of the first entry of the main cross-reference table, and `/H` the
  offset and length of the primary hint stream.
* **Two cross-reference sections, and their order.** The first-page cross-reference table
  and its trailer appear near the front, as a valid table not at the end of the file, in a
  single subsection with no free entries. The main cross-reference table appears at the
  end. All generation numbers are 0.
* **Every one of those values is a byte offset into the file being written**, and several
  of them are offsets to positions that are not known until the file is complete. The
  circularity — the length of the parameter dictionary changes the offsets it records — is
  the central difficulty of writing this form, and it is resolved by padding the numbers to
  a fixed width or by iterating to a fixed point.

### 5.3 The hint tables — F.3.6, F.4

The primary hint stream carries the page offset hint table followed by the shared object
hint table. F.4 treats the stream as a bit stream, high-order bit first, divided into
fields of arbitrary width without regard to byte boundaries, each table beginning on a byte
boundary. The page offset table records, for each page, the number of objects, the page's
length, and which shared objects it uses, all as deltas from per-table minima with
per-table bit widths. Offsets within the hint tables are interpreted as if the primary hint
stream were not present: a position greater than the hint stream's offset has the hint
stream's length added to it to give the real offset.

**This is where linearizers fail.** The parameter dictionary is arithmetic; the hint tables
are arithmetic in a packed bit stream whose field widths you chose, describing a file
whose layout you also chose. An inconsistency here does not corrupt the document — every
reader that ignores hint tables will open the file happily — which is exactly why it must
be checked by something that does not ignore them.

### 5.4 What the checker will and will not accept

The reference is a real implementation's linearization checker, and the bar it sets is *its*
notion of a correct hint table rather than "some reader accepts this". That is worth
stating precisely, because two widely used tools do not clear it: one produced shared
object lengths that contradicted its own hint table, and another a bit stream that overran
while being read; both were rejected. Neither of those files is unusable — readers open
them — and both were rejected all the same.

So the target is not "a file that works". It is a file whose recorded structure is
arithmetically consistent with its actual structure, checked field by field. A file that is
merely *not linearized* is a clean, legitimate answer to a request that does not ask for
linearization — and, as `/app/TEST_INTERFACE.md` §4.1 records, it is reported with a
success status, which is why the report rather than the status is what gets compared.

---

## 6. What the utilities must report

The interfaces, their option spellings and the comparison rules are in
`/app/TEST_INTERFACE.md`.

* `pdftoppm` under the frozen command line of §2.1 there — for each page in the requested
  range, one image file holding the page painted under §1 to §3 of this document, in the
  page's own pixel dimensions. One file per page requested, and none missing.
* `qpdf --linearize IN OUT` — at `OUT`, a file conforming to Annex F under §5, containing
  the same document as `IN`; and status 0.
* `qpdf --check-linearization FILE` — whether the file carries linearization data and
  whether that data agrees with the file, in the form the qpdf manual describes and with
  the status that goes with it. A file with no linearization data is reported as not
  linearized, and that is not an error.
* The earlier milestones' utilities — unchanged. A page carrying shadings, blend modes or
  a soft mask is an ordinary page to all of them, and a linearized file is an ordinary file
  to all of them; every report of theirs over such a file is what its own milestone fixed.

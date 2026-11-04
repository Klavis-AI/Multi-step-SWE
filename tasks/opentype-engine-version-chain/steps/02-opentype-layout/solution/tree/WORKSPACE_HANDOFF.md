# Workspace handoff

This engine is a from-scratch JavaScript port of the reference tools, laid out so
that the tree root becomes `/app` and each CLI lives at `/app/bin/<tool>.js`.

- Modules are ES modules; `package.json` declares `"type": "module"`.
- Every module is self-contained: no dependencies outside the Node standard
  library, and nothing is resolved outside the workspace root.
- `bin/ftlint.js` mirrors freetype-demos' `ftlint`; `bin/hb-shape.js`,
  `bin/hb-vector.js` and `bin/hb-subset.js` mirror the HarfBuzz utilities of the
  same name. Their option sets, defaults and output syntax are the tools' own.
- Numeric paths use the same fixed-point conventions as the C originals
  (16.16 and 26.6), because output is compared byte-for-byte.

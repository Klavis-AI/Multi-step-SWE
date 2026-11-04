# Documentation provenance and licence

The `*.html` files in this directory (including `c3ref/`) are copied **byte for
byte** from the official SQLite documentation archive for the release this step
targets:

* archive: `sqlite-doc-<release id>.zip`, published at <https://sqlite.org/>
* release: SQLite 3.8.0, 2013-08-26
* `SQLITE_SOURCE_ID`: `f64cd21e2e23ed7cff48f7dafa5e76adde9321c2`

`copyright.html` in this directory is upstream's own statement: all SQLite code
and documentation is **dedicated to the public domain**. No attribution is
legally required; this notice records provenance so the origin of every
normative sentence is traceable.

`SHA256SUMS` covers every copied page. Verify with:

```sh
cd /app/sqlite-doc && sha256sum -c SHA256SUMS
```

`README.md`, `PROFILE.md` and this file are written for this benchmark task and
are **not** upstream documents. Where they disagree with a copied page about
SQL semantics, the copied page wins; where they restrict scope, the restriction
holds.

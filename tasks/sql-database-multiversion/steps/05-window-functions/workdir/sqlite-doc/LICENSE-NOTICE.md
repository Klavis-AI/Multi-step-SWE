# Documentation provenance and licence

The `*.html` files in this directory (including `c3ref/`) are copied **byte for
byte** from the official SQLite documentation archive for the release this step
targets:

* archive: `sqlite-doc-<release id>.zip`, published at <https://sqlite.org/>
* release: SQLite 3.25.0, 2018-09-15
* `SQLITE_SOURCE_ID`: `b63af6c3bd33152742648d5d2e8dc5d5fcbcdd27df409272b6aea00a6f761760`

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

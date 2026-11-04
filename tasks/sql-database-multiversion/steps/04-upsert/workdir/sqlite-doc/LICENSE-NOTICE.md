# Documentation provenance and licence

The `*.html` files in this directory (including `c3ref/`) are copied **byte for
byte** from the official SQLite documentation archive for the release this step
targets:

* archive: `sqlite-doc-<release id>.zip`, published at <https://sqlite.org/>
* release: SQLite 3.24.0, 2018-06-04
* `SQLITE_SOURCE_ID`: `c7ee0833225bfd8c5ec2f9bf62b97c4e04d03bd9566366d5221ac8fb199a87ca`

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

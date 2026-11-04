# fhirconf — local conformance runner for this milestone

`run_conformance.py` drives the DSTU2 and STU3 service bases of a running server
and compares the answers with ones recorded from a reference FHIR server at each
of those versions.

```sh
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --only dstu2 --verbose
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --validate
```

* `dstu2-cases.json` / `stu3-cases.json` and their `-expected.json` — request
  cases and the answers a reference server at that version gave. A case whose URL
  or header contains `@VID:<case>` or `@ETAG:<case>` takes the version id or
  ETag the named earlier case returned, so nothing depends on how this server
  numbers versions.
* `valcases-<version>/` and `valexpected-<version>.json` — instances with the
  verdict the official HL7 FHIR validator gave at that version; the runner posts
  each to `[base]/[Type]/$validate`.
* `valdiff-<version>/` and `valdiffexpected-<version>.json` — the same, for
  instances built from elements the two releases actually disagree about. Each
  one is valid at one version and invalid at the other, so they can only be
  answered with the right version's definitions.
* The datasets in `/app/testdata/seed-dstu2` and `/app/testdata/seed-stu3` are
  written before the request cases, in an order where a resource is written after
  everything it references.

These are a small sample. What this milestone requires also includes the
earlier milestone's R4 behaviour, the official FHIR Mapping Language test cases
driven through `StructureMap/$transform`, and the server's own answers, read
back, passing the official HL7 validator at each base's own FHIR version, so a
clean local run is necessary but not sufficient.

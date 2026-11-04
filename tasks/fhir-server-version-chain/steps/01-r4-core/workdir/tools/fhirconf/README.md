# fhirconf — local conformance runner

`run_conformance.py` drives a running FHIR service base through the FHIR RESTful
API and compares the answers with recorded ones.

```sh
# start the server first, then:
python3 /app/tools/fhirconf/run_conformance.py --base http://127.0.0.1:8080/baseR4
python3 /app/tools/fhirconf/run_conformance.py --base http://127.0.0.1:8080/baseR4 --verbose --only search-date
python3 /app/tools/fhirconf/run_conformance.py --base http://127.0.0.1:8080/baseR4 --validate --verbose
```

* `rest-cases.json` / `rest-expected.json` — request cases and the answers a
  reference FHIR server gave for them. A case whose URL or header contains
  `@VID:<case>` or `@ETAG:<case>` takes the version id or ETag the named earlier
  case returned, so nothing here depends on how this server numbers versions. Volatile fields (`meta.lastUpdated`,
  `Bundle.id`, paging link text, `OperationOutcome` wording, the base URL) are
  normalised away by `lib/fhirhttp.py`, and only the fields each case lists in
  its `compare` key are compared.
* `valcases/` / `valexpected.json` — resource instances with the verdict the
  official HL7 FHIR validator gave for them; the runner posts each one to
  `[base]/[Type]/$validate` and checks that the server agrees on whether the
  instance has an error, and that it names the element the error is on.
* The sample dataset in `/app/testdata/seed` is written before the request cases
  run, in an order where a resource is written after everything it references:
  a server that resolves references while indexing them cannot index a reference
  whose target does not exist yet.

The dataset, the request cases and the validation cases here are a small sample
of the same kind, so a clean local run is necessary but not sufficient.

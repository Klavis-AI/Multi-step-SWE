# fhirconf — local conformance runner for this milestone

```sh
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --validate
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --profiles
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --terminology
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --profiles --verbose
```

* `<version>-cases.json` / `-expected.json` — request cases and the answers a
  reference FHIR server at that version gave.
* `valcases-<version>/`, `valdiff-<version>/` and their `*expected*.json` —
  instances with the verdict the official HL7 validator gave at that version;
  the `valdiff` ones are built from elements the releases disagree about.
* `profcases-<ig>/` and `profexpected-<ig>.json` — instances with the verdict
  the official validator gave **with and without** `-profile`. Only the cases
  whose verdict differs between the two are here, so a case a server could
  answer without reading the profile is not among them.
* `term-corpus.json` / `term-expected.json` — terminology operations with the
  answer the published `CodeSystem`, `ValueSet` and `ConceptMap` resources
  give. The expected expansion of a value set is the one its publisher shipped.

These are a small sample. The requirements of the earlier milestones still
apply, and the server's own answers, read back, must pass the official HL7
validator at each base's own FHIR version, so a clean local run is necessary
but not sufficient.

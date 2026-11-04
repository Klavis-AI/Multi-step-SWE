# Workspace handoff

Keep this file current. A later session starts from this workspace with no memory
of how it got here, so record what exists and how to build and run it.

## What is here at the start

| Path | What it is |
| --- | --- |
| `/app/SPEC/` | the HL7 FHIR specification pages this milestone is defined by, with `SPEC/PROVENANCE.md` recording where each page came from |
| `/app/testdata/seed/` | the sample dataset (official FHIR example instances) |
| `/app/tools/fhirconf/` | the local conformance runner; see its `README.md` |
| `/root/.fhir/packages/` | the published FHIR definition packages |

There is no `/app/src` yet — writing it, with the makefile inside it, is the work.

## What has to exist when you are finished

```sh
make -C /app/src                   # builds /app/bin/blaze from the C sources
SERVER_PORT=8080 BASE_URL=http://127.0.0.1:8080 \
  INDEX_DB_DIR=/tmp/d/index TRANSACTION_DB_DIR=/tmp/d/tx RESOURCE_DB_DIR=/tmp/d/res \
  /app/bin/blaze &
python3 /app/tools/fhirconf/run_conformance.py --base http://127.0.0.1:8080/baseR4
python3 /app/tools/fhirconf/run_conformance.py --base http://127.0.0.1:8080/baseR4 --validate
```

## Notes for the next session

(Replace this section: what is implemented, what is not, where the sharp edges are.)

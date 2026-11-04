# Milestone 6 — the R6 ballot, and version-negotiated responses

## 1. Implement this milestone

The server in `/app` serves DSTU2, STU3, R4, R4B and R5, each at its own service
base. Add the release that is still in ballot — **R6 (`6.0.0-ballot3`)** — and
the other way a FHIR client asks for a version: the **`fhirVersion` parameter on
the media type**.

### 1.1 How it is built, started and configured — unchanged

* Everything the build needs lives under **`/app/src`**. `make -C /app/src`,
  with no target, produces **`/app/bin/blaze`**; object files go under
  `/app/build`. The project must build with exactly that command in a clean
  container that holds only `/app/src`.
* The tree carries **C sources only**. A source file in another compiled
  language is not allowed. Every compiled file is deleted before the build,
  the process that owns the listening socket must be the ELF that
  build produced, and no JVM or other language runtime may appear in its process
  tree: do not wrap, embed, exec or proxy HAPI FHIR, Blaze, fhir-candle, Firely
  or any other existing FHIR server, and do not call the HL7 validator.
  This must hold from the moment the server comes up until it is stopped:
  something must be listening, and the listener must be that ELF, the whole
  time, so a server that stops partway through does not meet it.
* Configuration is still only these five environment variables, with Blaze's
  documented meanings: `SERVER_PORT`, `BASE_URL`, `INDEX_DB_DIR`,
  `TRANSACTION_DB_DIR`, `RESOURCE_DB_DIR`. No others are set, and the FHIR
  package cache is read from its standard location.
* The server runs under an **address-space limit of 8 GiB** and must answer
  every request within **120 seconds**; a build must finish within 15 minutes under the same address-space limit.
* Write the files under `/app/src` in **ASCII**; the copy of `/app/src` that the
  clean build starts from is made over a channel that is not byte-transparent.

### 1.2 `/baseR6`

A sixth service base, `**/baseR6**`, serving `6.0.0-ballot3` from
`hl7.fhir.r6.core`, with everything the earlier bases do: CRUD, `_history`, the
whole search grammar with R6's own `SearchParameter` definitions, `_include` and
`_revinclude`, `_sort`, `_summary`, `_elements`, transactions and batches,
`$validate` against R6's `StructureDefinition`s, JSON and XML, and a
`CapabilityStatement` whose `fhirVersion` is **`6.0.0-ballot3`** — the package's
own version string, not `6.0.0`. A failed transaction, and a Bundle posted to a base that is neither a
`transaction` nor a `batch`, are answered `400` with an `OperationOutcome`.

R6 is a ballot, and it is the largest set of changes since R4: resources added
and removed, elements renamed and retyped, and `SearchParameter`s that do not
exist in any earlier release. As before, the definitions are the specification:
a base that reads its own release's package gets this for free, and one that
reuses R5's answers R5's questions at an R6 endpoint.

### 1.3 Version negotiation on the media type

`versions.html` gives three ways a FHIR version is determined, and this milestone
implements the second: **the `fhirVersion` parameter on the MIME type**.

* A request whose `Accept` is `application/fhir+json; fhirVersion=4.0` is asking
  for the R4 representation, whatever base it arrives at. The response's
  `Content-Type` **echoes the parameter it answered with**.
* `Content-Type: application/fhir+json; fhirVersion=5.0` on a `POST` or `PUT`
  says the **body** is in that release's representation.
* The parameter's value is the release's `major.minor` — `1.0`, `3.0`, `4.0`,
  `4.3`, `5.0`, `6.0` — not the full version string. A value that names no
  release this server holds is **`406`** for `Accept` and **`415`** for
  `Content-Type`, in both cases with an `OperationOutcome`.
* When the parameter names a release **other than the base's own**, the server
  converts: the resource is read at the base it is stored in and rendered in the
  requested release's representation. That representation is decided by the
  requested release's own `StructureDefinition`s and by nothing else — an element
  that release does not define is not part of it, and the elements it does define
  keep their values. A resource whose type the requested release does not define
  has no representation there at all, and is `422` with an `OperationOutcome`,
  not a silent partial answer.
* `Accept` may carry several media types with q-values; the one the server picks
  must be the highest-weighted acceptable one, and `fhirVersion` is part of what
  makes a media type acceptable.
* `_format` still works and still loses to nothing: a `_format` query parameter
  overrides `Accept` for the format, and the `fhirVersion` parameter of the
  chosen `Accept` entry still selects the release.

`CapabilityStatement.format` lists the media types the base answers with; a
media type it lists must be one it can actually produce, and the `fhirVersion`
parameter is not part of `format` — it is negotiated per request.

### 1.4 What the earlier milestones still have to do

Every requirement of the earlier milestones must still hold: DSTU2, STU3, R4B,
R5, Bulk Data export, SMART scopes, `$everything`, topic-based subscriptions,
profile validation, terminology operations, and the server's own output being
valid at each base's release. That includes the R4 endpoint at `/baseR4`: its
own output, what its `CapabilityStatement` declares, `$validate` on instances
the releases disagree about, profile validation, the terminology operations,
and answers computed from the data loaded into it.

## 2. Read these local references

**Normative semantics.** `/app/SPEC/R6/` holds R6's own `http.html`,
`search.html`, `resourcelist.html`, `bundle.html`, `capabilitystatement.html`,
`json.html`, `xml.html` and `versions.html`, beside the pages the earlier
milestones use. `versions.html` is the one that defines the media-type
parameter.

**Machine-readable definitions.** This milestone's package is in
`/app/packages/` as a tarball; install it before you start:

```sh
sh /app/packages/install.sh          # unpacks into /root/.fhir/packages/<name>#<version>
```

It is `hl7.fhir.r6.core#6.0.0-ballot3`, beside the packages already installed.
The same packages are installed at the same path wherever the server runs.

**Sample data and local checks.** `/app/testdata/seed-r6/` holds R6's published
example instances. `/app/tools/fhirconf/` holds this milestone's runner; its
`README.md` says how to use it.

### Checking progress

```sh
sh /app/packages/install.sh
make -C /app/src
SERVER_PORT=8080 BASE_URL=http://127.0.0.1:8080 \
  INDEX_DB_DIR=/tmp/d/index TRANSACTION_DB_DIR=/tmp/d/tx RESOURCE_DB_DIR=/tmp/d/res \
  /app/bin/blaze &
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --only r6
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --negotiate
```

The runner compares with answers taken from a reference FHIR server at R6 and
from the official HL7 validator. The local cases are representative, not
exhaustive, and the requirements of every earlier milestone still apply, so a
clean local run is necessary but not sufficient.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue in the cumulative `/app` workspace. Preserve the inherited sources,
  specification pages, sample data and tools; do not create a prose answer or a
  replacement entry point.
- Do not search for, download, copy, wrap or invoke
  an online implementation or a task-specific answer; work only from this
  workspace, its documentation and `/opt/docs`, and do not search the system for
  other implementations or answers.
- Write the files under `/app/src` in ASCII. Run the local checks named above
  and leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`.

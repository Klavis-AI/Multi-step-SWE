# Milestone 5 — Bulk Data export, SMART on FHIR scopes, and `$everything`

## 1. Implement this milestone

The server in `/app` serves five FHIR releases, validates against published
profiles, answers the terminology operations, and notifies topic-based
subscriptions. Add the three things a FHIR server needs before anyone can put
real data through it at scale: **asynchronous Bulk Data export**, **SMART on
FHIR authorization**, and **`$everything`**.

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

### 1.2 The asynchronous request pattern

`async.html` and `async-bulk.html` define one pattern, and everything in §1.3
uses it.

* A client asks for an asynchronous response with **`Prefer: respond-async`**;
  a kick-off request without that header is refused with **`400`**.
* The server answers **`202 Accepted`** with a **`Content-Location`** header
  holding an absolute URL — the *status endpoint* — and no body.
* **`GET <status endpoint>`** while the job runs answers **`202`**, with
  `X-Progress` describing how far it has got and, when the server can estimate
  one, `Retry-After`.
* When the job is done that same `GET` answers **`200`** with a JSON body that is
  **not** a FHIR resource: `transactionTime`, `request` (the original request
  URL), `requiresAccessToken`, `output`, `deleted` and `error`, where each entry
  of `output` has a `type` and a `url`.
* **`DELETE <status endpoint>`** cancels the job and answers `202`; a `GET`
  afterwards answers `404`.
* A status endpoint that never existed is `404`. A job that failed answers
  `500` with an `OperationOutcome`.

### 1.3 `$export`

Three levels, all at `/baseR4` and `/baseR5`, per the published
`OperationDefinition`s in `hl7.fhir.uv.bulkdata`:

| request | what it exports |
|---|---|
| `GET [base]/$export` | everything in the store |
| `GET [base]/Patient/$export` | every resource in the patient compartment |
| `GET [base]/Group/[id]/$export` | the patient compartment of that group's members |

Parameters: **`_type`** (a comma-separated list of resource types),
**`_since`** (an `instant`; only resources whose `meta.lastUpdated` is later),
**`_elements`**, **`_typeFilter`** (a comma-separated list of search queries, each
`Type?params`, restricting that type) and **`_outputFormat`**, which must be one
of `application/fhir+ndjson`, `application/ndjson` or `ndjson` — anything else is
`400`. An unknown `_type` is `400`, not an empty file.

The files the manifest points at are **newline-delimited JSON**: one resource per
line, no wrapping array, no pretty-printing, one file per resource type, and a
`type` in the manifest entry that matches what the file contains. A type with no
matching resources gets **no entry at all** rather than an empty file. The server
serves those URLs itself, with `Content-Type: application/fhir+ndjson`.

### 1.4 SMART on FHIR

`scopes-and-launch-context.html` and `conformance.html` define this.

* **`GET [base]/.well-known/smart-configuration`** answers `200` with
  `application/json` carrying at least `issuer`, `jwks_uri`,
  `authorization_endpoint`, `token_endpoint`, `capabilities` and
  `code_challenge_methods_supported`. It is **not** a FHIR resource and carries
  no `resourceType`.
* `CapabilityStatement.rest.security` declares `service` with the
  `http://terminology.hl7.org/CodeSystem/restful-security-service` code `SMART-on-FHIR`,
  and the OAuth URIs extension
  `http://fhir-registry.smarthealthit.org/StructureDefinition/oauth-uris` with
  `token` and `authorize`.
* Requests carry **`Authorization: Bearer <jwt>`**. The token is a JWS with
  `alg: HS256`, signed with the key in `/app/testdata/smart/jwks.json` — a JWK
  set with one `oct` key whose `kid` the token's header names. A token whose
  signature does not verify, whose `kid` is unknown, whose `exp` has passed or
  whose `iss` is not this server's `BASE_URL` is **`401`** with an
  `OperationOutcome` and a `WWW-Authenticate: Bearer` header.
* The token's `scope` claim is a space-separated list of SMART v2 scopes:
  `patient/Observation.rs`, `user/*.cruds`, `system/Patient.r`, and the v1
  spellings `patient/*.read` and `user/Observation.write`, which mean `.rs` and
  `.cu` respectively. A request the granted scopes do not permit is **`403`**
  with an `OperationOutcome`; the interaction letters are `c` create, `r` read,
  `u` update, `d` delete, `s` search.
* A `patient/` scope is limited to the patient the token's `patient` claim names:
  a read of another patient's resource is `403`, and a search returns only that
  patient's compartment.
* **Requests with no `Authorization` header at all are answered normally.** This
  server is open by default and enforces only what a token asks it to; that is
  what makes the scope enforcement testable without an authorization server.

### 1.5 `$everything`

* **`GET [base]/Patient/[id]/$everything`** returns a `searchset` `Bundle` of
  every resource in that patient's compartment, the `Patient` included, and
  nothing outside it (no referenced resources the compartment does not hold), with
  `_since`, `_type`, `start`, `end` and `_count` as the published
  `OperationDefinition` defines them.
* The compartment is the one `compartmentdefinition-patient.json` in the R4
  package defines — not a guess, and not "everything that mentions the id".
* `Patient/$everything` at type level, with no id, is an error; an id that does
  not exist is `404`.

## 2. Read these local references

**Normative semantics.** `/app/SPEC/R4/` now also holds `async.html` and
`async-bulk.html`; `/app/SPEC/smart/` holds the SMART App Launch IG's
`index.html`, `scopes-and-launch-context.html`, `app-launch.html`,
`backend-services.html` and `conformance.html`.  The patient compartment is the
`CompartmentDefinition` resource in the R4 package -- `compartmentdefinition.html`
in the R4 pages (local copy: `/opt/docs/fhir/R4/compartmentdefinition.html`)
describes what one is -- and a copy of it is at
`/app/tools/fhirconf/compartment-patient.json`.

**Machine-readable definitions.** This milestone's packages are in
`/app/packages/` as tarballs; install them before you start:

```sh
sh /app/packages/install.sh          # unpacks into /root/.fhir/packages/<name>#<version>
```

They are `hl7.fhir.uv.bulkdata#2.0.0` and
`hl7.fhir.uv.smart-app-launch#2.1.0`, beside the packages already installed.
The same packages are installed at the same path wherever the server runs.

**Sample data and local checks.** `/app/testdata/smart/jwks.json` holds the key
tokens are signed with, and `/app/testdata/smart/tokens.json` a set of example
tokens with the scopes they carry. `/app/tools/fhirconf/` holds this milestone's
runner; its `README.md` says how to use it.

### Checking progress

```sh
sh /app/packages/install.sh
make -C /app/src
SERVER_PORT=8080 BASE_URL=http://127.0.0.1:8080 \
  INDEX_DB_DIR=/tmp/d/index TRANSACTION_DB_DIR=/tmp/d/tx RESOURCE_DB_DIR=/tmp/d/res \
  /app/bin/blaze &
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --bulk
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --smart
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --everything
```

The runner compares with answers computed from the published
`CompartmentDefinition`, `OperationDefinition`s and the SMART scope grammar. The
local cases are representative, not exhaustive, and the requirements of the
earlier milestones still apply, so a clean local run is necessary but not
sufficient.

At every base, as before: a failed transaction, and a Bundle posted to a base that is neither a
`transaction` nor a `batch`, are answered `400` with an `OperationOutcome`.

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

# Milestone 1 — a FHIR R4 server: REST, search, transactions and validation

## 1. Implement this milestone

Build, in **C**, a FHIR server that speaks the **HL7 FHIR RESTful API at version
R4 (4.0.1)** over HTTP/1.1, persists what it is given, and answers searches,
transactions and `$validate`.

### 1.1 How it is built, started and configured

* Everything the build needs — the C sources and the makefile — lives under
  **`/app/src`**. `make -C /app/src`, with no target, builds the server and
  produces the executable **`/app/bin/blaze`**; object files go under
  `/app/build`. The project must build with exactly that command in a clean
  container that holds only `/app/src`, so anything outside `/app/src` is not
  there when it builds.
* The tree carries **C sources only** (`.c`, `.h`, and headers you include).
  A source file in another compiled language — C++, Rust, Go, Java, Scala, C#,
  Swift, Kotlin, Objective-C, Erlang, Clojure and so on — is not allowed. Shell
  and Python helper scripts are fine.
* The project must build in a clean container with a C toolchain after **every
  compiled file in the tree has been removed**, so nothing may be
  pre-built. The process that ends up listening on the port must be the ELF
  binary that build produced, and neither it nor any process it starts may be a
  JVM, a Python, Ruby, Node, Perl, PHP, .NET, Erlang or other language runtime:
  do not wrap, embed, exec or proxy HAPI FHIR, Blaze, fhir-candle, Firely or any
  other existing FHIR server.
  This must hold from the moment the server comes up until it is stopped:
  something must be listening, and the listener must be that ELF, the whole
  time, so a server that stops partway through does not meet it.
* Configuration is read from the environment, using **Blaze's** documented
  variable names and meanings
  (<https://github.com/samply/blaze/blob/master/docs/deployment/environment-variables.md>):

  | Variable | Meaning | Default |
  | --- | --- | --- |
  | `SERVER_PORT` | the port of the main HTTP server | `8080` |
  | `BASE_URL` | the URL under which the server is accessible by clients | `http://localhost:8080` |
  | `INDEX_DB_DIR` | the directory where the index database files are stored | `/app/data/index` |
  | `TRANSACTION_DB_DIR` | the directory where the transaction log files are stored | `/app/data/transaction` |
  | `RESOURCE_DB_DIR` | the directory where the resource files are stored | `/app/data/resource` |

  There are no other knobs: no command-line switches, no mode flags, no marker
  files, no custom headers. Behaviour is selected the way FHIR selects it — by
  the request's service base, media type, search parameters and body.
* The server runs under an **address-space limit of 8 GiB** and must answer
  every request within **120 seconds**; a build must finish within 15 minutes under the same address-space limit. Exceeding any of
  those is a failure of that request or build.
* The **FHIR service base is `$BASE_URL/baseR4`**, the version-named base the
  HAPI FHIR public test server publishes (`https://hapi.fhir.org/baseR4`). The
  server binds `SERVER_PORT` so that `127.0.0.1:$SERVER_PORT` reaches it.
* Everything written through the API is stored under the three configured
  directories — the transaction log under `TRANSACTION_DB_DIR`, resource content
  under `RESOURCE_DB_DIR`, the search index under `INDEX_DB_DIR` — and survives
  a stop and start: content, `meta.versionId` and history all come back.

### 1.2 The RESTful API (`/app/SPEC/R4/http.html`)

Support, at `[base] = $BASE_URL/baseR4`:

* `GET [base]/metadata` → a `CapabilityStatement` whose `fhirVersion` is
  `4.0.1`, with `status`, `kind`, a `format` list naming a JSON and an XML media
  type, `rest[0].mode` = `server`, `rest[0].resource[]` giving each served
  `type`, its `interaction[].code`s and its `searchParam[]` (`name`, the
  parameter's `type`, and the `definition` canonical of the `SearchParameter` it
  comes from), and `rest[0].interaction` including `transaction` and `batch`.
  Every resource type that appears in `/app/testdata/seed` is a type this server
  serves and must be declared. What it declares must be true: a search parameter
  it lists is accepted by search, and a parameter it does not list is refused
  with `400`.
* `read`, `vread` (`[base]/[type]/[id]/_history/[vid]`), `update`, `create`,
  `delete`, conditional create (`If-None-Exist`), conditional update
  (`PUT [base]/[type]?<criteria>`), conditional delete, `history` at instance,
  type and system level, `search` at type level, and
  `POST [base]/[type]/_search` with `application/x-www-form-urlencoded`
  parameters.
* `ETag: W/"<versionId>"` and `Last-Modified` on every read and write, `Location`
  on create, `If-Match`, `If-None-Match` (a match answers `304`), and
  `Prefer: return=minimal | representation | OperationOutcome`.
  The version id must appear as a **positive decimal integer** — `"1"` on first
  create or PUT, incrementing by exactly one on each subsequent write — and the
  **same** id must appear in `meta.versionId`, in the `ETag`, in the
  `_history/[vid]` a `vread` answers, in the `Location` a create returns, and in
  what `If-Match` accepts; every write gives the resource a new one.
* The status codes `http.html` names, with an `OperationOutcome` on every error:
  `404` for an unknown resource type or id, `410` for a resource that was
  deleted, `400` when the body cannot be parsed or its `id`/`resourceType` does
  not match the request URL, `412` when conditional criteria match more than one
  resource, `409`/`412` on a version conflict, `304` for a matching
  `If-None-Match`.
* `POST [base]` with a `transaction` or a `batch` Bundle, answered with a
  `transaction-response` / `batch-response` Bundle: entries processed in the
  order `http.html` gives, `urn:uuid:` full URLs resolved to the ids the server
  assigns with every reference to them rewritten, `request.ifNoneExist`
  honoured, a `urn:uuid:` reference that resolves to no entry of the bundle
  failing the transaction, the **whole transaction rolled back** when any entry
  fails, and each entry of a batch succeeding or failing on its own with its own
  `response.status`. A failed transaction, and a Bundle posted to `[base]` that
  is neither a `transaction` nor a `batch`, are answered `400` with an
  `OperationOutcome`.
* Both wire formats, named in the response's `Content-Type` as
  `application/fhir+json` and `application/fhir+xml`, chosen by `Accept` or by
  the `_format` parameter (`_format=json`/`xml` select the same two media types),
  parsed on input and produced on output, following `/app/SPEC/R4/json.html` and `/app/SPEC/R4/xml.html` —
  including primitive extensions (`_birthDate`), `Extension.url` and element
  `id` as XML attributes, and the narrative `div` as embedded XHTML.

### 1.3 Search (`/app/SPEC/R4/search.html`)

The server's search parameters are the ones **published in
`hl7.fhir.r4.core#4.0.1`**: each `SearchParameter` resource gives the `code`, the
`type`, and the **FHIRPath `expression`** that selects the values it indexes. Use
them; do not invent a parameter set.

Support all of it: the parameter types `number`, `date`, `string`, `token`,
`reference`, `composite`, `quantity` and `uri`; the prefixes `eq ne gt lt ge le
sa eb`; the modifiers `:missing`, `:exact`, `:contains`, `:not`, `:above` and
`:below`; `,` as OR inside one parameter and a repeated
parameter as AND; chaining (`subject.name=`), typed chaining
(`subject:Patient.gender=`) and reverse chaining
(`_has:Observation:subject:code=`); `_include`, `_revinclude`, `:iterate`, `*`
and typed include targets, with `Bundle.entry.search.mode` set to `match` or
`include`; the parameters every resource has (`_id`, `_lastUpdated`, `_tag`,
`_profile`, `_security`); and the result parameters `_sort`
(ascending, `-` for descending, several keys), `_count`, `_summary`
(`true|text|data|count|false`), `_elements` and `_total`.

A search answers a `searchset` Bundle with `Bundle.total`, `Bundle.link` for
`self` and — when more results remain — `next`, and `Bundle.entry.fullUrl` built
from `BASE_URL`. A parameter the server does not know is `400` with an
`OperationOutcome`.

### 1.4 Validation against the StructureDefinitions

`POST [base]/[type]/$validate` and `POST [base]/[type]/[id]/$validate`, with the
instance to check as the request body, answer an `OperationOutcome` reporting the
issues in that instance, with `issue.expression` naming the element each issue is
on. The rules are the
**StructureDefinition snapshots published in `hl7.fhir.r4.core#4.0.1`**; at
minimum, detect

* an element that the StructureDefinition does not define,
* a required element (`min` ≥ 1) that is missing,
* a primitive whose JSON type does not match its declared type,
* a primitive whose value does not match the regex its type declares,
* an array where the element does not repeat, and a bare value where it does,
* an empty array, and a `null` value,
* a choice element written with a type suffix that is not one of its types,
* a logical `id` that is not `[A-Za-z0-9\-\.]{1,64}`,
* more occurrences of an element than its maximum cardinality allows.

Write interactions are not required to validate; only `$validate` is.

Everything the server returns — resources, search Bundles, history Bundles,
transaction responses, `OperationOutcome`s and the `CapabilityStatement`, in
both JSON and XML — must itself be valid FHIR R4.

## 2. Read these local references

**Normative semantics.** The HL7 FHIR R4 specification pages under `/app/SPEC/R4/`:
`http.html` (the RESTful API, status codes, headers, transactions),
`search.html` (parameter types, prefixes, modifiers, chaining, result
parameters), `formats.html`, `json.html`, `xml.html` (the wire formats),
`bundle.html`, `capabilitystatement.html`, `operations.html`, `resource.html`,
`datatypes.html`, `references.html`, `extensibility.html`,
`operationoutcome.html`, `resource-operation-validate.html`, `profiling.html`
and `fhirpath.html`.
`/app/SPEC/PROVENANCE.md` records where each page came from.

**Machine-readable definitions.** The published FHIR package cache at
`/root/.fhir/packages/`. This milestone's definitions are
`hl7.fhir.r4.core#4.0.1/package/`: the `StructureDefinition-*.json` snapshots,
the `SearchParameter-*.json` resources, the `CompartmentDefinition-*.json` and
`OperationDefinition-*.json` resources, with `.index.json` listing every file.

**Sample data and local checks.** `/app/testdata/seed/` holds official FHIR R4
example instances. `/app/tools/fhirconf/` holds a conformance runner; its
`README.md` says how to use it. `/app/WORKSPACE_HANDOFF.md` is the handoff for
the next session — keep it current.

### Checking progress

```sh
make -C /app/src
SERVER_PORT=8080 BASE_URL=http://127.0.0.1:8080 \
  INDEX_DB_DIR=/tmp/d/index TRANSACTION_DB_DIR=/tmp/d/tx RESOURCE_DB_DIR=/tmp/d/res \
  /app/bin/blaze &
python3 /app/tools/fhirconf/run_conformance.py --base http://127.0.0.1:8080/baseR4
python3 /app/tools/fhirconf/run_conformance.py --base http://127.0.0.1:8080/baseR4 --validate
```

The runner loads the sample dataset and compares the answers with ones recorded
from a reference FHIR server; `--verbose` prints the expected and actual value
for each miss. The local cases are representative, not exhaustive, and the
server's responses must also pass the official HL7 FHIR validator, so a clean
local run is necessary but not sufficient.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied sources,
  specification pages, sample data and tools. Do not create a prose answer or an
  alternate entry point.
- Do not search for, download, copy, wrap or invoke
  an online implementation or a task-specific answer; work only from this
  workspace, its documentation and `/opt/docs`, and do not search the system for
  other implementations or answers.
- Write the files under `/app/src` in **ASCII**. The clean build starts from a
  copy of `/app/src` made over a channel that is not byte-transparent, and a
  non-ASCII byte can truncate that copy, which would lose your work. Put
  non-ASCII test data in files outside `/app/src`.
- Run the local checks named above and leave a reproducible handoff in
  `/app/WORKSPACE_HANDOFF.md`.

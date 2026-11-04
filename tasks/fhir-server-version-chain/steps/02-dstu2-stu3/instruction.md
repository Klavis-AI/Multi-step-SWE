# Milestone 2 — DSTU2 and STU3 beside R4, and FHIR Mapping Language conversion

## 1. Implement this milestone

The server in `/app` speaks the HL7 FHIR RESTful API at R4 (4.0.1) on the service
base `$BASE_URL/baseR4`. Add two more FHIR versions to the same process, on the
same port, and add the FHIR Mapping Language.

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
  or any other existing FHIR server.
  This must hold from the moment the server comes up until it is stopped:
  something must be listening, and the listener must be that ELF, the whole
  time, so a server that stops partway through does not meet it.
* Configuration is still only these five environment variables, with Blaze's
  documented meanings
  (<https://github.com/samply/blaze/blob/master/docs/deployment/environment-variables.md>):
  `SERVER_PORT`, `BASE_URL`, `INDEX_DB_DIR`, `TRANSACTION_DB_DIR`,
  `RESOURCE_DB_DIR`. There are no other knobs, and no new ones: the FHIR version
  is selected by the service base in the request path, never by a flag.
* The server runs under an **address-space limit of 8 GiB** and must answer
  every request within **120 seconds**; a build must finish within 15 minutes under the same address-space limit.
* The required build **deletes every compiled file and builds from clean**, so
  it is always consistent. Your own incremental builds are not. `/app` is
  the same container the previous session worked in, so **its object files are
  already sitting in `/app/build`**, and if the makefile you inherited does not
  make an object depend on the headers its source includes, editing a header and
  running `make` leaves those stale objects linked against a new struct layout.
  The result is a server that misbehaves rather than a build that fails. This
  milestone asks you to change headers. Check that rule, or `make clean`, before
  you trust an incremental build.
* Write the files under `/app/src` in **ASCII**; the copy of `/app/src` that the
  clean build starts from is made over a channel that is not byte-transparent.

### 1.2 Three service bases, one process

Serve, at the same time and on the same port:

| Service base | FHIR version | Definitions |
| --- | --- | --- |
| `$BASE_URL/baseDstu2` | **1.0.2** (DSTU2) | `hl7.fhir.r2.core#1.0.2` |
| `$BASE_URL/baseDstu3` | **3.0.2** (STU3) | `hl7.fhir.r3.core#3.0.2` |
| `$BASE_URL/baseR4` | 4.0.1 (R4) | `hl7.fhir.r4.core#4.0.1` |

These are the version-named bases the HAPI FHIR public test server publishes
(`https://hapi.fhir.org/baseDstu2`, `/baseDstu3`, `/baseR4`).

Each base is its own world:

* `GET [base]/metadata` answers with **that version's** conformance resource at
  **that version's** `fhirVersion`: DSTU2 calls it **`Conformance`** and STU3
  renamed it to `CapabilityStatement`, so a server that answers
  `CapabilityStatement` at `/baseDstu2` is wrong. What it declares is that
  release's too — the resource types it lists are the ones that release defines
  (94, 117 and 146 of them), each with the search parameters that release
  publishes for it, including the ones every resource has (`_id`,
  `_lastUpdated`, `_tag`, `_profile`, `_security`, `_has`, `_list`, `_filter`,
  and `_source` from STU3). A parameter the server declares must be accepted;
  one it does not declare and cannot evaluate is a `400`.
* Resources are stored per base. The same logical id at two bases is two
  different resources, and a resource written at one base is `404` at another.
* A resource type that a version does not define is `404` at that base:
  `MedicationOrder` exists in DSTU2 and not in R4; `MedicationRequest` exists
  from STU3 and not in DSTU2. Which types exist is in each version's package.
* **The media types are the release's own.** DSTU2 `http.html` names
  `application/json+fhir` and `application/xml+fhir`; STU3 and R4 use
  `application/fhir+json` and `application/fhir+xml`. Each base answers, and
  accepts, the forms its own release defines, and the `format` list in its
  conformance resource says which. `/baseDstu2` also takes the later
  `application/fhir+json` and `application/fhir+xml` spellings in `Accept` and
  `Content-Type` as the same two formats, and answers in its own spelling.
* The whole R4 milestone keeps working at `/baseR4` — CRUD, conditional
  interactions, history, the search grammar, transactions and batches,
  `$validate`, both wire formats.
* At every base, a failed transaction, and a Bundle posted to the base that is
  neither a `transaction` nor a `batch`, are answered `400` with an
  `OperationOutcome`.

### 1.3 Each version's own definitions

Everything version-specific comes from that version's published package: the
resource types, the element names and cardinalities, the primitive types, the
conformance resource, and the search parameters. Three differences this
milestone is built on, all of them visible in the packages:

* **DSTU2 defines its search parameters with XPath.** In
  `hl7.fhir.r2.core#1.0.2` a `SearchParameter` carries `xpath`
  (`f:Patient/f:telecom[system/@value='phone']`) and no `expression`, and
  `SearchParameter.base` is a **string** rather than the array it becomes in
  STU3. The `type`, `code` and `target` fields mean what they mean in
  `search.html`.
* **A package publishes more than one definition for the same code.** DSTU2
  ships the DAF profile's search parameters next to the core ones, and the core
  packages ship example `SearchParameter` resources; the definition whose
  canonical is named `<ResourceType>-<code>` is the resource's own.
* **Element shapes changed.** `HumanName.family` repeats in DSTU2 and is a
  single string from STU3; `Patient.careProvider` in DSTU2 became
  `Patient.generalPractitioner` in STU3. An instance that is valid at one
  version is not at another, and `[base]/[type]/$validate` must answer for the
  version of the base it is called on. Each issue it reports names the element
  it is on in `issue.expression`; at `/baseDstu2`, whose `OperationOutcome` has
  no `expression`, `issue.location` may name it instead, as DSTU2's simple XPath
  (e.g. `f:Patient/f:gender`).

The primitive types differ too. DSTU2 defines `code`, `id`, `oid`, `uuid`,
`markdown` and others as **constraints on another datatype** (`constrainedType`),
so their element paths are rooted at the base type, not at their own name — and
DSTU2 publishes **no `regex` extension** on its primitives at all, naming the XML
Schema type in `structuredefinition-xml-type` instead, so a validator driven
only by what the package happens to carry accepts `"not-an-instant"` as an
`instant`.

The search *syntax* is a version's own, not only its parameter list. Three that
this milestone requires, each written in that release's `search.html`:

* DSTU2 sorts with the **`_sort:asc` and `_sort:desc` modifiers** (e.g.
  `_sort:asc=birthdate`), not the `-` prefix that arrived in STU3. A request
  `_sort=-birthdate` at `/baseDstu2` names a parameter that release does not
  define and is answered `400`. `_sort=birthdate` (no modifier and no prefix)
  must sort ascending, which DSTU2's search grammar treats as the default.
* `_total` is defined from R4. A release that does not define it treats it as a
  control parameter the server does not implement — which `search.html` says to
  ignore unless the client asked for strict handling — so the count is still
  reported.
* `fhir_comments` is a legal JSON-only property in DSTU2, carrying what the XML
  form keeps as XML comments; it was dropped from STU3 onwards. Each comment,
  its text unchanged, goes on the element that follows it among its siblings
  (that element's object, the array item for a repeating element, or the
  `_name` object for a primitive); a comment with no element after it goes on
  the element before it, and a comment inside a primitive element is dropped.

And it applies to what the server **writes**, not only to what it reads: every
resource, Bundle and `OperationOutcome` the server returns from a base must be
valid at that base's version, in both wire formats. A conformance resource that
carries R4 element names at `/baseDstu2`, a search Bundle whose entries use a
later version's shape, or an `OperationOutcome` with the wrong `issue.code`
value set is wrong even when the status line is right. The server's own
answers, read back, must pass the official HL7 validator at each base's own FHIR
version.

### 1.4 FHIR Mapping Language and `StructureMap/$transform`

Implement the FHIR Mapping Language of `/app/SPEC/STU3/mapping-language.html`
(the same language R4 publishes) and the `StructureMap-transform` operation:

* A `StructureMap` may be created through the REST API from FML source with
  `Content-Type: text/fhir-mapping`; the server stores it as a `StructureMap`
  resource, and reading it back gives the resource form of **the same map**.
  That resource posted back as JSON transforms identically. Source that is not a
  map is `400`.

  "The same map" means what the source text pins, and that is what must match:
  `url` and `name`; the `structure` entries (url, mode, alias); each `group`
  with its `extends`, its type mode (`<<types>>` and `<<type+>>` are the FML
  spellings of `types` and `type-and-types`) and its `input` list (name, type,
  mode); and inside each group every `rule`, in order, with

  - its **name** — the quoted name at the end of the rule when it has one, and
    otherwise a name derived from what the rule reads;
  - its `source` entries: `context`, `element`, `condition` (the `where`
    clause), `check`, list mode;
  - its `target` entries: `context`, `element`, `transform` and the
    **values** of the transform's parameters (a quoted argument carries its
    value, not its lexeme), list mode;
  - its `dependent` invocations by name, and its nested rules.

  Three things are deliberately left to you, because the FML text does not pin
  them: the variable name you invent for the short form `src.a -> tgt.b;`,
  whether you render that short form as `copy`, `create` or no transform at all,
  and the argument of `create` and `reference`. FHIRPath expression text must
  match after FHIRPath's own normalisation — `5 days` and `5 'days'` are the
  same literal, `"x"` and `'x'` are the same string, and whitespace inside an
  expression is not significant.
* `POST [base]/StructureMap/$transform?source=<canonical url>` runs the named map
  over the resource in the request body and answers the target resource. A
  canonical no stored map has is `404`.
* The language features the maps need: `map`/`uses`/`imports` headers, `group`
  with typed `source`/`target` parameters, rules of the form
  `src.path as v where <fhirpath> -> tgt.path = <transform> as w then group(a, b)`,
  dependent groups (`then name(args)` and `then { … }`), `share <id>` targets
  that write into one created value, a parenthesised right-hand side (which is
  the `evaluate` transform), `log` clauses, and the transforms `create`, `copy`,
  `evaluate`, `truncate`, `append`, `reference`, `cast`, `c`, `cc`, `uuid`.
  Target elements are written with their concrete choice-type name
  (`value` + `Reference` = `valueReference`).

## 2. Read these local references

**Normative semantics.** `/app/SPEC/DSTU2/` and `/app/SPEC/STU3/` hold those
versions' specification pages — `http.html`, `search.html`, `formats.html`,
`json.html`, `xml.html`, `conformance.html` (DSTU2) /
`capabilitystatement.html` (STU3), `operations.html`, `resource.html`,
`datatypes.html`, `references.html`, `operationoutcome.html`, `profiling.html`,
and (STU3) `mapping-language.html`; FHIRPath is `/app/SPEC/R4/fhirpath.html`.
`SPEC/PROVENANCE-DSTU2.md` and
`SPEC/PROVENANCE-STU3.md` record where each page came from. The R4 pages from
the previous milestone are still in `/app/SPEC/R4/`.

**Machine-readable definitions.** This milestone's packages are shipped as
tarballs in `/app/packages/`. Install them into the FHIR package cache before you
start:

```sh
sh /app/packages/install.sh          # unpacks into /root/.fhir/packages/<name>#<version>
```

The same packages are installed at the same path wherever the server runs, so read
them from `/root/.fhir/packages/` — `hl7.fhir.r2.core#1.0.2`,
`hl7.fhir.r3.core#3.0.2` and the `hl7.fhir.r4.core#4.0.1` you already use.

**Sample data and local checks.** `/app/testdata/seed-dstu2/` and
`/app/testdata/seed-stu3/` hold official example instances for those versions.
`/app/tools/fhirconf/` holds this milestone's conformance runner; its
`README.md` says how to use it. `/app/WORKSPACE_HANDOFF.md` is the handoff for
the next session — keep it current.

### Checking progress

```sh
make -C /app/src
SERVER_PORT=8080 BASE_URL=http://127.0.0.1:8080 \
  INDEX_DB_DIR=/tmp/d/index TRANSACTION_DB_DIR=/tmp/d/tx RESOURCE_DB_DIR=/tmp/d/res \
  /app/bin/blaze &
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --validate
```

The runner loads each version's dataset, compares the answers with ones recorded
from a reference FHIR server **at that version**, and sends the instances under
`valcases-*` and `valdiff-*` to `$validate` at the matching base. The
`valdiff-*` instances are built from elements the releases actually disagree
about, so each one is valid at one version and invalid at the other; `--verbose` prints the
expected and actual value for each miss. The local cases are a small sample of
what this milestone requires, which also includes the R4 milestone's behaviour,
the FHIR Mapping Language as the official FML test cases exercise it through
`StructureMap/$transform`, and the cross-version behaviour above, so a clean
local run is necessary but not sufficient.

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

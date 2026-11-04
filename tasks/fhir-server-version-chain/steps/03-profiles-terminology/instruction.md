# Milestone 3 — profile validation and the terminology operations

## 1. Implement this milestone

The server in `/app` serves FHIR DSTU2, STU3 and R4 on version-named service
bases and validates instances against the base resource definitions. Add the two
things a FHIR server needs before anyone can use it for real data: **validation
against a published profile**, and the **terminology operations**.

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
* The required build **deletes every compiled file and builds from clean**. Your
  own incremental builds do not: `/app` is the same container the previous session
  worked in, its object files are already in `/app/build`, and a makefile that
  does not make an object depend on the headers its source includes will leave
  stale objects linked against a new struct layout. `make clean` first if you are
  not sure.
* Write the files under `/app/src` in **ASCII**; the copy of `/app/src` that the
  clean build starts from is made over a channel that is not byte-transparent.

### 1.2 Validating against a profile

`profiling.html` and `validation.html` define what a profile is: a
`StructureDefinition` whose `derivation` is `constraint`, which narrows a base
resource. Implement validation against one.

* **`POST [base]/[Type]/$validate?profile=<canonical>`** validates the body
  against that profile as well as against the base resource, and returns an
  `OperationOutcome` whose issues carry the **`expression`** of the element at
  fault. The canonical may carry a `|version` suffix. A canonical the server
  does not hold is an error, not a silent pass.
* `Resource.meta.profile` names the profiles an instance claims. `$validate`
  with no `profile` parameter validates against the base resource and against
  every profile in `meta.profile` that the server holds.
* The profiles come from the **published implementation guides installed in the
  FHIR package cache**, the same way the core definitions do. A profile's
  `snapshot` is the resolved element list; where a profile ships only a
  `differential`, it is layered on its `baseDefinition`.

What a profile constrains, all of it in `ElementDefinition`
(`elementdefinition.html`):

* **cardinality**: `min` and `max` narrower than the base's.
* **type**: a narrower type list, and `targetProfile` on a `Reference`.
* **fixed and pattern values**: `fixed[x]` must be equal; `pattern[x]` must be
  present as a subset — an object matches when every property it names matches,
  and an array matches when every entry it names has a match.
* **bindings**: a `required` binding means the code must be in the value set; an
  `extensible` one means it must be unless no suitable code exists; `preferred`
  and `example` are not errors.
* **slicing**: an element with `slicing` splits its repeats into named slices,
  each with its own cardinality. A repeat belongs to the slice whose
  **discriminators** it matches — `value` and `pattern` compare the element the
  discriminator's `path` selects, `type` compares the type of the element,
  `profile` compares against a profile the target conforms to, and `exists`
  compares whether it is present. `slicing.rules` says whether repeats that
  match no slice are allowed (`open`), forbidden (`closed`), or allowed only at
  the end (`openAtEnd`). A slice with `min >= 1` that no repeat matches is an
  error located at the sliced element.
* **`mustSupport`** is a statement about the *implementer*, not about the
  instance: it is never an error on its own.

### 1.3 The terminology operations

`terminology-service.html` describes them; each operation's parameters are fixed
by its `OperationDefinition` in the published package, and the answers are in the
published `CodeSystem` and `ValueSet` resources. Implement the five, at every
service base, in both the `GET` form with URL parameters and the `POST` form
with a `Parameters` body:

| operation | what it answers |
| --- | --- |
| `ValueSet/$expand` | the value set's `expansion.contains`, with `filter`, `count`, `offset`, `includeDesignations` and `activeOnly` as `valueset-operations.html` defines them |
| `ValueSet/$validate-code` | whether a `code`+`system`, a `coding` or a `codeableConcept` is in the value set, returning `result`, and `display` and `message` where they apply |
| `CodeSystem/$lookup` | a concept's `name`, `version`, `display`, `designation` and `property`, as `codesystem-operations.html` defines the output |
| `CodeSystem/$subsumes` | `equivalent`, `subsumes`, `subsumed-by` or `not-subsumed` between two concepts of one code system, from the hierarchy the resource carries |
| `ConceptMap/$translate` | the `match` list for a source code, each with its `equivalence` (or `relationship`) and `concept`, from a `ConceptMap` the server holds |

Rules that apply to all five:

* a value set is named by `url` (with an optional `|version`) or by the id in the
  path; both forms work, and a value set the server does not hold is `404`.
* `ValueSet.compose` composes: `include`/`exclude` by `system` (all of it), by
  explicit `concept` list, by `filter` (`is-a`, `descendent-of`, `=`,
  `in`, `not-in`, `regex`), and by `valueSet` reference. `expansion.contains` is
  the result, deduplicated, each entry carrying `system`, `code` and `display`.
* a code system with `hierarchyMeaning` `is-a` nests concepts in
  `concept.concept`; `$subsumes` and the `is-a` filter both read that.
* an operation called on a resource the server does not hold, or with a
  parameter combination the `OperationDefinition` does not allow, answers `400`
  or `404` with an `OperationOutcome` — never a `200` with an empty answer.

### 1.4 What keeps working

Everything the previous milestones asked for, unchanged: the three service
bases with their own conformance resources, CRUD, conditional interactions,
history, the search grammar, transactions and batches, both wire formats,
structural `$validate` at each base against its own release (each issue naming
its element in `issue.expression`, or at `/baseDstu2` in `issue.location`), and
`StructureMap/$transform` with the FHIR Mapping Language. A failed transaction,
and a Bundle posted to a base that is neither a `transaction` nor a `batch`, are
answered `400` with an `OperationOutcome`.

## 2. Read these local references

**Normative semantics.** `/app/SPEC/R4/` now also holds `profiling.html`,
`validation.html`, `elementdefinition.html`, `terminology-service.html`,
`valueset-operations.html`, `codesystem-operations.html` and
`conceptmap-operations.html`, beside the pages the earlier milestones use.
`/app/SPEC/DSTU2/` and `/app/SPEC/STU3/` are unchanged.

**Machine-readable definitions.** This milestone's packages are in
`/app/packages/` as tarballs; install them before you start:

```sh
sh /app/packages/install.sh          # unpacks into /root/.fhir/packages/<name>#<version>
```

They are `hl7.fhir.us.core#6.1.0`, `hl7.fhir.uv.ips#1.1.0` and
`hl7.terminology.r4#5.3.0`, beside the core packages already installed. Each
declares the FHIR versions it is for in its own `package.json`; a package that
does not declare this base's version is not this base's package. The same
packages are installed at the same path wherever the server runs.

**Sample data and local checks.** `/app/testdata/uscore/` holds the US Core
example instances, each declaring its profiles in `meta.profile`.
`/app/tools/fhirconf/` holds this milestone's runner; its `README.md` says how to
use it.

### Checking progress

```sh
sh /app/packages/install.sh
make -C /app/src
SERVER_PORT=8080 BASE_URL=http://127.0.0.1:8080 \
  INDEX_DB_DIR=/tmp/d/index TRANSACTION_DB_DIR=/tmp/d/tx RESOURCE_DB_DIR=/tmp/d/res \
  /app/bin/blaze &
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --validate
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --profiles
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --terminology
```

The runner compares with answers taken from the official HL7 validator and from
the published packages. The local cases are representative, not exhaustive, and
the requirements of the earlier milestones still apply, so a clean local run is
necessary but not sufficient.

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

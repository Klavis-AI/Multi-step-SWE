# Milestone 4 — R4B and R5, and topic-based subscriptions

## 1. Implement this milestone

The server in `/app` serves FHIR DSTU2, STU3 and R4, validates against published
profiles, and answers the terminology operations. Add the two releases that
follow R4 — **R4B (4.3.0)** and **R5 (5.0.0)** — served at the same time as the
three already there, and the feature those two releases introduced: **topic-based
subscriptions**.

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
* The required build **deletes every compiled file and builds from clean**, so a
  makefile that does not make an object depend on the headers its source
  includes will pass your incremental builds and fail the clean one.
* Write the files under `/app/src` in **ASCII**; the copy of `/app/src` that the
  clean build starts from is made over a channel that is not byte-transparent.

### 1.2 Two more service bases

`http.html` fixes the shape: a FHIR server publishes each release at its own
service base and answers there with that release's own definitions. The five
bases are

| base | release | `CapabilityStatement.fhirVersion` | resource named in `metadata` |
|---|---|---|---|
| `/baseDstu2` | DSTU2 | `1.0.2` | `Conformance` |
| `/baseDstu3` | STU3 | `3.0.2` | `CapabilityStatement` |
| `/baseR4` | R4 | `4.0.1` | `CapabilityStatement` |
| `/baseR4B` | R4B | `4.3.0` | `CapabilityStatement` |
| `/baseR5` | R5 | `5.0.0` | `CapabilityStatement` |

Everything the earlier milestones require of a base is required of these two:
CRUD with `ETag`/`If-Match`/`If-None-Exist`, `_history`, the whole search
grammar with that release's own `SearchParameter` definitions, `_include` and
`_revinclude`, `_sort`, `_summary`, `_elements`, transactions and batches,
`$validate` against that release's `StructureDefinition`s, JSON and XML in both
directions, and a `CapabilityStatement` that describes what the base actually
does. A failed transaction, and a Bundle posted to a base that is neither a
`transaction` nor a `batch`, are answered `400` with an `OperationOutcome`. Each base has its **own store**: a resource written to `/baseR4` is not
visible at `/baseR5`, and the ids are independent.

The media types follow the release. R4B and R5 both use
`application/fhir+json` and `application/fhir+xml`; a request that asks for
`application/json+fhir` — the DSTU2 spelling — is not what these bases answer
with.

### 1.3 The resource shapes that changed

R4B is a small release: it keeps R4's resource set, adds `SubscriptionTopic`,
`SubscriptionStatus`, `Citation`, `Evidence`, `EvidenceReport` and
`EvidenceVariable`, and reshapes a handful of elements. R5 is a large one: it
renames elements, changes types, moves cardinalities, replaces several
`Reference(x)`/`CodeableConcept` pairs with `CodeableReference`, and adds and
removes resource types outright.

None of this is a list to memorise. **The definitions are the specification**:
each release's `StructureDefinition`s are in its own package, and the server
already reads them for R4. A base that reads its own release's definitions gets
the changed shapes for free; a base that reuses R4's definitions answers R4's
questions at an R5 endpoint. Each base must answer for its own release —
including for instances built from the elements the releases disagree about, and
for the same instance posted to `$validate` at more than one base.

The same holds for search: a `SearchParameter` one release defines may be absent
in another, the expression behind a name can change between releases, and a
parameter a release does not define is an error at that base, not an ignored
parameter.

### 1.4 Topic-based subscriptions

A topic-based subscription says "notify me when something interesting happens",
where *interesting* is defined by a `SubscriptionTopic` resource rather than by a
search string. Both new releases support it, and **they express it differently** —
that difference is the substance of this milestone.

**`SubscriptionTopic`** is a resource in both releases. Store, read, update and
search it like any other. The parts that matter here:

* `url` — the canonical a `Subscription` names.
* `status`, `title`, `description`.
* `resourceTrigger`, repeating: `resource` (the type it watches),
  `supportedInteraction` (`create`, `update`, `delete`), and
  `queryCriteria` with `previous`, `current`, `resultForCreate`,
  `resultForDelete` and `requireBoth`. A trigger fires when the interaction is
  one of `supportedInteraction` **and** `queryCriteria` says so: `current` is a
  search expression the resource after the interaction must match, `previous` one
  the resource before it must match, and `requireBoth` says whether both are
  needed.
* `canFilterBy`, repeating: `resource`, `filterParameter` and
  `comparator`/`modifier` — the filters a `Subscription` on this topic may use.
* `notificationShape`, repeating: `resource`, `include`, `revInclude` — what a
  notification for that resource carries when the content level asks for it.

**A `Subscription` at `/baseR5`** is the native shape:

```jsonc
{ "resourceType": "Subscription", "status": "requested",
  "topic": "http://example.org/FHIR/SubscriptionTopic/admission",
  "filterBy": [ { "resourceType": "Encounter",
                  "filterParameter": "patient", "value": "Patient/p1" } ],
  "channelType": { "system": "http://terminology.hl7.org/CodeSystem/subscription-channel-type",
                   "code": "rest-hook" },
  "endpoint": "http://127.0.0.1:PORT/notify",
  "contentType": "application/fhir+json",
  "content": "id-only",
  "heartbeatPeriod": 60, "maxCount": 20 }
```

**A `Subscription` at `/baseR4B`** says the same thing through the published
backport IG, because R4B's `Subscription` is still R4's resource:

* `Subscription.criteria` holds **the topic canonical**, not a search string.
* each filter is an extension on `criteria`:
  `.../StructureDefinition/backport-filter-criteria`, `valueString` holding
  `<Resource>?<parameter>=<value>`.
* `Subscription.channel.type` is `rest-hook`, `channel.endpoint` the URL,
  `channel.payload` the content type, with the content level in the extension
  `.../StructureDefinition/backport-payload-content` (`empty`, `id-only`,
  `full-resource`) on `channel.payload`.
* `.../StructureDefinition/backport-heartbeat-period` (`valueUnsignedInt`) and
  `.../StructureDefinition/backport-max-count` (`valuePositiveInt`) sit on
  `channel`.

The package that defines all of these is installed with the others; read the
definitions rather than trusting the sketch above.

**Lifecycle, both bases.** A subscription is created with `status` `requested`.
The server sends a **handshake** notification to the endpoint; when the endpoint
accepts it the subscription becomes `active`. An endpoint that cannot be reached
leaves the subscription's `status` at `error`; R4B records the reason in
`Subscription.error`, R5 has no such element and reports it in the
`SubscriptionStatus.error` that `$status` returns. `status` may be set to `off` by an
update, and an `off` subscription notifies nothing.

### 1.5 Notifications

When a stored resource matches a topic trigger of an `active` subscription, the
server **POSTs a notification bundle to the endpoint** with the negotiated
content type.

* At `/baseR5` the bundle's `type` is **`subscription-notification`**.
* At `/baseR4B` it is **`history`** — R4B's `Bundle.type` has no
  `subscription-notification` code.
* In both, `entry[0].resource` is a **`SubscriptionStatus`** carrying `status`,
  `type` (the `SubscriptionStatus` code list: `handshake`,
  `event-notification`, `query-status`, `query-event`, and `heartbeat` when the
  server sends one), `eventsSinceSubscriptionStart`, `subscription`,
  `topic`, and one `notificationEvent` per event with `eventNumber`, `timestamp`
  and `focus`. Notifications are sent when an event matches; this milestone
  does not ask the server to send `heartbeat` notifications on a timer, and
  nothing here depends on them.
* `entry[0].fullUrl` is absent — the `SubscriptionStatus` is not a stored
  resource — and the remaining entries depend on the content level:
  **`empty`** adds nothing, **`id-only`** adds an entry per focus whose
  `fullUrl` is the resource's absolute URL and which carries a `request` but no
  `resource`, and **`full-resource`** adds the resource itself.
* `eventNumber` starts at 1 for the first event of a subscription and increases
  by one per event, never repeating and never skipping. A handshake carries no
  `notificationEvent` and does not consume a number.

The endpoint is hosted outside the server, so the server must make an ordinary
outbound HTTP/1.1 `POST` — `Content-Type` per `contentType`, a `Host` header, a
correct `Content-Length`, and the connection closed or kept alive consistently
with what it announces. A notification the endpoint cannot parse counts as not
delivered.

### 1.6 The two operations

Both bases answer, per the published `OperationDefinition`s (R5's
`Subscription-status` and `Subscription-events`; the backport IG's
`backport-subscription-status` and `backport-subscription-events` at R4B):

* **`GET [base]/Subscription/[id]/$status`** — a `searchset` `Bundle` of
  `SubscriptionStatus` resources, `type` `query-status`, one per subscription.
  `GET [base]/Subscription/$status?ids=a,b` reports several; `?status=active`
  filters by status.
* **`GET [base]/Subscription/[id]/$events`** — the events this subscription has
  already sent, as a notification bundle whose `SubscriptionStatus.type` is
  `query-event`. The operation's `eventsSinceNumber` and `eventsUntilNumber`
  parameters bound the range, both inclusive; `content` overrides the
  subscription's own content level for this response only.

An id that does not exist is `404`; an operation on a base that does not define
it is `404`, not a silent success.

## 2. Read these local references

**Normative semantics.** `/app/SPEC/R4B/` and `/app/SPEC/R5/` hold each
release's own `http.html`, `search.html`, `resourcelist.html`, `bundle.html`,
`valueset-bundle-type.html`, `subscription.html`, `subscriptiontopic.html`,
`subscriptionstatus.html` and `capabilitystatement.html` (R5 also has
`subscriptions.html`, the framework page R4B does not publish), beside the
DSTU2, STU3 and R4 pages the earlier milestones use. `/app/SPEC/backport/` holds
the subscriptions backport IG's `index.html`, `components.html` and
`notifications.html`, which are where the R4B shape is defined.

**Machine-readable definitions.** This milestone's packages are in
`/app/packages/` as tarballs; install them before you start:

```sh
sh /app/packages/install.sh          # unpacks into /root/.fhir/packages/<name>#<version>
```

They are `hl7.fhir.r4b.core#4.3.0`, `hl7.fhir.r5.core#5.0.0` and
`hl7.fhir.uv.subscriptions-backport#1.1.0`, beside the packages already
installed. Each declares the FHIR versions it is for in its own `package.json`;
a package that does not declare this base's version is not this base's package,
and the backport IG declares `4.3.0` alone. The same packages are installed at
the same path wherever the server runs.

**Sample data and local checks.** `/app/testdata/seed-r4b/` and
`/app/testdata/seed-r5/` hold each release's published example instances, and
`/app/testdata/topics/` a set of `SubscriptionTopic` resources.
`/app/tools/fhirconf/` holds this milestone's runner. It hosts the notification
receiver itself, so `--subscriptions` measures delivery rather than assuming it;
its `README.md` says how to use it.

### Checking progress

```sh
sh /app/packages/install.sh
make -C /app/src
SERVER_PORT=8080 BASE_URL=http://127.0.0.1:8080 \
  INDEX_DB_DIR=/tmp/d/index TRANSACTION_DB_DIR=/tmp/d/tx RESOURCE_DB_DIR=/tmp/d/res \
  /app/bin/blaze &
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --validate
python3 /app/tools/fhirconf/run_conformance.py --root http://127.0.0.1:8080 --subscriptions
```

The runner compares with answers taken from a reference FHIR server at each
release and from the official HL7 validator. The local cases are representative,
not exhaustive, and the requirements of the earlier milestones still apply, so a
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

# How the control plane is built, started and driven

Everything in this document is upstream Kubernetes' own interface: every flag,
path and format below is the one the release itself uses.

## 1. Build

`make` in `/app` must produce, exactly as upstream's own build does:

```
/out/bin/kube-apiserver
/out/bin/kube-controller-manager
/out/bin/kube-scheduler
```

If your Makefile compiles through object files, give it header dependencies
(`-MMD -MP` and an `-include` of the generated `.d` files) or rebuild from clean.
A later session inherits this workspace and its build, and the work these steps
ask for changes shared headers; an object left over from before a struct changed
links without complaint and misbehaves at run time in ways that look like a
serialisation bug. Compiling each component in one command sidesteps it.

`/out` is outside the workspace on purpose. `/app` is carried from one session
to the next as a tar of its own contents, and that transfer is reliable only for
text. The workspace convention is therefore: `/app` holds sources, manifests and
documents; every build product goes under `/out`. Compiling straight to the
executables leaves nothing else behind.

The build runs with GNU make and a C toolchain only. The three binaries are
dynamically linked and may link **only** against:

```
libc.so.6  libm.so.6  libpthread.so.0  libdl.so.2  librt.so.1  libgcc_s.so.1
libssl.so.3  libcrypto.so.3  libssl.so.1.1  libcrypto.so.1.1
ld-linux-x86-64.so.2  linux-vdso.so.1
```

Any other shared object, or a statically linked binary, breaks the build-provenance
rule, and so does a pre-built **executable or shared object** committed anywhere
under `/app` -- the components have to be built here, from source in this
workspace, not shipped in. Everything else is plain C11 source. A component must
be that program and nothing more, not a launcher for another: it must not fetch,
decode, unpack or `exec` a second implementation at run time, nor proxy the API
to one. The process that answers the API is the binary this build produced from
the workspace's C sources. The build and every component run as an
unprivileged user, not root: build output goes under `/out`, scratch files
belong under `/tmp` (`$HOME` is not guaranteed to exist or be writable), and
nothing should need to write a root-owned path.

Two more provenance rules follow from that, and apply the same way: no
component may be larger than **25 MiB**, and none may contain the markers of a Go
build (`k8s.io/kubernetes`, `go.buildid`, `runtime.gcWriteBarrier`,
`golang.org/x/net`); either one marks an upstream binary shipped in rather than a
control plane built here. A C build of this workspace is a small fraction of that
size, including the embedded OpenAPI document.

## 2. Starting the components

The API server is started with this flag set, plus the `--runtime-config` the
current step's instruction names when it names one, and nothing else:

```
kube-apiserver \
  --etcd-servers=http://127.0.0.1:<port> \
  --secure-port=<port> --bind-address=127.0.0.1 \
  --tls-cert-file=<path> --tls-private-key-file=<path> \
  --client-ca-file=<path> \
  --token-auth-file=<path> \
  --authorization-mode=RBAC \
  --storage-media-type=application/json \
  --service-account-key-file=<path> \
  --service-account-signing-key-file=<path> \
  --service-account-issuer=https://kubernetes.default.svc \
  --api-audiences=https://kubernetes.default.svc
```

The controller manager and the scheduler are started with

```
kube-controller-manager --kubeconfig=<path> --leader-elect=false
kube-scheduler          --kubeconfig=<path> --leader-elect=false
```

`--kubeconfig` points at a `clientcmd` v1 file (`apiVersion: v1`, `kind: Config`)
naming the API server and a bearer token.

`--token-auth-file` is upstream's static token CSV:
`token,user,uid[,"group1,group2"]`. A request with no `Authorization: Bearer`
header, or an empty token, is the anonymous user `system:anonymous` in group
`system:unauthenticated`; a bearer token that is not in the file is answered
`401`. Every authenticated user is additionally in `system:authenticated`.

The API server serves HTTPS only, on `--secure-port`.

## 3. Bootstrap content

A freshly started API server has the namespaces upstream creates --
`default`, `kube-system`, `kube-public`, `kube-node-lease`, each labelled
`kubernetes.io/metadata.name: <name>` -- and the default RBAC objects the
`system:masters`, `system:authenticated` and `system:unauthenticated` groups rely
on: `cluster-admin`, `system:basic-user`, `system:discovery` and
`system:public-info-viewer`, with the matching `ClusterRoleBinding`s.

The two discovery roles each grant `get` on non-resource URLs in a single rule,
listed in this order:

```
system:discovery            /api /api/* /apis /apis/* /healthz /livez /openapi /openapi/* /readyz /version /version/
system:public-info-viewer   /healthz /livez /readyz /version /version/
```

An authenticated user holds both. Its `SelfSubjectRulesReview` therefore lists two
`nonResourceRules` entries, and `kubectl auth can-i --list` prints each URL that
the two rules share on two rows.

## 4. Storage

Objects are persisted to the etcd server named by `--etcd-servers`, as JSON
(`--storage-media-type=application/json`), under upstream's own key layout:

```
/registry/<storage-prefix>/<namespace>/<name>     namespaced resources
/registry/<storage-prefix>/<name>                 cluster-scoped resources
```

where `<storage-prefix>` is the upstream prefix for that resource -- the plural
name for most resources, and specifically `minions` for nodes,
`services/specs` for services, `services/endpoints` for endpoints, and
`apiextensions.k8s.io/customresourcedefinitions` for CRDs. State survives an API
server restart because it is in etcd, not in the process.

`metadata.resourceVersion` is the etcd revision of the object, as a decimal
string, and a list's `metadata.resourceVersion` is the store's current revision.

## 5. What drives the control plane

* Plain HTTPS requests to the API, as any Kubernetes client makes them.
* The `kubectl` that ships with the Kubernetes release the current step names,
  through a kubeconfig. Each client is installed as `/usr/local/bin/kubectl-1.NN`
  and `/usr/local/bin/kubectl` is the 1.25 one; the conformance runner
  (`/app/conformance/run.sh`) picks the right client for its release through
  `KCP_KUBECTL`.
* `etcdctl` against the same etcd server, to read back the key layout.
* A node agent standing in for the kubelet: it creates `Node` objects, writes
  their `status` (capacity, allocatable, conditions), moves pod `status.phase`
  and conditions, and confirms graceful pod deletions. It never touches `spec`.

### Ceilings

`make -C /app` must finish within **900 seconds**; a build still running then is
stopped and the tree is left unbuilt. A complete implementation builds in about
18 seconds.

Every process started after the build -- etcd and the three components -- runs
in its own process group under a watchdog. A component whose resident set passes
**2048 MiB** is killed, so is every one of them if their total passes **8192 MiB**,
and the whole run is stopped if it passes **2100 seconds** of wall clock. A
complete implementation replays the whole corpus in about 80 seconds and uses a
few tens of MiB per component.

The memory ceilings sit below the container's own 16 GiB, so when a component runs
away the cases it did not answer fail rather than the container going down with
it; the two time ceilings together sit below the step's own timeout, so a build or
a run that never finishes still produces a result that says so. The conformance
runner applies these process ceilings; `KCP_RSS_LIMIT_MB`, `KCP_RSS_TOTAL_MB` and
`KCP_RUN_LIMIT_SEC` in its environment set them.

## 6. Comparison rules

Responses must match upstream's after normalising what no implementation can reproduce:
`metadata.uid`, `creationTimestamp`, `resourceVersion`, `managedFields`,
`deletionTimestamp`, `generateName`-derived names, timestamps inside conditions,
allocated addresses and ports, the random suffix of the projected
`kube-api-access-*` volume, `pod-template-hash` and `controller-uid` label
values, the `?hash=` query in `/openapi/v3` URLs, and the free-text `message`
of `metav1.Status` and of `SubjectAccessReview`/`TokenReview` answers. The one
exception is the error text the release's own `kubectl` prints to stderr (for
example `Error from server (NotFound): configmaps "nope" not found`): that output
must match verbatim, so a `metav1.Status` `message` that `kubectl` surfaces must
be upstream's.
Two more values are neutralised because they are not determined by the request:
an object's own `spec.nodeName` (or an endpoint address's `nodeName`) -- which of
several equally suitable nodes the scheduler picks is a tie-break, and the
scheduler cases assert placement only where exactly one node is suitable -- and
the pod-template hash in a `/registry/replicasets/<ns>/<name>-<hash>` key.
Everything else -- status codes, `reason`, `details.causes[].reason` and
`details.causes[].field`, defaulted fields, discovery attributes, watch event
types and objects, controller and scheduler outcomes -- must match exactly.

## 7. This release's behaviour and the regression surface

The control plane that is started is the one built from this workspace's sources,
exactly as section 1 describes. A tree that does not compile provides none of the
behaviour below, so keep it buildable as you go rather than only at the end.

Its answers must match, under the rules in section 6, recordings of the
release's own `kube-apiserver`, `kube-controller-manager` and `kube-scheduler`.
The corpus in `/app/conformance` is a representative sample of that behaviour,
not an exhaustive one.

Each case belongs to one of two sets, decided by comparing this release's
recording with the **previous** release's recording of the same case:

* **this release's own behaviour** -- the cases the previous release answered
  differently, or did not have at all;
* **the chain's regression surface** -- everything else, which the previous
  release already answered the same way.

Both must hold. Carrying the previous release's work forward unchanged is not
enough: the release's own changes have to be made, and the compatibility clause
means the regression surface has to keep working. For the first step, which has
no previous release, every case is the step's own behaviour.

## 8. Response details

Upstream's answers carry a few details that the OpenAPI document and the
API-concepts page leave implicit. Each is the release's own behaviour, and each
must match exactly under section 6.

**Core discovery documents.** The core group's documents carry no `apiVersion`:
`/api` answers an `APIVersions` and `/api/v1` an `APIResourceList` with
`groupVersion: v1`, and neither has an `apiVersion` key. Everything under `/apis`
-- the `APIGroupList` and each `/apis/<group>/<version>` `APIResourceList` --
carries `"apiVersion": "v1"`.

**`Status` bodies.**

* Creating a `Binding`, through a pod's `binding` subresource or through the
  namespace's `bindings` resource, is answered `201` with
  `{"kind": "Status", "apiVersion": "v1", "metadata": {}, "status": "Success", "code": 201}`.
  The `Success` `Status` of an immediate delete, by contrast, carries `details`
  and no `code`.
* A verb that a resource's discovery entry does not list is answered `405` with
  `reason: MethodNotAllowed` and empty `details`. `bindings` lists only `create`,
  so a `GET` of a namespace's `bindings` is such a request.
* A delete whose `preconditions` do not hold -- a `preconditions.uid` other than
  the stored object's -- is answered `409` with `reason: Conflict`, and its
  `details.kind` is the object's kind (`ConfigMap`). Other `Status` errors,
  `AlreadyExists` among them, name the resource there instead (`configmaps`).
* A `Status` `message` that names a resource, as the `NotFound` and
  `AlreadyExists` texts `kubectl` prints do, uses upstream's qualified resource
  name: the plural alone in the core group (`configmaps "nope" not found`) and
  `<plural>.<group>` in every other group (`deployments.apps "web" already exists`).

**Validation causes.** `details.causes` must match as a whole list. Order does not
matter, but a cause that upstream reports twice must appear twice, and an extra
or a missing cause fails. Upstream reports these, each cause written here as
`{field, reason}`:

* an update that adds a finalizer to an object that already has a
  `deletionTimestamp`: `{metadata.finalizers, FieldValueForbidden}`, twice;
* a `Deployment` with no `spec.selector`: `{spec.selector, FieldValueRequired}`
  and `{spec.template.metadata.labels, FieldValueInvalid}`;
* a `Service` port outside 1-65535 with no `targetPort` given:
  `{spec.ports[i].port, FieldValueInvalid}` and, because `targetPort` defaults to
  the port, `{spec.ports[i].targetPort, FieldValueInvalid}`;
* a `Job` whose pod template sets `restartPolicy: Always`, which a Job does not
  accept (only `OnFailure` and `Never`): `{spec.template.spec.restartPolicy, FieldValueRequired}`;
* a container whose request for a resource exceeds its limit for that resource:
  `{spec.containers[i].resources.requests, FieldValueInvalid}`;
* a `PersistentVolumeClaim` access mode that upstream does not define:
  `{spec.accessModes, FieldValueNotSupported}`;
* a `Role` rule whose `verbs` list is empty: `{rules[i].verbs, FieldValueRequired}`;
* a `Secret` of type `kubernetes.io/dockercfg` whose `data` has no `.dockercfg`
  key: `{data[.dockercfg], FieldValueRequired}`;
* a `CustomResourceDefinition` in which no version has `storage: true`:
  `{spec.versions, FieldValueInvalid}` and `{status.storedVersions, FieldValueInvalid}`;
* a custom resource whose value at its CRD's `scale` `specReplicasPath` is not an
  integer: the schema's type cause at that field (`{spec.size, FieldValueTypeInvalid}`)
  and a `FieldValueInvalid` cause whose `field` is the `specReplicasPath` exactly as
  the CRD writes it, leading dot included (`{.spec.size, FieldValueInvalid}`).

**Serialised fields.**

* An `EndpointSlice` always carries `ports`: a slice written without it reads back
  with `"ports": null`.
* A `TokenReview` is answered `201` with `metadata: {}` and the submitted `spec`.
  When the token does not authenticate, `status` is `{"user": {}}`:
  `authenticated` is omitted, not `false`. An access review's `status.allowed`,
  by contrast, is present when it is `false`, as in a denied
  `SelfSubjectAccessReview`.
* A `LocalSubjectAccessReview` is answered with the submitted `metadata`
  (`namespace` included) and `spec`, plus `status`.
* A `SelfSubjectRulesReview` is answered with `"metadata": {}` and `"spec": {}`:
  the `spec.namespace` it asked about is not echoed. Its `status` holds
  `incomplete` and one `resourceRules` or `nonResourceRules` entry for each rule
  of each role that applies, without merging them.

**Tables.** `kubectl get` asks the server for a `meta.k8s.io/v1` `Table` (see
"Receiving resources as Tables" on the API-concepts page). A `ConfigMap`'s
`Table` is not the default name-and-age one: its columns are `Name`, `Data` (the
number of keys in `data` plus `binaryData`) and `Age`, which `kubectl` prints as
`NAME   DATA   AGE`.

# Step 1 -- a Kubernetes 1.25 control plane in C

## 1. Implement this milestone

Build, in C11, the three Kubernetes control-plane components for **Kubernetes
1.25** -- `kube-apiserver`, `kube-controller-manager` and `kube-scheduler` -- storing
their objects in a real **etcd v3** server, and serve the Kubernetes API for the
group-versions and resources listed in `/app/SPEC/REQUIRED-API-SURFACE.json`.

The API you must serve is Kubernetes' own, exactly as the release publishes it:
the paths, query parameters, media types, request and response kinds, defaulting,
validation, admission, RBAC, watch semantics and discovery documents that the
1.25 OpenAPI specification in `/app/SPEC/openapi/swagger.json` and the
API-concepts reference describe. `/app/SPEC/TEST_INTERFACE.md` fixes the parts of
that interface the control plane is driven through -- the component flags, the build output paths,
the required admission plugins, the required controllers, the scheduler's
predicates, the etcd key layout, the default roles' rules, and the response
details that the OpenAPI document and the API-concepts page leave implicit -- and
nothing beyond it is assumed.

Concretely, this milestone must provide:

* **API machinery** -- REST for every required resource and subresource: `get`,
  `list`, `watch`, `create`, `update`, `patch`, `delete`, `deletecollection`;
  `labelSelector`, `fieldSelector`, `limit`/`continue` pagination,
  `resourceVersion`, `allowWatchBookmarks`, `dryRun`, `gracePeriodSeconds`,
  `propagationPolicy`, `fieldValidation`; the three patch content types
  (`application/json-patch+json` per RFC 6902, `application/merge-patch+json`
  per RFC 7386, and `application/strategic-merge-patch+json` with upstream's
  merge keys and `$patch` directives); `metav1.Status` error bodies carrying upstream's `code`, `status`,
  `reason` and, for `Invalid`, `details.causes[].reason` and `details.causes[].field`;
  server-side defaulting and validation for every required kind; conversion
  between every served version of a resource; `metadata.uid`,
  `creationTimestamp`, `resourceVersion`, `generation`, `ownerReferences`,
  `finalizers` and `deletionTimestamp` semantics.
* **Discovery and OpenAPI** -- `/api`, `/apis`, `/api/v1`, `/apis/<group>/<version>`,
  where the root documents are **complete**: `/apis` returns an `APIGroupList` naming
  every group the server serves, and `/openapi/v3` a root whose `paths` holds an
  entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and
  one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are
  compared in full against the release's own output,
  so a root that covers only part of the surface fails even when each individual
  group-version answers correctly,
  `/version`, `/healthz`, `/openapi/v2`, `/openapi/v3` and `/openapi/v3/<path>`,
  serving the required surface. A discovery request whose `Accept` header asks
  only for a media type this server cannot produce is answered `406` with a
  `metav1.Status`, served as `application/json` -- an `Accept` of
  `application/json;g=apidiscovery.k8s.io;v=v2beta1;as=APIGroupDiscoveryList`
  with no alternative, for instance, since this release serves no aggregated
  discovery at all. A header that also offers `application/json` gets the
  ordinary document.
* **Watch** -- `?watch=true` streaming `ADDED`/`MODIFIED`/`DELETED` as
  newline-delimited `WatchEvent` JSON, honouring `resourceVersion` (including `0`,
  which replays the current set before streaming) and `labelSelector` /
  `fieldSelector`; `allowWatchBookmarks=true` is accepted.
* **Authentication and RBAC** -- static bearer tokens from `--token-auth-file`, the
  anonymous user, the `system:authenticated` / `system:unauthenticated` groups, the
  default `system:*` roles and their bindings, the `SubjectAccessReview` family, and
  privilege-escalation prevention on `Role`/`ClusterRole` writes.
* **Admission** -- `NamespaceLifecycle`, `ServiceAccount` (including the projected
  `kube-api-access-*` volume and `automountServiceAccountToken`), `Priority`,
  `DefaultTolerationSeconds`, `StorageObjectInUseProtection` and `PodSecurity`
  (GA in 1.25), enforcing the `pod-security.kubernetes.io/enforce` namespace label
  at the `baseline` and `restricted` levels.
* **CustomResourceDefinitions** -- `apiextensions.k8s.io/v1` with structural
  schemas: pruning of unknown fields, schema defaults, `required`, `enum`, type,
  `minimum`/`maximum`, the `status` and `scale` subresources, and the custom
  resources appearing in discovery.
* **Unknown fields** -- a field the kind's schema does not describe is dropped on
  write and is absent when the object is read back, unless the request asks for
  `fieldValidation=Strict`, which refuses it instead. A Pod volume whose only
  source is such a field therefore ends up with no source at all, and is stored
  with the default one, `emptyDir: {}`, while the `volumeMounts` entry that
  refers to it stays.
* **Job bookkeeping** -- a `Job` created without a selector gets one generated from its
  own uid, and the Job, its pod template and the pods it creates carry the labels
  upstream stamps on them, including the `batch.kubernetes.io/job-tracking`
  annotation this release adds to every Job.
* **Controllers** -- `serviceaccount` (the `default` ServiceAccount in every active
  namespace), `deployment`, `replicaset` (including adoption of matching orphans),
  `job`, `namespace` (content deletion and the `kubernetes` finalizer),
  `garbagecollector` (owner-reference cascade plus the `Orphan` propagation policy)
  and `endpoint`/`endpointslice`.
* **Scheduler** -- bind unscheduled pods through the `pods/binding` subresource
  honouring `spec.nodeName`, `spec.nodeSelector`, required node affinity,
  taints and tolerations, and CPU capacity; and report an
  unschedulable pod with a `PodScheduled=False` condition whose reason is
  `Unschedulable`.

Manifests written against the 1.25 API must apply unchanged.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` -- the OpenAPI specification this
  release publishes: every type, field, path and operation id.
* `/app/SPEC/REQUIRED-API-SURFACE.json` -- the group-versions and resources to
  serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` -- how the control plane is built, started and
  driven, the etcd key layout, the link-set rule, and the upstream response
  details (section 8) that are compared exactly.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy:
  `/opt/docs/kubernetes-docs/1.25/api-concepts.md`) and the
  `kube-apiserver` / `kube-controller-manager` / `kube-scheduler` reference pages
  (local copies: `/opt/docs/kubernetes-docs/1.25/kube-apiserver.md`,
  `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` -- a conformance corpus recorded from Kubernetes 1.25 and a
  runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three
  components, replays the corpus and prints per-area results. It is a
  representative sample of those areas, not an exhaustive one.
* `/app/WORKSPACE_HANDOFF.md` -- write what a later session needs to know.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and handoff files. Do not create a prose answer or
  alternate entry point unless Section 1 explicitly requires one.
- Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; work only from this
  workspace, its documentation and `/opt/docs`, and do not search the
  system for other implementations or answers.
- Run the local checks named by the references and leave a reproducible
  handoff for the next fresh session.

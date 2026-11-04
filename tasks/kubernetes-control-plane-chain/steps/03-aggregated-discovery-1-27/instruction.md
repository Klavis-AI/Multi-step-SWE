# Step 03 — Kubernetes 1.27

## 1. Implement this milestone

The control plane in this workspace serves the Kubernetes 1.26 API. Move it to **Kubernetes 1.27**.

`/app/SPEC/REQUIRED-API-SURFACE.json` has been replaced with 1.27's surface and `/app/SPEC/openapi/swagger.json` with that release's published OpenAPI specification. The served group-versions, resources and subresources must match the new surface exactly — every entry it lists is reachable, and every entry that is gone answers as an absent one does. The root discovery documents stay complete against it: `/apis` returns an `APIGroupList` naming every group this release serves, and `/openapi/v3` a root whose `paths` holds an entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are compared in full, so a root that covers only part of the surface fails even when each individual group-version answers correctly.

**No longer served** (a request to one of these is `404`, and it is absent from discovery):

* `storage.k8s.io/v1beta1/csistoragecapacities`

**Newly served**:

* `admissionregistration.k8s.io/v1alpha1/validatingadmissionpolicies/status` — only when the API server is started with `--runtime-config=admissionregistration.k8s.io/v1alpha1=true`

**Aggregated discovery.** `/api` and `/apis` answer an `Accept` header of `application/json;g=apidiscovery.k8s.io;v=v2beta1;as=APIGroupDiscoveryList` with an `apidiscovery.k8s.io/v2beta1` `APIGroupDiscoveryList` — one entry per group, each with its versions, each version listing its resources with `resource`, `responseKind`, `scope`, `singularResource`, `verbs`, and nested `subresources`. A `responseKind` names only the `kind` — its `group` and `version` are the empty string — when the kind is served by the group-version that contains it; a subresource whose response is a kind from another group, such as `scale` returning `autoscaling/v1` `Scale`, names that group and version. The unaggregated `APIVersions`, `APIGroupList` and `APIResourceList` answers are still served when the header is absent; from this release each top-level resource in an `APIResourceList` carries its `singularName`, while subresources keep the empty string, exactly as the surface lists them. The release's own `kubectl` negotiates this format for both `/api` and `/apis`, so every client operation depends on it.

**`storage.k8s.io/v1beta1` is gone**, taking `CSIStorageCapacity` with it.

**`ValidatingAdmissionPolicy` gains `status`** at `admissionregistration.k8s.io/v1alpha1`.

**Deployment validation names its own group again.** A `Deployment` rejected by validation reports `details.group: "apps"`, not the `extensions` this field carried at 1.26.

**Job selectors use the prefixed label only.** The selector the server generates for a `Job` that supplies none is `spec.selector.matchLabels: {"batch.kubernetes.io/controller-uid": "<uid>"}`. The Job's own labels and its pod template's labels still carry all four of `controller-uid`, `job-name`, `batch.kubernetes.io/controller-uid` and `batch.kubernetes.io/job-name`.

**Every authenticated user may review itself.** The bootstrapped `system:basic-user` ClusterRole grants `create` on `authentication.k8s.io` `selfsubjectreviews` in addition to `authorization.k8s.io` `selfsubjectaccessreviews` and `selfsubjectrulesreviews`, so a `SelfSubjectRulesReview` lists that rule for an ordinary user.

**A binding says what a denial does.** `ValidatingAdmissionPolicyBinding` gains `spec.validationActions`, which round-trips as written; policies are still not enforced at this release.

**An image volume source is unknown here.** A Pod whose `spec.volumes[]` carries an `image` source is accepted, the unrecognised field is pruned, and the volume is left with the default source, `emptyDir: {}`; the mount that refers to it stays.

**Container resources are not mutable through the Pod itself.** A patch to `spec.containers[].resources` sent to the Pod object is rejected with HTTP 422, `reason: Invalid` and `details.causes` holding the single entry `{field: spec, reason: FieldValueForbidden}`.

Everything the workspace already does must keep working: the REST, discovery, OpenAPI, watch, RBAC, admission, CRD, controller and scheduler behaviour of the previous release must still hold in full, and the behaviours this release changes must change. Manifests written for 1.25 still apply wherever the API is still served.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` — the OpenAPI specification this release publishes.
* `/app/SPEC/REQUIRED-API-SURFACE.json` — the group-versions and resources to serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` — how the control plane is built, started and driven, the etcd key layout, the link-set rule, and the upstream response details (section 8) that are compared exactly. Unchanged.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy: `/opt/docs/kubernetes-docs/1.27/api-concepts.md`) and the component reference pages (local copies: `/opt/docs/kubernetes-docs/1.27/kube-apiserver.md`, `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` — the conformance corpus recorded from Kubernetes 1.27 and a runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three components, replays the corpus and prints per-area results. It is a representative sample of those areas, not an exhaustive one.
* `/app/WORKSPACE_HANDOFF.md` — what the previous session left behind; keep it current.

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

# Step 08 — Kubernetes 1.32

## 1. Implement this milestone

The control plane in this workspace serves the Kubernetes 1.31 API. Move it to **Kubernetes 1.32**.

`/app/SPEC/REQUIRED-API-SURFACE.json` has been replaced with 1.32's surface and `/app/SPEC/openapi/swagger.json` with that release's published OpenAPI specification. The served group-versions, resources and subresources must match the new surface exactly — every entry it lists is reachable, and every entry that is gone answers as an absent one does. The root discovery documents stay complete against it: `/apis` returns an `APIGroupList` naming every group this release serves, and `/openapi/v3` a root whose `paths` holds an entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are compared in full, so a root that covers only part of the surface fails even when each individual group-version answers correctly.

**No longer served** (a request to one of these is `404`, and it is absent from discovery):

* `admissionregistration.k8s.io/v1alpha1/validatingadmissionpolicies`
* `admissionregistration.k8s.io/v1alpha1/validatingadmissionpolicies/status`
* `admissionregistration.k8s.io/v1alpha1/validatingadmissionpolicybindings`
* `coordination.k8s.io/v1alpha1/leasecandidates`
* `flowcontrol.apiserver.k8s.io/v1beta3/flowschemas`
* `flowcontrol.apiserver.k8s.io/v1beta3/flowschemas/status`
* `flowcontrol.apiserver.k8s.io/v1beta3/prioritylevelconfigurations`
* `flowcontrol.apiserver.k8s.io/v1beta3/prioritylevelconfigurations/status`

**Newly served**:

* `coordination.k8s.io/v1alpha2/leasecandidates` — only when the API server is started with `--runtime-config=coordination.k8s.io/v1alpha2=true`

**API Priority and Fairness finishes its migration.** `flowcontrol.apiserver.k8s.io/v1beta3` is removed; only `v1` remains.

**`admissionregistration.k8s.io/v1alpha1` is gone.** `ValidatingAdmissionPolicy` is served at `v1` and, behind `--runtime-config`, at `v1beta1`.

**In-place pod resize is not here yet.** The fields are in the published schema, but the feature is off: a write to a pod's `resize` subresource is a `404`, and a Pod's `spec` is still immutable apart from the fields upstream has always allowed to change (a container's `image`, `spec.tolerations`, `spec.activeDeadlineSeconds` and `spec.terminationGracePeriodSeconds`).

**`coordination.k8s.io/v1alpha2`** replaces `v1alpha1` for `leasecandidates`.

**A Pod's QoS class is fixed at admission.** `status.qosClass` is computed when the Pod is created and is carried forward unchanged by every later write through the `status` subresource, including one that does not mention it.

**HorizontalPodAutoscaler status per version.** The `autoscaling/v2` view of a status carries `currentMetrics` (null when there are none) and `desiredReplicas` and neither `currentReplicas` nor `conditions`; the `autoscaling/v1` view carries `currentReplicas` and `desiredReplicas` and neither `currentMetrics` nor `conditions`.

**The release's `kubectl` writes protobuf.** From 1.32 the client sends the body of a create as `Content-Type: application/vnd.kubernetes.protobuf` rather than JSON, so the server has to decode that format for the kinds a client writes this way — at least `ConfigMap`, `SelfSubjectAccessReview`, `SelfSubjectRulesReview` and `Namespace`. The envelope is the four bytes `k8s\x00` followed by a `runtime.Unknown` message (`typeMeta` = 1 with `apiVersion` = 1 and `kind` = 2, `raw` = 2, `contentEncoding` = 3, `contentType` = 4); `raw` is the object in its own generated schema, whose field numbers are published in each API group's `generated.proto` (for example `https://github.com/kubernetes/api/blob/master/core/v1/generated.proto`; local copies at this release, laid out by Go import path under `/opt/docs/kubernetes-proto/1.32/k8s.io/`, e.g. `/opt/docs/kubernetes-proto/1.32/k8s.io/api/core/v1/generated.proto`). Reading a `-f` manifest still sends JSON, and a response may still be JSON: the client honours the `Content-Type` the server answers with.

**`LeaseCandidate` moves to `coordination.k8s.io/v1alpha2`.** The `v1alpha1` version is gone -- a request for it is a `404` even when the API server is started with `--runtime-config=coordination.k8s.io/v1alpha2=true` -- and `v1alpha2` takes its place alongside `storage.k8s.io/v1beta1` and `networking.k8s.io/v1beta1`, which are still opt-in and unchanged.

**An image volume source is known but off.** A Pod whose `spec.volumes[]` carries an `image` source is accepted, and the whole volume is dropped on write along with every `volumeMounts` entry that referred to it, because the feature is not on by default at this release.

**`LeaseCandidate` validation moves with it.** At `v1alpha2` the field that names the election strategy is `spec.strategy`, and it is required: an object that carries the old `spec.preferredStrategies` instead is rejected with HTTP 422, `reason: Invalid` and a cause `{field: spec.strategy, reason: FieldValueRequired}`.

**Container resources are not mutable through the Pod itself.** A patch to `spec.containers[].resources` sent to the Pod object is rejected with HTTP 422, `reason: Invalid` and `details.causes` holding, at this release, the single entry `{field: spec, reason: FieldValueForbidden}`.

Everything the workspace already does must keep working: the REST, discovery, OpenAPI, watch, RBAC, admission, CRD, controller and scheduler behaviour of the previous release must still hold in full, and the behaviours this release changes must change. Manifests written for 1.25 still apply wherever the API is still served.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` — the OpenAPI specification this release publishes.
* `/app/SPEC/REQUIRED-API-SURFACE.json` — the group-versions and resources to serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` — how the control plane is built, started and driven, the etcd key layout, the link-set rule, and the upstream response details (section 8) that are compared exactly. Unchanged.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy: `/opt/docs/kubernetes-docs/1.32/api-concepts.md`) and the component reference pages (local copies: `/opt/docs/kubernetes-docs/1.32/kube-apiserver.md`, `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` — the conformance corpus recorded from Kubernetes 1.32 and a runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three components, replays the corpus and prints per-area results. It is a representative sample of those areas, not an exhaustive one.
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

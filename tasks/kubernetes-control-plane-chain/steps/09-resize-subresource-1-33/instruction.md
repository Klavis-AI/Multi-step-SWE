# Step 09 — Kubernetes 1.33

## 1. Implement this milestone

The control plane in this workspace serves the Kubernetes 1.32 API. Move it to **Kubernetes 1.33**.

`/app/SPEC/REQUIRED-API-SURFACE.json` has been replaced with 1.33's surface and `/app/SPEC/openapi/swagger.json` with that release's published OpenAPI specification. The served group-versions, resources and subresources must match the new surface exactly — every entry it lists is reachable, and every entry that is gone answers as an absent one does. The root discovery documents stay complete against it: `/apis` returns an `APIGroupList` naming every group this release serves, and `/openapi/v3` a root whose `paths` holds an entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are compared in full, so a root that covers only part of the surface fails even when each individual group-version answers correctly.

**Newly served**:

* `coordination.k8s.io/v1beta1/leasecandidates` — only when the API server is started with `--runtime-config=coordination.k8s.io/v1beta1=true`
* `networking.k8s.io/v1/ipaddresses`
* `networking.k8s.io/v1/servicecidrs`
* `networking.k8s.io/v1/servicecidrs/status`
* `v1/pods/resize`

**The `pods/resize` subresource.** In-place resize is driven through `PATCH /api/v1/namespaces/<ns>/pods/<name>/resize` rather than by patching the pod, and the subresource appears in discovery.

**ServiceCIDR and IPAddress reach GA** at `networking.k8s.io/v1`, served by default; `networking.k8s.io/v1beta1` is still served behind `--runtime-config` and an object written at either version reads back at the other. A create whose `metadata.name` is not a valid IP address is refused at **either** version with HTTP 422 and `details.causes[0] = {field: metadata.name, reason: FieldValueInvalid}`.

**Aggregated discovery `v2beta1` is gone.** An `Accept` of `application/json;g=apidiscovery.k8s.io;v=v2beta1;as=APIGroupDiscoveryList` on `/api` or `/apis` is answered with HTTP **406** and a `metav1.Status` served as `application/json`, not with the unaggregated document and not with an aggregated one; `v=v2` is the only aggregated version this release serves.

**`coordination.k8s.io/v1beta1`** replaces `v1alpha2` for `leasecandidates`.

**Pods carry a generation.** `metadata.generation` is set to 1 when a Pod is created and behaves as it does for the other kinds that track it.

**`resize` is the subresource that changes resources.** A Pod's `spec` is otherwise immutable, but a write to `/api/v1/namespaces/<ns>/pods/<name>/resize` may change `spec.containers[].resources`, and the response is the updated Pod. A resize that would move the Pod out of the QoS class it was created in is refused: that class is fixed for the Pod's lifetime, so a write that adds a `requests` entry to a BestEffort Pod, or that breaks the request/limit equality of a Guaranteed one, is answered with HTTP 422, `reason: Invalid` and a cause `{field: spec, reason: FieldValueInvalid}` instead of with the updated Pod.

**`spec.hostUsers` is kept.** User namespaces are on by default, so a Pod created with `spec.hostUsers: false` reads back with that field.

**`LeaseCandidate` graduates to `coordination.k8s.io/v1beta1`.** Both earlier versions, `v1alpha1` and `v1alpha2`, are absent, and `v1beta1` is served when the API server is started with `--runtime-config=coordination.k8s.io/v1beta1=true`, alongside the `storage.k8s.io/v1beta1` and `networking.k8s.io/v1beta1` opt-ins.

**The `resize` subresource starts answering.** A write to `/api/v1/namespaces/<ns>/pods/<name>/resize` is served here -- answered on its merits, whether that is the updated Pod or a validation refusal -- where every earlier release in the chain answers `404`.

**An image volume source is known but off.** A Pod whose `spec.volumes[]` carries an `image` source is accepted, and the whole volume is dropped on write along with every `volumeMounts` entry that referred to it, because the feature is not on by default at this release.

**`LeaseCandidate` keeps its matured shape.** `spec.strategy` is still required at `v1beta1`: an object carrying `spec.preferredStrategies` instead is rejected with HTTP 422 and a cause `{field: spec.strategy, reason: FieldValueRequired}`.

**Container resources are not mutable through the Pod itself.** A patch to `spec.containers[].resources` sent to the Pod object is rejected with HTTP 422, `reason: Invalid` and `details.causes` holding the single entry `{field: spec, reason: FieldValueForbidden}`; `resize` is the subresource that may change them.

**A watch from a `resourceVersion` behind the server's current one is refused.** Through 1.32 a watch whose `resourceVersion` is behind the server's current one gives up and replays the current set as `ADDED`; from 1.33 it answers a single `ERROR` event whose object is a `metav1.Status` -- the 410 `Expired` that upstream uses for a resourceVersion it will not serve from -- and ends the stream. `resourceVersion=0` is unaffected and still replays the current set before streaming.

**Both earlier `LeaseCandidate` versions are absent.** With the `--runtime-config` this step names, `coordination.k8s.io/v1alpha1` and `coordination.k8s.io/v1alpha2` answer `404`; only `v1beta1` is served.

Everything the workspace already does must keep working: the REST, discovery, OpenAPI, watch, RBAC, admission, CRD, controller and scheduler behaviour of the previous release must still hold in full, and the behaviours this release changes must change. Manifests written for 1.25 still apply wherever the API is still served.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` — the OpenAPI specification this release publishes.
* `/app/SPEC/REQUIRED-API-SURFACE.json` — the group-versions and resources to serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` — how the control plane is built, started and driven, the etcd key layout, the link-set rule, and the upstream response details (section 8) that are compared exactly. Unchanged.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy: `/opt/docs/kubernetes-docs/1.33/api-concepts.md`) and the component reference pages (local copies: `/opt/docs/kubernetes-docs/1.33/kube-apiserver.md`, `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` — the conformance corpus recorded from Kubernetes 1.33 and a runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three components, replays the corpus and prints per-area results. It is a representative sample of those areas, not an exhaustive one.
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

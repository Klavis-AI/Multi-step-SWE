# Step 07 — Kubernetes 1.31

## 1. Implement this milestone

The control plane in this workspace serves the Kubernetes 1.30 API. Move it to **Kubernetes 1.31**.

`/app/SPEC/REQUIRED-API-SURFACE.json` has been replaced with 1.31's surface and `/app/SPEC/openapi/swagger.json` with that release's published OpenAPI specification. The served group-versions, resources and subresources must match the new surface exactly — every entry it lists is reachable, and every entry that is gone answers as an absent one does. The root discovery documents stay complete against it: `/apis` returns an `APIGroupList` naming every group this release serves, and `/openapi/v3` a root whose `paths` holds an entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are compared in full, so a root that covers only part of the surface fails even when each individual group-version answers correctly.

**Newly served**:

* `coordination.k8s.io/v1alpha1/leasecandidates` — only when the API server is started with `--runtime-config=coordination.k8s.io/v1alpha1=true`
* `networking.k8s.io/v1beta1/ipaddresses` — only when the API server is started with `--runtime-config=networking.k8s.io/v1beta1=true`
* `networking.k8s.io/v1beta1/servicecidrs` — only when the API server is started with `--runtime-config=networking.k8s.io/v1beta1=true`
* `networking.k8s.io/v1beta1/servicecidrs/status` — only when the API server is started with `--runtime-config=networking.k8s.io/v1beta1=true`
* `storage.k8s.io/v1beta1/volumeattributesclasses` — only when the API server is started with `--runtime-config=storage.k8s.io/v1beta1=true`

**Job success policy.** `spec.successPolicy` is accepted on an `Indexed` Job, with `rules[].succeededIndexes` and `rules[].succeededCount`; upstream's validation rejects it on a non-indexed Job.

**A finished Job records two conditions.** When a Job reaches its completions it gets `SuccessCriteriaMet` followed by `Complete`, both `status: "True"` with `reason: CompletionsReached` and message `Reached expected number of succeeded pods`.

**`spec.successPolicy` needs an indexed Job.** A `Job` carrying `spec.successPolicy` without `spec.completionMode: Indexed` is rejected with `reason: Invalid` and a cause `{field: spec.successPolicy, reason: FieldValueInvalid}`.

**The OpenAPI v3 root lists only what is served.** A group-version that exists in the surface but is enabled only by `--runtime-config` is absent from `/openapi/v3` until it is enabled.

**Three group-versions arrive behind `--runtime-config`.** `storage.k8s.io/v1beta1` (`VolumeAttributesClass`), `networking.k8s.io/v1beta1` (`ServiceCIDR` with a `status` subresource, and `IPAddress`) and `coordination.k8s.io/v1alpha1` (`LeaseCandidate`) are served only when the API server is started with `--runtime-config=storage.k8s.io/v1beta1=true,networking.k8s.io/v1beta1=true,coordination.k8s.io/v1alpha1=true`, and then they behave like any other group-version: discovery lists them, their objects are defaulted and validated, and `ServiceCIDR` has a status subresource. A `VolumeAttributesClass` without `driverName` is rejected with a `FieldValueRequired` cause on `driverName`, and an `IPAddress` whose `metadata.name` is not an IP address with a `FieldValueInvalid` cause on `metadata.name`. The coordination versions this release does not serve stay absent even with this one enabled.

**An image volume source is known but off.** A Pod whose `spec.volumes[]` carries an `image` source is accepted, and the whole volume is dropped on write along with every `volumeMounts` entry that referred to it, because the feature is not on by default at this release.

Everything the workspace already does must keep working: the REST, discovery, OpenAPI, watch, RBAC, admission, CRD, controller and scheduler behaviour of the previous release must still hold in full, and the behaviours this release changes must change. Manifests written for 1.25 still apply wherever the API is still served.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` — the OpenAPI specification this release publishes.
* `/app/SPEC/REQUIRED-API-SURFACE.json` — the group-versions and resources to serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` — how the control plane is built, started and driven, the etcd key layout, the link-set rule, and the upstream response details (section 8) that are compared exactly. Unchanged.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy: `/opt/docs/kubernetes-docs/1.31/api-concepts.md`) and the component reference pages (local copies: `/opt/docs/kubernetes-docs/1.31/kube-apiserver.md`, `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` — the conformance corpus recorded from Kubernetes 1.31 and a runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three components, replays the corpus and prints per-area results. It is a representative sample of those areas, not an exhaustive one.
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

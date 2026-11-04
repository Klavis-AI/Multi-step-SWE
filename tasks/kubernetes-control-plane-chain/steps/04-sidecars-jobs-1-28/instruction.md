# Step 04 — Kubernetes 1.28

## 1. Implement this milestone

The control plane in this workspace serves the Kubernetes 1.27 API. Move it to **Kubernetes 1.28**.

`/app/SPEC/REQUIRED-API-SURFACE.json` has been replaced with 1.28's surface and `/app/SPEC/openapi/swagger.json` with that release's published OpenAPI specification. The served group-versions, resources and subresources must match the new surface exactly — every entry it lists is reachable, and every entry that is gone answers as an absent one does. The root discovery documents stay complete against it: `/apis` returns an `APIGroupList` naming every group this release serves, and `/openapi/v3` a root whose `paths` holds an entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are compared in full, so a root that covers only part of the surface fails even when each individual group-version answers correctly.

**No longer served** (a request to one of these is `404`, and it is absent from discovery):

* `networking.k8s.io/v1/networkpolicies/status`

**Newly served**:

* `admissionregistration.k8s.io/v1beta1/validatingadmissionpolicies` — only when the API server is started with `--runtime-config=admissionregistration.k8s.io/v1beta1=true`
* `admissionregistration.k8s.io/v1beta1/validatingadmissionpolicies/status` — only when the API server is started with `--runtime-config=admissionregistration.k8s.io/v1beta1=true`
* `admissionregistration.k8s.io/v1beta1/validatingadmissionpolicybindings` — only when the API server is started with `--runtime-config=admissionregistration.k8s.io/v1beta1=true`
* `authentication.k8s.io/v1/selfsubjectreviews`

**`networkpolicies/status` is gone** from `networking.k8s.io/v1`.

**NetworkPolicy has no status.** `NetworkPolicyStatus` was withdrawn: a `NetworkPolicy` read back after any write carries no `status` field at all.

**A missing `default` ServiceAccount is waited for.** Admission of a Pod resolves `spec.serviceAccountName`; when the account is named `default` and is not there yet, the lookup is retried for a few seconds before the request is refused, because the controller creates the account moments after the namespace appears. A Pod created immediately after its namespace therefore succeeds rather than failing with `Forbidden`.

**Per-index backoff changes the overall limit, and is then dropped.** A `Job` with `spec.backoffLimitPerIndex` set is accepted and defaults `spec.backoffLimit` to 2147483647 rather than 6 -- at either completion mode. The field itself, and `spec.maxFailedIndexes` with it, is not stored at this release: both are gone from the object that comes back.

**The Job tracking annotation is gone.** A `Job` is no longer stamped with `batch.kubernetes.io/job-tracking`; releases up to 1.27 added it to every Job.

**An init container's `restartPolicy` is pruned.** The sidecar field is not honoured at this release: `spec.initContainers[].restartPolicy` is dropped from a Pod on write and does not appear when the Pod is read back.

**Every user can review itself.** A `SelfSubjectReview` created by an ordinary user comes back with `status.userInfo` naming that user, its uid and its groups, including `system:authenticated`.

**`ValidatingAdmissionPolicy` reaches beta.** The policy and binding kinds move to `admissionregistration.k8s.io/v1beta1`, served with `--runtime-config=admissionregistration.k8s.io/v1beta1=true`; they are still not enforced on any request. A policy read back at `v1beta1` carries `spec.variables` explicitly as `null` when the client supplied none.

**An image volume source is unknown here.** A Pod whose `spec.volumes[]` carries an `image` source is accepted, the unrecognised field is pruned, and the volume is left with the default source, `emptyDir: {}`; the mount that refers to it stays.

**Container resources are not mutable through the Pod itself.** A patch to `spec.containers[].resources` sent to the Pod object is rejected with HTTP 422, `reason: Invalid` and `details.causes` holding the single entry `{field: spec, reason: FieldValueForbidden}`.

Everything the workspace already does must keep working: the REST, discovery, OpenAPI, watch, RBAC, admission, CRD, controller and scheduler behaviour of the previous release must still hold in full, and the behaviours this release changes must change. Manifests written for 1.25 still apply wherever the API is still served.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` — the OpenAPI specification this release publishes.
* `/app/SPEC/REQUIRED-API-SURFACE.json` — the group-versions and resources to serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` — how the control plane is built, started and driven, the etcd key layout, the link-set rule, and the upstream response details (section 8) that are compared exactly. Unchanged.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy: `/opt/docs/kubernetes-docs/1.28/api-concepts.md`) and the component reference pages (local copies: `/opt/docs/kubernetes-docs/1.28/kube-apiserver.md`, `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` — the conformance corpus recorded from Kubernetes 1.28 and a runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three components, replays the corpus and prints per-area results. It is a representative sample of those areas, not an exhaustive one.
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

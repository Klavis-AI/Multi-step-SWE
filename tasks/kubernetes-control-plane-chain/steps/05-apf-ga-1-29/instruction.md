# Step 05 — Kubernetes 1.29

## 1. Implement this milestone

The control plane in this workspace serves the Kubernetes 1.28 API. Move it to **Kubernetes 1.29**.

`/app/SPEC/REQUIRED-API-SURFACE.json` has been replaced with 1.29's surface and `/app/SPEC/openapi/swagger.json` with that release's published OpenAPI specification. The served group-versions, resources and subresources must match the new surface exactly — every entry it lists is reachable, and every entry that is gone answers as an absent one does. The root discovery documents stay complete against it: `/apis` returns an `APIGroupList` naming every group this release serves, and `/openapi/v3` a root whose `paths` holds an entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are compared in full, so a root that covers only part of the surface fails even when each individual group-version answers correctly.

**No longer served** (a request to one of these is `404`, and it is absent from discovery):

* `flowcontrol.apiserver.k8s.io/v1beta2/flowschemas`
* `flowcontrol.apiserver.k8s.io/v1beta2/flowschemas/status`
* `flowcontrol.apiserver.k8s.io/v1beta2/prioritylevelconfigurations`
* `flowcontrol.apiserver.k8s.io/v1beta2/prioritylevelconfigurations/status`

**Newly served**:

* `flowcontrol.apiserver.k8s.io/v1/flowschemas`
* `flowcontrol.apiserver.k8s.io/v1/flowschemas/status`
* `flowcontrol.apiserver.k8s.io/v1/prioritylevelconfigurations`
* `flowcontrol.apiserver.k8s.io/v1/prioritylevelconfigurations/status`

The API server is started with `--runtime-config=admissionregistration.k8s.io/v1beta1=true`, so the `admissionregistration.k8s.io/v1beta1` resources in the surface are served.

**API Priority and Fairness reaches GA.** `flowcontrol.apiserver.k8s.io/v1beta2` is removed and `v1` is added; the stored form moves with it, and objects written at any served version still read back at every other served version.

**`PriorityLevelConfiguration` at `v1`.** The `v1` view defaults `spec.limited.lendablePercent` to 0, exactly as `v1beta3` does.

**Job defaulting gains two fields.** Every `Job` defaults `spec.manualSelector` to `false` and `spec.podReplacementPolicy` to `TerminatingOrFailed`, and a `Job` read back after a write to its `status` subresource carries `status.terminating: 0`.

**Per-index backoff is stored from this release.** An `Indexed` `Job` carrying `spec.backoffLimitPerIndex`, and `spec.maxFailedIndexes` with it, keeps both: each is present, with the value the client sent, on the object that comes back. `spec.backoffLimit` still defaults to 2147483647 rather than 6 when `spec.backoffLimitPerIndex` is set.

**A refused Pod `spec` write names two fields.** A patch to `spec.containers[].resources` sent to the Pod object is still refused with HTTP 422 and `reason: Invalid`, and from this release its `Status` carries two entries in `details.causes`, in this order: `{field: metadata, reason: FieldValueInvalid}` then `{field: spec, reason: FieldValueForbidden}`.

**Sidecars are real.** `spec.initContainers[].restartPolicy` is kept, and the only value accepted is `Always`: anything else is rejected with `reason: Invalid` and a cause `{field: spec.initContainers[i].restartPolicy, reason: FieldValueNotSupported}`.

**A policy carries its matured fields.** `spec.matchConditions`, `spec.variables` and `spec.validations[].messageExpression` are stored and read back as written on a `ValidatingAdmissionPolicy`, with the same defaulting as any other policy.

Everything the workspace already does must keep working: the REST, discovery, OpenAPI, watch, RBAC, admission, CRD, controller and scheduler behaviour of the previous release must still hold in full, and the behaviours this release changes must change. Manifests written for 1.25 still apply wherever the API is still served.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` — the OpenAPI specification this release publishes.
* `/app/SPEC/REQUIRED-API-SURFACE.json` — the group-versions and resources to serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` — how the control plane is built, started and driven, the etcd key layout, the link-set rule, and the upstream response details (section 8) that are compared exactly. Unchanged.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy: `/opt/docs/kubernetes-docs/1.29/api-concepts.md`) and the component reference pages (local copies: `/opt/docs/kubernetes-docs/1.29/kube-apiserver.md`, `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` — the conformance corpus recorded from Kubernetes 1.29 and a runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three components, replays the corpus and prints per-area results. It is a representative sample of those areas, not an exhaustive one.
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

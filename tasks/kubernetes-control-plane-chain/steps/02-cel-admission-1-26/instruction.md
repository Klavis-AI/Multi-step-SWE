# Step 02 — Kubernetes 1.26

## 1. Implement this milestone

The control plane in this workspace serves the Kubernetes 1.25 API. Move it to **Kubernetes 1.26**.

`/app/SPEC/REQUIRED-API-SURFACE.json` has been replaced with 1.26's surface and `/app/SPEC/openapi/swagger.json` with that release's published OpenAPI specification. The served group-versions, resources and subresources must match the new surface exactly — every entry it lists is reachable, and every entry that is gone answers as an absent one does. The root discovery documents stay complete against it: `/apis` returns an `APIGroupList` naming every group this release serves, and `/openapi/v3` a root whose `paths` holds an entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are compared in full, so a root that covers only part of the surface fails even when each individual group-version answers correctly.

**No longer served** (a request to one of these is `404`, and it is absent from discovery):

* `autoscaling/v2beta2/horizontalpodautoscalers`
* `autoscaling/v2beta2/horizontalpodautoscalers/status`
* `flowcontrol.apiserver.k8s.io/v1beta1/flowschemas`
* `flowcontrol.apiserver.k8s.io/v1beta1/flowschemas/status`
* `flowcontrol.apiserver.k8s.io/v1beta1/prioritylevelconfigurations`
* `flowcontrol.apiserver.k8s.io/v1beta1/prioritylevelconfigurations/status`

**Newly served**:

* `admissionregistration.k8s.io/v1alpha1/validatingadmissionpolicies` — only when the API server is started with `--runtime-config=admissionregistration.k8s.io/v1alpha1=true`
* `admissionregistration.k8s.io/v1alpha1/validatingadmissionpolicybindings` — only when the API server is started with `--runtime-config=admissionregistration.k8s.io/v1alpha1=true`
* `flowcontrol.apiserver.k8s.io/v1beta3/flowschemas`
* `flowcontrol.apiserver.k8s.io/v1beta3/flowschemas/status`
* `flowcontrol.apiserver.k8s.io/v1beta3/prioritylevelconfigurations`
* `flowcontrol.apiserver.k8s.io/v1beta3/prioritylevelconfigurations/status`

**CEL admission policies.** `ValidatingAdmissionPolicy` and `ValidatingAdmissionPolicyBinding` arrive at `admissionregistration.k8s.io/v1alpha1`, served only when the API server is started with upstream's `--runtime-config=admissionregistration.k8s.io/v1alpha1=true`. A policy's `spec.validations[].expression` is a **CEL** expression, defined by `https://github.com/google/cel-spec` (local copy of its language definition: `/opt/docs/cel-spec/v0.7.1/langdef.md`) with the Kubernetes subset and activation variables described at `https://kubernetes.io/docs/reference/using-api/cel/` (local copy: `/opt/docs/kubernetes-docs/1.26/cel.md`). At this release the objects are stored, defaulted and validated, but a policy is **not yet enforced** on any request: the feature is alpha, so a bound policy whose validation is false still lets the request through. A validation whose expression does not compile is rejected when the policy is written, with HTTP 422, `reason: Invalid` and a cause `{field: spec.validations[i].expression, reason: FieldValueInvalid}`. A `ValidatingAdmissionPolicyBinding` has no `spec.validationActions` at `v1alpha1`: the field is dropped on write.

**HorizontalPodAutoscaler `autoscaling/v2beta2` is gone.** Objects written through `autoscaling/v1` or `autoscaling/v2` are unaffected and still convert between those two versions.

**API Priority and Fairness moves on.** `flowcontrol.apiserver.k8s.io/v1beta1` is removed and `v1beta3` is added. A `FlowSchema` or `PriorityLevelConfiguration` written at one served version reads back correctly at every other served version, and `PriorityLevelConfiguration`'s concurrency field is named `spec.limited.assuredConcurrencyShares` in `v1beta2` and `spec.limited.nominalConcurrencyShares` in `v1beta3`.

**A group with no served version answers in plain text.** A request whose path names a group the server does not serve at any version — `extensions` has none from this release's surface onward — is answered with HTTP 404 and the body `404 page not found\n` as `text/plain`, not with a `Status`. A group that *is* served answers a request for one of its removed versions with an ordinary `Status` 404.

**A policy is stored with upstream's defaults.** `spec.matchConstraints.matchPolicy` defaults to `Equivalent`, `spec.matchConstraints.namespaceSelector` and `spec.matchConstraints.objectSelector` to `{}`, and every `spec.matchConstraints.resourceRules[].scope` to `*`. A binding defaults `spec.matchResources.matchPolicy` to `Equivalent` and `spec.matchResources.objectSelector` to `{}`, leaving a `namespaceSelector` the client supplied as it was written.

**`PriorityLevelConfiguration` limited fields.** `spec.limited.lendablePercent` defaults to 0 and is present in both the `v1beta2` and the `v1beta3` view of a `PriorityLevelConfiguration`; `borrowingLimitPercent` is absent unless the client set it. The concurrency field is `spec.limited.assuredConcurrencyShares` in `v1beta2` and `spec.limited.nominalConcurrencyShares` in `v1beta3`, and an object written at either version reads back correctly at the other.

**Deployment validation names the legacy group.** A `Deployment` rejected by validation reports `details.group: "extensions"` in its `Status` at this release.

**An image volume source is still unknown.** A Pod whose `spec.volumes[]` carries an `image` source is accepted, the unrecognised field is pruned, and the volume is left with the default source, `emptyDir: {}`; the mount that refers to it stays.

**Container resources are not mutable through the Pod itself.** A patch to `spec.containers[].resources` sent to the Pod object is rejected with HTTP 422, `reason: Invalid` and a cause `{field: spec, reason: FieldValueForbidden}`.

Everything the workspace already does must keep working: the REST, discovery, OpenAPI, watch, RBAC, admission, CRD, controller and scheduler behaviour of the previous release must still hold in full, and the behaviours this release changes must change. Manifests written for 1.25 still apply wherever the API is still served.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` — the OpenAPI specification this release publishes.
* `/app/SPEC/REQUIRED-API-SURFACE.json` — the group-versions and resources to serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` — how the control plane is built, started and driven, the etcd key layout, the link-set rule, and the upstream response details (section 8) that are compared exactly. Unchanged.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy: `/opt/docs/kubernetes-docs/1.26/api-concepts.md`) and the component reference pages (local copies: `/opt/docs/kubernetes-docs/1.26/kube-apiserver.md`, `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` — the conformance corpus recorded from Kubernetes 1.26 and a runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three components, replays the corpus and prints per-area results. It is a representative sample of those areas, not an exhaustive one.
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

# Step 06 — Kubernetes 1.30

## 1. Implement this milestone

The control plane in this workspace serves the Kubernetes 1.29 API. Move it to **Kubernetes 1.30**.

`/app/SPEC/REQUIRED-API-SURFACE.json` has been replaced with 1.30's surface and `/app/SPEC/openapi/swagger.json` with that release's published OpenAPI specification. The served group-versions, resources and subresources must match the new surface exactly — every entry it lists is reachable, and every entry that is gone answers as an absent one does. The root discovery documents stay complete against it: `/apis` returns an `APIGroupList` naming every group this release serves, and `/openapi/v3` a root whose `paths` holds an entry for `api`, one for `apis`, one for every served group (`apis/<group>`) and one for every served group-version (`api/v1`, `apis/<group>/<version>`). Both are compared in full, so a root that covers only part of the surface fails even when each individual group-version answers correctly.

**Newly served**:

* `admissionregistration.k8s.io/v1/validatingadmissionpolicies`
* `admissionregistration.k8s.io/v1/validatingadmissionpolicies/status`
* `admissionregistration.k8s.io/v1/validatingadmissionpolicybindings`

**`ValidatingAdmissionPolicy` reaches GA** at `admissionregistration.k8s.io/v1`, served by default rather than behind `--runtime-config`.

**Aggregated discovery reaches `v2`.** `/api` and `/apis` answer an `Accept` header of `application/json;g=apidiscovery.k8s.io;v=v2;as=APIGroupDiscoveryList` with an `apidiscovery.k8s.io/v2` `APIGroupDiscoveryList`. Apart from `apiVersion`, it is the same document the `v2beta1` form returns: one entry per group, each with its versions, each version listing its resources with `resource`, `responseKind`, `scope`, `singularResource`, `verbs` and nested `subresources`. `apidiscovery.k8s.io/v2beta1` is still served and still answers with `apiVersion: apidiscovery.k8s.io/v2beta1`, and a request whose `Accept` asks for neither still gets the unaggregated `APIVersions`, `APIGroupList` or `APIResourceList` answer.

**AppArmor is a field, and the annotation follows it.** A container's `securityContext.appArmorProfile.type` of `RuntimeDefault` is reflected back into the deprecated pod annotation `container.apparmor.security.beta.kubernetes.io/<container>` as `runtime/default` when the pod does not already carry that annotation; `Unconfined` and a `localhostProfile` map to `unconfined` and `localhost/<profile>` the same way.

**Admission policies are enforced from this release.** With `ValidatingAdmissionPolicy` GA, a binding with `spec.validationActions: ["Deny"]` whose `spec.matchResources.namespaceSelector` selects the request's namespace makes a false expression reject the request: HTTP 422, `reason: Invalid`, `details.group`, `details.kind` (the plural resource) and `details.name` naming the rejected object, and one entry in `details.causes` per failed validation. A request in a namespace the binding does not select is unaffected.

**`spec.hostUsers` is dropped.** User namespaces are off at this release: a Pod created with `spec.hostUsers` is accepted, and the field is removed on write, so it is absent when the Pod is read back.

**An image volume source is unknown here.** A Pod whose `spec.volumes[]` carries an `image` source is accepted, the unrecognised field is pruned, and the volume is left with the default source, `emptyDir: {}`; the mount that refers to it stays.

Everything the workspace already does must keep working: the REST, discovery, OpenAPI, watch, RBAC, admission, CRD, controller and scheduler behaviour of the previous release must still hold in full, and the behaviours this release changes must change. Manifests written for 1.25 still apply wherever the API is still served.

## 2. Read these local references

**Normative semantics.**
* `/app/SPEC/openapi/swagger.json` — the OpenAPI specification this release publishes.
* `/app/SPEC/REQUIRED-API-SURFACE.json` — the group-versions and resources to serve, with upstream's own discovery attributes for each.
* `/app/SPEC/TEST_INTERFACE.md` — how the control plane is built, started and driven, the etcd key layout, the link-set rule, and the upstream response details (section 8) that are compared exactly. Unchanged.
* `https://kubernetes.io/docs/reference/using-api/api-concepts/` (local copy: `/opt/docs/kubernetes-docs/1.30/api-concepts.md`) and the component reference pages (local copies: `/opt/docs/kubernetes-docs/1.30/kube-apiserver.md`, `kube-controller-manager.md` and `kube-scheduler.md`).

**Additional files.**
* `/app/conformance/` — the conformance corpus recorded from Kubernetes 1.30 and a runner. `bash /app/conformance/run.sh` builds the tree, starts etcd and the three components, replays the corpus and prints per-area results. It is a representative sample of those areas, not an exhaustive one.
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

import json
"""Normalization of Kubernetes API responses so that two runs of the same corpus
against two different implementations of the same release are byte-comparable.

Only volatile / non-reproducible material is removed.  Everything that the API
contract fixes (defaulted fields, validation outcomes, generation counters,
owner references, labels, conditions' type/status/reason) is kept.
"""
import re

VOLATILE_META = (
    "resourceVersion", "uid", "creationTimestamp", "managedFields", "selfLink",
    "deletionTimestamp", "generateName", "continue", "remainingItemCount",
)
VOLATILE_TIME_KEYS = (
    "lastTransitionTime", "lastUpdateTime", "lastProbeTime", "lastScheduleTime",
    "lastSuccessfulTime", "startTime", "completionTime", "eventTime", "firstTimestamp",
    "lastTimestamp", "renewTime", "acquireTime", "time", "expirationTimestamp",
    "notBefore", "notAfter", "lastScaleTime", "observedTime", "creationTimestamp",
)
# Cluster-allocated values.
ALLOC_KEYS = ("clusterIP", "clusterIPs", "nodePort", "podIP", "podIPs", "hostIP",
              "hostIPs", "ip", "addresses")

GEN_SUFFIX = re.compile(r"^(?P<base>.*?)-(?P<h>[a-z0-9]{5,10})$")


def _is_addr(k, v):
    if k != "addresses":
        return True
    # EndpointSlice.endpoints[].addresses is a list of IP strings; Endpoints
    # subsets[].addresses is a list of EndpointAddress objects, handled per-field.
    return isinstance(v, list) and all(isinstance(x, str) for x in v)


def _scrub(obj, path=()):
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            if k in VOLATILE_TIME_KEYS and (isinstance(v, str) or v is None):
                out[k] = "<ts>"
                continue
            if k in ALLOC_KEYS and _is_addr(k, v):
                out[k] = "<alloc>"
                continue
            if k == "uid" and isinstance(v, str):
                out[k] = "<uid>"
                continue
            out[k] = _scrub(v, path + (k,))
        return out
    if isinstance(obj, list):
        return [_scrub(v, path + ("[]",)) for v in obj]
    return obj


REVIEW_KINDS = ("SubjectAccessReview", "SelfSubjectAccessReview",
                "LocalSubjectAccessReview", "SelfSubjectRulesReview")


def _sort_review_rules(o):
    if not isinstance(o, dict) or o.get("kind") != "SelfSubjectRulesReview":
        return
    st = o.get("status")
    if isinstance(st, dict):
        for k in ("resourceRules", "nonResourceRules"):
            if isinstance(st.get(k), list):
                st[k] = sorted(st[k], key=lambda x: json.dumps(x, sort_keys=True))


def _drop_deployment_condition_text(o):
    """A Deployment's Progressing condition names the ReplicaSet, whose name ends
    in a pod-template hash no independent implementation reproduces."""
    if not isinstance(o, dict):
        return
    if o.get("kind") not in ("Deployment", "DeploymentList"):
        return
    targets = o.get("items") if o.get("kind") == "DeploymentList" else [o]
    for d in targets or []:
        st = d.get("status") if isinstance(d, dict) else None
        for c in (st or {}).get("conditions", []) or []:
            if isinstance(c, dict):
                c.pop("message", None)


def _drop_review_text(o):
    """Authorizer and authenticator explanations are free text, not contract."""
    if not isinstance(o, dict):
        return
    k = o.get("kind")
    st = o.get("status")
    if isinstance(st, dict):
        if k in REVIEW_KINDS:
            st.pop("reason", None)
            st.pop("evaluationError", None)
        if k == "TokenReview":
            st.pop("error", None)


def normalize_object(o, name_map=None):
    """Normalize one API object (or list).  `name_map` maps server-generated names
    to stable placeholders."""
    if not isinstance(o, dict):
        return o
    o = _scrub(o)
    _strip_meta(o, name_map)
    _drop_review_text(o)
    _sort_review_rules(o)
    _drop_deployment_condition_text(o)
    return o


def _strip_meta(o, name_map):
    if isinstance(o, list):
        for e in o:
            _strip_meta(e, name_map)
        return
    if not isinstance(o, dict):
        return
    md = o.get("metadata")
    if isinstance(md, dict):
        for k in VOLATILE_META:
            md.pop(k, None)
        # ownerReferences carry the owner uid, which is not reproducible
        for ref in md.get("ownerReferences", []) or []:
            if isinstance(ref, dict):
                ref.pop("uid", None)
        if name_map and isinstance(md.get("name"), str) and md["name"] in name_map:
            md["name"] = name_map[md["name"]]
        ann = md.get("annotations")
        if isinstance(ann, dict):
            for k in list(ann):
                if k in ("kubectl.kubernetes.io/last-applied-configuration",
                         "deployment.kubernetes.io/revision",
                         "endpoints.kubernetes.io/last-change-trigger-time",
                         "batch.kubernetes.io/cronjob-scheduled-timestamp",
                         "cronjob.kubernetes.io/instantiate"):
                    if k == "deployment.kubernetes.io/revision":
                        continue
                    ann.pop(k, None)
            if not ann:
                md.pop("annotations", None)
    for k, v in list(o.items()):
        if isinstance(v, (dict, list)):
            _strip_meta(v, name_map)


def normalize_status(o):
    """Normalize a metav1.Status error body: keep the structured contract, drop
    free-form human text."""
    if not isinstance(o, dict):
        return o
    out = {
        "kind": o.get("kind"), "apiVersion": o.get("apiVersion"),
        "status": o.get("status"), "reason": o.get("reason"), "code": o.get("code"),
    }
    d = o.get("details")
    if isinstance(d, dict):
        nd = {k: d.get(k) for k in ("group", "kind", "name") if d.get(k) is not None}
        causes = d.get("causes")
        if isinstance(causes, list):
            cs = []
            for c in causes:
                if isinstance(c, dict):
                    e = {}
                    if c.get("reason") is not None:
                        e["reason"] = c["reason"]
                    if c.get("field") is not None:
                        e["field"] = c["field"]
                    cs.append(e)
            cs.sort(key=lambda e: (e.get("field", ""), e.get("reason", "")))
            if cs:
                nd["causes"] = cs
        if nd:
            out["details"] = nd
    return out

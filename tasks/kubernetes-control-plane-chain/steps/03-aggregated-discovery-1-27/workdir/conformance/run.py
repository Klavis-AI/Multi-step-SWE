#!/usr/bin/env python3
"""Conformance runner.

Replays the corpus recorded from the Kubernetes release this step targets against
the control plane built in this workspace and prints, per area, how many cases
match.  Pass a case id
prefix to see the expected and observed values for the failures in that area,
e.g.  `python3 /app/conformance/run.py S4`.
"""
import json, os, shutil, sys, time

sys.dont_write_bytecode = True   # keep /app text-only

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "kcorpus"))
os.environ.setdefault("KCP_SURFACE", "/app/SPEC/REQUIRED-API-SURFACE.json")
import runplan  # noqa: E402

BASE = "/tmp/kcp-conformance"
BIN = "/out/bin"

AREAS = {
    "S1": "discovery",
    "S2": "openapi",
    "S3": "CRUD over every required kind",
    "S4": "defaulting",
    "S5": "validation",
    "S6": "patch types",
    "S7": "selectors and pagination",
    "S8": "watch",
    "S9": "RBAC",
    "S10": "admission",
    "S11": "subresources",
    "S12": "deletion semantics",
    "S13": "group-versions that must not be served",
    "S14": "version conversion",
    "S15": "CustomResourceDefinitions",
    "S16": "controllers",
    "S17": "scheduler",
    "S18": "kubectl",
    "S19": "storage and restart",
    "S20": "CEL admission policies",
    "S21": "aggregated discovery",
}


def main():
    focus = sys.argv[1] if len(sys.argv) > 1 else None
    shutil.rmtree(BASE, ignore_errors=True)
    os.makedirs(BASE, exist_ok=True)
    extra = [a for a in os.environ.get("KCP_APISERVER_ARGS", "").split() if a]
    cl = runplan.Cluster(BASE, BIN, 22379, 6443, extra)
    cl.certs()
    if not cl.start_etcd():
        print("etcd did not start"); return 1
    if not cl.start_apiserver():
        print("kube-apiserver did not become healthy; see " + BASE + "/apiserver.log")
        print(open(BASE + "/apiserver.log").read()[-3000:])
        cl.stop(); return 1
    cl.start_controllers()
    t0 = time.time()
    plan_file = os.path.join(HERE, "plan.json")
    kubectl = os.environ.get("KCP_KUBECTL", "/usr/local/bin/kubectl")
    doc, obs = runplan.replay(cl, plan_file, kubectl, "/usr/local/bin/etcdctl")
    expected = json.load(open(os.path.join(HERE, "expected.json")))["obs"]
    per, fails = runplan.score(doc, obs, expected)
    cl.stop()
    total = sum(v[1] for v in per.values())
    passed = sum(v[0] for v in per.values())
    print("\n%-6s %-42s %s" % ("area", "what it covers", "passed"))
    for st in sorted(per, key=lambda x: (len(x), x)):
        print("%-6s %-42s %d/%d" % (st, AREAS.get(st, ""), per[st][0], per[st][1]))
    print("\nTOTAL %d/%d  (%.1f%%)  in %.0fs" % (passed, total, 100.0 * passed / max(total, 1),
                                                 time.time() - t0))
    if focus:
        shown = 0
        for cid in fails:
            if not cid.startswith(focus):
                continue
            print("\n=== " + cid)
            print("  expected: " + json.dumps(expected.get(cid), sort_keys=True)[:1400])
            print("  observed: " + json.dumps(obs.get(cid), sort_keys=True)[:1400])
            shown += 1
            if shown >= 6:
                break
    else:
        print("\nfailing cases: " + ", ".join(fails[:40]) + (" ..." if len(fails) > 40 else ""))
    return 0


sys.exit(main())

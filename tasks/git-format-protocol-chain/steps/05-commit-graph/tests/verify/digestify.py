#!/usr/bin/env python3
"""Ship digests, never the expected bytes.

The candidate is built and run inside the verifier image, so /tests is on its
filesystem: an implementation that reads the recorded expectations and replays
them scores like a correct one.  Shipping sha256 digests instead removes the
class outright -- to print an output whose digest matches, an implementation
has to produce the output.

Full expectations are kept next to the authoring tree for failure analysis;
only digests go into the task package.
"""
import argparse, hashlib, json, os, shutil


def d_probe(status, stdout, stderr):
    h = hashlib.sha256()
    h.update(b"%d\0" % int(status))
    h.update(stdout.encode("utf-8", "surrogateescape") + b"\0")
    h.update(stderr.encode("utf-8", "surrogateescape"))
    return h.hexdigest()


def d_output(status, output):
    return d_probe(status, output, "")


def workload(path):
    data = json.load(open(path))
    n = 0
    for sc in data:
        for p in sc["probes"]:
            if "digest" in p:
                continue
            p["digest"] = d_probe(p["status"], p["stdout"], p["stderr"])
            for k in ("status", "stdout", "stderr"):
                p.pop(k, None)
            n += 1
    json.dump(data, open(path, "w"), indent=1)
    return n


def corpus(path):
    data = json.load(open(path))
    n = 0
    for repo, probes in data.items():
        for p in probes:
            if "digest" in p:
                continue
            p["digest"] = d_output(p["status"], p["output"])
            for k in ("status", "output"):
                p.pop(k, None)
            n += 1
    json.dump(data, open(path, "w"), indent=1, sort_keys=True)
    return n


def interop(path):
    data = json.load(open(path))
    n = 0
    for r in data:
        if r.get("cmd") == "<build>":
            # not an expectation: it only records that the build ran
            for k in ("status", "output"):
                r.pop(k, None)
            continue
        if "digest" in r:
            continue
        r["digest"] = d_output(r["status"], r["output"])
        for k in ("status", "output"):
            r.pop(k, None)
        n += 1
    json.dump(data, open(path, "w"), indent=1)
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fixtures")
    ap.add_argument("--keep", required=True, help="directory for the full expectations")
    args = ap.parse_args()
    os.makedirs(args.keep, exist_ok=True)
    jobs = [("expect.json", workload), ("corpus/expect.json", corpus),
            ("interop-expect.json", interop)]
    for rel, fn in jobs:
        path = os.path.join(args.fixtures, rel)
        if not os.path.exists(path):
            print("   %-22s absent" % rel)
            continue
        keep = os.path.join(args.keep, rel.replace("/", "_"))
        if not os.path.exists(keep):
            shutil.copy(path, keep)
        n = fn(path)
        print("   %-22s %d expectations digested (full copy: %s)" % (rel, n, keep))


if __name__ == "__main__":
    main()

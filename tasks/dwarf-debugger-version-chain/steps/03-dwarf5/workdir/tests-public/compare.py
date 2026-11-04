#!/usr/bin/env python3
"""Compare this workspace's output with the recorded real-tool output.

The comparison rules are the ones stated in INTERFACE.md section 4.
"""
import json
import os
import re
import subprocess
import sys

ROOT = sys.argv[1]
EXPDIR = sys.argv[2]
FX = "/dw/out/fx"
CORES = "/dw/out/cores"
WEIGHTS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                      "weights.json")))["groups"]


def normalize(text, mode):
    if mode == "ws":
        return "\n".join(re.sub(r"[ \t]+", " ", l).rstrip() for l in text.split("\n"))
    return "\n".join(l.rstrip() for l in text.split("\n"))


def split_units(text, mode):
    if mode == "blank":
        return [b for b in text.strip("\n").split("\n\n")]
    if mode == "addrline":
        out, cur = [], []
        for l in text.split("\n"):
            if re.match(r"^0x[0-9a-f]+$", l):
                if cur:
                    out.append("\n".join(cur))
                cur = [l]
            elif l or cur:
                cur.append(l)
        if cur:
            out.append("\n".join(cur))
        return out
    if mode == "line":
        return [l for l in text.split("\n") if l.strip()]
    if mode == "fdeblock":
        lines = text.split("\n")
        i = 0
        while i < len(lines) and not re.match(
                r"^Contents of the \.(eh_frame|debug_frame) section", lines[i]):
            i += 1
        return [b for b in "\n".join(lines[i:]).strip("\n").split("\n\n") if b.strip()]
    return [text]


def main():
    total = 0.0
    covered_w = 0.0
    failures = []
    for group, weight in WEIGHTS.items():
        path = os.path.join(EXPDIR, group + ".json")
        if not os.path.exists(path):
            continue
        matched = count = 0
        for case in json.load(open(path))["cases"]:
            if case.get("normalize") == "gdb" or "args" not in case:
                continue
            exe = os.path.join(ROOT, "bin", case["tool"])
            fx = os.path.join(FX, case["fixture"]) if case.get("fixture") else ""
            core = os.path.join(CORES, case["core"] + ".core") if case.get("core") else ""
            args = [a.replace("@FX@", fx).replace("@CORE@", core) for a in case["args"]]
            stdin = case.get("stdin", "").replace("@FX@", fx).replace("@CORE@", core)
            try:
                out = subprocess.run([exe] + args, input=stdin, capture_output=True,
                                     text=True, timeout=120).stdout
            except FileNotFoundError:
                print("missing executable: %s" % exe)
                out = ""
            except subprocess.TimeoutExpired:
                out = "<<TIMEOUT>>"
            mode = case.get("normalize", "none")
            eu = split_units(normalize(case["expect"], mode), case["split"])
            au = split_units(normalize(out, mode), case["split"])
            m = sum(1 for i in range(min(len(eu), len(au))) if eu[i] == au[i])
            d = max(len(eu), len(au)) or 1
            matched += m
            count += d
            if m != d:
                failures.append((case["id"], m, d))
        if count == 0:
            print("%-13s     no cases in this sample" % group)
            continue
        frac = matched / count
        covered_w += weight
        total += weight * frac
        print("%-13s %5d/%-5d = %.4f" % (group, matched, count, frac))
    print("---\noverall %.4f  (over the groups this sample covers)"
          % (total / covered_w if covered_w else 0.0))
    if failures:
        print("\nworst cases:")
        for id_, m, d in sorted(failures, key=lambda x: x[1] / x[2])[:15]:
            print("  %-46s %4d/%-4d" % (id_, m, d))


main()

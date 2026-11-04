#!/usr/bin/env python3
"""Drive your WebDriver remote end over the practice documents and report.

This is the same protocol, the same tolerance and the same comparison the engine
is held to -- one check per measured element, which passes only when the
element's rect and every resolved value recorded for it are correct; only the
documents differ. Run it as often as you like:

    python3 /app/css-corpus/run.py --binary /app/build/cssdriver
    python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only css21-floats-clearance
    python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only css21-       # prefix
    python3 /app/css-corpus/run.py --binary /app/build/cssdriver --only css21-box-model,css21-positioning
    python3 /app/css-corpus/run.py --binary /app/build/cssdriver --list-strata
    python3 /app/css-corpus/run.py --binary /app/build/cssdriver --show-failures 20
"""
import argparse, collections, json, os, re, signal, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wd import WebDriver

HERE = os.path.dirname(os.path.abspath(__file__))
RECT_KEYS = ("x", "y", "width", "height")
TOL = 0.5

_LEN = re.compile(r"^-?\d+(?:\.\d+)?px$")


def css_equal(got, want, tol=TOL):
    """The comparison rule: a length compares numerically to the same
    tolerance as a rect; a keyword must match exactly. These two comparisons must
    not diverge, or the practice corpus fails a correct engine."""
    if got is None:
        return False
    if got == want:
        return True
    if _LEN.match(got.strip()) and _LEN.match(want.strip()):
        return abs(float(got.strip()[:-2]) - float(want.strip()[:-2])) <= tol
    return False


def rect_ok(got, want, tol=TOL):
    return got is not None and all(
        abs(float(got.get(k, 1e12)) - float(want[k])) <= tol for k in RECT_KEYS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="/app/build/cssdriver")
    ap.add_argument("--expected", default=os.path.join(HERE, "expected.json"))
    ap.add_argument("--documents", default=os.path.join(HERE, "documents"))
    ap.add_argument("--port", type=int, default=14555)
    ap.add_argument("--only", default="",
                    help="restrict to these strata: a comma-separated list of "
                         "names, or of prefixes (`--only css21-`)")
    ap.add_argument("--list-strata", action="store_true",
                    help="print the strata and their sizes, and exit")
    ap.add_argument("--show-failures", type=int, default=8)
    ap.add_argument("--startup-timeout", type=float, default=30.0)
    a = ap.parse_args()

    exp = json.load(open(a.expected))
    vw, vh = exp["viewport"]

    # EVERYTHING RECORDED MUST BE COMPARED. A document that sits in `fixtures`
    # but in no stratum is loaded by nothing and reported by nothing, so a run
    # over a corpus like that would print a clean total while silently skipping
    # part of it. The runner REFUSES rather than warning, because a warning above
    # a green total is the failure mode it exists to prevent.
    planned = [d for v in exp["strata"].values() for d in v]
    orphans = sorted(set(exp["fixtures"]) - set(planned))
    ghosts = sorted(set(planned) - set(exp["fixtures"]))
    dupes = sorted({d for d in planned if planned.count(d) > 1})
    if orphans or ghosts or dupes:
        print(f"COVERAGE FAIL: {len(orphans)} recorded documents in no stratum, "
              f"{len(ghosts)} planned documents with no recording, "
              f"{len(dupes)} documents in two strata")
        for d in (orphans + ghosts + dupes)[:5]:
            print(f"    {d}")
        print("REFUSING to report: this corpus cannot report what it does not run.")
        return 2

    if a.list_strata:
        print(f"{'stratum':<26} {'documents':>9} {'checks':>8}")
        for s in sorted(exp["strata"]):
            n = sum(len(exp["fixtures"][d]) for d in exp["strata"][s])
            print(f"  {s:<24} {len(exp['strata'][s]):>9} {n:>8}")
        return 0

    if not os.path.exists(a.binary):
        print(f"no such binary: {a.binary}  (run `make` in /app first)")
        return 2

    sel = [p for p in a.only.split(",") if p]
    strata = {s: d for s, d in exp["strata"].items()
              if not sel or any(s == p or s.startswith(p) for p in sel)}
    if not strata:
        print(f"nothing matches --only {a.only!r}; known strata: "
              f"{', '.join(sorted(exp['strata']))}")
        return 2

    proc = subprocess.Popen([a.binary, f"--port={a.port}"], start_new_session=True,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    passed = collections.Counter(); total = collections.Counter()
    failures = []
    try:
        d = WebDriver(f"http://127.0.0.1:{a.port}", timeout=15)
        try:
            d.wait_ready(a.startup_timeout)
            d.new_session({"browserName": "cssdriver"})
            d.set_window_rect(vw, vh)
        except Exception as e:
            print(f"the remote end never became usable on port {a.port}: {e}")
            return 1
        for s, docs in sorted(strata.items()):
            for doc in docs:
                try:
                    d.navigate("file://" + os.path.join(a.documents, doc))
                except Exception as e:
                    failures.append(f"{doc}: navigate failed: {str(e)[:70]}")
                    total[s] += len(exp["fixtures"][doc])
                    continue
                # One check per element: the
                # element passes only when its rect AND every resolved value
                # recorded for it are correct. The failure line names the first
                # part that was wrong.
                for eid, want in exp["fixtures"][doc].items():
                    total[s] += 1
                    els = []
                    try:
                        els = d.find_elements("#" + eid)
                        got = d.element_rect(els[0]) if els else None
                    except Exception:
                        got = None
                    why = None
                    if not rect_ok(got, want["rect"]):
                        why = f"rect: got {got} want {want['rect']}"
                    else:
                        for prop, wv in want.get("css", {}).items():
                            try:
                                gv = d.element_css(els[0], prop) if els else None
                            except Exception:
                                gv = None
                            if not css_equal(gv, wv):
                                why = f"{prop}: got {gv!r} want {wv!r}"
                                break
                    if why is None:
                        passed[s] += 1
                    elif len(failures) < 10000:
                        failures.append(f"{doc} #{eid} {why}")
        try:
            d.delete_session()
        except Exception:
            pass
    finally:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM); proc.wait(timeout=10)
        except Exception:
            proc.kill()

    print(f"{'stratum':<32} {'passed':>8} {'total':>8}   rate")
    for s in sorted(strata):
        sc = passed[s] / total[s] if total[s] else 0.0
        print(f"  {s:<30} {passed[s]:>8} {total[s]:>8}   {sc:.4f}")
    P, T = sum(passed.values()), sum(total.values())
    label = f"TOTAL over {len(strata)}/{len(exp['strata'])} strata"
    print(f"  {label:<30} {P:>8} {T:>8}   {P/T if T else 0.0:.4f}")
    # The strata are of very different sizes and answer different questions, so
    # the per-stratum lines above are the signal and this sum is not.
    print("  (the per-stratum lines are the signal; this sum counts every "
          "check equally)")
    if failures and a.show_failures:
        print(f"\nfirst {min(a.show_failures, len(failures))} of {len(failures)} failing checks:")
        for f in failures[:a.show_failures]:
            print("  " + f)
    return 0


sys.exit(main())

#!/usr/bin/env python3
"""Score a candidate W3C WebDriver remote end against Chrome-recorded expectations.

Shared, byte-identical, across every step; the step differs only in which
expectation file and which document set it is pointed at.

Properties this file is responsible for:
  * the candidate runs as an unprivileged uid under a memory ceiling, so it can
    neither read the answers nor take the container down;
  * strata are weighted equally, so a large family cannot make a small
    behaviour optional;
  * documents are emitted round-robin across strata, so a deadline truncates
    evenly instead of deleting whichever family sorts last;
  * every count is reported with its denominator, and "ran and failed" is
    distinguished from "never ran";
  * a candidate that never becomes usable scores 0;
  * every process of the candidate's uid, for as long as it is graded, must be
    the remote end that was built here and checked by provenance.py, running
    no code but its own and the C library's -- a remote end that hands the
    layout to another program or loaded engine scores 0.
"""
import argparse, collections, json, os, re, signal, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wd import WebDriver, WDError
import provenance

RECT_KEYS = ("x", "y", "width", "height")

_LEN = re.compile(r"^-?\d+(?:\.\d+)?px$")


def css_equal(got, want, tol):
    """Compare a resolved value.

    A LENGTH is compared numerically to the same tolerance as a rect. Comparing
    it as a string would re-impose the browser's sub-pixel convention through the
    back door: the rect tolerance exists precisely so a candidate need not
    reproduce 1/64px snapping, and an exact-string check on "240.016px" would
    fail a specification-correct engine that reports "240px" while its rect
    passes. Everything else - keywords like `block`, `auto`, `none` - is an exact
    string, because there is nothing to be approximately right about.
    """
    if got is None:
        return False
    if got == want:
        return True
    if _LEN.match(got.strip()) and _LEN.match(want.strip()):
        return abs(float(got.strip()[:-2]) - float(want.strip()[:-2])) <= tol
    return False


def interleave(strata, order):
    lists = [list(strata[s]) for s in order]
    out, i = [], 0
    while any(i < len(l) for l in lists):
        for l in lists:
            if i < len(l):
                out.append(l[i])
        i += 1
    return out


def start_candidate(binary, port, run_as, mem_kb, log_path):
    cmd = f"ulimit -v {mem_kb}; exec {binary!r} --port={port}"
    argv = (["runuser", "-u", run_as, "--", "/bin/sh", "-c", cmd] if run_as
            else ["/bin/sh", "-c", cmd])
    logf = open(log_path, "wb")
    return subprocess.Popen(argv, stdout=logf, stderr=logf, start_new_session=True)


def stop_candidate(proc):
    if proc.poll() is not None:
        return
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except Exception:
        proc.terminate()
    try:
        proc.wait(timeout=10)
    except Exception:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except Exception:
            proc.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--expected", required=True)
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--port", type=int, default=14444)
    ap.add_argument("--deadline", type=float, default=1200.0)
    ap.add_argument("--startup-timeout", type=float, default=60.0)
    ap.add_argument("--req-timeout", type=float, default=10.0)
    ap.add_argument("--rect-tol", type=float, default=0.5)
    ap.add_argument("--run-as", default="grader")
    ap.add_argument("--mem-kb", type=int, default=4194304)
    a = ap.parse_args()

    exp = json.load(open(a.expected))
    vw, vh = exp["viewport"]
    docs = exp["fixtures"]
    strata = exp["strata"]
    order = sorted(strata)

    # Denominators are fixed up front from the SHIPPED expectation file, so a
    # run that dies early cannot shrink its own population.
    # One check per measured ELEMENT: it passes only when its rect and every
    # resolved value recorded for it are all correct. Crediting each value on its
    # own would let a constant answer (`display: block` is 549 of step 1's 594
    # display values) earn reward with no layout at all.
    total = collections.OrderedDict((s, 0) for s in order)
    for s in order:
        for doc in strata[s]:
            total[s] += len(docs[doc])
    passed = collections.OrderedDict((s, 0) for s in order)
    ran = collections.OrderedDict((s, 0) for s in order)

    # Everything recorded must be compared. A document that sits in `fixtures`
    # but in no stratum runs nowhere and is reported nowhere.
    planned = {d for s in order for d in strata[s]}
    orphans = sorted(set(docs) - planned)
    ghosts = sorted(planned - set(docs))
    if orphans or ghosts:
        print(f"COVERAGE FAIL: {len(orphans)} recorded documents in no stratum, "
              f"{len(ghosts)} planned documents with no recording")
        for d in (orphans + ghosts)[:5]:
            print(f"    {d}")

    stratum_of = {d: s for s in order for d in strata[s]}
    sequence = interleave(strata, order)

    # Runtime provenance. Nothing of the candidate's uid may be running yet; the
    # monitor first proves on a known process that it can see what it checks.
    mon, watch, ident, cand = None, False, None, None
    if a.run_as:
        cand = provenance.candidate_ids(a.run_as)[:2]
        provenance.sweep(*cand)
        st = os.stat(a.binary)
        ident = (st.st_dev, st.st_ino)
        for _ in range(3):
            watch = provenance.monitor_selftest(cand[0], cand[1], a.run_as)
            if watch:
                break
        print("PROVENANCE runtime inspection: %s" % ("available" if watch else
              "UNAVAILABLE on this platform; the remote end is not watched while it serves"))

    t0 = time.time()
    notes, deadline_hit, docs_done = [], False, 0
    proc = start_candidate(a.binary, a.port, a.run_as, a.mem_kb, "/logs/verifier/candidate.log")
    live = False
    d = WebDriver(f"http://127.0.0.1:{a.port}", timeout=a.req_timeout)
    try:
        try:
            d.wait_ready(a.startup_timeout)
            d.new_session({"browserName": "cssdriver"})
            d.set_window_rect(vw, vh)
            live = True
        except Exception as e:
            notes.append(f"candidate never became usable: {str(e)[:160]}")

        if live and watch:
            mon = provenance.Monitor(cand[0], cand[1], "serve", serve_ident=ident)
            mon.start()

        if live:
            for doc in sequence:
                if mon is not None and mon.violations:
                    notes.append("stopped early: a provenance violation was observed")
                    break
                if time.time() - t0 > a.deadline:
                    deadline_hit = True
                    notes.append(f"deadline {a.deadline}s hit after {docs_done}/{len(sequence)} documents")
                    break
                s = stratum_of[doc]
                try:
                    d.navigate("file://" + os.path.join(a.fixtures, doc))
                except Exception as e:
                    notes.append(f"{doc}: navigate: {str(e)[:80]}")
                    docs_done += 1
                    continue
                for eid, want in docs[doc].items():
                    try:
                        els = d.find_elements("#" + eid)
                    except Exception:
                        els = []
                    got = None
                    if els:
                        try:
                            got = d.element_rect(els[0])
                        except Exception:
                            got = None
                    ran[s] += 1
                    ok = got is not None and all(
                        abs(float(got.get(k, 1e12)) - float(want["rect"][k])) <= a.rect_tol
                        for k in RECT_KEYS)
                    for prop, wv in want.get("css", {}).items():
                        if not ok:
                            break
                        try:
                            gv = d.element_css(els[0], prop) if els else None
                            ok = css_equal(gv, wv, a.rect_tol)
                        except Exception:
                            ok = False
                    if ok:
                        passed[s] += 1
                docs_done += 1
        try:
            d.delete_session()
        except Exception:
            pass
    finally:
        if mon is not None:
            mon.stop()
        stop_candidate(proc)
        if cand:
            provenance.sweep(*cand)

    provenance_fail = []
    if mon is not None:
        provenance_fail = list(mon.violations)
        if mon.error:
            provenance_fail.append("the process monitor failed: %s" % mon.error)
        if not mon.fsuid_ok:
            provenance_fail.append("the process monitor could not take the candidate's identity")
        if mon.seen_ident == 0:
            provenance_fail.append("the monitor never saw the remote end it was checking")

    per = {s: (passed[s] / total[s] if total[s] else 0.0) for s in order}
    empty = [s for s in order if total[s] == 0]
    reward = (sum(per.values()) / len(per)) if per and not empty else 0.0
    if empty:
        notes.append(f"REFUSING to score: strata with a zero population: {empty}")
    if not live:
        reward = 0.0
    if orphans or ghosts:
        reward = 0.0
        notes.append(f"REFUSING to score: corpus coverage mismatch "
                     f"({len(orphans)} unplanned, {len(ghosts)} unrecorded)")
    if provenance_fail:
        reward = 0.0
        notes.append("REFUSING to score: the remote end did not do the work itself")

    report = {
        "reward": round(reward, 6),
        "live": live,
        "candidate_ran_as": a.run_as or "root",
        "euid_of_scorer": os.geteuid(),
        "deadline_hit": deadline_hit,
        "documents_scored": docs_done,
        "documents_total": len(sequence),
        "elapsed_s": round(time.time() - t0, 1),
        "rect_tolerance_px": a.rect_tol,
        "strata": {s: {"passed": passed[s], "ran": ran[s], "total": total[s],
                       "never_ran": total[s] - ran[s],
                       "score": round(per[s], 6)} for s in order},
        "notes": notes[:25],
        "provenance": {"watched": mon is not None,
                       "samples": mon.samples if mon is not None else 0,
                       "processes": len(mon.inspected) if mon is not None else 0,
                       "violations": provenance_fail[:20]},
    }
    json.dump(report, open(a.report, "w"), indent=2)
    for s in order:
        r = report["strata"][s]
        print(f"  {s:<28} {r['passed']}/{r['total']} passed "
              f"({r['ran']} ran, {r['never_ran']} never ran) = {r['score']:.6f}")
    if mon is not None:
        print("PROVENANCE watched %d sample(s) of %d process(es)"
              % (mon.samples, len(mon.inspected)))
    for v in provenance_fail[:20]:
        print("PROVENANCE FAIL: " + provenance.clean(v))
    print(f"SCORE={reward:.6f}")


main()

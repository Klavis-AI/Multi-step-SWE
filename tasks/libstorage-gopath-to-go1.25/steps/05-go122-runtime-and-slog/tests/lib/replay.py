#!/usr/bin/env python3
"""Replay the recorded behavior suite against a tree and score the result.

    replay.py <suite.json> <expect-dir> <lss-binary> <repo-root>
              [--jobs N] [--only PREFIX] [--json OUT] [--covdir DIR]
              [--verbose]
    replay.py <suite.json> - <lss-binary> <repo-root> --record OUT
              [--ids ID,ID,...] [--jobs N] [--covdir DIR]

`--record` is how the verifier runs it. It drives the same scenarios and writes
the normalized answers to OUT without opening the expectation directory at all:
the candidate's server runs at the same uid as this process, so the answers it
is compared against must never be readable here. `judge.py`, run as root,
compares, decides the retry below, and scores. `--ids` limits the run to the
named scenarios, which is how the retry pass is driven.

Same module, same normalization, same scenarios as the recording: the only
difference is that the answers are read from disk instead of written to it.
An input the recording found unstable is absent from the expectation files and
is therefore not scored here either.

One scenario-level retry is allowed, and only for an answer that says the
server never finished -- see `unfinished_retry_ids`. The summary names every
scenario that was retried and every one whose failure survived the retry, so a
run that leaned on it is visible rather than silent.

Exit status is 0 when every scored input matched, 1 otherwise. The JSON
summary is what the verifier reads; the printed lines are for a human reading
the log.
"""
import concurrent.futures
import json
import os
import sys

import harness


def compare(expected, actual):
    """Return a list of (field, expected, actual) triples that differ."""
    if expected == actual:
        return []
    if not isinstance(expected, dict) or not isinstance(actual, dict):
        return [("value", expected, actual)]
    diffs = []
    for key in sorted(set(expected) | set(actual)):
        if expected.get(key) != actual.get(key):
            diffs.append((key, expected.get(key), actual.get(key)))
    return diffs


def clip(value, width=400):
    text = value if isinstance(value, str) else json.dumps(value)
    if text is None:
        return "<missing>"
    return text if len(text) <= width else text[:width] + "...[%d]" % len(text)


def _unfinished(have):
    """Does this answer say the server never finished, rather than what it found?

    Two shapes say it, and both are silence dressed differently:

      * a transport failure -- a `TimeoutError` or a reset connection. The
        request produced no answer at all.
      * HTTP 408 carrying a task still in `running` state. That is this
        server's own words for the same thing: `httputils.WriteTask` selects
        between the task finishing and a `server.tasks.exeTimeout` timer
        (default `1m`), and the timer arm writes the unfinished task with
        `StatusRequestTimeout`. The body is the task, not the result, so the
        answer carries its own admission that the operation had not completed.

    Both say nothing about the tree under test: the box is shared, six
    scenarios run at once, and a request whose recorded answer took
    milliseconds can still miss a 60-second deadline when something else owns
    the CPU. The transport shape was observed once in fourteen full-suite runs
    and the 408 shape once in seven; at 1 118 inputs either one is enough to
    put a false 0.9991 on an oracle that must report 1.

    What this is not is a second chance at a different answer. A tree that
    returns the wrong volume map returns the same wrong volume map on the
    retry, because neither shape here is "the body differs" -- they are "there
    is no body yet".
    """
    if not isinstance(have, dict):
        return False
    if have.get("transport_error"):
        return True
    if have.get("status") != 408:
        return False
    try:
        body = json.loads(have.get("body") or "")
    except (TypeError, ValueError):
        return False
    return isinstance(body, dict) and body.get("state") == "running"


def unfinished_retry_ids(scenarios, expected, actual):
    """Scenario ids whose answers say the server never finished, unpredicted.

    A scenario whose *expectation* is itself one of those answers -- a refused
    connection, a TLS handshake the recording captured as a failure, a 408 the
    recording captured -- is not in this list. That answer is the thing being
    scored, and re-running it would be re-running the test until it agrees.
    """
    out = []
    for scn in scenarios:
        sid = scn["id"]
        got = actual.get(sid) or {}
        for key, want in expected[sid].items():
            if not _unfinished(got.get(key)):
                continue
            if _unfinished(want):
                continue
            out.append(sid)
            break
    return out


def run_scenarios(ids, by_id, configs, lss, repo, covdir, jobs, actual):
    """Run each named scenario and put its answers in `actual`."""
    if not ids:
        return
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=max(1, min(jobs, len(ids)))) as pool:
        futures = {pool.submit(harness.run_scenario, by_id[sid], configs, lss,
                               repo, covdir): sid for sid in ids}
        for fut in concurrent.futures.as_completed(futures):
            actual[futures[fut]] = fut.result()


def summarize(scenarios, expected, actual, retried, verbose=False):
    """Compare `actual` with `expected` and return the summary the scorer reads.

    Shared by `main` and `judge.py`, so a run scored as root and a run scored
    by hand produce the same numbers from the same answers.
    """
    passed, failures = 0, []
    for scn in scenarios:
        sid = scn["id"]
        got = actual.get(sid, {})
        if not isinstance(got, dict):
            got = {}
        for key in sorted(expected[sid]):
            diffs = compare(expected[sid][key], got.get(key))
            if not diffs:
                passed += 1
                if verbose:
                    print("PASS %s/%s" % (sid, key))
                continue
            failures.append({
                "scenario": sid,
                "input": key,
                "diffs": [{"field": f, "expected": clip(e), "actual": clip(a)}
                          for f, e, a in diffs],
            })

    total = passed + len(failures)
    for fail in failures:
        print("FAIL %s/%s" % (fail["scenario"], fail["input"]))
        for d in fail["diffs"]:
            print("      %s:\n        expected: %s\n        actual:   %s"
                  % (d["field"], d["expected"], d["actual"]))

    summary = {
        "total": total,
        "passed": passed,
        "failed": len(failures),
        "score": (float(passed) / total) if total else 0.0,
        "failures": failures[:200],
        "retried": retried,
        "retry_survived": unfinished_retry_ids(scenarios, expected, actual),
    }
    print("behavior: %d/%d passed (%.4f)"
          % (passed, total, summary["score"]))
    return summary


def main(argv):
    suite_path, expect_dir, lss, repo = argv[1:5]
    jobs, only, out_path, verbose, covdir = 4, None, None, False, None
    record_path, ids = None, None
    rest = argv[5:]
    while rest:
        flag = rest.pop(0)
        if flag == "--jobs":
            jobs = int(rest.pop(0))
        elif flag == "--only":
            only = rest.pop(0)
        elif flag == "--record":
            record_path = rest.pop(0)
        elif flag == "--ids":
            ids = [i for i in rest.pop(0).split(",") if i]
        elif flag == "--json":
            out_path = rest.pop(0)
        elif flag == "--covdir":
            covdir = rest.pop(0)
        elif flag == "--verbose":
            verbose = True
        else:
            sys.exit("unknown flag %s" % flag)

    configs, scenarios = harness.load_suite(suite_path)
    if only:
        scenarios = [s for s in scenarios if s["id"].startswith(only)]
    if ids is not None:
        wanted = set(ids)
        scenarios = [s for s in scenarios if s["id"] in wanted]

    if record_path is not None:
        # Answers only. The expectation directory is not opened, and nothing
        # is compared, retried or scored here. With `-` the answers are the
        # last line of stdout and everything else goes to stderr.
        out = sys.stdout
        sys.stdout = sys.stderr
        by_id = {scn["id"]: scn for scn in scenarios}
        actual = {}
        run_scenarios([scn["id"] for scn in scenarios], by_id, configs, lss,
                      repo, covdir, jobs, actual)
        text = json.dumps(actual, sort_keys=True)
        if record_path == "-":
            out.write(text + "\n")
            out.flush()
        else:
            with open(record_path, "w") as fh:
                fh.write(text + "\n")
        print("recorded %d scenario(s)" % len(actual))
        return 0

    expected = {}
    for scn in scenarios:
        path = os.path.join(expect_dir, "%s.json" % scn["id"])
        with open(path) as fh:
            expected[scn["id"]] = json.load(fh)

    by_id = {scn["id"]: scn for scn in scenarios}
    actual = {}
    run_scenarios([scn["id"] for scn in scenarios], by_id, configs, lss, repo,
                  covdir, jobs, actual)

    # One retry, at scenario granularity, for transport failures only. Whole
    # scenario rather than single exchange: a scenario's exchanges are a
    # sequence against one server, and a POST that timed out may have applied
    # on the far side, so replaying that one exchange is not idempotent while
    # re-running the scenario from a fresh scratch directory and a fresh
    # server is. Nothing about the comparison changes -- a tree whose server
    # genuinely hangs, hangs twice.
    retried = unfinished_retry_ids(scenarios, expected, actual)
    if retried:
        print("retrying, the server never finished: %s" % ", ".join(retried))
        run_scenarios(retried, by_id, configs, lss, repo, covdir, jobs, actual)
        still = unfinished_retry_ids(scenarios, expected, actual)
        if still:
            print("unfinished after the retry: %s" % ", ".join(still))

    summary = summarize(scenarios, expected, actual, retried, verbose)
    if out_path:
        with open(out_path, "w") as fh:
            json.dump(summary, fh, indent=2, sort_keys=True)
            fh.write("\n")
    return 0 if not summary["failed"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Turn one step's group results into `reward = gate x score`.

Byte-identical in all fifteen steps; `gen-step.py` copies it beside `test.sh`.
The step-specific part is `/tests/groups.json`, the manifest that declares which
tier every group is in and what the scored ones are worth. Nothing here knows a
group name.

    gate   in {0, 1}   every gate group passed, or the step is zero
    score  in [0, 1]   the weights of the scored groups that passed
    reward = gate x score

Why multiplicative. Under an additive model a candidate banks the inherited
share for doing nothing: carry the previous milestone's workspace forward
untouched and every `reg-*` group passes, which is most of a step's groups at
the far end of the ladder. Multiplying prices it correctly -- a no-op preserves
prior behaviour so its gate is 1, implements nothing new so its score is 0, and
scores exactly 0.

The two ways this can be wrong are both silent, so both are checked here rather
than trusted:

* a group in the manifest with no recorded result is a driver defect, and is
  counted as a failure -- a gate group that never ran has not been shown to
  pass, and a scored group that never ran has not earned its weight;
* a recorded result for a group the manifest does not declare earns nothing and
  is reported, because a group that is scored but unpriced is a group nobody
  reviewed.

`reward_details.json` always names the gate group that vetoed the step and
quotes what it saw. A step that reports 0 without saying which gate failed is
indistinguishable from a candidate that did nothing, and the difference is the
whole point of the gate.

It does the same for a scored group that failed. A scored failure is the
opposite case -- the step is not zero, a fraction of it is missing, and which
fraction is exactly what a reader needs -- and a bare `passed: 0` cannot say
whether the candidate's code was wrong or the case never ran. The case's own
log holds the answer (the panic and the assertion message), it is written
either way, and this is the last place it exists: the verifier container is
discarded with the step.
"""
import json
import pathlib
import re
import sys

# A Rust test failure, a cargo build failure, or an e2e driver's own complaint.
# Failure signatures only. `warning:` is deliberately absent: a cargo log that
# ends in an error is full of warnings first, and quoting the first of them as
# the reason a gate fired would point at the wrong line every time.
INTERESTING = re.compile(
    r"thread '[^']*' panicked at|^\s*error(\[|:)|^assertion|^\s*left:|"
    r"^E2E-FAIL|test result: FAILED|the aggregate case budget")


def reason(log: pathlib.Path, limit: int = 600) -> str:
    """The most useful few lines of a failing case's log.

    A Rust panic puts the location on one line and the message on the next, so
    the match is never enough on its own; take the matching line and the three
    after it, which is the whole of an `assert_eq!` report and the head of a
    hand-written message.
    """
    if not log.is_file():
        return "no log was written: the case did not run"
    try:
        lines = log.read_text(errors="replace").splitlines()
    except OSError as e:
        return f"the case log could not be read: {e}"
    for i, ln in enumerate(lines):
        if INTERESTING.search(ln):
            return " / ".join(x.strip() for x in lines[i:i + 4] if x.strip())[:limit]
    for ln in reversed(lines):
        if ln.strip():
            return ln.strip()[:limit]
    return "the case produced no output"


def results(path: pathlib.Path):
    """`PASS <name>` / `FAIL <name>` lines -> {name: bool}, in file order."""
    out = {}
    if not path.is_file():
        return out
    for ln in path.read_text(errors="replace").splitlines():
        parts = ln.split()
        if len(parts) >= 2 and parts[0] in ("PASS", "FAIL"):
            out[parts[1]] = parts[0] == "PASS"
    return out


def main():
    env = json.load(sys.stdin)
    log_dir = pathlib.Path(env["log_dir"])
    manifest = json.loads(pathlib.Path(env["manifest"]).read_text())

    gate_res = results(log_dir / "gate.txt")
    scored_res = results(log_dir / "groups.txt")

    defects = []

    # --- the gate ----------------------------------------------------------
    gate_ok = 1
    gate_details, gate_failures = {}, []
    for g in manifest["gate"]:
        name = g["name"]
        recorded = name in gate_res
        if recorded:
            ok = gate_res.pop(name)
        else:
            ok = False
            defects.append(f"gate group `{name}` is declared in the manifest but the "
                           f"driver recorded no result for it")
        gate_details[name] = 1 if ok else 0
        if not ok:
            gate_ok = 0
            # An unrecorded group must not borrow the reason out of a log left
            # behind by something else; say that it did not report, which is a
            # different fault and points at the driver rather than the code.
            why = (reason(log_dir / f"{name}.log") if recorded else
                   "the driver recorded no result for this gate group, so it cannot "
                   "have been shown to pass: this is a verifier defect")
            gate_failures.append({"group": name, "why": why})
    for name, ok in gate_res.items():
        defects.append(f"the driver recorded gate group `{name}`, which the manifest "
                       f"does not declare; it neither vetoed nor scored")
        gate_details[name] = 1 if ok else 0

    # --- the scored tier ---------------------------------------------------
    score = 0.0
    weight_total = 0.0
    scored_details, scored_failures = {}, []
    for g in manifest["scored"]:
        name, w = g["name"], float(g["weight"])
        weight_total += w
        ran = name in scored_res
        if ran:
            ok = scored_res.pop(name)
        else:
            ok = False
            defects.append(f"scored group `{name}` is declared in the manifest but the "
                           f"driver recorded no result for it")
        detail = {"passed": 1 if ok else 0, "weight": round(w, 6)}
        if not ok:
            detail["why"] = (reason(log_dir / f"{name}.log") if ran else
                             "the driver recorded no result for this scored group, so "
                             "it never ran to a verdict: this is a verifier defect "
                             "rather than a capability gap")
            scored_failures.append({"group": name, "why": detail["why"]})
        scored_details[name] = detail
        if ok:
            score += w
    for name, ok in scored_res.items():
        defects.append(f"the driver recorded scored group `{name}`, which the manifest "
                       f"does not price; it earned nothing")
        scored_details[name] = {"passed": 1 if ok else 0, "weight": 0.0}

    # A manifest whose weights do not sum to one is a packaging defect, and the
    # honest response is to report the step as it was actually priced rather
    # than to rescale and hide it.
    if abs(weight_total - 1.0) > 1e-6:
        defects.append(f"the manifest's scored weights sum to {weight_total:.6f}, not "
                       f"1.0; the step was scored as priced and its maximum is that sum")

    score = min(max(score, 0.0), 1.0)
    reward = round(gate_ok * score, 6)

    # --- the note ----------------------------------------------------------
    notes = []
    if env["crate_dir"] == "unresolved":
        notes.append("no Cargo.toml declaring the package name `quiche` was found at "
                     "/app or /app/quiche, so the transport crate could not be located: "
                     "every group failed for want of a crate to build, which is a "
                     "workspace-layout result and not one capability gap per group")
    if not env["candidate_build"]:
        notes.append("the candidate workspace did not build (`cargo build --offline` at "
                     "$QUICHE_WORKSPACE failed), so every group failed at compile time "
                     "rather than at run time")
    if not env["harness_build"]:
        notes.append("the verifier harness crate did not build against the candidate's "
                     "public surface: at least one test target failed to compile, so the "
                     "groups in that target failed at compile time. Targets that did "
                     "compile still ran and are scored normally")
    if not env["fixture_ok"]:
        notes.append("the verifier-owned asset corpus failed its integrity check, so any "
                     "group that reads it is unreliable: treat those groups as "
                     "unattributable rather than as capability gaps")
    if env["budget_exhausted"]:
        notes.append("the aggregate case budget was exhausted, so the groups after that "
                     "point were failed without running - they are unattributable, not "
                     "measured capability gaps")
    notes += defects
    if gate_ok == 0:
        notes.append("the gate vetoed this step: "
                     + "; ".join(f["group"] for f in gate_failures)
                     + " failed, so the step is 0 whatever the scored groups did. The "
                       "scored fraction it would otherwise have earned is "
                       f"{score:.6f}, and it is reported below so that a regression and "
                       "an empty implementation do not look alike")
    if not notes:
        notes.append("ok")

    pathlib.Path("/logs/verifier/reward_details.json").write_text(json.dumps({
        "step": env["step_num"],
        "tag": env["tag"],
        "reward": reward,
        "gate": gate_ok,
        "score": round(score, 6),
        "gate_failures": gate_failures,
        "scored_failures": scored_failures,
        "candidate_build": 1 if env["candidate_build"] else 0,
        "harness_build": 1 if env["harness_build"] else 0,
        "fixture_ok": 1 if env["fixture_ok"] else 0,
        "budget_exhausted": 1 if env["budget_exhausted"] else 0,
        "crate_dir": env["crate_dir"],
        "build_note": "; ".join(notes),
        "gate_groups": gate_details,
        "scored_groups": scored_details,
    }, indent=2) + "\n")

    print(f"{reward:.6f}")


if __name__ == "__main__":
    main()

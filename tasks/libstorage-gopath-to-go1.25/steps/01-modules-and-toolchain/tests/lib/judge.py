"""Score the recorded wire suite, as root, and compute the step's reward.

    judge.py retry-ids <score.json>
        Print the scenarios the one transport retry should re-run, comma
        separated (an empty line for none).
    judge.py final <score.json> <retry.json | -> <outdir>
        Compare, apply the retry's answers, and write <outdir>/behavior.json,
        <outdir>/breakdown.json and <outdir>/reward.

Run by `test.sh` as `python3 -I -B`, so neither the working directory nor any
environment variable reaches `sys.path`; this file's own directory, which is
root-owned, is added by hand for `replay`, `harness` and `score`.

Why this is a separate process at a separate uid: the candidate's server and
`score.py` both run as the candidate's uid, so anything they can read, the
server can read. The recorded answers in `expect/` are made root-only by
`test.sh` before any candidate code runs, and only this script opens them.
What `score.py` hands over -- the server's answers and the other three reward
inputs -- arrives in root-owned files that `test.sh` opened for it, and is read
here with a size bound and without following a link.
"""
import json
import os
import stat
import sys

LIB = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, LIB)

import harness  # noqa: E402
import replay  # noqa: E402
import score as scoring  # noqa: E402

# Far above anything a real run writes (the answers are ~1 MB), far below
# anything that could exhaust the container.
MAX_BYTES = 64 * 1024 * 1024


def read_json_bounded(path):
    """A JSON value from `path`, or None: regular file, no link, size-bounded."""
    if path == "-":
        return None
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1 \
                or st.st_size > MAX_BYTES:
            return None
        chunks, left = [], MAX_BYTES + 1
        while left > 0:
            chunk = os.read(fd, min(left, 1 << 20))
            if not chunk:
                break
            chunks.append(chunk)
            left -= len(chunk)
        raw = b"".join(chunks)
        if len(raw) > MAX_BYTES:
            return None
        return json.loads(raw.decode("utf-8", "replace"))
    except (OSError, ValueError, RecursionError):
        return None
    finally:
        os.close(fd)


def load():
    configs, scenarios = harness.load_suite(os.path.join(LIB, "suite.json"))
    expected = {}
    for scn in scenarios:
        with open(os.path.join(LIB, "expect", "%s.json" % scn["id"])) as fh:
            expected[scn["id"]] = json.load(fh)
    return scenarios, expected


def answers_of(obj, scenarios):
    """The per-scenario answer maps out of a record, keeping only the shapes
    a real run produces: a dict per known scenario id."""
    known = set(s["id"] for s in scenarios)
    raw = obj.get("answers") if isinstance(obj, dict) else None
    if not isinstance(raw, dict):
        return {}
    return {sid: v for sid, v in raw.items()
            if sid in known and isinstance(v, dict)}


def pass_one(score_obj, scenarios):
    replay_obj = score_obj.get("replay") if isinstance(score_obj, dict) else None
    return answers_of(replay_obj, scenarios)


def unit(value):
    """A reward input: a number in [0, 1], else None."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    value = float(value)
    if value != value or value < 0.0 or value > 1.0:
        return None
    return value


def cmd_retry_ids(score_path):
    scenarios, expected = load()
    score_obj = read_json_bounded(score_path)
    if not isinstance(score_obj, dict):
        print("")
        return 0
    actual = pass_one(score_obj, scenarios)
    print(",".join(replay.unfinished_retry_ids(scenarios, expected, actual)))
    return 0


def cmd_final(score_path, retry_path, outdir):
    scenarios, expected = load()
    score_obj = read_json_bounded(score_path)
    if not isinstance(score_obj, dict):
        sys.exit("judge: no readable breakdown from score.py")
    inputs = score_obj.get("inputs") if isinstance(
        score_obj.get("inputs"), dict) else {}
    own = unit(inputs.get("own_tests"))
    migrated = unit(inputs.get("migrated"))
    regression = unit(inputs.get("regression"))
    if own is None or migrated is None or regression is None:
        sys.exit("judge: score.py's inputs are missing or out of range")

    actual = pass_one(score_obj, scenarios)
    # The same one retry replay.py's own main applies, for the same reason and
    # with the same rule: whole scenarios whose answer says the server never
    # finished, unless the recording says that is the answer.
    retried = replay.unfinished_retry_ids(scenarios, expected, actual)
    if retried:
        again = answers_of(read_json_bounded(retry_path), scenarios)
        for sid in retried:
            if sid in again:
                actual[sid] = again[sid]
    summary = replay.summarize(scenarios, expected, actual, retried)
    recorded = float(summary["score"])

    # reward = T1 * T2: T1 = migrated, T2 = behavior * regression.
    reward, behavior, preservation = scoring.combine(
        recorded, own, migrated, regression)

    log = {k: v for k, v in score_obj.items() if k != "replay"}
    rec = log.get("recorded") if isinstance(log.get("recorded"), dict) else {}
    rec = dict(rec)
    rec.update({
        "score": round(recorded, 6),
        "passed": summary.get("passed"),
        "total": summary.get("total"),
        # A run that needed the transport retry says so. Nothing here feeds
        # the reward; it is how a 1.0 that was won on the second attempt is
        # told apart from one that was won outright.
        "retried": summary.get("retried") or [],
        "retry_survived": summary.get("retry_survived") or [],
        "failures": (summary.get("failures") or [])[:40],
    })
    log["recorded"] = rec
    log["families"] = {
        "behavior": round(behavior, 6),
        "migrated": round(migrated, 6),
        "regression": round(regression, 6),
        "T1": round(migrated, 6),
        "T2": round(preservation, 6),
    }
    log["reward"] = round(reward, 6)

    with open(os.path.join(outdir, "behavior.json"), "w") as fh:
        json.dump(summary, fh, indent=2, sort_keys=True)
        fh.write("\n")
    with open(os.path.join(outdir, "breakdown.json"), "w") as fh:
        json.dump(log, fh, indent=2, sort_keys=True)
        fh.write("\n")
    with open(os.path.join(outdir, "reward"), "w") as fh:
        fh.write("%.6f\n" % reward)
    print("reward %.6f = T1 %.4f (migrated) x T2 %.4f (behavior %.4f "
          "[recorded %.4f, own %.4f] x regression %.4f)"
          % (reward, migrated, preservation, behavior, recorded, own,
             regression))
    return 0


def main(argv):
    if len(argv) == 3 and argv[1] == "retry-ids":
        return cmd_retry_ids(argv[2])
    if len(argv) == 5 and argv[1] == "final":
        return cmd_final(argv[2], argv[3], argv[4])
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

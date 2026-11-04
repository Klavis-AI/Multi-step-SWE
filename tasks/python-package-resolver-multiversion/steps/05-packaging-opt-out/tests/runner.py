#!/usr/bin/env python3
"""Grade one `uv` binary against a step's scored groups.

This is the verifier's own runner. It takes the same declarative probes the
author used to discover the step's behaviour, but instead of fingerprinting
them it evaluates the `[[assert]]` predicates each probe carries, and rolls
the results up into groups:

    reward = gate * score

`gate` is 1 only when every gate group passes: the inherited regression from
the earlier steps plus proof of life. It earns nothing on its own.
`score` is the weighted fraction of the scored groups that passed; the
weights sum to 1.0.

Run it author-side against each release binary in the chain to check that a
step's assertions pass from its own tag onward and fail at the tag before —
that is the same property Phase 6 measures with real containers, available
here for the cost of a few seconds.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import tomllib
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import mkindex  # noqa: E402

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):  # noqa: D102
        pass


def serve(directory: Path):
    handler = partial(QuietHandler, directory=str(directory))
    ThreadingHTTPServer.allow_reuse_address = True
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, httpd.server_address[1]


def normalise(text: str, work: Path, index: str, pyroot: Path) -> str:
    out = ANSI.sub("", text)
    if index:
        out = out.replace(index, "{INDEX}")
    out = out.replace(str(work), "{WORK}").replace(str(pyroot), "{PYROOT}")
    out = re.sub(r"\b\d+(\.\d+)?\s?(ms|s)\b", "{T}", out)
    out = re.sub(r"\buv(?:\.exe)? \d+\.\d+\.\d+\b", "uv {V}", out)
    out = re.sub(r"127\.0\.0\.1:\d+", "127.0.0.1:{PORT}", out)
    out = re.sub(r"/tmp/[A-Za-z0-9_.-]+", "{TMP}", out)
    out = re.sub(r"\bCPython 3\.\d+\.\d+\b", "CPython {PY}", out)
    out = re.sub(r"[ \t]+$", "", out, flags=re.M)
    return out.strip()


def parse_lock(text: str) -> list[str]:
    """The package set a `uv.lock` names, as sorted `name==version` lines.

    The lock file's own schema revision, its metadata block and its per-fork
    marker guards all change across the chain; the set of packages it resolves
    to does not. Recording the set rather than the file keeps a scenario's
    assertion about the resolver's answer, not about the lock format.

    The file is read as TOML -- `TEST_INTERFACE.md` states that only the
    `package` array of tables is read and that key order inside an element is
    therefore free -- so an element is picked up whatever order its `name` and
    `version` keys are written in. A file that does not parse as TOML falls
    back to the line scan this reader used to be, so a malformed lock still
    records whatever elements it can.
    """
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError:
        data = None
    if isinstance(data, dict):
        out: dict[str, str] = {}
        for element in data.get("package") or []:
            if isinstance(element, dict) and "name" in element and "version" in element:
                out[normalise_name(str(element["name"]))] = str(element["version"])
        out.pop("project", None)
        return sorted(f"{k}=={v}" for k, v in out.items())
    out = {}
    name = None
    for line in text.splitlines():
        line = line.strip()
        if line == "[[package]]":
            name = None
        elif line.startswith("name = "):
            name = line.split("=", 1)[1].strip().strip('"')
        elif line.startswith("version = ") and name:
            out[normalise_name(name)] = line.split("=", 1)[1].strip().strip('"')
            name = None
    out.pop("project", None)
    return sorted(f"{k}=={v}" for k, v in out.items())


def parse_pins(text: str) -> list[str]:
    """The pins a `uv pip compile` run wrote, as sorted `name==version` lines."""
    out: dict[str, str] = {}
    for line in text.splitlines():
        line = line.split("#")[0].strip()
        m = re.match(r"^([A-Za-z0-9._-]+)==([^\s;]+)", line)
        if m:
            out[normalise_name(m.group(1))] = m.group(2)
    return sorted(f"{k}=={v}" for k, v in out.items())


def normalise_name(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name).lower()


def expand(value, index: str, pyroot: str = ""):
    if isinstance(value, str):
        return value.replace("{INDEX}", index).replace("{PYROOT}", pyroot)
    if isinstance(value, list):
        return [expand(v, index, pyroot) for v in value]
    if isinstance(value, dict):
        return {k: expand(v, index, pyroot) for k, v in value.items()}
    return value


def observe(uv: Path, pyroot: Path, probe: dict, port: int, scratch: Path) -> dict:
    """Run one probe and return its recorded fields."""
    pid = probe["id"]
    work = scratch / pid
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    (work / "home").mkdir()

    pyver = str(probe.get("python", "3.12"))
    pydir = pyroot / f"cpython-{pyver}-linux-x86_64-gnu"
    index = f"http://127.0.0.1:{port}/{pid}/simple" if probe.get("index") else ""

    env = dict(os.environ)
    env.update({
        "HOME": str(work / "home"),
        "PATH": f"{pydir / 'bin'}:/usr/bin:/bin",
        "UV_CACHE_DIR": str(work / "cache"),
        "UV_NO_PROGRESS": "1",
        "NO_COLOR": "1",
        "UV_PYTHON_DOWNLOADS": "never",
        "UV_PYTHON_INSTALL_DIR": str(work / "pyinstall"),
        "UV_TOOL_DIR": str(work / "tools"),
        "UV_TOOL_BIN_DIR": str(work / "bin"),
        "XDG_BIN_HOME": str(work / "bin"),
        "XDG_DATA_HOME": str(work / "share"),
        "XDG_CONFIG_HOME": str(work / "config"),
        "COLUMNS": "100",
    })
    for k in ("VIRTUAL_ENV", "UV_EXCLUDE_NEWER", "CONDA_PREFIX",
              "UV_INDEX_URL", "UV_EXTRA_INDEX_URL", "UV_PRERELEASE"):
        env.pop(k, None)
    env.update({k: str(expand(v, index, str(pyroot))) for k, v in (probe.get("env") or {}).items()})

    for rel, body in (probe.get("files") or {}).items():
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(expand(body, index, str(pyroot)))

    def sh(cmd, **kw):
        return subprocess.run([str(c) for c in cmd], capture_output=True, text=True,
                              timeout=probe.get("timeout", 180), env=env,
                              cwd=str(work), **kw)

    fields: dict[str, object] = {}
    try:
        # Every setup command's exit code is kept, not just the last one.
        #
        # `setup_status` names the single setup command by default, which is what
        # most probes have and what all of them assert. Where a probe runs
        # several, to build a state such as `init` then `lock`, 
        # the last one keeps the name, because that is what the existing
        # assertions were written against and `runner.py` will not silently
        # change what a shipped assertion means. The earlier ones are recorded as
        # `setup_status_1`, `setup_status_2`, ... counting from the first, so a
        # probe can assert on the command actually under test and a probe whose
        # scaffolding succeeds while its subject fails can no longer read as a
        # pass. In `venv` mode the `uv venv` that creates the environment is
        # setup command zero.
        setup_rcs: list[int] = []
        if probe.get("mode", "venv") == "venv":
            v = sh([uv, "venv", "--python", pydir / "bin" / f"python{pyver}", ".venv"])
            setup_rcs.append(v.returncode)
            env["VIRTUAL_ENV"] = str(work / ".venv")
        for extra in probe.get("setup_argv") or []:
            setup_rcs.append(sh([uv] + expand(extra, index, str(pyroot))).returncode)
        if setup_rcs:
            fields["setup_status"] = setup_rcs[-1]
            for i, rc in enumerate(setup_rcs[:-1], 1):
                fields[f"setup_status_{i}"] = rc
        p = sh([uv] + expand(probe["argv"], index, str(pyroot)), input=expand(probe.get("stdin", ""), index, str(pyroot)))
    except subprocess.TimeoutExpired:
        return {"_timeout": True}
    except OSError as exc:
        return {"_error": repr(exc)}

    fields["status"] = p.returncode
    fields["stdout"] = normalise(p.stdout, work, index, pyroot)
    fields["stderr"] = normalise(p.stderr, work, index, pyroot)
    wants = {a["field"] for a in probe.get("assert", [])}
    if "freeze" in wants:
        f = sh([uv, "pip", "freeze"])
        fields["freeze"] = sorted(
            normalise(l, work, index, pyroot)
            for l in f.stdout.splitlines() if l.strip())
    if "lockset" in wants:
        lock = work / "uv.lock"
        fields["lockset"] = parse_lock(lock.read_text()) if lock.exists() else None
    if "pinset" in wants:
        fields["pinset"] = parse_pins(p.stdout)
    for want in wants:
        if want.startswith("file:"):
            f = work / want[5:]
            fields[want] = normalise(f.read_text(), work, index, pyroot) if f.exists() else None
        elif want.startswith("glob:"):
            fields[want] = sorted(str(q.relative_to(work)) for q in work.glob(want[5:]))
        elif want.startswith("exists:"):
            fields[want] = (work / want[7:]).exists()
    return fields


def check(fields: dict, rule: dict) -> tuple[bool, str]:
    if fields.get("_timeout"):
        return False, "probe timed out"
    if fields.get("_error"):
        return False, f"probe could not run: {fields['_error']}"
    got = fields.get(rule["field"])
    for kind, want in rule.items():
        if kind == "field":
            continue
        if kind == "equals":
            if got != want:
                return False, f"{rule['field']}: expected {want!r}, got {got!r}"
        elif kind == "contains":
            for needle in ([want] if isinstance(want, str) else want):
                if needle not in (got or ""):
                    return False, f"{rule['field']}: missing {needle!r}"
        elif kind == "contains_any":
            if not any(n in (got or "") for n in want):
                return False, f"{rule['field']}: none of {want!r} present"
        elif kind == "not_contains":
            for needle in ([want] if isinstance(want, str) else want):
                if needle in (got or ""):
                    return False, f"{rule['field']}: unexpected {needle!r}"
        elif kind == "matches":
            if not re.search(want, got if isinstance(got, str) else json.dumps(got)):
                return False, f"{rule['field']}: does not match /{want}/"
        elif kind == "not_matches":
            if re.search(want, got if isinstance(got, str) else json.dumps(got)):
                return False, f"{rule['field']}: unexpectedly matches /{want}/"
        else:
            return False, f"unknown assertion {kind!r}"
    return True, ""


def load_probes(base: Path) -> dict[str, dict]:
    probes: dict[str, dict] = {}
    for f in sorted(base.rglob("*.toml")):
        data = tomllib.loads(f.read_text())
        data.setdefault("id", f.stem)
        if data["id"] in probes:
            raise SystemExit(f"duplicate probe id {data['id']!r} at {f}")
        probes[data["id"]] = data
    return probes


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--uv", type=Path, required=True)
    ap.add_argument("--probes", type=Path, action="append", required=True,
                    help="probe directory; repeat for the inherited steps")
    ap.add_argument("--asserts", type=Path, action="append", default=[],
                    help="sidecar of observed assertions; repeat alongside --probes")
    ap.add_argument("--groups", type=Path, required=True)
    ap.add_argument("--pythons", type=Path, required=True)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--details", type=Path)
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    for k in ("uv", "groups", "pythons"):
        setattr(args, k, getattr(args, k).resolve())
    args.probes = [p.resolve() for p in args.probes]
    args.asserts = [p.resolve() for p in args.asserts]

    groups = tomllib.loads(args.groups.read_text())
    probes: dict[str, dict] = {}
    for base in args.probes:
        for pid, probe in load_probes(base).items():
            if pid in probes:
                raise SystemExit(f"duplicate probe id {pid!r} across probe directories")
            probes[pid] = probe
    for sidecar in args.asserts:
        for pid, rules in json.loads(sidecar.read_text()).items():
            if pid in probes:
                probes[pid]["assert"] = rules

    needed = {pid for g in groups["gate"] + groups["scored"] for pid in g["probes"]}
    missing = needed - set(probes)
    if missing:
        raise SystemExit(f"groups name probes that do not exist: {sorted(missing)}")

    # A group that names no probe has nothing to fail, so it would pass for
    # free and earn its weight. Refuse to grade one.
    empty = sorted(g["name"] for g in groups["gate"] + groups["scored"] if not g["probes"])
    if empty:
        raise SystemExit(f"groups name no probes: {empty}")
    # A probe with no rules collects no failing reasons, and the roll-up reads
    # "no reasons" as a pass -- so a group naming one would be scored on
    # nothing. Refuse to grade instead, the same way a missing probe does.
    vacuous = sorted(pid for pid in needed if not probes[pid].get("assert"))
    if vacuous:
        raise SystemExit(f"groups name probes with no assertions: {vacuous}")

    index_root = Path(tempfile.mkdtemp(prefix="uvidx-"))
    for pid in sorted(needed):
        if probes[pid].get("index"):
            mkindex.build_index(probes[pid]["index"], index_root / pid, f"/{pid}")
    httpd, port = serve(index_root)
    scratch = Path(tempfile.mkdtemp(prefix="uvrun-"))

    outcomes: dict[str, tuple[bool, list[str]]] = {}
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(observe, args.uv, args.pythons, probes[pid], port, scratch): pid
                for pid in sorted(needed)}
        for fut in cf.as_completed(futs):
            pid = futs[fut]
            try:
                fields = fut.result()
            except Exception as exc:  # noqa: BLE001
                outcomes[pid] = (False, [f"harness error: {exc!r}"])
                continue
            reasons = []
            for rule in probes[pid].get("assert", []):
                ok, why = check(fields, rule)
                if not ok:
                    reasons.append(why)
            outcomes[pid] = (not reasons, reasons)
    httpd.shutdown()
    shutil.rmtree(scratch, ignore_errors=True)
    shutil.rmtree(index_root, ignore_errors=True)

    def roll(group: dict) -> dict:
        results = {pid: outcomes[pid] for pid in group["probes"]}
        failed = [pid for pid, (ok, _) in results.items() if not ok]
        return {
            "name": group["name"], "weight": group.get("weight"),
            "passed": not failed, "failed_probes": failed,
            "reasons": {pid: results[pid][1] for pid in failed},
        }

    gate = [roll(g) for g in groups["gate"]]
    scored = [roll(g) for g in groups["scored"]]
    gate_ok = all(g["passed"] for g in gate)
    total_weight = sum(g["weight"] for g in groups["scored"])
    score = sum(g["weight"] for g, r in zip(groups["scored"], scored) if r["passed"])
    score = score / total_weight if total_weight else 0.0
    reward = round(score, 6) if gate_ok else 0.0

    details = {"gate_passed": gate_ok, "score": round(score, 6), "reward": reward,
               "gate": gate, "scored": scored,
               "weights_sum": round(total_weight, 6)}
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps({"reward": reward}) + "\n")
    if args.details:
        args.details.parent.mkdir(parents=True, exist_ok=True)
        args.details.write_text(json.dumps(details, indent=1) + "\n")
    if not args.quiet:
        print(json.dumps(details, indent=1))
    else:
        print(f"gate={'pass' if gate_ok else 'FAIL'} score={score:.4f} reward={reward:.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

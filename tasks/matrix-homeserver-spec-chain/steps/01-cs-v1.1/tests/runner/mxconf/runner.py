"""Run scenarios against a server: record a reference transcript, or verify."""

from __future__ import annotations

import concurrent.futures as cf
import json
import os
import shutil
import time
import traceback
from pathlib import Path

from .compare import compare_transcripts
from .ctx import Ctx, ScenarioAbort
from .registry import Scenario, all_scenarios
from .specindex import load_frozen
from .targets import Candidate, Synapse, free_port


# A scenario may ask for the *oracle* to be configured differently, where a
# release changes a behaviour the reference keeps behind a deployment setting.
# The candidate is never configured differently: for it the behaviour is the
# specification's, not a switch.
ORACLE_CONFIG = {
    # Release v1.11 moves media behind authentication; the reference keeps the
    # older behaviour unless this is on, and steps before v1.11 need it off.
    "authmedia": {"enable_authenticated_media": True},
    # `POST /login/get_token` is optional in release v1.9, and the reference
    # keeps it behind a deployment setting of its own.  The server under test
    # offers it unconditionally -- that is what its ticket asks for -- so this
    # setting exists only to make the *reference* comparable.
    "logintoken": {"login_via_existing_session": {
        "enabled": True, "require_ui_auth": True, "token_timeout": "2m"}},
}


def oracle_extra(sc: Scenario) -> dict:
    out: dict = {}
    for need in sc.needs:
        out.update(ORACLE_CONFIG.get(need, {}))
    return out


# --------------------------------------------------------------------------- #
def _run_one(sc: Scenario, ctx: Ctx) -> tuple[list[dict], str | None]:
    err = None
    try:
        sc.fn(ctx)
    except ScenarioAbort as exc:
        err = f"aborted: {exc}"
    except Exception as exc:                       # noqa: BLE001
        err = f"{type(exc).__name__}: {exc}\n{traceback.format_exc()[-1200:]}"
    return ctx.transcript(), err


def port_block(base: int, i: int, attempt: int) -> int:
    """First port of the block scenario `i` uses on its `attempt`-th try.

    Linux hands out ephemeral source ports from 32768-60999 by default, so a
    listener parked in that window is raced for by every outbound connection
    on the machine as well as by the other scenarios.  Blocks are folded back
    into 8000-32000, which nothing else here claims.
    """
    LO, HI = 8000, 32000
    return LO + (base - LO + i * 8 + attempt * 977) % (HI - LO)


class Harness:
    def __init__(self, *, index_path: Path, workroot: Path, port_base: int = 21000):
        self.spec = load_frozen(index_path)
        self.workroot = Path(workroot).resolve()
        self.workroot.mkdir(parents=True, exist_ok=True)
        self.port_base = port_base

    # ---- record (authoring time, against pinned Synapse) ------------------ #
    @staticmethod
    def _needs_federation(sc: Scenario) -> bool:
        """Does this scenario need a second, real homeserver to talk to?"""
        return "federation" in sc.needs

    @staticmethod
    def _needs_tls(sc: Scenario) -> bool:
        """Does it need the server under test to be reachable *as* a homeserver?

        A scenario that federates with something of its own -- `mxconf/fakefed`
        -- needs the HTTPS listener and a server name that names its port, but
        no partner homeserver.  Starting one anyway costs a Synapse per
        scenario for nothing.
        """
        return "federation" in sc.needs or "tls" in sc.needs

    def record(self, scenarios: list[Scenario], python: str, outdir: Path,
               workers: int = 8) -> dict:
        outdir.mkdir(parents=True, exist_ok=True)
        results: dict[str, dict] = {}

        def job(i_sc):
            i, sc = i_sc
            run = self.workroot / "rec" / sc.sid.replace("/", "_")
            if run.exists():
                shutil.rmtree(run)
            run.mkdir(parents=True)
            fed = self._needs_federation(sc)
            tls_needed = self._needs_tls(sc)
            hs = peer = None
            for attempt in range(6):
                # Ports are raced for by the other recordings running in
                # parallel, so a failed bind retries the whole set on a fresh
                # block -- the federation partner included, since losing the
                # race on its port is exactly as likely.
                base = port_block(self.port_base, i, attempt)
                port = free_port(base)
                tls = free_port(port + 1) if tls_needed else 0
                name = f"127.0.0.1:{tls}" if tls_needed else f"127.0.0.1:{port}"
                shutil.rmtree(run / "hs1", ignore_errors=True)
                shutil.rmtree(run / "hs2", ignore_errors=True)
                hs = Synapse(python=python, rundir=run / "hs1", server_name=name,
                             port=port, tls_port=tls, extra=oracle_extra(sc))
                hs.prepare()
                if not hs.start():
                    hs.stop()
                    hs = None
                    continue
                if not fed:
                    break
                pport = free_port(port + 2)
                ptls = free_port(pport + 1)
                peer = Synapse(python=python, rundir=run / "hs2",
                               server_name=f"127.0.0.1:{ptls}", port=pport,
                               tls_port=ptls, extra=oracle_extra(sc))
                peer.prepare()
                if peer.start():
                    break
                peer.stop()
                hs.stop()
                hs = peer = None
            if hs is None:
                log = run / "hs1" / "synapse.out"
                return sc.sid, {"error": "synapse failed to start",
                                "log": log.read_text()[-2000:] if log.exists() else ""}
            try:
                ctx = Ctx(hs.base_url, self.spec, name)
                ctx.syms.aliases = {name: "<HS1>"}
                if peer is not None:
                    ctx.attach_peer(peer.base_url, peer.server_name)
                    ctx.syms.aliases[peer.server_name] = "<HS2>"
                ctx.extra["server"] = hs
                ctx.extra["peer"] = peer
                ctx.extra["python"] = python
                ctx.extra["rundir"] = run
                tr, err = _run_one(sc, ctx)
                ctx.close()
                return sc.sid, {"transcript": tr, "error": err,
                                "tags": list(sc.tags), "step": sc.step}
            finally:
                hs.stop()
                if peer is not None:
                    peer.stop()

        with cf.ThreadPoolExecutor(max_workers=workers) as pool:
            for sid, res in pool.map(job, list(enumerate(scenarios))):
                results[sid] = res
                status = "ok" if not res.get("error") else f"ERR {res['error'][:90]}"
                n = len(res.get("transcript") or [])
                print(f"  rec {sid}: {n} checks {status}", flush=True)

        for sid, res in results.items():
            path = outdir / (sid.replace("/", "__") + ".json")
            path.write_text(json.dumps(res, indent=1, sort_keys=True))
        return results

    # ---- verify (scoring time, against the candidate) --------------------- #
    def verify(self, scenarios: list[Scenario], expected: dict[str, list],
               appdir: Path, workers: int = 6, timeout: float = 180.0,
               python: str | None = None, deadline: float | None = None) -> dict:
        """Score each scenario against `expected`, which is already in memory.

        The transcripts are passed in rather than read from disk here because
        by the time a candidate is running there is no copy of them left on
        disk to read: a server that could open the file it is being scored
        against could answer from it instead of implementing anything."""
        out: dict[str, dict] = {}

        def job(i_sc):
            i, sc = i_sc
            # A candidate whose server never converges costs every scenario its
            # full poll budget, and the sum of those can outrun the wall clock
            # the step is given.  Past the deadline the remaining scenarios are
            # scored as not run rather than left to have the verifier killed
            # with no reward file at all -- which would cost the trial every
            # step after this one as well as this one.
            if deadline is not None and time.time() > deadline:
                return sc.sid, {"checks": len(expected.get(sc.sid) or []),
                                "passed": 0, "score": 0.0,
                                "failures": [{"i": -1, "label": "not run",
                                              "problems": [
                                                  "the step's time ran out "
                                                  "before this scenario "
                                                  "started"]}]}
            # `scen` stays root's: each attempt gets a new run directory inside
            # it (the candidate account's, from `prepare()` on) and a log
            # directory beside that one (root's alone).  A retry never reuses a
            # directory the candidate account has already owned.
            scen = self.workroot / "ver" / sc.sid.replace("/", "_")
            if scen.exists():
                shutil.rmtree(scen)
            scen.mkdir(parents=True)
            run = scen
            fed = self._needs_federation(sc)
            tls_needed = self._needs_tls(sc)
            last = ""
            cand = peer = None
            for attempt in range(6):
                base = port_block(self.port_base, i, attempt)
                port = free_port(base)
                sport = free_port(port + 1)
                name = f"127.0.0.1:{sport}" if tls_needed else f"127.0.0.1:{port}"
                run = scen / f"a{attempt}"
                cand = Candidate(workdir=appdir, rundir=run, server_name=name,
                                 http_port=port, https_port=sport,
                                 logdir=scen / f"a{attempt}.log")
                cand.prepare()
                if not cand.start():
                    last = cand.tail(25)
                    cand.stop()
                    cand = None
                    continue
                if not fed:
                    break
                # The partner races for its port against every other scenario
                # running in parallel; a lost race retries the pair rather than
                # failing the scenario outright.
                pport = free_port(port + 2)
                ptls = free_port(pport + 1)
                # The partner is the harness's own server and runs as root, so its
                # state lives outside `run`, which the candidate account owns and
                # could rearrange: a planted module on the partner's import path,
                # a swapped config or a rewritten partner database. Root-only.
                prun = self.workroot / "peer" / sc.sid.replace("/", "_")
                shutil.rmtree(prun, ignore_errors=True)
                prun.mkdir(mode=0o700, parents=True)
                os.chmod(prun.parent, 0o700)
                peer = Synapse(python=python or "python3", rundir=prun / "hs2",
                               server_name=f"127.0.0.1:{ptls}", port=pport,
                               tls_port=ptls, extra=oracle_extra(sc))
                try:
                    peer.prepare()
                    started = peer.start()
                except Exception as exc:                        # noqa: BLE001
                    last = f"the federation partner could not be prepared: {exc}"
                    started = False
                if started:
                    break
                last = "the federation partner did not start"
                peer.stop()
                cand.stop()
                cand = peer = None
            if cand is None:
                return sc.sid, {"checks": 0, "passed": 0, "score": 0.0,
                                "failures": [{"i": -1, "label": "startup",
                                              "problems": ["server did not start: "
                                                           + last]}]}
            try:
                ctx = Ctx(cand.base_url, self.spec, name, timeout=timeout)
                ctx.syms.aliases = {name: "<HS1>"}
                if peer is not None:
                    ctx.attach_peer(peer.base_url, peer.server_name)
                    ctx.syms.aliases[peer.server_name] = "<HS2>"
                ctx.extra["server"] = cand
                ctx.extra["peer"] = peer
                ctx.extra["rundir"] = run
                tr, err = _run_one(sc, ctx)
                ctx.close()
                # Look at the process tree now rather than only at startup: a
                # server that started an existing homeserver on its first real
                # request would have passed the startup check.
                stowaway = cand.foreign_child()
                res = compare_transcripts(expected[sc.sid], tr, self.spec)
                if stowaway:
                    res["passed"] = 0
                    res["score"] = 0.0
                    res.setdefault("failures", []).append(
                        {"i": -1, "label": "provenance",
                         "problems": [f"a `{stowaway}` process is running inside "
                                      "the server's process group: the homeserver "
                                      "has to be the program built here, not a "
                                      "front for one written in something else"]})
                if err:
                    res.setdefault("failures", []).append(
                        {"i": -1, "label": "scenario", "problems": [err[:400]]})
                return sc.sid, res
            finally:
                cand.stop()
                if peer is not None:
                    peer.stop()

        with cf.ThreadPoolExecutor(max_workers=workers) as pool:
            for sid, res in pool.map(job, list(enumerate(scenarios))):
                out[sid] = res
        return out

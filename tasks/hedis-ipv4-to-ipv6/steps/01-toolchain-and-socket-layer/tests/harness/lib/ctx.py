"""Scenario context shared by the recorder and the replayer.

A scenario is a module exposing:

    PORTS        how many consecutive TCP ports it needs
    FAMILY       'behaviour' or 'gate'
    def run(ctx) emits one observation per scored input

`record.py` runs a scenario against the pinned baseline and writes what came
back. `replay.py` runs the *same code* against a candidate tree and compares.
Because both use this class, an expectation cannot be produced any other way
than by running a tree.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import norm  # noqa: E402
import srv  # noqa: E402


def sort_array(value):
    """Sort a ['array', [...]] reply by each element's canonical JSON.

    Used for replies whose order depends on Redis' per-process hash seed
    (`SMEMBERS`, `HKEYS`, `KEYS`, `CONFIG GET`, ...). Ordering is the only thing
    removed; every element is compared in full.
    """
    if isinstance(value, list) and len(value) == 2 and value[0] == "array" \
            and isinstance(value[1], list):
        return ["array", sorted(value[1], key=lambda e: json.dumps(e, sort_keys=True))]
    return value


def sort_lines(text):
    if isinstance(text, list) and len(text) == 2 and isinstance(text[1], str):
        return [text[0], "\n".join(sorted(text[1].splitlines()))]
    if isinstance(text, str):
        return "\n".join(sorted(text.splitlines()))
    return text


class Ctx:
    def __init__(self, name, tree, workspace, base_port, mode,
                 expectations=None):
        self.name = name
        self.tree = tree
        self.workspace = workspace
        self.base_port = base_port
        self.mode = mode                  # 'record' | 'replay'
        self.observations = {}            # name -> {"v":..., "adv":bool, "w":float}
        self.expectations = expectations or {}
        self.notes = []
        self._servers = []

    # -- ports -------------------------------------------------------------
    def port(self, i=0):
        return self.base_port + i

    # -- observations ------------------------------------------------------
    def emit(self, name, value, adv=False, w=1.0):
        if name in self.observations:
            raise KeyError("duplicate observation name %r in %s"
                           % (name, self.name))
        self.observations[name] = {"v": value, "adv": bool(adv), "w": float(w)}

    def emit_reply(self, name, reply, adv=False, w=1.0, sort=False):
        v = norm.reply_to_json(reply)
        if sort:
            v = sort_array(v)
        self.emit(name, v, adv=adv, w=w)

    def emit_text(self, name, text, adv=False, w=1.0, sort=False):
        t = norm.normalize_text(text)
        if sort:
            t = sort_lines(t)
        self.emit(name, t, adv=adv, w=w)

    def note(self, msg):
        self.notes.append(msg)

    # -- process helpers ---------------------------------------------------
    def server(self, name, port, directives, **kw):
        s = srv.Server(self.tree, self.workspace, name, port, directives, **kw)
        self._servers.append(s)
        return s

    def shutdown_all(self):
        for s in reversed(self._servers):
            try:
                s.stop()
            except Exception:
                pass
        self._servers = []


def default_directives(port, host="::1", extra=()):
    """The smallest real configuration that pins *where* a server runs.

    Every directive here exists in the project's own redis.conf and is a
    where-to-run hook: bind address, port, data directory, log file.
    `save ""` and `appendonly no` keep the run from writing background
    snapshots on its own schedule, which is a determinism control, not a
    behaviour switch.
    """
    d = [
        "bind %s" % host,
        "port %d" % port,
        "dir .",
        "logfile server.log",
        "daemonize no",
        'save ""',
        "appendonly no",
    ]
    d.extend(extra)
    return d

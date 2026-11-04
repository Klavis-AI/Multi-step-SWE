"""A second, independent homeserver, for checking the recordings themselves.

Every scored expectation in this task is recorded from one reference
implementation, so the reference, the recordings and the instruction are not
three independent artefacts: the second is derived from the first.  The failure
that leaves is the one where the release's text, the reference's reading of it
and these recordings all agree and are all wrong.

The only thing that catches that is a *different* implementation of the same
published specification, which has no reason to share the mistake.  This starts
one -- conduwuit, a homeserver written in Rust, unrelated to the recording
reference -- so a scenario can be replayed against it and the answers compared.

It is not a grading target and never will be: it is an authoring instrument.
Where it disagrees with the recordings, one of three things is true, and each is
worth knowing: the recording pins a choice the release leaves open, the release
is ambiguous enough that two conforming servers differ, or the recording is
simply wrong.
"""

from __future__ import annotations

import os
import shutil
import signal
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

from .targets import free_port, wait_http

BINARY = Path(__file__).resolve().parents[2] / "conduwuit"


@dataclass
class Conduwuit:
    rundir: Path
    port: int = 0
    server_name: str = ""
    proc: subprocess.Popen | None = field(default=None, repr=False)

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def prepare(self) -> None:
        self.rundir.mkdir(parents=True, exist_ok=True)
        if not self.port:
            self.port = free_port(38200)
        if not self.server_name:
            self.server_name = f"127.0.0.1:{self.port}"
        db = self.rundir / "db"
        db.mkdir(parents=True, exist_ok=True)
        (self.rundir / "conduwuit.toml").write_text(
            "[global]\n"
            f'server_name = "{self.server_name}"\n'
            f'database_path = "{db}"\n'
            f"port = {self.port}\n"
            'address = "127.0.0.1"\n'
            "allow_registration = true\n"
            "yes_i_am_very_very_sure_i_want_an_open_registration_server_prone_"
            "to_abuse = true\n"
            "allow_federation = true\n"
            "allow_encryption = true\n"
            "allow_check_for_updates = false\n"
            "ip_range_denylist = []\n"
            "trusted_servers = []\n"
            'log = "error"\n')

    def start(self, timeout: float = 90.0) -> bool:
        if not BINARY.exists():
            return False
        out = open(self.rundir / "conduwuit.out", "ab")
        self.proc = subprocess.Popen(
            [str(BINARY), "-c", str(self.rundir / "conduwuit.toml")],
            stdout=out, stderr=out, cwd=str(self.rundir),
            preexec_fn=os.setsid)
        return wait_http(self.base_url + "/_matrix/client/versions", timeout,
                         self.proc)

    def stop(self) -> None:
        if self.proc is None:
            return
        try:
            os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
            self.proc.wait(timeout=20)
        except Exception:                                        # noqa: BLE001
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except Exception:                                    # noqa: BLE001
                pass
        self.proc = None

    def __enter__(self) -> "Conduwuit":
        self.prepare()
        if not self.start():
            raise RuntimeError("the second homeserver did not start")
        return self

    def __exit__(self, *exc) -> None:
        self.stop()
        shutil.rmtree(self.rundir, ignore_errors=True)

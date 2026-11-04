#!/usr/bin/env python3
"""What `initdb` leaves behind: PG_VERSION's contents and which of the
directories named in the "Database File Layout" chapter exist."""
import json, os, pathlib, stat, sys

WATCH = ["base", "global", "pg_commit_ts", "pg_dynshmem", "pg_logical",
         "pg_multixact", "pg_notify", "pg_replslot", "pg_serial", "pg_snapshots",
         "pg_stat", "pg_stat_tmp", "pg_subtrans", "pg_tblspc", "pg_twophase",
         "pg_wal", "pg_xlog", "pg_xact", "pg_clog",
         "PG_VERSION", "postgresql.conf", "pg_hba.conf", "pg_ident.conf"]


def collect(datadir):
    d = pathlib.Path(datadir)
    out = {"present": sorted(n for n in WATCH if (d / n).exists())}
    # The data directory is owned by the uid the candidate's server runs as, so
    # it can replace PG_VERSION with a FIFO or a link at any moment, including
    # between a check and the read; a root read_text() on a FIFO never returns.
    # So the check is made on what was opened: O_NOFOLLOW|O_NONBLOCK, a regular
    # file only -- what initdb actually writes -- and at most PG_VERSION_CAP
    # bytes, so this root read is bounded and never follows a link.
    out["PG_VERSION"] = _read_pg_version(d / "PG_VERSION")
    return out


PG_VERSION_CAP = 4096


def _read_pg_version(path):
    try:
        fd = os.open(str(path), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            return None
        data = os.read(fd, PG_VERSION_CAP)
    except OSError:
        return None
    finally:
        os.close(fd)
    return data.decode("utf-8", "replace").strip()


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--datadir", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    pathlib.Path(a.out).write_text(json.dumps(collect(a.datadir), indent=1, sort_keys=True))

#!/usr/bin/env python3
"""Kill anything the candidate left running, and fingerprint the grader's tree.

Two jobs, both about the same assumption: that the number leaving this container
is the number the grader computed.

    sweep.py kill              kill every process owned by the unprivileged uids
                               the candidate's code runs as
    sweep.py fingerprint DIR   print one digest of every file under DIR

The kill sweep matches on the owning uid, never on the text of a command line:
a pattern that matches the program containing it is its own bug.
"""
import hashlib, os, signal, sys

UIDS = {int(os.environ.get("LUA_GRADER_UID", "4242")),
        int(os.environ.get("LUA_BUILD_UID", "4243"))}


def proc_uid(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("Uid:"):
                    return int(line.split()[1])
    except (OSError, ValueError, IndexError):
        pass
    return None


def kill():
    killed = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        pid = int(entry)
        if pid == os.getpid() or proc_uid(pid) not in UIDS:
            continue
        try:
            cmd = open("/proc/%d/cmdline" % pid, "rb").read().replace(b"\0", b" ").strip()
        except OSError:
            cmd = b""
        try:
            os.kill(pid, signal.SIGKILL)
            killed.append("%d %s" % (pid, cmd.decode("latin-1")[:120]))
        except OSError:
            pass
    print("SWEEP: killed %d candidate process(es)" % len(killed))
    for k in killed:
        print("  " + k)
    return len(killed)


def fingerprint(root):
    h = hashlib.sha256()
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            p = os.path.join(dirpath, name)
            h.update(os.path.relpath(p, root).encode("utf-8") + b"\0")
            try:
                with open(p, "rb") as f:
                    while True:
                        b = f.read(1 << 20)
                        if not b:
                            break
                        h.update(b)
            except OSError as e:
                h.update(str(e).encode("utf-8"))
            h.update(b"\0")
    print(h.hexdigest())


if __name__ == "__main__":
    if sys.argv[1] == "kill":
        # One pass can miss a child forked while it scanned /proc; repeat
        # until a pass finds nothing (bounded).
        for _ in range(20):
            if kill() == 0:
                break
    else:
        fingerprint(sys.argv[2])

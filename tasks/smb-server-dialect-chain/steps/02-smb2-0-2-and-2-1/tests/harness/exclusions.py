"""Tests excluded from every scored smbtorture list, with the reason.

The rule for exclusion is the same at every rung: a test is out if what it
measures is not fixed by a Microsoft document (it is a Samba policy), if it
measures wall-clock time, if it is a benchmark, or if it tests one conversion
so many times that the extra copies only pad the set.
"""
import re

EXCLUDED = [
    (re.compile(r"bench", re.I),
     "benchmark: measures throughput, not conformance"),
    (re.compile(r"^smb2\.timestamps\.delayed-"),
     "smbd:writetimeupdatedelay is a Samba policy, not a rule in [MS-SMB2]/[MS-FSCC], "
     "and it is timing-sensitive"),
    (re.compile(r"^base\.delaywrite"),
     "same write-time delay policy at NT1"),
    (re.compile(r"^smb2\.durable-v2-delay\."),
     "measures a wall-clock reconnect delay"),
    (re.compile(r"^smb2\.timestamps\.time_t_(?!0$|-1$)"),
     "one FILETIME conversion tested eight times; two edges are kept"),
    (re.compile(r"^smb2\.lease\.v2_bug15148$|^smb2\.lease\.v1_bug15148$"),
     "a regression test for a specific Samba bug number, not a protocol rule"),
    (re.compile(r"^smb2\.lease\.dynamic_share$"),
     "needs a share whose path changes under the server, which this "
     "configuration does not provide"),
    (re.compile(r"^smb2\.maxfid\."),
     "measures how many file identifiers the server can hold at once -- a "
     "resource limit, not a rule any Microsoft document fixes, and it is "
     "sensitive to what else is running on the host"),
]


def reason(test):
    for rx, why in EXCLUDED:
        if rx.search(test):
            return why
    return None


def filter_tests(tests):
    kept, dropped = [], []
    for t in tests:
        why = reason(t)
        (dropped if why else kept).append((t, why))
    return [t for t, _ in kept], dropped

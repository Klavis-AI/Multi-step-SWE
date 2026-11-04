#!/usr/bin/env python3
"""Step-03 Oracle: spell the Merge fork the way the task's vocabulary does.

`TEST_INTERFACE.md` fixes the fork vocabulary for all seven steps and spells
the Merge `Merge`.  go-ethereum v1.10.23 -- the release this step's Oracle is
built from -- spells the same fork `Merged` in the `tests` fork table, and
renamed it to `Merge` itself one release line later (v1.11.5,
`tests/init.go`).  So this is a backport of upstream's own rename, exactly
parallel to step 01's schema patch, not an invention of this task.

Both spellings are left working: a candidate who reaches the milestone by
carrying the release forward gets `Merge` from upstream anyway, and nothing in
the graded surface asks for `Merged` to stop being accepted.
"""
import pathlib, re, sys

app = pathlib.Path(sys.argv[1])
p = app / "tests/init.go"
s = p.read_text()

if '"Merge": {' in s:
    print("already spells Merge; nothing to do:", p)
    sys.exit(0)

needle = '\t"Merged": {\n'
if needle not in s:
    sys.exit("ORACLE PATCH FAILED: no \"Merged\" fork entry in %s" % p)

# Find the whole `"Merged": { ... },` entry so it can be duplicated under the
# documented name.  The entry ends at the first line that is exactly "\t},".
start = s.index(needle)
end = s.index("\n\t},\n", start) + len("\n\t},\n")
entry = s[start:end]
s = s[:end] + entry.replace('"Merged": {', '"Merge": {', 1) + s[end:]

p.write_text(s)
print("patched", p, "-- Merge now names the same fork as Merged")

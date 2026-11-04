#!/usr/bin/env python3
"""Remove the optimized x86_64 implementation of named multi-buffer hash
stages from a pinned isa-l_crypto tree, and route the public API of every
removed stage at the portable C reference that upstream already ships.

For each stage directory D named on the command line:

  * delete every ``D/*.asm`` -- the directory's multi-buffer kernels, its
    lane-scheduling manager, and the ``*_multibinary.asm`` dispatcher upstream
    uses to select among ISA tiers at load time;
  * delete ``D/aarch64/`` and ``D/riscv64/`` -- upstream ships the same
    multi-buffer widening for those ISAs, as ``.S`` (multi-buffer rounds) and
    as readable C intrinsics (the managers).  Left in place the task is a
    transliteration exercise, so they go with the stage.
  * in ``D/Makefile.am`` and ``cmake/D.cmake``, drop the sources that were
    deleted and re-register the directory's ``*_base_aliases.c`` set for
    x86_64.  On x86_64 the public symbols of the stage then resolve to the
    scalar C implementation in ``*_base.c`` / ``*_ref.c`` -- a complete,
    correct, API-identical library, which is exactly what
    ``make -f Makefile.unx arch=noarch`` builds.

The removed code's C reference is *not* removed: it is the correctness oracle
and the speedup denominator, so it must survive the strip.

``Makefile.nmake`` is upstream-generated (``make -f Makefile.unx
Makefile.nmake``); regenerate it after stripping rather than editing it here.

Idempotent: re-running on an already-stripped tree changes nothing.
"""

import os
import re
import shutil
import sys

# The ladder's stage directories, in ladder order.
STAGES = [
    "sha256_mb",
    "sha1_mb",
    "sha512_mb",
    "md5_mb",
    "sm3_mb",
    "mh_sha1",
    "mh_sha1_murmur3_x64_128",
    "mh_sha256",
    "rolling_hash",
]

# cmake/<stage>.cmake variable prefix, so the same stage can be routed at the
# C reference in that build description too.
CMAKE_PREFIX = {
    "sha256_mb": "SHA256_MB",
    "sha1_mb": "SHA1_MB",
    "sha512_mb": "SHA512_MB",
    "md5_mb": "MD5_MB",
    "sm3_mb": "SM3_MB",
    "mh_sha1": "MH_SHA1",
    "mh_sha1_murmur3_x64_128": "MH_SHA1_MURMUR3",
    "mh_sha256": "MH_SHA256",
    "rolling_hash": "ROLLING_HASH",
}

# Makefile.am source-list variables whose assignment is replaced wholesale.
DROP_VARS = ("lsrc_x86_64", "lsrc_aarch64", "lsrc_riscv64")

# Object stems that need upstream's NOARCH macro once their directory is
# stripped: these three translation units dispatch to per-ISA kernels from
# inside the file, guarded by `#if (!defined(NOARCH)) && defined(__x86_64__)`.
NOARCH_STEMS = {
    "mh_sha1": ["mh_sha1"],
    "mh_sha256": ["mh_sha256"],
    "mh_sha1_murmur3_x64_128": ["mh_sha1_murmur3_x64_128"],
}

ASSIGN_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\+?=)\s*(.*)$")


def logical_lines(text):
    """Yield (start, end, joined) for each make/logical line, 1-indexed spans."""
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        j = i
        while j < len(lines) and lines[j].rstrip().endswith("\\"):
            j += 1
        if j >= len(lines):
            j = len(lines) - 1
        yield i, j, "\n".join(lines[i:j + 1])
        i = j + 1


def emit_assignment(name, op, tokens):
    """Re-emit one assignment, one token per line, make continuation style."""
    if not tokens:
        return "%s %s" % (name, op)
    head = "%s %s " % (name, op)
    out = [head + tokens[0] + (" \\" if len(tokens) > 1 else "")]
    for k, tok in enumerate(tokens[1:]):
        out.append("\t\t" + tok + (" \\" if k < len(tokens) - 2 else ""))
    return "\n".join(out)


def filter_assignment(chunk, deleted):
    """Drop tokens naming deleted files from one logical line.

    Returns (replacement_text_or_None, changed).  ``None`` means the whole
    assignment vanished because nothing was left in it.
    """
    first = chunk.split("\n")[0]
    m = ASSIGN_RE.match(first)
    if not m:
        return chunk, False
    name, op, rest = m.group(1), m.group(2), m.group(3)
    body = rest + "\n" + "\n".join(chunk.split("\n")[1:])
    toks = body.replace("\\\n", " ").replace("\\", " ").split()
    kept = [t for t in toks if t.rstrip("\\") not in deleted]
    if len(kept) == len(toks):
        return chunk, False
    return emit_assignment(name, op, kept), True


def sweep_list_files(root, deleted, paths):
    """Remove references to deleted sources from make/cmake source lists."""
    for rel in paths:
        p = os.path.join(root, rel)
        if not os.path.isfile(p):
            continue
        with open(p) as fh:
            text = fh.read()
        out, changed = [], False
        for _s, _e, chunk in logical_lines(text):
            repl, ch = filter_assignment(chunk, deleted)
            changed = changed or ch
            if repl is not None:
                out.append(repl)
        if changed:
            new = "\n".join(out)
            if not new.endswith("\n"):
                new += "\n"
            with open(p, "w") as fh:
                fh.write(new)


def rewrite_makefile_am(path, noarch_stems):
    """Replace the directory's x86_64/arch source lists with the C reference."""
    with open(path) as fh:
        text = fh.read()
    keep, dropped, base_tokens = [], [], []
    for _s, _e, chunk in logical_lines(text):
        m = ASSIGN_RE.match(chunk.split("\n")[0])
        if not m:
            keep.append(chunk)
            continue
        if m.group(1) == "lsrc_base_aliases":
            # The directory's own scalar-C file set.  lsrc_base_aliases is a
            # single global make variable that every unit's Makefile.am
            # contributes to, so it has to be read as a literal here -- a
            # reference to $(lsrc_base_aliases) would pull in the other eight
            # units' entries as well.
            body = m.group(3) + "\n" + "\n".join(chunk.split("\n")[1:])
            base_tokens += [t for t in body.replace("\\", " ").split() if t]
            keep.append(chunk)
            continue
        if m.group(1) in DROP_VARS:
            dropped.append(m.group(1))
            continue
        keep.append(chunk)
    if not dropped:
        return False
    if not base_tokens:
        raise SystemExit("strip.py: %s has no lsrc_base_aliases to route at" % path)
    out = "\n".join(keep)
    if not out.endswith("\n"):
        out += "\n"
    out += "\n" + emit_assignment("lsrc_x86_64", "+=", base_tokens) + "\n"
    if noarch_stems:
        # The three multi-hash translation units carry an in-file ISA
        # dispatcher inside `#if (!defined(NOARCH)) && defined(__x86_64__...)`.
        # With the dispatcher's kernels gone it has to take the same branch the
        # upstream `arch=noarch` build takes, and this is upstream's own macro.
        objs = " ".join("bin/%s.o bin/shared_ver_%s.o" % (s, s) for s in noarch_stems)
        out += "%s: CFLAGS += -DNOARCH\n" % objs
    with open(path, "w") as fh:
        fh.write(out)
    return True


def cmake_blocks(text):
    return [(m.group(1), m.start(), m.end())
            for m in re.finditer(r"^set\(([A-Za-z0-9_]+)([^)]*)\)", text, re.M)]


def rewrite_cmake(path, prefix):
    """Point a stage's CMake source sets at the portable C reference.

    cmake/<dir>.cmake names every one of the directory's optimized sources, so
    leaving it alone after the sources are deleted would both break that build
    route and hand a solver the exact file list it is being asked to produce.
    """
    if not os.path.isfile(path):
        return False
    with open(path) as fh:
        text = fh.read()
    blocks = dict((name, (s, e)) for name, s, e in cmake_blocks(text))
    if prefix + "_X86_64_SOURCES" not in blocks:
        return False
    base_name = prefix + "_BASE_ALIASES_SOURCES"
    if base_name not in blocks:
        raise SystemExit("strip.py: %s has no %s to route at" % (path, base_name))
    body = text[blocks[base_name][0]:blocks[base_name][1]]
    body = body[body.index("\n"):]            # keep everything after `set(NAME`
    new = text
    targets = [n for n in (prefix + "_X86_64_SOURCES", prefix + "_AARCH64_SOURCES",
                           prefix + "_RISCV64_SOURCES") if n in blocks]
    # Rewrite back-to-front so earlier offsets stay valid.
    for name in sorted(targets, key=lambda n: blocks[n][0], reverse=True):
        s, e = blocks[name]
        if name == prefix + "_X86_64_SOURCES":
            repl = "set(%s%s" % (name, body)
        else:
            repl = "set(%s\n)\n" % name           # those siblings are gone too
        new = new[:s] + repl + new[e:]
    if new == text:
        return False
    with open(path, "w") as fh:
        fh.write(new)
    return True


def _resolve(tokens, table, depth=0):
    """Expand $(VAR) references inside a token list, one directory's worth."""
    out = []
    for t in tokens:
        m = re.fullmatch(r"\$\(([A-Za-z_][A-Za-z0-9_]*)\)", t)
        if m and depth < 4 and m.group(1) in table:
            out += _resolve(table[m.group(1)], table, depth + 1)
        else:
            out.append(t)
    return out


def source_lists(path):
    """(per-arch sources, portable-C sources, shared sources) for one unit.

    Read from that unit's own Makefile.am with one level of $(VAR) expansion,
    because lsrc_base_aliases is a single global make variable that every
    unit's Makefile.am contributes to.
    """
    with open(path) as fh:
        text = fh.read()
    raw = {}
    for _s, _e, chunk in logical_lines(text):
        m = ASSIGN_RE.match(chunk.split("\n")[0])
        if not m:
            continue
        body = m.group(3) + " " + " ".join(chunk.split("\n")[1:])
        toks = [t for t in body.replace("\\", " ").split() if t]
        raw.setdefault(m.group(1), []).extend(toks)
    per_arch = set()
    for var in ("lsrc_x86_64", "lsrc_aarch64", "lsrc_riscv64"):
        per_arch |= set(_resolve(raw.get(var, []), raw))
    ref = set(_resolve(raw.get("lsrc_base_aliases", []), raw))
    shared = set(_resolve(raw.get("lsrc", []), raw))
    return per_arch, ref, shared


def strip_stage(root, stage):
    d = os.path.join(root, stage)
    if not os.path.isdir(d):
        raise SystemExit("strip.py: no such stage directory: %s" % d)
    removed = []
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if os.path.isfile(p) and name.endswith(".asm"):
            os.remove(p)
            removed.append(os.path.relpath(p, root))

    # The stage is more than its assembly.  The per-ISA context managers
    # (*_ctx_<isa>.c, *_mb_mgr_init_<isa>.c) carry the whole lane-scheduling,
    # resubmit and padding logic, and upstream registers them for x86_64
    # alongside the kernels; left in the tree they are the file the candidate
    # is being asked to write.  So everything the unit builds for x86_64 that
    # is neither its portable C reference nor a file it builds everywhere goes
    # with the stage.
    per_arch, ref, shared = source_lists(os.path.join(d, "Makefile.am"))
    for rel in sorted(per_arch - ref - shared):
        if os.path.dirname(rel) != stage:
            continue
        p = os.path.join(root, rel)
        if os.path.isfile(p):
            os.remove(p)
            removed.append(rel)
    for sub in ("aarch64", "riscv64"):
        p = os.path.join(d, sub)
        if os.path.isdir(p):
            for dirpath, _dirs, files in os.walk(p):
                for f in files:
                    removed.append(os.path.relpath(os.path.join(dirpath, f), root))
            shutil.rmtree(p)
    return removed


def main(argv):
    if len(argv) < 3:
        raise SystemExit("usage: strip.py <tree> <stage> [<stage> ...]")
    root, stages = argv[1], argv[2:]
    unknown = [s for s in stages if s not in STAGES]
    if unknown:
        raise SystemExit("strip.py: unknown stage(s): %s" % ", ".join(unknown))

    all_removed = []
    for s in stages:
        n = strip_stage(root, s)
        print("strip: %-26s removed %3d file(s)" % (s, len(n)))
        all_removed += n

    deleted = set(all_removed)
    # Every source list in the tree that could still name a deleted file.
    lists = ["Makefile.am"]
    for s in STAGES:
        lists.append(os.path.join(s, "Makefile.am"))
        lists.append(os.path.join("cmake", s + ".cmake"))
    for extra in ("fips", "aes", "misc", "tests", "examples", "intel-ipsec-mb"):
        p = os.path.join(root, extra, "Makefile.am")
        if os.path.isfile(p):
            lists.append(os.path.relpath(p, root))
    sweep_list_files(root, deleted, lists)

    am = 0
    cm = 0
    for s in stages:
        am += 1 if rewrite_makefile_am(os.path.join(root, s, "Makefile.am"),
                                       NOARCH_STEMS.get(s, [])) else 0
        cm += 1 if rewrite_cmake(os.path.join(root, "cmake", s + ".cmake"),
                                 CMAKE_PREFIX[s]) else 0
    print("strip: %d file(s) removed; Makefile.am rewritten: %d; cmake rewritten: %d"
          % (len(all_removed), am, cm))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

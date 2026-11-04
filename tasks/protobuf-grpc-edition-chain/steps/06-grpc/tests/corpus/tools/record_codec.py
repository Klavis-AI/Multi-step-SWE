#!/usr/bin/env python3
"""Record the codec expectations: --encode, --decode and --decode_raw.

The contract this script exists to honour: *protoc writes every expected byte.*
This script only chooses which message to exercise and authors the text-format
INPUT, deriving it from the message's own descriptor so no expectation is ever
hand-typed.

For each selected message:
    input.txt   authored here from the descriptor
    encode.bin  = protoc --encode=TYPE  < input.txt      (protoc's bytes)
    decode.txt  = protoc --decode=TYPE  < encode.bin     (protoc's text)
    raw.txt     = protoc --decode_raw   < encode.bin     (protoc's text)

Bounded by construction: one protoc call per candidate for the probe and three
per accepted target, with a hard cap on candidates.
"""
import os
import re
import subprocess
import sys

PROTOC = None
ROOT = None
OUT = None
MAX_TARGETS = 48


def run(args, stdin=b"", cwd=None):
    return subprocess.run(args, input=stdin, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, cwd=cwd, timeout=60)


def decode_set(path, inc):
    """protoc's own text rendering of a FileDescriptorSet, used only to learn
    field names and types for input derivation."""
    r = run([PROTOC, "-I" + inc, "--decode=google.protobuf.FileDescriptorSet",
             "google/protobuf/descriptor.proto"], stdin=open(path, "rb").read())
    return r.stdout.decode("utf-8", "replace")


def parse_blocks(text):
    """Indent-based reader for protoc's text output. Returns nested dicts of
    key -> list of (scalar string | dict)."""
    root, stack = {}, [(-1, {})]
    stack = [({}, -1)]
    cur = stack[0][0]
    for raw in text.split("\n"):
        line = raw.rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip())
        while len(stack) > 1 and indent <= stack[-1][1]:
            stack.pop()
        cur = stack[-1][0]
        body = line.strip()
        m = re.match(r'^(\w+)\s*\{$', body)
        if m:
            child = {}
            cur.setdefault(m.group(1), []).append(child)
            stack.append((child, indent))
            continue
        m = re.match(r'^(\w+):\s*(.*)$', body)
        if m:
            cur.setdefault(m.group(1), []).append(m.group(2))
    root = stack[0][0]
    return root


def unq(s):
    return s[1:-1] if len(s) >= 2 and s[0] == '"' else s


SCALAR_INPUT = {
    "TYPE_DOUBLE": "1.5", "TYPE_FLOAT": "2.5",
    "TYPE_INT64": "-9007199254740993", "TYPE_UINT64": "18446744073709551615",
    "TYPE_INT32": "-2147483648", "TYPE_FIXED64": "1234567890123",
    "TYPE_FIXED32": "4294967295", "TYPE_BOOL": "true",
    "TYPE_STRING": '"héllo\\n\\"quoted\\" \\x01"', "TYPE_BYTES": '"\\001\\377\\000abc"',
    "TYPE_UINT32": "4294967295", "TYPE_SFIXED32": "-2147483648",
    "TYPE_SFIXED64": "-9223372036854775808", "TYPE_SINT32": "-12345",
    "TYPE_SINT64": "-1234567890123",
}


def build_index(fileblocks):
    """full_name -> message dict, for every message in the set."""
    idx = {}

    def walk(msgs, scope):
        for m in msgs:
            name = unq(m.get("name", ['""'])[0])
            fq = (scope + "." + name) if scope else name
            idx[fq] = m
            walk(m.get("nested_type", []), fq)
    for f in fileblocks.get("file", []):
        pkg = unq(f.get("package", ['""'])[0]) if "package" in f else ""
        walk(f.get("message_type", []), pkg)
    return idx


def render(msg, idx, depth=0, enums=None):
    """Author a text-format body for `msg`. Depth-bounded; repeated fields get
    two elements so packing is exercised."""
    if depth > 2:
        return []
    lines = []
    for f in msg.get("field", []):
        fname = unq(f.get("name", ['""'])[0])
        ftype = f.get("type", ["TYPE_STRING"])[0]
        label = f.get("label", ["LABEL_OPTIONAL"])[0]
        tname = unq(f.get("type_name", ['""'])[0]) if "type_name" in f else ""
        if "oneof_index" in f and label != "LABEL_REPEATED":
            # only fill the first member of each oneof
            if any(unq(g.get("name", ['""'])[0]) != fname and
                   g.get("oneof_index") == f.get("oneof_index")
                   for g in msg.get("field", [])[:msg.get("field", []).index(f)]):
                continue
        reps = 2 if label == "LABEL_REPEATED" else 1
        for _ in range(reps):
            if ftype in SCALAR_INPUT:
                lines.append("%s: %s" % (fname, SCALAR_INPUT[ftype]))
            elif ftype == "TYPE_ENUM":
                e = (enums or {}).get(tname.lstrip("."))
                lines.append("%s: %s" % (fname, e if e else "0"))
            elif ftype in ("TYPE_MESSAGE", "TYPE_GROUP"):
                sub = idx.get(tname.lstrip("."))
                if sub is None or depth >= 2:
                    continue
                body = render(sub, idx, depth + 1, enums)
                if not body:
                    continue
                open_name = fname
                if ftype == "TYPE_GROUP":
                    open_name = tname.rsplit(".", 1)[-1]
                lines.append("%s {" % open_name)
                lines += ["  " + b for b in body]
                lines.append("}")
    return lines


def collect_enums(fileblocks):
    out = {}

    def walk(enums, scope):
        for e in enums:
            name = unq(e.get("name", ['""'])[0])
            fq = (scope + "." + name) if scope else name
            vals = e.get("value", [])
            if vals:
                out[fq] = unq(vals[0].get("name", ['""'])[0])

    def walkmsg(msgs, scope):
        for m in msgs:
            name = unq(m.get("name", ['""'])[0])
            fq = (scope + "." + name) if scope else name
            walk(m.get("enum_type", []), fq)
            walkmsg(m.get("nested_type", []), fq)
    for f in fileblocks.get("file", []):
        pkg = unq(f.get("package", ['""'])[0]) if "package" in f else ""
        walk(f.get("enum_type", []), pkg)
        walkmsg(f.get("message_type", []), pkg)
    return out


def main():
    global PROTOC, ROOT, OUT
    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    PROTOC = os.environ.get("PROTOC", os.path.join(base, "..", "protoc", "v32.1", "bin", "protoc"))
    inc = os.path.join(base, "..", "protoc", "v32.1", "include")
    ROOT = os.path.join(base, "proto2")
    OUT = os.path.join(base, "expect", "codec")
    os.makedirs(OUT, exist_ok=True)

    manifest_path = os.path.join(ROOT, "MANIFEST.tsv")
    rows = [l.rstrip("\n").split("\t") for l in open(manifest_path, encoding="utf-8") if l.strip()]

    # Spread targets across strata rather than taking the first N files.
    by_stratum = {}
    for stratum, rel, _d in rows:
        by_stratum.setdefault(stratum, []).append(rel)
    ordered = []
    i = 0
    while len(ordered) < len(rows):
        added = False
        for s in sorted(by_stratum):
            if i < len(by_stratum[s]):
                ordered.append((s, by_stratum[s][i]))
                added = True
        if not added:
            break
        i += 1

    man = []
    n = 0
    for stratum, rel in ordered:
        if n >= MAX_TARGETS:
            break
        dsbin = os.path.join(base, "expect", "desc", rel + ".bin")
        if not os.path.exists(dsbin):
            continue
        blocks = parse_blocks(decode_set(dsbin, inc))
        idx = build_index(blocks)
        enums = collect_enums(blocks)
        files = blocks.get("file", [])
        if not files:
            continue
        f0 = files[-1]
        pkg = unq(f0.get("package", ['""'])[0]) if "package" in f0 else ""
        for m in f0.get("message_type", []):
            name = unq(m.get("name", ['""'])[0])
            fq = (pkg + "." + name) if pkg else name
            if m.get("options") and any("map_entry" in o for o in m.get("options", [])):
                continue
            body = render(m, idx, 0, enums)
            if not body:
                continue
            text = "\n".join(body) + "\n"
            enc = run([PROTOC, "-I.", "--encode=" + fq, rel],
                      stdin=text.encode("utf-8"), cwd=ROOT)
            if enc.returncode != 0 or not enc.stdout:
                continue
            stem = rel[:-len(".proto")].replace("/", "__") + "__" + name
            open(os.path.join(OUT, stem + ".in.txt"), "wb").write(text.encode("utf-8"))
            open(os.path.join(OUT, stem + ".enc.bin"), "wb").write(enc.stdout)
            dec = run([PROTOC, "-I.", "--decode=" + fq, rel], stdin=enc.stdout, cwd=ROOT)
            if dec.returncode != 0:
                continue
            open(os.path.join(OUT, stem + ".dec.txt"), "wb").write(dec.stdout)
            raw = run([PROTOC, "--decode_raw"], stdin=enc.stdout)
            if raw.returncode != 0:
                continue
            open(os.path.join(OUT, stem + ".raw.txt"), "wb").write(raw.stdout)
            man.append(("encode", rel, fq, stem + ".in.txt", stem + ".enc.bin"))
            man.append(("decode", rel, fq, stem + ".enc.bin", stem + ".dec.txt"))
            man.append(("decode_raw", rel, fq, stem + ".enc.bin", stem + ".raw.txt"))
            n += 1
            break

    with open(os.path.join(OUT, "MANIFEST.tsv"), "w", encoding="utf-8") as fh:
        for row in man:
            fh.write("\t".join(row) + "\n")
    print("codec targets recorded: %d  (%d manifest rows)" % (n, len(man)))


if __name__ == "__main__":
    sys.exit(main())

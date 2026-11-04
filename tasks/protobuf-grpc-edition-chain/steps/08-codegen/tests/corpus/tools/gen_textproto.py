#!/usr/bin/env python3
"""Derive a text-format instance of a message from a real protoc descriptor.

Usage:
  gen_textproto.py --list  <descset.txt> <primary-file-name>
  gen_textproto.py --emit  <descset.txt> <fully.qualified.MessageName>

<descset.txt> is the output of
  protoc --decode=google.protobuf.FileDescriptorSet google/protobuf/descriptor.proto
fed from a --descriptor_set_out binary. Every type fact used here therefore came
out of protoc; this script only chooses *placeholder values*, never expectations.
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import descriptor_text  # noqa: E402

MAX_DEPTH = 4
SKIP_TYPES = {".google.protobuf.Any"}  # text format gives Any special treatment


def build_index(fds):
    """Return (messages, enums) keyed by fully-qualified name, plus file owner."""
    messages, enums, owner = {}, {}, {}

    def walk_msg(msg, prefix, filename):
        name = prefix + "." + msg.get1("name")
        messages[name] = msg
        owner[name] = filename
        for nested in msg.getall("nested_type"):
            walk_msg(nested, name, filename)
        for en in msg.getall("enum_type"):
            enums[name + "." + en.get1("name")] = en

    for f in fds.getall("file"):
        filename = f.get1("name")
        pkg = f.get1("package", "")
        prefix = "." + pkg if pkg else ""
        for msg in f.getall("message_type"):
            walk_msg(msg, prefix, filename)
        for en in f.getall("enum_type"):
            enums[prefix + "." + en.get1("name")] = en
    return messages, enums, owner


def is_map_entry(msg):
    opts = msg.get1("options")
    return bool(opts) and opts.get1("map_entry") == "true"


def scalar_literal(ftype, seed):
    seed = (seed % 1000) + 1  # keep placeholder literals inside every scalar range
    if ftype == "TYPE_DOUBLE":
        return "%d.5" % seed
    if ftype == "TYPE_FLOAT":
        return "%d.25" % seed
    if ftype == "TYPE_INT64":
        return "-%d" % (seed * 1000 + 7)
    if ftype == "TYPE_UINT64":
        return "%d" % (seed * 1000 + 7)
    if ftype == "TYPE_INT32":
        return "-%d" % (seed * 10 + 1)
    if ftype == "TYPE_FIXED64":
        return "%d" % (seed * 100 + 3)
    if ftype == "TYPE_FIXED32":
        return "%d" % (seed * 100 + 3)
    if ftype == "TYPE_BOOL":
        return "true" if seed % 2 else "false"
    if ftype == "TYPE_STRING":
        return '"s%d"' % seed
    if ftype == "TYPE_BYTES":
        return '"b%d\\001\\377"' % seed
    if ftype == "TYPE_UINT32":
        return "%d" % (seed * 3 + 1)
    if ftype == "TYPE_SFIXED32":
        return "-%d" % (seed * 7 + 2)
    if ftype == "TYPE_SFIXED64":
        return "-%d" % (seed * 7 + 2)
    if ftype == "TYPE_SINT32":
        return "-%d" % (seed * 5 + 3)
    if ftype == "TYPE_SINT64":
        return "-%d" % (seed * 5 + 3)
    raise ValueError("not a scalar: %s" % ftype)


class Emitter:
    def __init__(self, messages, enums):
        self.messages = messages
        self.enums = enums

    def enum_first_value(self, type_name):
        en = self.enums.get(type_name)
        if en is None:
            return None
        vals = en.getall("value")
        return vals[0].get1("name") if vals else None

    def text_field_name(self, field):
        # A group is written in text format under its *message* name, not the
        # lowercased field name protoc synthesises.
        if field.get1("type") == "TYPE_GROUP":
            return field.get1("type_name").rsplit(".", 1)[-1]
        return field.get1("name")

    def emit_field(self, field, indent, stack, out):
        ftype = field.get1("type")
        label = field.get1("label")
        number = int(field.get1("number"))
        tname = field.get1("type_name")
        if tname in SKIP_TYPES:
            return
        pad = "  " * indent
        fname = self.text_field_name(field)
        repeated = label == "LABEL_REPEATED"

        if ftype in ("TYPE_MESSAGE", "TYPE_GROUP"):
            sub = self.messages.get(tname)
            if sub is None:
                return
            if is_map_entry(sub):
                self.emit_map(field, sub, indent, stack, out)
                return
            reps = 2 if repeated else 1
            for _ in range(reps):
                if tname in stack or len(stack) >= MAX_DEPTH:
                    if label != "LABEL_REQUIRED":
                        return
                    out.append("%s%s {}" % (pad, fname))
                    return
                out.append("%s%s {" % (pad, fname))
                self.emit_body(tname, indent + 1, stack + [tname], out)
                out.append("%s}" % pad)
            return

        if ftype == "TYPE_ENUM":
            val = self.enum_first_value(tname)
            if val is None:
                return
            for k in range(2 if repeated else 1):
                out.append("%s%s: %s" % (pad, fname, val))
            return

        for k in range(2 if repeated else 1):
            out.append("%s%s: %s" % (pad, fname, scalar_literal(ftype, number + k)))

    def emit_map(self, field, entry, indent, stack, out):
        pad = "  " * indent
        fname = field.get1("name")
        key_f = next(f for f in entry.getall("field") if f.get1("name") == "key")
        val_f = next(f for f in entry.getall("field") if f.get1("name") == "value")
        vtype = val_f.get1("type")
        vname = val_f.get1("type_name")
        n_entries = 1 if vtype in ("TYPE_MESSAGE", "TYPE_GROUP") else 2
        for i in range(n_entries):
            ktype = key_f.get1("type")
            klit = '"k%d"' % i if ktype == "TYPE_STRING" else (
                ("true" if i % 2 == 0 else "false") if ktype == "TYPE_BOOL"
                else scalar_literal(ktype, i + 1).lstrip("-"))
            out.append("%s%s {" % (pad, fname))
            out.append("%s  key: %s" % (pad, klit))
            if vtype in ("TYPE_MESSAGE", "TYPE_GROUP"):
                sub = self.messages.get(vname)
                if sub is not None and vname not in stack and len(stack) < MAX_DEPTH:
                    out.append("%s  value {" % pad)
                    self.emit_body(vname, indent + 2, stack + [vname], out)
                    out.append("%s  }" % pad)
                else:
                    out.append("%s  value {}" % pad)
            elif vtype == "TYPE_ENUM":
                v = self.enum_first_value(vname)
                if v:
                    out.append("%s  value: %s" % (pad, v))
            else:
                out.append("%s  value: %s" % (pad, scalar_literal(vtype, i + 1)))
            out.append("%s}" % pad)

    def emit_body(self, type_name, indent, stack, out):
        msg = self.messages[type_name]
        fields = msg.getall("field")
        seen_oneofs = set()
        for field in fields:
            oi = field.get1("oneof_index")
            if oi is not None:
                # Exactly one member per oneof may be set.
                if oi in seen_oneofs:
                    continue
                seen_oneofs.add(oi)
            self.emit_field(field, indent, stack, out)

    def emit(self, type_name):
        out = []
        self.emit_body(type_name, 0, [type_name], out)
        return "\n".join(out) + ("\n" if out else "")


def list_messages(messages, owner, primary_file):
    names = []
    for name, msg in messages.items():
        if owner.get(name) != primary_file:
            continue
        if is_map_entry(msg):
            continue
        names.append(name)
    return sorted(names)


def main():
    mode, path = sys.argv[1], sys.argv[2]
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
        fds = descriptor_text.parse(fh.read())
    messages, enums, owner = build_index(fds)
    if mode == "--list":
        for n in list_messages(messages, owner, sys.argv[3]):
            print(n)
        return 0
    if mode == "--emit":
        target = sys.argv[3]
        if not target.startswith("."):
            target = "." + target
        if target not in messages:
            sys.stderr.write("unknown message %s\n" % target)
            return 2
        sys.stdout.write(Emitter(messages, enums).emit(target))
        return 0
    sys.stderr.write("bad mode\n")
    return 2


if __name__ == "__main__":
    sys.exit(main())

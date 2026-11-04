#!/usr/bin/env python3
"""Read compiled classes -- the artifact -- and emit sorted TSV facts.

Rule 11's "artifacts, not text" has a sharper meaning for the removed-API
question than for anything else here. Source says `sqlContext.registerTempTable`
and leaves you to work out what that resolved to; the class file says
`org/apache/spark/sql/SQLContext.registerTempTable:(Ljava/lang/String;)V`,
because the compiler already did the work. A gate that reads the bytecode is
asking what the build actually produced.

The constant pool is parsed here rather than shelled out to `javap`, for the
reason §4.8.2 records: `javap`'s output is a rendering and renderings differ
between JDKs, while the class file is a format with a version number on it. The
same bytes give the same facts on 8 and on 21 by construction.

Fact kinds:

    container    <path>    <n-classes>
    in           <binary/name>   <container>
    class        <binary/name>   <major>
    classversion <major>   <count>
    ref          <binary/name>   <owner>   <member>  <descriptor>
    strconst     <value>   <count>            deduplicated, counted
    error        <path>    <why>

`ref` covers Fieldref, Methodref and InterfaceMethodref alike: "is this API
still referenced" does not care which kind of reference it is, and a gate that
only looked at method calls would miss a removed constant.

`--only <prefix>[,<prefix>...]` restricts the scan to classes whose binary
name starts with one of the prefixes, and it is not an optimisation. This project ships a shaded jar:
5519 of its 5719 classes belong to its dependencies, with class majors running
from 46 to 66. A "the build targets JDK 21" gate pointed at that jar would be
reading biojava's Java 1.2 classes, and a "no removed-API use" gate would be
reporting references written by people who are not the candidate. The scope is
part of the question, so it is stated rather than assumed.

More than one prefix, comma separated, because a step can have two scoped
questions about the same jar at once: step 1 asks "is the project's own code
still compiling to what it compiled to" about `edu/sdsc/mmtf` and "did
`com/databricks` leave the uber jar" about a package the project does not own.
Scanning the whole jar would answer both, and would also answer a dozen
questions nobody asked, about biojava classes written for Java 1.2.

Where a class came from is an `in` fact rather than a column on `class`, so that
scanning target/classes and scanning the jar built from it produce the same
`class`, `ref` and `strconst` lines. Those are the facts a rule reads, and they
should not change because the same bytes arrived in a different container.
"""
import os
import struct
import sys
import zipfile

MAGIC = 0xCAFEBABE

# Tag -> fixed byte length after the tag, for the entries we skip wholesale.
FIXED = {3: 4, 4: 4, 5: 8, 6: 8, 7: 2, 8: 2, 9: 4, 10: 4, 11: 4, 12: 4,
         15: 3, 16: 2, 17: 4, 18: 4, 19: 2, 20: 2}
TWO_SLOT = (5, 6)
REF_TAGS = (9, 10, 11)


class Bad(Exception):
    pass


def parse_pool(data):
    """(pool, this_class_index, major). pool maps index -> (tag, payload)."""
    if len(data) < 10:
        raise Bad('%d byte(s), not a class file' % len(data))
    magic, _minor, major, count = struct.unpack_from('>IHHH', data, 0)
    if magic != MAGIC:
        raise Bad('magic %08x, not a class file' % magic)
    pool, i, off = {}, 1, 10
    while i < count:
        if off >= len(data):
            raise Bad('constant pool runs off the end at entry %d' % i)
        tag = data[off]
        off += 1
        if tag == 1:
            (n,) = struct.unpack_from('>H', data, off)
            pool[i] = (1, data[off + 2:off + 2 + n])
            off += 2 + n
        elif tag in FIXED:
            size = FIXED[tag]
            # Bounds-checked here so a truncated file is our error message and
            # not struct's: the rehearsal reads the text of these.
            if off + size > len(data):
                raise Bad('constant pool runs off the end at entry %d' % i)
            pool[i] = (tag, struct.unpack_from('>%dB' % size, data, off))
            if tag in (7, 8, 16, 19, 20):
                pool[i] = (tag, struct.unpack_from('>H', data, off))
            elif tag in (9, 10, 11, 12, 17, 18):
                pool[i] = (tag, struct.unpack_from('>HH', data, off))
            off += size
        else:
            raise Bad('unknown constant pool tag %d at entry %d' % (tag, i))
        i += 2 if tag in TWO_SLOT else 1
    if off + 6 > len(data):
        raise Bad('truncated before this_class')
    _flags, this_class, _super = struct.unpack_from('>HHH', data, off)
    return pool, this_class, major


def utf8(pool, idx):
    entry = pool.get(idx)
    if not entry or entry[0] != 1:
        raise Bad('index %d is not a Utf8' % idx)
    return entry[1].decode('utf-8', 'replace')


def class_name(pool, idx):
    entry = pool.get(idx)
    if not entry or entry[0] != 7:
        raise Bad('index %d is not a Class' % idx)
    return utf8(pool, entry[1][0])


def scan_class(data):
    """(name, major, refs, strings) for one class file."""
    pool, this_idx, major = parse_pool(data)
    name = class_name(pool, this_idx)
    refs, strings = set(), set()
    for tag, payload in pool.values():
        if tag in REF_TAGS:
            owner_idx, nat_idx = payload
            nat = pool.get(nat_idx)
            if not nat or nat[0] != 12:
                raise Bad('reference points at a non-NameAndType')
            name_idx, desc_idx = nat[1]
            refs.add((class_name(pool, owner_idx),
                      utf8(pool, name_idx), utf8(pool, desc_idx)))
        elif tag == 8:
            strings.add(utf8(pool, payload[0]))
    return name, major, refs, strings


def walk(paths):
    """(container, member-name, bytes) for every class under every path."""
    for path in paths:
        if os.path.isdir(path):
            for root, _dirs, files in os.walk(path):
                for f in sorted(files):
                    if f.endswith('.class'):
                        full = os.path.join(root, f)
                        with open(full, 'rb') as fh:
                            yield path, os.path.relpath(full, path), fh.read()
        elif zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as z:
                for n in sorted(z.namelist()):
                    if n.endswith('.class'):
                        yield path, n, z.read(n)
        elif os.path.exists(path):
            with open(path, 'rb') as fh:
                yield os.path.dirname(path) or '.', os.path.basename(path), fh.read()


def prefixes(only):
    """`--only` as a list; None or empty means no restriction."""
    return tuple(p for p in (only or '').split(',') if p)


def scan(paths, only=None):
    facts, versions, strings = [], {}, {}
    counts = {}
    want = prefixes(only)
    for container, member, data in walk(paths):
        counts.setdefault(container, 0)
        try:
            name, major, refs, strs = scan_class(data)
        except Bad as exc:
            facts.append(('error', '%s!%s' % (container, member), str(exc)))
            continue
        except Exception as exc:                      # pragma: no cover
            facts.append(('error', '%s!%s' % (container, member),
                          '%s: %s' % (type(exc).__name__, exc)))
            continue
        if want and not name.startswith(want):   # a tuple: any prefix wins
            continue
        counts[container] += 1
        facts.append(('class', name, str(major)))
        facts.append(('in', name, container))
        versions[major] = versions.get(major, 0) + 1
        for owner, member_name, desc in refs:
            facts.append(('ref', name, owner, member_name, desc))
        for s in strs:
            strings[s] = strings.get(s, 0) + 1
    for container in sorted(counts):
        facts.append(('container', container, str(counts[container])))
    for major in sorted(versions):
        facts.append(('classversion', str(major), str(versions[major])))
    for s in sorted(strings):
        # Tabs and newlines inside a constant would break the one-fact-per-line
        # contract; they are rare and the value still has to be recognisable.
        # A carriage return too: rules.py reads with universal newlines, so a
        # raw CR splits the fact exactly as a raw LF does.
        facts.append(('strconst', s.replace('\t', '\\t').replace('\n', '\\n')
                      .replace('\r', '\\r'),
                      str(strings[s])))
    if not any(counts.values()) and not any(f[0] == 'error' for f in facts):
        facts.append(('error', ' '.join(paths),
                      'no class files found' if not only
                      else 'no class files under %s' % only))
    return facts


def main():
    args, only = sys.argv[1:], None
    if args[:1] == ['--only']:
        if len(args) < 3:
            raise SystemExit('usage: class-scan.py [--only <prefix>[,<prefix>...]]'
                             ' <dir|jar> [...]')
        only, args = args[1], args[2:]
    if not args:
        raise SystemExit('usage: class-scan.py [--only <prefix>[,<prefix>...]]'
                         ' <dir|jar|class> [...]')
    for f in sorted(scan(args, only)):
        sys.stdout.write('\t'.join(f) + '\n')


if __name__ == '__main__':
    main()

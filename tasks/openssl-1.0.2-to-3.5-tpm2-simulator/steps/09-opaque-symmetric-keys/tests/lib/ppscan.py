#!/usr/bin/env python3
"""Read the diagnostic pragmas a build acts on, through the preprocessor.

    ppscan.py fence  <compile_commands.json> [<compile_commands.json>...]
    ppscan.py harden <compile_commands.json> [<compile_commands.json>...]

The source-text greps beside this file read what the tree SPELLS.  The GCC
manual's own idiom for a pragma built by a macro -- `#define DO_PRAGMA(x)
_Pragma (#x)' -- never spells `#pragma GCC diagnostic ...' or `_Pragma("GCC
diagnostic ...")' anywhere, and the compiler honours it all the same.  What
the compiler acts on is what the preprocessor hands it, so that is what is
read here: every compile line of the build is replayed with -E, and in that
output every pragma the compiler will see is a `#pragma' line, under the line
marker of the file it came from, and a `#pragma GCC system_header' is the flag
`3' on that file's line markers.  Only files outside /usr count: the system's
and OpenSSL's headers are not the tree's.

  fence   a `#pragma GCC|clang diagnostic ignored "-Wdeprecated..."' or
          `"-Wimplicit..."', or a non-system file marked as a system header --
          the same scope as the step-08 fence's source reading
  harden  any `#pragma GCC|clang diagnostic', of any kind, or a non-system
          file marked as a system header -- the step-10 scope

The replayed line keeps only what decides the preprocessor's output (include
paths, macros, the language standard, -f/-m/-O switches that predefine
macros) and drops everything that would change the SHAPE of -E output or hand
the preprocessor to something else: -dM, -P, -fdirectives-only, -Wp,
-Xpreprocessor, -B, -specs, plugins, -wrapper, response files.  A compiler
outside /usr is not run at all and counts as a line that could not be read.

The attribution does not take the tree's word for where a token came from.
The replay runs with -ftrack-macro-expansion=0, so a token a macro produced is
placed where the macro was used and not where it was defined: GCC then writes
no system-header bracket around a system macro used in a tree file (`errno',
`NULL'), and every flag-3 marker on a file outside /usr is that file marked as
a system header, whatever marked it.  And a line-control directive (`#line',
or GCC's own `# <n> "<file>" <flags>') in a tree file is how a file declares
that its own text comes from somewhere else, so every file the replay names
outside /usr is read for one: a directive that names a system path, carries
flags, or spells its operand any other way than a plain number and a plain
file name is a finding.

Prints `TUS <read> <unread> <findings>' and then up to six findings.  Exit 0
when every line was read, 2 otherwise: the caller treats an unread line as a
reading that was not made.
"""
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

# Options whose argument is the next token when not joined.
WITH_ARG_KEEP = ("-I", "-D", "-U", "-isystem", "-idirafter", "-iquote",
                 "-include", "-imacros", "-iprefix", "-iwithprefix",
                 "-iwithprefixbefore", "-isysroot", "-x")
WITH_ARG_DROP = ("-o", "-MF", "-MT", "-MQ", "-Xpreprocessor", "-Xassembler",
                 "-Xlinker", "-wrapper", "-B", "-specs", "-aux-info",
                 "-dumpbase", "-dumpdir", "-dumpbase-ext", "-L", "-l")
# Joined or bare options that are kept.
KEEP_PREFIX = ("-I", "-D", "-U", "-isystem", "-idirafter", "-iquote",
               "-include", "-imacros", "-iprefix", "-iwithprefix",
               "-isysroot", "--sysroot", "-std=", "-O", "-f", "-m", "-x")
KEEP_EXACT = ("-nostdinc", "-nostdinc++", "-undef", "-ansi", "-pthread")
# -f switches that change the shape of -E output or load something.
DROP_F = ("-fdirectives-only", "-fpreprocessed", "-fplugin", "-fdump",
          "-fworking-directory", "-fno-working-directory", "-fcallgraph-info",
          "-fdebug-cpp")
# Appended after everything the line kept, so it is the last word.
REPLAY_TAIL = ["-ftrack-macro-expansion=0"]

MARK = re.compile(r'^#\s+(\d+)\s+"((?:[^"\\]|\\.)*)"((?:\s+\d+)*)\s*$')
PRAGMA = re.compile(r'^\s*#\s*pragma\s+(GCC|clang)\s+(\w+)(.*)$')
FENCE_IGNORED = re.compile(r'^\s*ignored\s*"-W(deprecated|implicit)')


def system(directory, path):
    p = os.path.normpath(os.path.join(directory, path))
    return p.startswith("/usr/")


def unescape(s):
    return re.sub(r"\\(.)", r"\1", s)


def replay_argv(entry):
    d = entry.get("directory", "/")
    argv = entry.get("arguments") or shlex.split(entry.get("command", ""))
    src = entry.get("file")
    if not argv or not src:
        return None, "no command or file"
    cc = argv[0]
    if not system(d, cc):
        return None, "compiler outside /usr: %s" % cc
    out = [cc]
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in WITH_ARG_DROP:
            i += 2
            continue
        if a in WITH_ARG_KEEP:
            if i + 1 < len(argv):
                out += [a, argv[i + 1]]
            i += 2
            continue
        if a.startswith("-f") and a.startswith(DROP_F):
            i += 1
            continue
        if a in KEEP_EXACT or (a.startswith(KEEP_PREFIX) and not a.startswith("-Wp")):
            out.append(a)
        # Everything else -- -c, -E, -S, -o<x>, -M*, -d*, -P, -C, -H, -g*, -W*,
        # -w, -pedantic*, -save-temps, -v, @file, -B<x>, -specs=..., positional
        # tokens -- is dropped; the source is appended below.
        i += 1
    return out + REPLAY_TAIL + ["-E", src], None


# A line-control directive: `#' (or `%:', or the trigraph `??=') first on a
# line, then `line' or a digit.  Read out of the file's text, because in the
# -E output a directive the tree wrote and a marker GCC wrote are the same line.
HASH = re.compile(r"#|%:")
CTRL = re.compile(r"line(?![A-Za-z0-9_$])|[0-9]")
SPLICE = re.compile(r"\\[ \t\f\v]*\n")
COMMENT = re.compile(r"/\*.*?\*/", re.S)
TRIGRAPH = {"=": "#", "/": "\\", "'": "^", "(": "[", ")": "]", "!": "|",
            "<": "{", ">": "}", "-": "~"}
# The one shape that cannot move a token's attribution: a line number, and at
# most a plain file name.  A system path, flags, an escape or a macro is not it.
PLAIN = re.compile(r'\s*[0-9]+\s*(?:"([^"\\\n]*)"\s*)?$')
CTRL_MAX = 64 << 20
_ctrl_cache = {}


def skip_blank(s, i):
    """Past blanks and whole comments, never past a newline outside one."""
    while i < len(s):
        if s[i] in " \t\f\v":
            i += 1
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2)
            if j < 0:
                return len(s)
            i = j + 2
        else:
            break
    return i


def operand(s, i):
    """The rest of a directive's logical line, comments read as one space."""
    out = []
    while i < len(s) and s[i] != "\n":
        if s.startswith("/*", i):
            j = s.find("*/", i + 2)
            out.append(" ")
            i = len(s) if j < 0 else j + 2
        elif s.startswith("//", i):
            break
        elif s[i] == '"':
            j = i + 1
            while j < len(s) and s[j] not in '"\n':
                j += 2 if s[j] == "\\" else 1
            j = min(j, len(s))
            if j < len(s) and s[j] == '"':
                j += 1
            out.append(s[i:j])
            i = j
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def line_controls(path):
    """[(line, operand)] for every line-control directive in a file's text.

    Read twice, once as written and once with trigraphs replaced, since a
    -std=c99 line reads `??=' as `#' and a gnu one does not.  Comments are
    skipped as the preprocessor skips them, and a `#' counts if only blanks
    and comments stand before it on its line or after a comment's end on it --
    which can also count one inside a comment, never miss one outside it.
    """
    if path in _ctrl_cache:
        return _ctrl_cache[path]
    if os.path.getsize(path) > CTRL_MAX:
        raise OSError("%s: larger than %d bytes" % (path, CTRL_MAX))
    with open(path, "rb") as fh:
        raw = fh.read().decode("latin-1")
    raw = raw.replace("\r\n", "\n").replace("\r", "\n")
    if raw.startswith("\xef\xbb\xbf"):      # a UTF-8 byte-order mark
        raw = raw[3:]
    hits = set()
    for text in (raw, re.sub(r"\?\?([=/'()!<>-])",
                             lambda m: TRIGRAPH[m.group(1)], raw)):
        # Splices out, remembering where each was so a hit can name the line
        # a reader would find it on.
        spl, logical, last, gone = [], [], 0, 0
        for m in SPLICE.finditer(text):
            logical.append(text[last:m.start()])
            gone += m.start() - last
            spl.append(gone)
            last = m.end()
        logical.append(text[last:])
        s = "".join(logical)
        for h in HASH.finditer(s):
            j = skip_blank(s, h.end())
            c = CTRL.match(s, j)
            if not c:
                continue
            ls = s.rfind("\n", 0, h.start()) + 1
            before = s[ls:h.start()]
            if before.strip() and COMMENT.sub(" ", before).strip() \
                    and before.rsplit("*/", 1)[-1].strip():
                continue
            k = c.end() if c.group(0) == "line" else c.start()
            phys = s.count("\n", 0, h.start()) + 1 \
                + sum(1 for p in spl if p <= h.start())
            hits.add((phys, operand(s, k)))
    _ctrl_cache[path] = sorted(hits)
    return _ctrl_cache[path]


def scan(mode, entry):
    """(read_ok, [finding...], note)"""
    d = entry.get("directory", "/")
    cmd, why = replay_argv(entry)
    label = entry.get("file", "?")
    if label.startswith("/app/"):
        label = label[len("/app/"):]
    if cmd is None:
        return False, [], "%s: %s" % (label, why)
    try:
        r = subprocess.run(cmd, cwd=d, stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=180)
    except Exception as exc:  # noqa: BLE001 -- any failure is an unread line
        return False, [], "%s: %s" % (label, exc)
    if r.returncode != 0:
        return False, [], "%s: exit %d: %s" % (
            label, r.returncode,
            r.stderr.decode("utf-8", "replace").strip().splitlines()[-1:])
    found = []
    cur = None
    cur_sys = False
    names = {entry.get("file", "")}
    # Under -ftrack-macro-expansion=0 GCC brackets nothing: the `# L "f.c" 3
    # 4' it otherwise writes around a system macro used in f.c -- `stdin',
    # `errno', `NULL' -- is gone (measured on gcc 12.2: with the bracket, a
    # tree file whose last token is such a macro leaves it open into the
    # includer, which then reads as a system header).
    # What is left carrying flag 3 is a file that IS a system header: found
    # through -isystem, or marked by `#pragma GCC system_header' in either
    # spelling, or by a line-control directive.  Outside /usr, each is one.
    for line in r.stdout.decode("utf-8", "replace").splitlines():
        s = line.lstrip()
        if not s.startswith("#"):
            continue
        m = MARK.match(line)
        if m:
            name = unescape(m.group(2))
            flags = m.group(3).split()
            names.add(name)
            cur, cur_sys = name, "3" in flags
            if cur_sys and not system(d, cur):
                found.append("system_header:%s" % cur)
            continue
        p = PRAGMA.match(line)
        # A pragma under a system file's marker is that file's; one in a tree
        # file marked as a system header is reported at the marker above.
        if not p or cur is None or cur_sys or system(d, cur):
            continue
        family, kind, rest = p.group(1), p.group(2), p.group(3)
        if kind == "system_header":
            found.append("system_header:%s" % cur)
        elif kind == "diagnostic":
            if mode == "harden" or FENCE_IGNORED.match(rest):
                found.append("%s-diagnostic:%s:%s" % (
                    family, cur, " ".join(rest.split())[:60]))
    # Every file the replay named outside /usr, read for a directive that
    # moves the attribution above.  A marker can only ADD names -- GCC writes
    # one for every file it enters, under the path it found the file by -- so
    # a forged one costs a read, never a file.  A file that cannot be read is
    # a line that was not read.
    for name in sorted(names):
        if not name or name.startswith("<") or system(d, name):
            continue
        path = os.path.normpath(os.path.join(d, name))
        if not os.path.isfile(path):
            continue
        try:
            ctrl = line_controls(path)
        except Exception as exc:  # noqa: BLE001 -- an unread file is unread
            return False, [], "%s: %s: %s" % (label, name, exc)
        for n, rest in ctrl:
            m = PLAIN.match(rest)
            if m and not (m.group(1) is not None and system(d, m.group(1))):
                continue
            found.append("line_control:%s:%d" % (
                path[len("/app/"):] if path.startswith("/app/") else path, n))
    return True, found, ""


def main(argv):
    if len(argv) < 3 or argv[1] not in ("fence", "harden"):
        sys.stderr.write(__doc__)
        return 2
    mode = argv[1]
    entries = []
    for path in argv[2:]:
        try:
            with open(path) as fh:
                entries += json.load(fh)
        except FileNotFoundError:
            # The second database is optional, like the compile-line scan's.
            if path == argv[2]:
                print("TUS 0 0 0")
                sys.stderr.write("%s: missing\n" % path)
                return 2
        except Exception as exc:  # noqa: BLE001
            print("TUS 0 0 0")
            sys.stderr.write("%s: %s\n" % (path, exc))
            return 2
    workers = max(1, min(8, os.cpu_count() or 1))
    read = unread = 0
    findings = []
    notes = []
    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        for ok, found, note in pool.map(lambda e: scan(mode, e), entries):
            if ok:
                read += 1
            else:
                unread += 1
                notes.append(note)
            findings += found
    seen, uniq = set(), []
    for f in findings:
        if f not in seen:
            seen.add(f)
            uniq.append(f)
    print("TUS %d %d %d" % (read, unread, len(uniq)))
    for f in uniq[:6]:
        print(f)
    for n in notes[:6]:
        sys.stderr.write("unread %s\n" % n)
    return 0 if (read > 0 and unread == 0) else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))

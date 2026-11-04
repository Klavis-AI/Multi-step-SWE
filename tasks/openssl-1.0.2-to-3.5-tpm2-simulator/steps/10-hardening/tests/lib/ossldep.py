#!/usr/bin/env python3
"""The set of OpenSSL functions the installed headers mark deprecated.

    ossldep.py [--include DIR] [--api 0x30500000L] [--out FILE]

Derived, never listed.  OpenSSL annotates its own retirements with
OSSL_DEPRECATEDIN_<version>, which expands to __attribute__((deprecated)) once
OPENSSL_API_COMPAT is at or above that version, so the authority on what is
deprecated at a given API level is the installed headers themselves.  This
parses them with libclang and collects every FunctionDecl clang reports as
deprecated.  Point it at a different prefix and you get that release's answer;
nothing here needs editing when the step's OpenSSL version moves.

OPENSSL_NO_DEPRECATED is deliberately NOT defined here: with it, the deprecated
declarations vanish entirely and the set would come back empty.  The gate wants
them visible and labelled.

Macro spellings (EVP_MD_CTX_create, EVP_MD_CTX_destroy, ...) cannot appear in a
symbol table and are not in this set; the build gate catches those instead, by
compiling with the same OPENSSL_API_COMPAT and OPENSSL_NO_DEPRECATED the step
requires, where they simply do not exist.
"""
import json
import os
import subprocess
import sys
import tempfile

LIBCLANG = os.environ.get("LIBCLANG", "/usr/lib/llvm-14/lib/libclang.so.1")


def umbrella(include):
    """A TU that includes every public OpenSSL header."""
    d = os.path.join(include, "openssl")
    skip = ("asn1_mac.h", "opensslconf.h", "configuration.h", "macros.h")
    names = sorted(n for n in os.listdir(d)
                   if n.endswith(".h") and n not in skip)
    return "".join("#include <openssl/%s>\n" % n for n in names)


def deprecated(include, api):
    import clang.cindex as C
    C.Config.set_library_file(LIBCLANG)
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "umbrella.c")
        with open(src, "w") as fh:
            fh.write(umbrella(include))
        tu = C.Index.create().parse(
            src, args=["-DOPENSSL_API_COMPAT=%s" % api, "-I" + include])
        fatal = [d.spelling for d in tu.diagnostics if d.severity >= 3]
        if fatal:
            sys.stderr.write("ossldep: headers did not parse cleanly:\n  %s\n"
                             % "\n  ".join(fatal[:5]))
            sys.exit(2)
        dep, seen = set(), set()
        stack = [tu.cursor]
        while stack:
            n = stack.pop()
            if n.kind == C.CursorKind.FUNCTION_DECL:
                seen.add(n.spelling)
                if n.availability == C.AvailabilityKind.DEPRECATED:
                    dep.add(n.spelling)
            stack.extend(n.get_children())
    if len(seen) < 1000:
        sys.stderr.write("ossldep: only %d functions declared -- the umbrella "
                         "did not really include OpenSSL\n" % len(seen))
        sys.exit(2)
    return sorted(dep), len(seen)


def main(argv):
    inc = argv[argv.index("--include") + 1] if "--include" in argv \
        else "/usr/local/include"
    api = argv[argv.index("--api") + 1] if "--api" in argv else "0x30500000L"
    dep, total = deprecated(inc, api)
    out = {"include": inc, "api": api, "declared": total, "deprecated": dep}
    if "--out" in argv:
        with open(argv[argv.index("--out") + 1], "w") as fh:
            json.dump(out, fh, indent=1)
    sys.stderr.write("ossldep: %d of %d declared functions are deprecated at "
                     "API %s\n" % (len(dep), total, api))
    if "--out" not in argv:
        json.dump(out, sys.stdout, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

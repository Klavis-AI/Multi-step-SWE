#!/usr/bin/env python3
"""What a built object really links against, read out of the ELF file.

    elfscan.py <object> [--libcrypto PATH] [--out FILE]

Three facts about the artifact, none of them inferable from the source:

  needed     the DT_NEEDED entries -- what the loader will pull in.
  imports    dynamic undefined symbols -- what it expects someone else to
             provide.
  defines    symbols it provides itself, from the symbol table.

The OpenSSL universe is taken from the installed libcrypto's own export list
rather than from a list of prefixes: "is this an OpenSSL symbol" is then a fact
about the installed library, not an author's guess, and it stays right when the
step's OpenSSL version moves.  It also catches the symbols that do not begin
with a recognisable prefix (OPENSSL_*, OSSL_*, and the bare ones like
`d2i_X509`) without anyone having to think of them.

A vendored static archive shows up here with no ambiguity at all: the object
*defines* OpenSSL symbols instead of importing them, and libcrypto is missing
from DT_NEEDED.
"""
import json
import os
import re
import subprocess
import sys


def nm(path, *flags):
    out = subprocess.run(["nm", "--no-demangle"] + list(flags) + [path],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.stderr.write("elfscan: nm %s: %s\n" % (path, out.stderr.strip()))
        sys.exit(2)
    syms = set()
    for line in out.stdout.splitlines():
        f = line.split()
        if not f:
            continue
        # "value type name" or, for undefined, "type name".
        name = f[-1]
        if len(f) >= 2 and len(f[-2]) == 1:
            syms.add(name.split("@")[0])
    return syms


def needed(path):
    out = subprocess.run(["readelf", "-d", path], capture_output=True,
                         text=True)
    return sorted(re.findall(r"\(NEEDED\).*\[(.+?)\]", out.stdout))


def scan(path, libcrypto):
    universe = set()
    for lib in libcrypto:
        if os.path.exists(lib):
            universe |= nm(lib, "-D", "--defined-only")
    if not universe:
        sys.stderr.write("elfscan: found no libcrypto to take the OpenSSL "
                         "symbol universe from (%s)\n" % ", ".join(libcrypto))
        sys.exit(2)
    imports = nm(path, "-D", "--undefined-only")
    defines = nm(path, "--defined-only") | nm(path, "-D", "--defined-only")
    return {
        "object": path,
        "needed": needed(path),
        "openssl_universe": len(universe),
        "imports": sorted(imports),
        "openssl_imports": sorted(imports & universe),
        "openssl_defines": sorted(defines & universe),
    }


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    libs = [argv[argv.index("--libcrypto") + 1]] if "--libcrypto" in argv else [
        "/usr/local/lib/libcrypto.so", "/usr/local/lib64/libcrypto.so"]
    r = scan(argv[1], libs)
    if "--out" in argv:
        with open(argv[argv.index("--out") + 1], "w") as fh:
            json.dump(r, fh, indent=1)
    sys.stderr.write(
        "elfscan: %s\n  needed          %s\n  openssl imports %d\n"
        "  openssl defines %d  (non-zero means OpenSSL is linked in "
        "statically)\n"
        % (r["object"], ", ".join(r["needed"]) or "(none)",
           len(r["openssl_imports"]), len(r["openssl_defines"])))
    if "--out" not in argv:
        json.dump(r, sys.stdout, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

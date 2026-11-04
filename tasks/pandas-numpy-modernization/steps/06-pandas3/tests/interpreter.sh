#!/usr/bin/env bash
# The interpreter this step's verifier runs on, fetched at image build time.
#
# It is the *same build* the agent container gets -- same python-build-standalone
# release, same tarball, same sha256 -- and not merely the same 3.12.x. The
# behaviour corpus records exception messages, and CPython rewords them between
# patch releases; scoring a 3.12.11 recording under the base image's 3.7.17 would
# charge the candidate for a sentence neither it nor the libraries wrote.
#
# `environment_mode = "separate"` still holds: this image fetches and verifies
# the tarball itself and shares nothing with the agent image but the checksum.
set -euo pipefail

PBS_TAG=20250818
PBS_FILE=cpython-3.12.11+20250818-x86_64-unknown-linux-gnu-install_only.tar.gz
PBS_BYTES=102707634
PBS_SHA256=98229938166f51deff81b00d71455fac84a57290b71089bd5fe673738557f053
PBS_DEST=/opt/py312

url="https://github.com/astral-sh/python-build-standalone/releases/download/${PBS_TAG}/${PBS_FILE}"
tarball=$(mktemp /tmp/pbs-XXXXXX.tar.gz)
ok=0
for attempt in 1 2 3 4 5; do
    if curl -fsSL --retry 5 --retry-delay 5 --connect-timeout 30 \
            -o "$tarball" "$url"; then
        got=$(wc -c < "$tarball")
        if [ "$got" != "$PBS_BYTES" ]; then
            echo "interpreter: $PBS_FILE is $got bytes, expected $PBS_BYTES" >&2
        else
            got=$(sha256sum "$tarball" | cut -d" " -f1)
            if [ "$got" = "$PBS_SHA256" ]; then
                ok=1
                break
            fi
            echo "interpreter: $PBS_FILE sha256 $got, expected $PBS_SHA256" >&2
        fi
    fi
    echo "interpreter: fetch attempt $attempt failed, retrying" >&2
    sleep $((attempt * 10))
done
if [ "$ok" -ne 1 ]; then
    echo "interpreter: could not fetch a verified interpreter" >&2
    exit 1
fi

mkdir -p "$PBS_DEST"
# install_only tarballs root everything under python/.
tar -xzf "$tarball" --strip-components=1 -C "$PBS_DEST"
rm -f "$tarball"
"$PBS_DEST/bin/python3.12" -V

# pkg-config for this interpreter, put on the default search path. The build
# below runs a meson Cython sanity compile that asks pkg-config for `python3`,
# and the base image is python:3.7-slim: its
# /usr/local/lib/pkgconfig/python3.pc names 3.7, so the compile links against
# 3.7 headers and Cython 3.1's generated C dies on `#error Python 3.8+`. PBS
# ships its own python-3.12.pc under $PBS_DEST/lib/pkgconfig, but with a
# relative `prefix=${pcfiledir}/../..` that only resolves from that directory.
# Setting PKG_CONFIG_PATH would not hold: the build runs in `bash -c` shells
# that inherit only the image's Docker ENV, and that ENV must not name
# $PBS_DEST (the agent image shares this base and a step-1 agent reading it
# would learn the ladder's shape). So rewrite the prefix to an absolute path and
# drop the files on the default search path, retiring the 3.7 ones. A bare
# `pkg-config python3` then resolves to 3.12 for every invocation, with no env.
rm -f /usr/local/lib/pkgconfig/python-3.7*.pc /usr/local/lib/pkgconfig/python3.pc
for pc in "$PBS_DEST"/lib/pkgconfig/*.pc; do
    sed "s#^prefix=\${pcfiledir}/\.\./\.\.#prefix=$PBS_DEST#" "$pc" \
        > "/usr/local/lib/pkgconfig/$(basename "$pc")"
done
# Prove it took, if pkg-config is already on PATH at this point in the build.
# The .pc files above are just files, so this rewrite does not depend on the
# apt layer's ordering; only this verification does, hence the guard.
if command -v pkg-config >/dev/null 2>&1; then
    got=$(pkg-config --cflags python3)
    case "$got" in
        *"$PBS_DEST/include"*) echo "interpreter: pkg-config python3 -> $got" ;;
        *) echo "interpreter: pkg-config python3 did not retarget: $got" >&2
           exit 1 ;;
    esac
fi

# python-build-standalone cross-builds CPython with clang and an LLVM ar that
# live under /tools -- paths its build image had and this one does not -- and
# records those names in the interpreter's own sysconfig. This verifier builds
# the candidate's package from source, and skutil ships a Fortran file and a
# Cython extension, so every one of those builds reads the compiler from there
# rather than from the environment and dies with "No such file or directory:
# 'clang'" whatever CC says in the shell. Retargeting the recorded values is the
# one spelling that holds for every invocation. Done before the venv is created,
# because a venv reads the base interpreter's sysconfigdata.
"$PBS_DEST/bin/python3.12" - <<'RETARGET'
import json
import re
import subprocess
import sys
import sysconfig

SUBS = [
    (re.compile(r"^clang\+\+"), "g++"),
    (re.compile(r"^clang"), "gcc"),
    (re.compile(r"^/tools/llvm/bin/llvm-(ar|ranlib|nm|objcopy|strip|readelf)$"),
     r"\1"),
    (re.compile(r"^/tools/host/bin/"), ""),
]
KEYS = ("CC", "CXX", "LDSHARED", "BLDSHARED", "LDCXXSHARED", "AR", "RANLIB",
        "READELF", "NM", "OBJCOPY", "STRIP", "LINKCC", "MAINCC")


def retarget(value):
    for pattern, replacement in SUBS:
        value = pattern.sub(replacement, value, count=1)
    return value


name = sysconfig._get_sysconfigdata_name()
module = __import__(name)
path = module.__file__
if not path.endswith(".py"):
    raise SystemExit("interpreter: sysconfigdata is not a source file: " + path)

variables = dict(module.build_time_vars)
changed = []
for key in KEYS:
    old = variables.get(key)
    if not isinstance(old, str):
        continue
    new = retarget(old)
    if new != old:
        variables[key] = new
        changed.append("%s: %r -> %r" % (key, old, new))
if not changed:
    raise SystemExit("interpreter: sysconfigdata names no cross-toolchain "
                     "paths; this is not the interpreter the retarget was "
                     "written for")

# Regenerated rather than patched in place: the file's whole content is one
# dict, and rewriting it from the dict cannot depend on how the generator
# happened to quote.
with open(path, "w") as fh:
    fh.write("# system configuration generated and used by the sysconfig module\n")
    fh.write("build_time_vars = ")
    json.dump(variables, fh, indent=4, sort_keys=True)
    fh.write("\n")

# distutils rebuilds a config var from the Makefile when it is asked for one
# the frozen dict does not carry.
makefile = sysconfig.get_makefile_filename()
with open(makefile) as fh:
    lines = fh.read().split("\n")
out = []
for line in lines:
    head, sep, tail = line.partition("=")
    if sep and not line.startswith("\t") and head.strip() in KEYS:
        line = head + "=" + retarget(tail.strip())
    out.append(line)
with open(makefile, "w") as fh:
    fh.write("\n".join(out))

# Prove it took, in a fresh interpreter that has to read the file back, and
# that every tool it now names is on PATH.
probe = ("import sysconfig,json;print(json.dumps({k: sysconfig.get_config_var(k)"
         " for k in ['CC','CXX','AR','LDSHARED']}))")
got = json.loads(subprocess.check_output([sys.executable, "-c", probe]).decode())
for key, value in got.items():
    if "clang" in value or "/tools/" in value:
        raise SystemExit("interpreter: retarget did not take: %s = %r"
                         % (key, value))
    subprocess.check_call(["sh", "-c", "command -v " + value.split()[0]],
                          stdout=subprocess.PIPE)
for line in changed:
    print("interpreter: sysconfig " + line)
RETARGET

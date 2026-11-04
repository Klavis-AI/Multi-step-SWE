#!/usr/bin/env bash
# Toolchain for this step. It runs once, in this container, before work on the
# step begins, and it deletes itself when it is done.
#
# /opt/venv is first on PATH (see environment/Dockerfile). Rebuilding it here is
# what makes the previous step's stack absent rather than merely shadowed.
set -euo pipefail

INTERPRETER=/opt/py312/bin/python3.12

# The first step that needs an interpreter newer than the image's Python 3.7,
# and the image deliberately does not carry one, so the interpreter is fetched
# by the step that needs it -- from a pinned python-build-standalone release
# whose tag, filename, byte count and sha256 are written down here. A pin that
# is a checksum does not depend on what an archive happens to serve this week.
PBS_TAG=20250818
PBS_FILE=cpython-3.12.11+20250818-x86_64-unknown-linux-gnu-install_only.tar.gz
PBS_BYTES=102707634
PBS_SHA256=98229938166f51deff81b00d71455fac84a57290b71089bd5fe673738557f053
PBS_DEST=/opt/py312

fetch_interpreter() {
    local url tarball got
    url="https://github.com/astral-sh/python-build-standalone/releases/download/${PBS_TAG}/${PBS_FILE}"
    tarball=$(mktemp /tmp/pbs-XXXXXX.tar.gz)
    for attempt in 1 2 3 4 5; do
        if curl -fsSL --retry 5 --retry-delay 5 --connect-timeout 30 \
                -o "$tarball" "$url"; then
            got=$(wc -c < "$tarball")
            if [ "$got" != "$PBS_BYTES" ]; then
                echo "setup: $PBS_FILE is $got bytes, expected $PBS_BYTES" >&2
            else
                got=$(sha256sum "$tarball" | cut -d" " -f1)
                if [ "$got" = "$PBS_SHA256" ]; then
                    mkdir -p "$PBS_DEST"
                    # install_only tarballs root everything under python/.
                    tar -xzf "$tarball" --strip-components=1 -C "$PBS_DEST"
                    rm -f "$tarball"
                    return 0
                fi
                echo "setup: $PBS_FILE sha256 $got, expected $PBS_SHA256" >&2
            fi
        fi
        echo "setup: interpreter fetch attempt $attempt failed, retrying" >&2
        sleep $((attempt * 10))
    done
    rm -f "$tarball"
    return 1
}

# Fetch and verify *before* retiring anything below. A container that has
# deleted its only interpreter and then failed to download the next one cannot
# run the step at all, and the failure would surface as a missing binary
# several lines later instead of as this message.
if [ ! -x "$INTERPRETER" ]; then
    if ! fetch_interpreter; then
        echo "setup: could not fetch this step's interpreter" >&2
        exit 1
    fi
fi
"$INTERPRETER" -V

# pkg-config for the fetched interpreter, on the default search path, so the
# meson build the step runs (`pip install --no-build-isolation .`) links its
# Cython/Fortran extensions against 3.12 rather than the base image's 3.7. The
# base python:3.7-slim ships /usr/local/lib/pkgconfig/python3.pc naming 3.7;
# meson's Cython sanity compile finds it first and Cython 3.1's generated C then
# fails `#error Python 3.8+`. PBS ships python-3.12.pc under $PBS_DEST with a
# relative `prefix=${pcfiledir}/../..` that only resolves from its own directory,
# so rewrite the prefix to an absolute path and drop the files on the default
# search path, retiring the 3.7 ones -- filesystem, not PKG_CONFIG_PATH, so it
# holds for every build invocation the step makes.
rm -f /usr/local/lib/pkgconfig/python-3.7*.pc /usr/local/lib/pkgconfig/python3.pc
for pc in "$PBS_DEST"/lib/pkgconfig/*.pc; do
    sed "s#^prefix=\${pcfiledir}/\.\./\.\.#prefix=$PBS_DEST#" "$pc" \
        > "/usr/local/lib/pkgconfig/$(basename "$pc")"
done

# python-build-standalone cross-builds CPython with clang and an LLVM ar that
# live under /tools -- paths its build image had and this one does not -- and
# records those names in the interpreter's own sysconfig.
# The step builds skutil from source (`pip install --no-build-isolation .`),
# and skutil ships a Fortran file and a Cython extension, so every one of
# those builds reads the compiler from there rather than from the environment
# and dies with "No such file or directory: 'clang'" whatever CC says in the
# shell. Retargeting the recorded values is the one spelling that holds for
# every invocation. Done before the venv is created, because a venv reads the
# base interpreter's sysconfigdata.
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

pins=$(mktemp)
cat > "$pins" <<'PINS'
setuptools==80.9.0
wheel==0.45.1
numpy==2.3.5
scipy==1.16.3
pandas==3.0.5
scikit-learn==1.7.2
matplotlib==3.9.4
seaborn==0.13.2
pyarrow==21.0.0
Cython==3.1.6
meson-python==0.18.0
ninja==1.13.0
pytest==8.4.2
PINS

# Retire the previous step's stack before installing this one, and drop pip's
# caches with it so no retired wheel survives anywhere in the container.
rm -rf /opt/venv /root/.cache/pip /tmp/pipcache
rm -rf /opt/py39

"$INTERPRETER" -m venv /opt/venv
/opt/venv/bin/python -m pip install -q --no-cache-dir --disable-pip-version-check \
    --retries 10 --timeout 60 --upgrade 'pip<25'

installed=0
for attempt in 1 2 3 4 5; do
    if /opt/venv/bin/python -m pip install -q --no-cache-dir --disable-pip-version-check \
            --retries 10 --timeout 60 -r "$pins"; then
        installed=1
        break
    fi
    echo "setup: pip attempt $attempt failed, retrying" >&2
    sleep $((attempt * 10))
done
rm -f "$pins"
if [ "$installed" -ne 1 ]; then
    echo "setup: could not install this step's toolchain" >&2
    exit 1
fi

rm -rf /root/.cache/pip
/opt/venv/bin/python - <<'PY'
import matplotlib, numpy, pandas, scipy, sklearn
print("stack", numpy.__version__, scipy.__version__, pandas.__version__,
      sklearn.__version__, matplotlib.__version__)
PY

# Leave the workspace exactly as the previous step left it.
rm -f -- "$0"

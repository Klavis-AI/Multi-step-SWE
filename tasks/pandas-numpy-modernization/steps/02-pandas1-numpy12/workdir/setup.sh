#!/usr/bin/env bash
# Toolchain for this step. It runs once, in this container, before work on the
# step begins, and it deletes itself when it is done.
#
# /opt/venv is first on PATH (see environment/Dockerfile). Rebuilding it here is
# what makes the previous step's stack absent rather than merely shadowed.
set -euo pipefail

INTERPRETER=/opt/py39/bin/python3.9

# The first step that needs an interpreter newer than the image's Python 3.7,
# and the image deliberately does not carry one, so the interpreter is fetched
# by the step that needs it -- from a pinned python-build-standalone release
# whose tag, filename, byte count and sha256 are written down here. A pin that
# is a checksum does not depend on what an archive happens to serve this week.
PBS_TAG=20250818
PBS_FILE=cpython-3.9.23+20250818-x86_64-unknown-linux-gnu-install_only.tar.gz
PBS_BYTES=39855893
PBS_SHA256=d2251aea8f4c8df45d04ed9462c1a121c923aac61c0c77da44758511c976bd4a
PBS_DEST=/opt/py39

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

# python-build-standalone cross-builds CPython with clang and an LLVM ar that
# live under /tools -- paths its build image had and this one does not -- and
# records those names in the interpreter's own sysconfig. Every extension build
# reads the compiler from there rather than from the environment, numpy's
# compiler probe first of all, so a `pip install` of a tree with a Cython or a
# Fortran extension dies with "No such file or directory: 'clang'" whatever CC
# says in the shell. Retargeting the recorded values is the one spelling that
# holds for every invocation. Done before the venv is created, because a venv
# reads the base interpreter's sysconfigdata.
"$INTERPRETER" - <<'RETARGET'
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
    raise SystemExit("setup: sysconfigdata is not a source file: " + path)

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
    raise SystemExit("setup: sysconfigdata names no cross-toolchain paths; "
                     "this is not the interpreter the retarget was written for")

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
        raise SystemExit("setup: retarget did not take: %s = %r" % (key, value))
    subprocess.check_call(["sh", "-c", "command -v " + value.split()[0]],
                          stdout=subprocess.PIPE)
for line in changed:
    print("setup: sysconfig " + line)
RETARGET

pins=$(mktemp)
cat > "$pins" <<'PINS'
setuptools==59.8.0
wheel==0.37.1
numpy==1.21.6
scipy==1.7.3
pandas==1.3.5
scikit-learn==0.24.2
matplotlib==3.5.3
seaborn==0.11.2
six==1.16.0
Cython==0.29.36
pytest==7.4.4

# ---- transitive closure, pinned (tools/pin_closure.py) ----
#
# Not named by anything above; pip chose them while resolving it.
# Written down because the library's behaviour on this step depends
# on these exact versions and some of them move on their own
# schedule -- pytz publishes a release whenever a government
# changes a timezone rule -- so an image rebuilt later would not
# be the same image.
#
# Regenerate: python tools/pin_closure.py <image> <this file>
cycler==0.12.1
exceptiongroup==1.3.1
fonttools==4.60.2
iniconfig==2.1.0
joblib==1.5.3
kiwisolver==1.4.7
packaging==26.3
pillow==11.3.0
pluggy==1.6.0
pyparsing==3.3.2
python-dateutil==2.9.0.post0
pytz==2026.3.post1
threadpoolctl==3.7.0
tomli==2.4.1
typing_extensions==4.16.0
PINS

# Retire the previous step's stack before installing this one, and drop pip's
# caches with it so no retired wheel survives anywhere in the container.
rm -rf /opt/venv /root/.cache/pip /tmp/pipcache
rm -rf /usr/local/lib/python3.7 \
       /usr/local/include/python3.7m \
       /usr/local/bin/python3.7 \
       /usr/local/bin/python3.7m \
       /usr/local/bin/pip3.7 \
       /usr/local/bin/pydoc3.7 \
       /usr/local/bin/idle3.7 \
       /usr/local/bin/2to3-3.7 \
       /usr/local/lib/libpython3.7m.so.1.0 \
       /usr/local/lib/libpython3.7m.so \
       /usr/local/lib/libpython3.so \
       /usr/local/bin/python \
       /usr/local/bin/python3 \
       /usr/local/bin/pip \
       /usr/local/bin/pip3 \
       /usr/local/bin/pydoc \
       /usr/local/bin/idle3 \
       /usr/local/bin/2to3 \
       /usr/local/bin/pyvenv-3.7 \
       /usr/local/bin/python3-config \
       /usr/local/bin/python3.7-config \
       /usr/local/bin/python3.7m-config

# The Debian image's /usr/local/bin also holds a handful of symlinks and
# console scripts pointing into that interpreter. Each is now either dangling
# or a script whose shebang names a binary that is gone, and either one is a
# trap for anything that resolves it before /opt/venv on PATH. Swept by what
# they now are rather than by name, so the list cannot fall behind the image.
for candidate in /usr/local/bin/*; do
    [ -e "$candidate" ] || { rm -f -- "$candidate"; continue; }
    [ -f "$candidate" ] || continue
    shebang=$(head -c 256 -- "$candidate" 2>/dev/null | head -1)
    case "$shebang" in
        '#!'*)
            interpreter=${shebang#\#!}
            interpreter=${interpreter%% *}
            [ -x "$interpreter" ] || rm -f -- "$candidate"
            ;;
    esac
done

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
if [ "$installed" -ne 1 ]; then
    rm -f "$pins"
    echo "setup: could not install this step's toolchain" >&2
    exit 1
fi

# Did pip produce the stack this file describes, or merely one pip was willing
# to build today? The pins above include the transitive closure -- pytz,
# packaging, joblib and the rest -- because those move on their own schedule
# and the library's behaviour on this step depends on one specific set of
# them. A dependency that appeared, vanished or shifted version since then
# makes a different container from the one this step was prepared against,
# and the only symptom would be correct code behaving differently. So it stops
# the run here instead, where the reason is still legible.
/opt/venv/bin/python - "$pins" <<'CLOSURE'
import re
import subprocess
import sys

NOT_IN_FREEZE = ("pip", "setuptools", "wheel")


def norm(name):
    return re.sub(r"[-_.]+", "-", name).lower()


def pins(text):
    out = {}
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "==" in line:
            name, _, _version = line.partition("==")
            out[norm(name)] = line
    return out


with open(sys.argv[1]) as handle:
    declared = pins(handle.read())
frozen = pins(subprocess.check_output(
    [sys.executable, "-m", "pip", "freeze"]).decode("utf-8", "replace"))
for name in NOT_IN_FREEZE:
    declared.pop(norm(name), None)
    frozen.pop(norm(name), None)

unpinned = sorted(frozen[n] for n in frozen if n not in declared)
uninstalled = sorted(declared[n] for n in declared if n not in frozen)
shifted = sorted(frozen[n] for n in frozen
                 if n in declared and frozen[n] != declared[n])
if unpinned or uninstalled or shifted:
    sys.exit("setup: the installed stack is not the one pinned above: "
             "installed but not pinned=%r, pinned but not installed=%r, "
             "pinned at another version=%r" % (unpinned, uninstalled, shifted))
print("setup: closure verified, %d packages, all at the pinned version"
      % len(frozen))
CLOSURE

rm -f "$pins"

rm -rf /root/.cache/pip
/opt/venv/bin/python - <<'PY'
import matplotlib, numpy, pandas, scipy, sklearn
print("stack", numpy.__version__, scipy.__version__, pandas.__version__,
      sklearn.__version__, matplotlib.__version__)
PY

# Assert the retirement, in both a login and a non-login shell. A `python` that
# resolves anywhere but the new venv, or a /usr/local/bin entry that points at
# something absent, is the failure this whole block exists to prevent -- and it
# has to fail here, loudly, rather than inside the step's first command.
for shell in "bash -lc" "bash -c"; do
    got=$($shell 'command -v python') || got="<none>"
    if [ "$got" != "/opt/venv/bin/python" ]; then
        echo "setup: $shell resolves python to $got, expected /opt/venv/bin/python" >&2
        exit 1
    fi
done
broken=$(find /usr/local/bin -mindepth 1 -xtype l 2>/dev/null | head -5)
if [ -n "$broken" ]; then
    echo "setup: dangling entries left in /usr/local/bin: $broken" >&2
    exit 1
fi

# Leave the workspace exactly as the previous step left it.
rm -f -- "$0"

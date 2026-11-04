"""Assert the agent image is the stack the step's first sentence names.

`instruction.md` opens by telling the agent what is already installed --
"Python 3.7, NumPy 1.14, SciPy 1.1, pandas 0.23, scikit-learn 0.20,
Matplotlib 2.2, seaborn 0.7" -- and says the step does not change any of them.
The verifier's `selfcheck.py` asserts these versions of *its own* image and
fails `docker build` when they drift; this script does the same for the agent
image, so an edit to `environment/stack.txt` that changes pandas fails the
build instead of leaving the instruction describing a container that does not
exist. The pins below are the full versions from `stack.txt`; the instruction
quotes them to two components.

It checks one more thing than the instruction's sentence needs. `stack.txt`
also pins the transitive closure -- pytz, packaging, python-dateutil and the
rest, which pip chose rather than anyone naming -- because those move on their
own schedule and the behaviour corpus was recorded against one specific set of
them. Writing them down is half of it; the other half is asserting that the
image pip actually produced is the one the file describes, in both directions,
so a dependency that appeared, vanished or shifted version since the recording
fails `docker build` instead of quietly becoming the new baseline.

    $ python verify-stack.py /tmp/stack.txt
"""
import re
import subprocess
import sys

import matplotlib
import numpy
import pandas
import scipy
import seaborn
import sklearn

WANT = {
    "python": "3.7",
    "numpy": "1.14.6",
    "scipy": "1.1.0",
    "pandas": "0.23.4",
    "scikit-learn": "0.20.4",
    "matplotlib": "2.2.5",
    "seaborn": "0.7.1",
}

got = {
    "python": "%d.%d" % sys.version_info[:2],
    "numpy": numpy.__version__,
    "scipy": scipy.__version__,
    "pandas": pandas.__version__,
    "scikit-learn": sklearn.__version__,
    "matplotlib": matplotlib.__version__,
    "seaborn": seaborn.__version__,
}

print("stack " + " ".join("%s=%s" % (k, got[k]) for k in sorted(got)))
bad = [(k, WANT[k], got[k]) for k in sorted(WANT) if got[k] != WANT[k]]
if bad:
    sys.exit("stack does not match the one instruction.md names: %r" % (bad,))

# pip freeze never lists these, and stack.txt pins the two that matter anyway.
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


if len(sys.argv) > 1:
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
    print("closure %d packages, all pinned" % len(frozen)
          if not (unpinned or uninstalled or shifted) else "closure MISMATCH")
    if unpinned or uninstalled or shifted:
        sys.exit("stack.txt does not describe this image: "
                 "installed but not pinned=%r, pinned but not installed=%r, "
                 "pinned at another version=%r"
                 % (unpinned, uninstalled, shifted))

#!/usr/bin/env bash
# Toolchain for this step. It runs once, in this container, before work on the
# step begins, and it deletes itself when it is done.
#
# /opt/venv is first on PATH (see environment/Dockerfile). Rebuilding it here is
# what makes the previous step's stack absent rather than merely shadowed.
set -euo pipefail

INTERPRETER=/opt/py312/bin/python3.12

# Fetched by the earlier step that first needed it. Every step runs in one
# long-lived container, so it is still here; if it is not, say so rather than
# failing on a missing binary further down.
if [ ! -x "$INTERPRETER" ]; then
    echo "setup: $INTERPRETER is missing -- the step that fetches it did not run" >&2
    exit 1
fi

pins=$(mktemp)
cat > "$pins" <<'PINS'
setuptools==80.9.0
wheel==0.45.1
numpy==2.3.5
scipy==1.16.3
pandas==3.0.5
scikit-learn==1.7.2
matplotlib==3.10.9
seaborn==0.13.2
pyarrow==21.0.0
Cython==3.1.6
meson-python==0.18.0
ninja==1.13.0
pytest==8.4.2
polars==1.35.1
duckdb==1.4.4
nbclient==0.10.2
nbformat==5.10.4
ipykernel==6.30.1
skops==0.13.0
PINS

# Retire the previous step's stack before installing this one, and drop pip's
# caches with it so no retired wheel survives anywhere in the container.
rm -rf /opt/venv /root/.cache/pip /tmp/pipcache

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

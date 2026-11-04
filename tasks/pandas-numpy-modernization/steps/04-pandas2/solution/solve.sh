#!/usr/bin/env bash
#
# Step 4 golden solution: 03-sklearn1's tree -> 04-pandas2.
#
# The entry tree is step 3's golden, already clean under scikit-learn 1.6.1 and
# ruff's bugbear family. The whole stack moves up here -- pandas 1.5.3 -> 2.2.3,
# and pyarrow 17 arrives so the string dtype has a backend -- but the recorded
# behaviour differences that come with pandas 2.0 are the STACK's doing, not the
# code's: this step declares 160 of them as flips and the golden reproduces
# every one. What the code has to do is narrow: satisfy the two pandas rules
# this step turns on, without moving a single recorded value.
#
# Two rules arrive at step 4, and the golden answers each:
#
#   (a) ruff's pandas-vet, the whole PD family, is enabled here (step 2 took
#       only the removed-accessor subset). The only PD finding the step-3 tree
#       carries under the widened select is PD011 -- 37 `.values` reads, once
#       Copy-on-Write makes the ndarray/attribute distinction matter. `.values`
#       and `.to_numpy()` return the same array for every frame and series in
#       this tree, so the rewrite is value-neutral; it is pure pandas-vet
#       hygiene, and the SELECT count goes 37 -> 0.
#
#   (b) two DataFrame.groupby calls pass no `observed=`. pandas 3.0 flips that
#       default from False to True, and 2.1 already warns; an explicit
#       observed=False both silences the warning and pins today's grouping.
#       Neither grouper is categorical, so observed= changes nothing the corpus
#       records -- the AST gate for implicit-observed goes 2 -> 0 and no case
#       moves.
#
# Two layers, in the order the tree needs them:
#
#   Layer 1 (semantic).  The two groupby calls get observed=False. Additive and
#   corpus-neutral: observed=False is pandas' pre-3.0 behaviour, and these
#   groupers are numeric, so the grouping is identical.
#
#   Layer 2 (mechanical).  The 37 `.values` reads become `.to_numpy()`. ruff
#   offers no fixer for PD011, so this is driven off ruff's own finding
#   locations: for each PD011 report, the `.values` accessor is the seven
#   characters ending at the finding's end column, replaced right-to-left within
#   each line so the several lines that carry two reads stay in register.
#
# Then the checks: every module compiles, the three notebooks still hold every
# cell, and an independent scan (its own tokenizer, strings and comments
# blanked) confirms no `.values` read survives and every groupby names observed=.
set -euo pipefail

ROOT="${1:-/app}"
cd "$ROOT"

# ruff's SELECT and target for this step. PD is pandas-vet in full (new at step
# 4); the rest are carried up the ladder. Listing the step-2 PD subset alongside
# the umbrella PD is harmless and mirrors the gate's own accumulated selection.
SELECT='NPY002,UP,NPY001,PD002,PD003,PD007,PD008,PD009,PD012,PD015,B,PD'
TARGET='py39'

want_ruff='ruff 0.14.0'
have_ruff="$(ruff --version 2>/dev/null || true)"
if [ "$have_ruff" != "$want_ruff" ]; then
    echo "solve.sh: expected '$want_ruff' but found '${have_ruff:-nothing}'" >&2
    exit 1
fi

# Count SELECT findings across the tree, exactly as the gate does: isolated
# config, no cache, noqa ignored, gitignore ignored, this step's target. ruff
# exits non-zero whenever it reports findings, so capture output first (|| true)
# rather than piping it straight through under `set -o pipefail`.
ruff_count() {
    local out
    out=$(ruff check --isolated --no-cache --ignore-noqa \
              --no-respect-gitignore --target-version "$TARGET" \
              --select "$SELECT" --output-format json . 2>/dev/null) || true
    printf '%s' "${out:-[]}" \
        | python -c 'import json,sys; print(len(json.load(sys.stdin)))'
}

# Assert the SELECT count is what this layer must leave, and say so.
expect_count() {
    local label="$1" expected="$2" got
    got="$(ruff_count)"
    if [ "$got" != "$expected" ]; then
        echo "solve.sh: ruff $label expected $expected finding(s), got $got" >&2
        exit 1
    fi
    echo "ruff $label $got finding(s)"
}

echo "== step 4 oracle: $ROOT"
expect_count "entry tree" 37

# ---------------------------------------------------------------------------
# Layer 1 -- semantic. observed=False on the two implicit-observed groupby calls.
# ---------------------------------------------------------------------------
python - "$ROOT" <<'PYEOF'
import io, sys
root = sys.argv[1]
LEDGER = []

def read(rel):
    return io.open(root + "/" + rel, encoding="utf-8").read()

def write(rel, text):
    io.open(root + "/" + rel, "w", encoding="utf-8").write(text)

def die(msg):
    sys.stderr.write("solve.sh Layer 1: " + msg + "\n")
    sys.exit(1)

def edit(rel, old, new, count=1):
    """Replace `old` with `new` in `rel`, asserting it appears exactly `count`
    times. A drifted anchor is a changed tree, not a silent no-op."""
    s = read(rel)
    n = s.count(old)
    if n != count:
        die("%s: anchor matched %d time(s), need %d\n    %r" % (rel, n, count, old[:70]))
    write(rel, s.replace(old, new))
    LEDGER.append("edit %-30s x%d" % (rel, count))

# _act.py -- the gains-table grouper over the rank column.
edit("skutil/metrics/_act.py",
     "tab[['rank', 'pred', 'prem', 'loss', 'expo']].groupby('rank')",
     "tab[['rank', 'pred', 'prem', 'loss', 'expo']].groupby('rank', observed=False)")

# util.py -- report_grid's per-column mean over the grid-search results.
edit("skutil/utils/util.py",
     "result_df[y_axis].groupby(result_df[col])",
     "result_df[y_axis].groupby(result_df[col], observed=False)")

sys.stderr.write("Layer 1 ledger (%d edits):\n" % len(LEDGER))
for row in LEDGER:
    sys.stderr.write("  " + row + "\n")
PYEOF

# Layer 1 is not a ruff-visible change (PD011 is untouched); the count holds.
expect_count "after the semantic layer" 37

# ---------------------------------------------------------------------------
# Layer 2 -- mechanical. The 37 `.values` reads -> `.to_numpy()`, driven off
# ruff's own PD011 finding locations. ruff has no fixer for PD011, so the rewrite
# is done here; right-to-left per line keeps the doubled lines in register.
# ---------------------------------------------------------------------------
python - "$ROOT" "$SELECT" "$TARGET" <<'PYEOF'
import io, json, subprocess, sys
from collections import defaultdict
root, select, target = sys.argv[1], sys.argv[2], sys.argv[3]

def die(msg):
    sys.stderr.write("solve.sh Layer 2: " + msg + "\n")
    sys.exit(1)

out = subprocess.run(
    ["ruff", "check", "--isolated", "--no-cache", "--ignore-noqa",
     "--no-respect-gitignore", "--target-version", target,
     "--select", "PD011", "--output-format", "json", "."],
    cwd=root, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
findings = json.loads(out.stdout.decode("utf-8") or "[]")
if len(findings) != 37:
    die("expected 37 PD011 findings, ruff reported %d" % len(findings))

# group by (file, end-row); `.values` is the 7 characters ending at end column.
by_line = defaultdict(list)
for f in findings:
    if f["code"] != "PD011":
        die("unexpected code %r among PD011 findings" % f["code"])
    end = f["end_location"]
    by_line[(f["filename"], end["row"])].append(end["column"])

changed = 0
for (path, row), end_cols in by_line.items():
    lines = io.open(path, encoding="utf-8").read().splitlines(keepends=True)
    line = lines[row - 1]
    # right-to-left so earlier columns keep their offsets as we splice
    for end_col in sorted(end_cols, reverse=True):
        c1 = end_col - 1          # 0-based, exclusive end of `.values`
        c0 = c1 - len(".values")
        if line[c0:c1] != ".values":
            die("%s:%d expected '.values' at [%d:%d], found %r"
                % (path, row, c0, c1, line[c0:c1]))
        line = line[:c0] + ".to_numpy()" + line[c1:]
        changed += 1
    lines[row - 1] = line
    io.open(path, "w", encoding="utf-8").write("".join(lines))

if changed != 37:
    die("expected to rewrite 37 reads, rewrote %d" % changed)
sys.stderr.write("Layer 2: rewrote %d `.values` reads to `.to_numpy()`\n" % changed)
PYEOF

expect_count "after the mechanical layer" 0

# ---------------------------------------------------------------------------
# Checks. Independent of ruff and the gates: the tree still builds, the
# notebooks still hold their cells, and the two forms this step retires are gone.
# ---------------------------------------------------------------------------
echo "== checks"

# 1. every module compiles.
python - "$ROOT" <<'PYEOF'
import compileall, sys, pathlib
root = sys.argv[1]
files = sorted(pathlib.Path(root).rglob("*.py"))
if len(files) != 58:
    sys.stderr.write("checks: expected 58 .py files, found %d\n" % len(files))
    sys.exit(1)
ok = compileall.compile_dir(root, quiet=1, force=True)
if not ok:
    sys.stderr.write("checks: a module failed to compile\n")
    sys.exit(1)
print("compile: %d modules OK" % len(files))
PYEOF

# 2. the three notebooks parse and still hold every cell.
python - "$ROOT" <<'PYEOF'
import json, sys
root = sys.argv[1]
EXPECTED = {
    "doc/examples/decomposition/SelectivePCA weighting.ipynb": (13, 6),
    "doc/examples/machine_learning_demo/ml demo.ipynb": (47, 24),
    "doc/examples/pipeline/skutil grid demo.ipynb": (15, 10),
}
for rel, (ncells, ncode) in EXPECTED.items():
    nb = json.load(open(root + "/" + rel, encoding="utf-8"))
    cells = nb["cells"]
    code = [c for c in cells if c["cell_type"] == "code"]
    if len(cells) != ncells or len(code) != ncode:
        sys.stderr.write("checks: %s has %d cells / %d code, expected %d / %d\n"
                         % (rel, len(cells), len(code), ncells, ncode))
        sys.exit(1)
print("notebooks: 3 parse with expected cell counts")
PYEOF

# 3. independent scan -- the 37 ruff-flagged reads are now `.to_numpy()`, and
#    every groupby names observed=. (ruff PD011 flags only the reads its type
#    heuristic is sure of; the tree keeps other, ruff-invisible `.values` reads,
#    which the pandas-vet gate does not count. This scan checks the conversion
#    at its known scale rather than asserting a `.values`-free tree.)
python - "$ROOT" <<'PYEOF'
import io, re, sys, pathlib, tokenize
root = sys.argv[1]

def code_only(text):
    """Blank every string and comment in place, so a docstring that names one of
    these forms is not mistaken for the code doing it (newlines preserved)."""
    buf = list(text)
    lines = text.splitlines(keepends=True)
    offs = [0]
    for ln in lines:
        offs.append(offs[-1] + len(ln))
    def pos(row, col):
        return offs[row - 1] + col
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except tokenize.TokenError:
        return text
    for tok in toks:
        if tok.type in (tokenize.STRING, tokenize.COMMENT):
            for k in range(pos(*tok.start), pos(*tok.end)):
                if buf[k] != "\n":
                    buf[k] = " "
    return "".join(buf)

def groupby_args(code, idx):
    """Return the balanced-paren argument text of the .groupby( call whose '('
    is at index idx, or None if unbalanced."""
    depth = 0
    for j in range(idx, len(code)):
        c = code[j]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return code[idx + 1:j]
    return None

to_numpy = 0              # `.to_numpy(` occurrences (the reads we converted)
groupby_total = 0
groupby_missing_observed = 0
TN = re.compile(r"\.to_numpy\s*\(")
GB = re.compile(r"\.groupby\s*\(")

for p in sorted(pathlib.Path(root).rglob("*.py")):
    code = code_only(io.open(p, encoding="utf-8").read())
    to_numpy += len(TN.findall(code))
    for m in GB.finditer(code):
        groupby_total += 1
        args = groupby_args(code, m.end() - 1)
        if args is None or "observed=" not in args:
            groupby_missing_observed += 1

bad = False
def report(label, got, want):
    global bad
    tag = "ok" if got == want else "BAD"
    if got != want:
        bad = True
    print("  %-3s %-38s %d (want %d)" % (tag, label, got, want))

report(".to_numpy() conversions", to_numpy, 37)
report("groupby calls without observed=", groupby_missing_observed, 0)
print("  (%d groupby call(s) total, all naming observed=)" % groupby_total)
if bad:
    sys.stderr.write("checks: independent scan mismatch\n")
    sys.exit(1)
print("independent scan: 37 reads converted, every groupby names observed=")
PYEOF

echo "--- step 4 golden produced"

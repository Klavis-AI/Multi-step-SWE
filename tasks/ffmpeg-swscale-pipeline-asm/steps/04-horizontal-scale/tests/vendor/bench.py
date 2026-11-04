#!/usr/bin/env python3
"""Score one step from FFmpeg checkasm's per-function benchmark output.

    bench.py --funcs FUNCS.stepN --cand <checkasm --bench stdout> \
             [--base <same, for this step's base build>] [--stderr <captured stderr>]

checkasm re-runs every test once per CPU feature level, with
`av_force_cpu_flags()` set to that level, and prints one benchmark row per
(implementation) version it created. The first version of a function is the one
installed at the baseline level -- the portable C reference, suffixed `_c` --
and each later row is an implementation the library installed as the CPU level
rose. So for a function `f`:

    speedup_f = cycles(f at the baseline level) / cycles(f installed on this host)

Both figures are measured in the same binary, the same process and the same
run, on identical randomized inputs, and the differential assertion checkasm
performs between them *is* this step's per-function correctness gate. The
implementation timed is whatever the library installed -- reached through the
SwsContext method pointer, never a symbol named by the harness -- so a
candidate who optimizes the portable C path is measured like one who writes a
kernel.

The last row is the one a real frame would use, because a version only appears
when the installed pointer changes and the levels run in ascending order.

The numerator is `min(the candidate's own baseline row, this step's base
build's baseline row)`. Taking the smaller is what closes the C-pessimizer
exploit: a candidate who installs a deliberately slow implementation at the
baseline level inflates every ratio above it, and nothing else catches that --
checkasm never compares the baseline pointer against anything, and a real
decode installs the highest level, so the end-to-end digests do not see it
either. It is also conservative in the honest direction: the credited ratio is
never larger than the one measured inside the candidate's own binary.
Comparing the two figures and *failing* on the difference does not work and was
tried elsewhere -- code layout alone moves identical C functions 0.85x-1.14x --
so the ratio is reported as a diagnostic and is not a gate.

A function with no row above the baseline scores 1.0, which contributes
ln(1) = 0 and pulls the step's mean down: neglecting part of the scope costs,
which is what using a geometric mean rather than a sum of times buys.

    step speedup = exp(mean_f(ln(max(1, speedup_f))))
    reward       = correctness x ln(step speedup)

Missing rows are never a sentinel: a function that produced no number at all
scores 1.0 and is counted in `unmeasured`, and the caller fails the step on
correctness rather than on an unbounded ratio.
"""
import json
import math
import re
import sys

# Every suffix checkasm can append, from the CPU-level table in
# tests/checkasm/checkasm.c. `c` is the baseline level: the reference.
CPU_SUFFIXES = ("c", "mmx", "mmxext", "sse", "sse2", "sse3", "ssse3", "sse4",
                "sse42", "aesni", "clmul", "avx", "xop", "fma3", "fma4",
                "avx2", "avx512", "avx512icl")

LINE = re.compile(r"^\s+(\S+):\s+([0-9.]+)")
FAIL = re.compile(r"^\s*FAILURE:\s+(\S+)")


def parse_bench(path):
    """-> ({label: cycles}, [labels in first-seen order])."""
    best, order = {}, []
    in_results = False
    with open(path, errors="replace") as fh:
        for line in fh:
            if line.startswith("Benchmark results"):
                in_results = True
                continue
            if not in_results:
                continue
            m = LINE.match(line)
            if not m:
                continue
            label, cycles = m.group(1), float(m.group(2))
            if label in ("name", "nop") or cycles <= 0:
                continue
            if label not in best:
                order.append(label)
                best[label] = cycles
            else:
                best[label] = min(best[label], cycles)
    return best, order


def parse_failures(path):
    out = []
    try:
        with open(path, errors="replace") as fh:
            for line in fh:
                m = FAIL.match(line)
                if m:
                    out.append(m.group(1).rstrip("(").rstrip())
    except OSError:
        pass
    return out


def split(label):
    """-> (base name, cpu suffix) or None if the label carries no suffix."""
    i = label.rfind("_")
    if i < 0:
        return None
    suffix = label[i + 1:]
    if suffix not in CPU_SUFFIXES:
        return None
    return label[:i], suffix


def pair(labels):
    """Group benchmark rows into (base name -> {ref, variants}).

    Most functions are named `<base>_<cpu suffix>`: the portable C reference is
    `<base>_c` and each later row is an implementation the library installed as
    the CPU level rose. The fused-operation module is different -- it tags its
    rows with the *backend* that answered, so the x86 backend's row is
    `<base>_x86_<cpu suffix>` and upstream's memcpy backend's is
    `<base>_memcpy_c`.

    Two rules follow, and both are structural rather than a list of names:

      * a row is a *baseline* only when its stem is exactly some other row's
        stem minus `_c` -- so `..._memcpy_c` is not a baseline, because its stem
        is not `<base>_c` for any base that also has a plain `_c` row;
      * a row counts as an *optimized implementation* only when its CPU suffix
        is above the baseline level, which is what excludes a variant that was
        compiled for the baseline CPU level.

    The result is the same for both modules: the reference is the portable C
    implementation, and the implementation timed is whatever the library
    installed above it.
    """
    stems = {}
    for lab in labels:
        sp = split(lab)
        if sp:
            stems[lab] = sp
    base_set = {stem for stem, suf in stems.values() if suf == "c"}
    out = {}
    for lab in labels:
        sp = stems.get(lab)
        if not sp:
            continue
        stem, suf = sp
        if stem in base_set:
            b = stem
        else:
            cands = [x for x in base_set
                     if stem.startswith(x + "_")]
            if not cands:
                continue
            b = max(cands, key=len)
        e = out.setdefault(b, {"ref": None, "variants": []})
        if suf == "c" and stem == b:
            if e["ref"] is None:
                e["ref"] = lab
        elif suf != "c":
            e["variants"].append(lab)
    return out


def merge(best_orders):
    best, order = {}, []
    for b, o in best_orders:
        for lab in o:
            if lab not in best:
                order.append(lab)
                best[lab] = b[lab]
            else:
                best[lab] = min(best[lab], b[lab])
    return best, order


def score(cand_paths, funcs, base_paths=()):
    """Several benchmark runs may be given; the best (lowest) cycle count per
    label is taken across them, which is the usual best-of-N against a noisy
    host."""
    if isinstance(cand_paths, str):
        cand_paths = [cand_paths]
    best, order = merge([parse_bench(p) for p in cand_paths])
    grouped = pair(order)
    best_cycles = best

    base_ref = {}
    if base_paths:
        if isinstance(base_paths, str):
            base_paths = [base_paths]
        bbest, border = merge([parse_bench(p) for p in base_paths])
        for b, e in pair(border).items():
            if e["ref"]:
                base_ref[b] = bbest[e["ref"]]

    rows, logs, unmeasured = [], [], []
    for name in funcs:
        e = grouped.get(name)
        own = best_cycles[e["ref"]] if e and e["ref"] else None
        c = own
        if own is not None and base_ref.get(name, 0) > 0:
            c = min(own, base_ref[name])
        b = base_ref.get(name)
        if c is None:
            rows.append((name, None, None, 1.0, "not reported", b))
            logs.append(0.0)
            unmeasured.append(name)
            continue
        vs = e["variants"] if e else []
        if not vs:
            rows.append((name, c, None, 1.0, "no optimized implementation", b))
            logs.append(0.0)
            continue
        installed = vs[-1]
        t = best_cycles[installed]
        sp_ratio = c / t if t > 0 else 1.0
        rows.append((name, c, t, sp_ratio, installed.rsplit("_", 1)[1], b))
        logs.append(math.log(max(1.0, sp_ratio)))
    mean_log = sum(logs) / len(logs) if logs else 0.0
    return rows, mean_log, unmeasured


def main(argv):
    if "--funcs" not in argv or "--cand" not in argv:
        sys.exit(__doc__)
    fp = argv[argv.index("--funcs") + 1]
    cps = [argv[i + 1] for i, a in enumerate(argv) if a == "--cand"]
    bps = [argv[i + 1] for i, a in enumerate(argv) if a == "--base"]
    sp = argv[argv.index("--stderr") + 1] if "--stderr" in argv else None

    funcs = [l.strip() for l in open(fp) if l.strip() and not l.startswith("#")]
    rows, mean_log, unmeasured = score(cps, funcs, bps)
    failures = parse_failures(sp) if sp else []

    slow = [r for r in rows if r[3] < 1.0001]
    print("  %d scoped functions; %d without a speedup; %d not reported"
          % (len(rows), len(slow), len(unmeasured)))
    for name, c, t, sp_ratio, where, _ in sorted(rows, key=lambda r: r[3])[:10]:
        print("    %-52s %10s -> %-10s %6.2fx (%s)"
              % (name, "%.1f" % c if c else "-", "%.1f" % t if t else "-",
                 sp_ratio, where))
    if len(rows) > 10:
        print("    ... fastest 3:")
        for name, c, t, sp_ratio, where, _ in sorted(rows, key=lambda r: r[3])[-3:]:
            print("    %-52s %10s -> %-10s %6.2fx (%s)"
                  % (name, "%.1f" % c if c else "-", "%.1f" % t if t else "-",
                     sp_ratio, where))
    if bps:
        # Diagnostic only, never a gate: the two builds compile byte-identical
        # C, but into different binaries, and code layout alone moves identical
        # C functions by 0.85x-1.14x. What it is for is showing that neither
        # side has been deliberately pessimized.
        drifts = sorted(r[1] / r[5] for r in rows if r[1] and r[5])
        if drifts:
            print("  C-reference drift candidate/base: median %.3f "
                  "range %.3f-%.3f over %d functions"
                  % (drifts[len(drifts) // 2], drifts[0], drifts[-1],
                     len(drifts)))
    if failures:
        print("  checkasm FAILURE lines: %d (first 10: %s)"
              % (len(failures), ", ".join(failures[:10])))
        print("FAILURES %s" % json.dumps(sorted(set(failures))))
    print("SPEEDUP %.6f" % math.exp(mean_log))
    print("MEAN_LOG %.6f" % mean_log)
    print("UNMEASURED %s" % json.dumps(unmeasured))


if __name__ == "__main__":
    main(sys.argv)

#!/usr/bin/env python3
"""Remove accelerated-kernel stage file sets from a LAMMPS source tree.

    strip.py <tree> [stage-id ...]

Every stage named on the command line has its own files deleted from
src/INTEL/.  Files belonging to no stage are handled as follows:

  * src/INTEL package infrastructure (fix_intel, intel_buffers,
    intel_simd.h, intel_intrinsics.h, intel_preprocess.h) stays: it is
    style-agnostic and every accelerated style needs it.
  * The always-excluded set (the EAM family, pair_lj_long_coul_long_intel,
    the fix_*_intel integrators, and every accelerated pair style that is
    not a stage) is deleted for every invocation: those files are outside
    the ladder and each is a worked example of an accelerated kernel.
  * The accelerated NEIGHBOUR-LIST layer (nbin_intel, nstencil_*_intel,
    npair_*_intel) also stays: `-sf intel` resolves `neighbor 0.3 bin` as
    `bin/intel` and *hard-fails* rather than falling back when it is
    missing, so without it no accelerated style can be selected at all. It
    is the layer the seed held fixed ("the integrator, the domain
    decomposition, the communication layer and the input parser held
    fixed") and it does not move the section row any stage is measured on.
  * lib/gpu is deleted for every invocation: it is the same class of leak as
    src/KOKKOS -- another architecture's implementation of the same kernels
    (lal_lj, lal_sw, lal_tersoff, lal_airebo) -- and is only reached when
    PKG_GPU is on, which it never is here.
  * src/KOKKOS, src/OPENMP, src/GPU and src/OPT are deleted for every
    invocation: each implements the same pair, bonded, neighbour and PPPM
    styles a second time in another dialect -- src/OPT/pair_lj_cut_opt.cpp
    is the same Lennard-Jones kernel restructured for the compiler instead
    of for the hardware -- so leaving them would be a translation exercise
    rather than the task.

Idempotent: re-running on a tree where a stage is already absent is a
no-op for that stage.
"""

import os
import re
import shutil
import sys

# stage id -> files owned by that stage under src/INTEL/
STAGES = {
    "01-pair-lj-cut": [
        "pair_lj_cut_intel.cpp", "pair_lj_cut_intel.h",
    ],
    "02-pair-sw": [
        "pair_sw_intel.cpp", "pair_sw_intel.h",
    ],
    "03-pair-tersoff": [
        "pair_tersoff_intel.cpp", "pair_tersoff_intel.h",
    ],
    "04-pair-lj-cut-coul-long": [
        "pair_lj_cut_coul_long_intel.cpp", "pair_lj_cut_coul_long_intel.h",
    ],
    "05-pair-airebo": [
        "pair_airebo_intel.cpp", "pair_airebo_intel.h",
        "intel_intrinsics_airebo.h",
    ],
}

# deleted regardless of which stages were named: every other accelerated style
# in the package. Each is either a stage the measurement could not carry (see
# the seed rewrite) or a worked example of the file the candidate has to write.
ALWAYS = [
    "pair_eam_intel.cpp", "pair_eam_intel.h",
    "pair_eam_alloy_intel.cpp", "pair_eam_alloy_intel.h",
    "pair_eam_fs_intel.cpp", "pair_eam_fs_intel.h",
    "pair_lj_long_coul_long_intel.cpp", "pair_lj_long_coul_long_intel.h",
    "pair_lj_cut_coul_long_intel.cpp", "pair_lj_cut_coul_long_intel.h",
    "pair_buck_intel.cpp", "pair_buck_intel.h",
    "pair_buck_coul_long_intel.cpp", "pair_buck_coul_long_intel.h",
    "pair_buck_coul_cut_intel.cpp", "pair_buck_coul_cut_intel.h",
    "pair_lj_charmm_coul_long_intel.cpp", "pair_lj_charmm_coul_long_intel.h",
    "pair_lj_charmm_coul_charmm_intel.cpp", "pair_lj_charmm_coul_charmm_intel.h",
    "dihedral_charmm_intel.cpp", "dihedral_charmm_intel.h",
    "dihedral_opls_intel.cpp", "dihedral_opls_intel.h",
    "dihedral_harmonic_intel.cpp", "dihedral_harmonic_intel.h",
    "dihedral_fourier_intel.cpp", "dihedral_fourier_intel.h",
    "angle_charmm_intel.cpp", "angle_charmm_intel.h",
    "angle_harmonic_intel.cpp", "angle_harmonic_intel.h",
    "bond_harmonic_intel.cpp", "bond_harmonic_intel.h",
    "bond_fene_intel.cpp", "bond_fene_intel.h",
    "improper_harmonic_intel.cpp", "improper_harmonic_intel.h",
    "improper_cvff_intel.cpp", "improper_cvff_intel.h",
    "pair_gayberne_intel.cpp", "pair_gayberne_intel.h",
    "pair_dpd_intel.cpp", "pair_dpd_intel.h",
    "math_extra_intel.h",
    "pppm_intel.cpp", "pppm_intel.h",
    "pppm_disp_intel.cpp", "pppm_disp_intel.h",
    "pppm_electrode_intel.cpp", "pppm_electrode_intel.h",
    "verlet_lrt_intel.cpp", "verlet_lrt_intel.h",
    "fix_electrode_conp_intel.h", "fix_electrode_conq_intel.h",
    "fix_electrode_thermo_intel.h",
    "pair_snap_intel.cpp", "pair_snap_intel.h",
    "sna_intel.cpp", "sna_intel.h",
    "pair_rebo_intel.cpp", "pair_rebo_intel.h",
    "pair_airebo_morse_intel.cpp", "pair_airebo_morse_intel.h",
    "fix_nh_intel.cpp", "fix_nh_intel.h",
    "fix_npt_intel.cpp", "fix_npt_intel.h",
    "fix_nvt_intel.cpp", "fix_nvt_intel.h",
    "fix_nvt_sllod_intel.cpp", "fix_nvt_sllod_intel.h",
    "fix_nve_intel.cpp", "fix_nve_intel.h",
    "fix_nve_asphere_intel.cpp", "fix_nve_asphere_intel.h",
]

OTHER_DIALECT_DIRS = ["KOKKOS", "OPENMP", "GPU", "OPT"]

# library trees holding another-architecture implementation of the SAME kernels:
# lib/gpu ships lal_lj, lal_sw, lal_tersoff and lal_airebo as CUDA/OpenCL, i.e. a
# second implementation of four of the five milestones' algorithms. lib/gpu is
# only reached when PKG_GPU is on, which it never is here.
OTHER_DIALECT_LIBS = ["gpu"]


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    tree = argv[1]
    stages = argv[2:]
    for s in stages:
        if s not in STAGES:
            sys.stderr.write("unknown stage: %s\n" % s)
            return 2
    intel = os.path.join(tree, "src", "INTEL")
    if not os.path.isdir(intel):
        sys.stderr.write("no src/INTEL in %s\n" % tree)
        return 2

    removed = []
    for s in stages:
        for f in STAGES[s]:
            p = os.path.join(intel, f)
            if os.path.exists(p):
                os.remove(p)
                removed.append("INTEL/" + f)
    for f in ALWAYS:
        p = os.path.join(intel, f)
        if os.path.exists(p):
            os.remove(p)
            removed.append("INTEL/" + f)
    for d in OTHER_DIALECT_DIRS:
        p = os.path.join(tree, "src", d)
        if os.path.isdir(p):
            shutil.rmtree(p)
            removed.append("src/" + d + "/")
    for d in OTHER_DIALECT_LIBS:
        p = os.path.join(tree, "lib", d)
        if os.path.isdir(p):
            shutil.rmtree(p)
            removed.append("lib/" + d + "/")

    # cmake/Modules/Packages/INTEL.cmake is the one build file that NAMES
    # package files instead of globbing them. ``set(INTEL_SOURCES ...)`` is a
    # hardcoded list and two further statements append files conditionally;
    # naming a file that is not in the tree makes configure fail outright
    # ("Cannot find source file"), so the stripper has to keep this file's
    # bookkeeping in step with the tree.
    #
    # Two different edits, deliberately:
    #   * the ``set(INTEL_SOURCES ...)`` statement is KEPT but filtered, because
    #     INTEL_SOURCES must still be defined (it is what `fix_intel.cpp` and
    #     the buffers live in);
    #   * every other statement naming a file that is gone is DROPPED rather
    #     than commented out -- a trail of commented-out lines is a map of
    #     exactly what was removed.
    # Idempotent: on an already-filtered file both passes are no-ops.
    cm = os.path.join(tree, "cmake", "Modules", "Packages", "INTEL.cmake")
    if os.path.exists(cm):
        with open(cm) as fh:
            text = fh.read()
        def present(tok):
            m = re.match(r"\$\{INTEL_SOURCES_DIR\}/([A-Za-z0-9_.\-]+)$", tok)
            return m is None or os.path.exists(os.path.join(intel, m.group(1)))
        m = re.search(r"set\(INTEL_SOURCES\s+[^)]*\)", text)
        extra = []
        if m:
            toks = m.group(0).split()
            # the statement's own trailing ")" is a token of its own; drop it
            # and re-add it once, or the rewrite closes the list twice.
            # the closing ")" may be glued to the last filename
            # (".../npair_intel.cpp)"), so strip it before testing and never
            # carry it into the rebuilt statement.
            kept, dropped = [toks[0]], []
            for tok in toks[1:]:
                clean = tok.rstrip(")")
                if clean == "":
                    continue
                (kept if present(clean) else dropped).append(clean)
            if dropped:
                text = text[:m.start()] + " ".join(kept) + ")" + text[m.end():]
                extra += ["INTEL.cmake(" + d + ")" for d in dropped]
            elif ")" not in toks:
                pass
            # self-check: never write a file that will not parse
            if text.count("(") != text.count(")"):
                sys.stderr.write("strip: INTEL.cmake rewrite would unbalance "
                                 "parentheses; leaving it alone\n")
                raise SystemExit(3)
        out, touched = [], 0
        for ln in text.split("\n"):
            refs = re.findall(r"\$\{INTEL_SOURCES_DIR\}/([A-Za-z0-9_.\-]+)", ln)
            if refs and any(not os.path.exists(os.path.join(intel, r)) for r in refs):
                touched += 1
                continue
            out.append(ln)
        # an `if(PKG_X)` whose only statement the line pass removed is a
        # visible fingerprint of the strip; collapse it.
        joined = "\n".join(out)
        joined2 = re.sub(r"if\((PKG_[A-Za-z0-9_-]+)\)\n\s*endif\(\)\n", "", joined)
        if joined2 != joined:
            touched += 1
            out = joined2.split("\n")
        if extra or touched:
            with open(cm, "w") as fh:
                fh.write("\n".join(out))
            removed += extra
            removed.append("cmake/Modules/Packages/INTEL.cmake (%d statements)" % touched)

    # Install.sh is the make-based build's equivalent of the CMake source
    # list. It globs, so it needs no edits, but its per-file dependency table
    # names two files this strip removes -- a dangling name is a map of part
    # of what was taken away, so the lines are dropped.
    ins = os.path.join(intel, "Install.sh")
    if os.path.exists(ins):
        with open(ins) as fh:
            lines = fh.read().split("\n")
        kept, dropped = [], 0
        for ln in lines:
            if re.search(r"\b(math_extra_intel\.h|intel_intrinsics_airebo\.h|pair_tersoff_intel\.cpp|pair_airebo_intel\.cpp)\b", ln):
                dropped += 1
                continue
            kept.append(ln)
        if dropped:
            with open(ins, "w") as fh:
                fh.write("\n".join(kept))
            removed.append("INTEL/Install.sh (%d lines naming removed files)" % dropped)

    sys.stderr.write("strip: removed %d paths from %s\n" % (len(removed), tree))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

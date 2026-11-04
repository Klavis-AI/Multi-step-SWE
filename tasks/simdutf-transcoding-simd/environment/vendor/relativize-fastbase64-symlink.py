#!/usr/bin/env python3
"""Make simdutf's POST_BUILD fastbase64.coreutils symlink use a RELATIVE target.

Upstream's `tools/CMakeLists.txt` creates a link in every CMake build tree of
this source:

    cmake -E create_symlink $<TARGET_FILE:fastbase64> \\
        $<TARGET_FILE_DIR:fastbase64>/fastbase64.coreutils$<TARGET_FILE_SUFFIX:fastbase64>

`$<TARGET_FILE:fastbase64>` expands to an ABSOLUTE path, so the link's target is
absolute -- in whatever directory the tree was configured into.  The harness
takes its /app workspace snapshot with GNU tar and extracts it host-side with
Python's tarfile "data" filter (the trial artifact handler), which
raises `tarfile.AbsoluteLinkError` on any link whose target is absolute; that
aborts the whole download, the verifier's separate container receives no
`/app/simdutf`, and the step grades `correctness=0` on a correct tree.

The offender follows the build directory, and a candidate may name its build
directory anything, so an artifact `exclude` list cannot cover it.  The link
merely points at the executable beside it, so a bare relative target is
correct -- which is exactly what upstream's own install-time rule already uses:

    cmake -E create_symlink "fastbase64" "${BINDIR}/fastbase64.coreutils"

This script rewrites the POST_BUILD rule to that form.  It is idempotent and
fails loudly if the expected rule is not found, so an upstream change cannot
silently reintroduce absolute links.  Runs at image-build time only; the
candidate's tree differs from upstream in this one build-system token.
"""
import sys
from pathlib import Path

ADD_OLD = (
    "-E create_symlink $<TARGET_FILE:fastbase64> "
    "$<TARGET_FILE_DIR:fastbase64>/fastbase64.coreutils$<TARGET_FILE_SUFFIX:fastbase64>"
)
# The link lives beside its target, so a relative target keeps it working.
ADD_NEW = (
    "-E create_symlink fastbase64$<TARGET_FILE_SUFFIX:fastbase64> "
    "$<TARGET_FILE_DIR:fastbase64>/fastbase64.coreutils$<TARGET_FILE_SUFFIX:fastbase64>"
)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <simdutf tree>", file=sys.stderr)
        return 2
    cmake = Path(sys.argv[1]) / "tools" / "CMakeLists.txt"
    text = cmake.read_text()
    if ADD_OLD in text:
        if text.count(ADD_OLD) != 1:
            print(f"ERROR: {cmake}: expected exactly one POST_BUILD symlink rule", file=sys.stderr)
            return 1
        cmake.write_text(text.replace(ADD_OLD, ADD_NEW, 1))
        print(f"relativized the fastbase64.coreutils POST_BUILD symlink in {cmake}")
        return 0
    if ADD_NEW in text:
        print(f"already relativized: {cmake}")
        return 0
    print(f"ERROR: {cmake}: the expected POST_BUILD symlink rule is absent", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())

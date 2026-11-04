# Input conversion to planar

## What this milestone is

A source frame arrives in its own pixel format and has to be turned
into libswscale's internal planar form. Each format has its own extraction
kernel -- the 24-bit packed RGB cases need unaligned cross-register shuffles,
the YUV 4:2:2 cases a different lane pattern, the semi-planar ones an
interleave, and every planar-RGB depth its own bit-scale -- and they are the
first thing `sws_scale()` does to a frame, before anything is filtered.

**This milestone is only about this stage.** Make it fast; do not change what
it computes.

## Behaviour must not change

The library must do exactly what it did before, only faster. For every input,
every pixel format, every size and every configuration the output must be the
same as it is now -- not merely the cases you happen to test. An approximation,
a rounding change, a reordered sum, a narrower intermediate or a shortcut that
takes a different branch on some inputs all fail this milestone however fast
they are; correctness is a prerequisite, so a tree that changes behaviour
fails the milestone.

Where a kernel may legitimately differ from the portable C reference at the last
bit, the project's own differential suite already encodes that allowance per
function, and it is the suite that decides. A kernel that is *wrong* rather than
differently rounded is not covered by it, and the end-to-end guard compares
`sws_scale` output against the base within a small declared per-configuration
budget, which `BENCHMARK.md` lists.

## What is in scope

**350 functions** -- exactly the ones this stage's kernels implement, and
nothing else. They are listed by name in `SCORED_FUNCTIONS.txt`, in this
milestone's workspace, and that file defines the in-scope set: a function not in
it is out of scope, whatever else the library does.

Each of them is timed by FFmpeg's own benchmark harness, which prints one row
per function per implementation the library *installs* as the CPU feature level
rises, alongside the portable C implementation it starts from. `BENCHMARK.md`
gives the command and an example of reading its output, and you can check your
own coverage by comparing the names it prints against the in-scope list: every
in-scope function must be optimized, and one with no row above the baseline row
is not faster, so a broad implementation that covers the whole set is required,
not a deep one that covers part of it. `BENCHMARK.md`, in this
workspace, has the exact commands and an example of reading the output.

## Where the work goes

`libswscale/x86/` is yours. Add files there, list them in
`libswscale/x86/Makefile`, and install them through the same dispatch hooks the
portable C is installed through, so that the library reaches them without any
caller changing.

Everything outside `libswscale/x86/` must stay exactly as it is. In particular
the portable C implementations in `libswscale/*.c` are the *reference* the
milestone is measured against: they must keep computing what they compute now,
at the speed they compute it now.

## The instruction set

This milestone's reference implementation tops out at **avx2**. That is a
fairness rule rather than a technical limit of the machine: the comparison is
only like-for-like if both sides are allowed the same registers, so code that
reaches wider registers than the reference does is not an improvement, it is
access to a wider machine.

Where avx2 is narrower than what the target machine executes, this milestone's
dispatch is followed to the object files that implement it, they are
disassembled, and the milestone fails if any of them is wider. Where it is
not, the machine is itself the limit and there is nothing narrower to check.

## Getting the code back

Commit nothing; leave the tree built and consistent. `git` is not part of the
workflow. Do not cheat by using online solutions or hints specific to this task.

## Time budget

The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the
configured timeout is authoritative and resets for this step.

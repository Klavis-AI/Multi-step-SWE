# WORKSPACE_HANDOFF — step 10 of 13 (certificate-transparency-go v1.1.3)

## What /app is

`/app` is a **cumulative workspace**. Each step runs in a fresh conversation
with no memory of the previous one, but on the *same* `/app` tree, with that
step's overlay laid on top. Everything you write under `/app` is what the next
step starts from, so the workspace is the handoff — there is no other channel
between steps, and nothing you say at the end of a step is read by the agent
that continues it.

The overlay **replaces** files with the same name. These are overlay-owned and
are refreshed at the start of every step:

* `ct-spec/` — the documentation corpus, including this step's `PROFILE.md`;
* `TEST_INTERFACE.md` — the verifier contract, refreshed for this step;
* `WORKSPACE_HANDOFF.md` — this file;
* `timer.sh` — the countdown for this step's budget;
* `go.mod` and `go.sum` — the benchmark-owned build manifest.

**Everything else under `/app` is yours and is preserved exactly as you leave
it.** Do not keep work inside an overlay-owned path: it will be overwritten
without warning at the start of the next step.

## What you inherit

`/app` holds the stack through step 9 at certificate-transparency-go
v1.1.2 — the codec, the forked parsers with the `lax` relaxations,
the RFC 6962 structures, `ctutil`, the v1 and v2 log-list packages, the
front end with the inverted admission rule, frozen STHs, `/healthz`, EKU
filtering, get-entries alignment and binary-encoded configuration, the
submission proxy with `ctpolicy` and `schedule`, `migrillian`, and a `ctclient`
built on the `flag` package.

Continue in place. Most of this step adds; two pieces of it are a **move** and
a **deletion**, and both are graded on the old home being empty afterwards.

Be careful with the inheritance here: the type that moves is one the front end
and a log-list helper both hold by type, so the move is not confined to the two
files that declare it.

## Preservation rules

1. **Work in `/app`, in place.** Do not rename the module, do not move the
   tree, do not introduce a second `go.mod` below `/app`, and do not create a
   `go.work`. The verifier builds `/app` as one module and only as one module.
2. **Keep it building at every point you might be interrupted.** The snapshot
   is taken when the step's time runs out, not when you decide you are
   finished, so a workspace that does not compile is a workspace that scores
   nothing — and it is also what the next step inherits. Reach a compiling
   state early and improve it in place; prefer a small, working, honest surface
   to a large one that is half-edited when the clock stops.
3. **Do not delete or move the overlay-owned files** listed above. You may read
   them as often as you like; `ct-spec/` in particular is the only description
   of the behaviour you are implementing.
4. **Do not treat a built artefact as the deliverable.** Compiled binaries and
   the build cache are excluded from the snapshot; the verifier rebuilds from
   your sources in its own container. Sources are what carry forward.
5. **Do not weaken anything to make it pass.** There is nothing in `/app` that
   grades you, so there is nothing here to weaken — but the same applies to
   your own tests and to any behaviour a later step will re-check.

## Checkpoint before you finish

Copy-paste this block. It is the same check the verifier's first two steps
perform, and it takes seconds.

```sh
cd /app

# 1. The module is intact and correctly named.
head -1 go.mod
# expected on stdout:
#   module github.com/google/certificate-transparency-go

# 2. Everything compiles, offline, with the pinned manifest.
GOFLAGS=-mod=mod GOPROXY=off GOWORK=off go build ./... && echo BUILD-OK
# expected on stdout:
#   BUILD-OK
#   (nothing else; any compiler diagnostic here is a lost group at grading time)

# 3. The programs this milestone is supposed to produce exist and are built.
go build -o /tmp/ck-bin/ ./... && ls /tmp/ck-bin | sort | tr '\n' ' '; echo
# expected on stdout:
#   a space-separated list of this step's programs — see TEST_INTERFACE.md
#   section 4 for the ones the verifier runs, and CLI.md section 1's `present
#   at` column — which counts them at every release — for the number expected
#   at this milestone

# 4. This step's own probe.
go build -o /tmp/ck-bin/ ./... && ls /tmp/ck-bin | wc -l
# expected on stdout:
#   19   (the fifteen of the previous step plus the four that arrive with
#         internal/witness/cmd/ — see PROFILE.md)

/tmp/ck-bin/ctclient --help 2>&1 | grep -c '^Available Commands:$'
# expected on stdout:
#   1   (a command tree, not a flag-package usage block)

go doc ./x509util PEMCertPool >/dev/null 2>&1; echo "new=$?"
go doc ./trillian/ctfe PEMCertPool >/dev/null 2>&1; echo "old=$?"
# expected on stdout:
#   new=0
#   old=1   (non-zero: the type moved to x509util, it was not copied there)

# 5. The overlay is undamaged.
ls ct-spec/README.md TEST_INTERFACE.md WORKSPACE_HANDOFF.md timer.sh >/dev/null && echo OVERLAY-OK
# expected on stdout:
#   OVERLAY-OK

# 6. How much time is left.
bash /app/timer.sh
# expected on stdout:
#   Remaining time for this step (hours:minutes:seconds): H:MM:SS
```

If step 2 does not print `BUILD-OK`, fix that before anything else. A
non-compiling tree scores zero for this step *and* hands the next step a
broken start.

## Restoring this workspace by hand

If you damage the workspace, you can rebuild the parts that are not yours:

* `go.mod` / `go.sum` — the verifier replaces both with its own pinned copies
  before grading, so a local mistake here costs you only your own build. If you
  need the originals back and have no copy, the pinned require list is the one
  described in `TEST_INTERFACE.md` section 2; get the tree compiling again with
  what is in the module cache and do not add anything new.
* `ct-spec/` — every file is listed with its SHA-256 in `ct-spec/SHA256SUMS`.
  Run `cd /app/ct-spec && sha256sum -c SHA256SUMS` to find out what changed. A
  missing page cannot be restored from inside the container; it will come back
  with the next step's overlay.
* `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh` — overlay-owned;
  refreshed next step; not recoverable inside this one.
* **Your own source is not recoverable by any of this.** There is no snapshot,
  no `.git` you can rely on, and no backup. If you are about to attempt
  something sweeping, copy the tree to `/app/.bak-<something>` first and delete
  the copy when you are done — anything you leave behind is carried into the
  next step and read by the agent continuing your work.

## Verifier visibility

* The verifier runs in a **separate container**, on a copy of `/app` taken when
  this step's time ends. It reads your sources, restores its own `go.mod` and
  `go.sum`, and builds. It never sees your shell history, your scratch notes,
  or anything outside `/app`.
* Excluded from the snapshot: `.git/`, `bin/`, and `*.test` files. Do not park
  anything load-bearing in those.
* Notes you leave in the tree are read by the *next agent*, not by the
  verifier, and they are not scored. A short `NOTES.md` of your own naming what
  is unfinished is a kindness to your successor and costs nothing.

## This step's starting point

Whatever step 9 left, with two things from it re-checked here. If `ct_server`
does not start, fix that first: `gate-endtoend` boots your own front end before
it asserts anything, and a front end that will not start blocks the read-only
work as well. If a binary-encoded `LogConfigSet` no longer loads, fix that too:
`ctfe-readonly-mode` writes one and reads it back. Neither is an `inherited-`
group here — the three groups under that name at this step are pure in-process
codec and signature checks — and nothing at this step re-checks step 09's own
flag removals.

Then do the two structural changes before anything else, because the rest
compiles on top of them. Move `PEMCertPool` from `trillian/ctfe` to `x509util`
and retype every holder — the validation options' root pool, the constructor
that builds them, the v2 log-list root map — leaving nothing behind under the
old name. Delete the two packages that lost their callers; nothing in the tree
imports them, so this is a plain removal.

Then rebuild `ctclient`. Read CLI.md §3.1 and §3.2 side by side before you
start: the second is not a rename of the first. `main.go` ends up a shim and a
`cmd/` package owns the root command, the persistent flags, and one leaf per
subcommand with its own usage line, short description, aliases and flags.

The v3 log-list package, the witness, the 429 arm and `is_readonly` are
independent of all of that and of each other, and each is worth having on its
own.

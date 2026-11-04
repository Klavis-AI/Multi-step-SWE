# WORKSPACE_HANDOFF — step 13 of 13 (certificate-transparency-go v1.3.3)

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

`/app` holds the stack through step 12 at certificate-transparency-go
v1.2.0 — the codec, the forked parsers, the RFC 6962 structures,
`ctutil`, `loglist3` as the only log-list package, the Cobra-based `ctclient`,
the submission proxy, `migrillian`, and a front end carrying the whole
admission history: the inverted expiry rules, rejected extensions, EKU
filtering, a frozen STH, read-only mode, quota, handler prefixes, and
issuance-chain storage with its cache and its two leaf layouts.

You also inherit `internal/witness` and its four programs. They leave here.

Continue in place. This step is wide rather than deep: it adds to almost every
part of that stack and rewrites almost none of it.

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
#   14   (four fewer than the step before — see PROFILE.md)

ls internal/witness 2>&1 | head -1
# expected on stdout:
#   a "no such file or directory" diagnostic, because this subtree is deleted
#   at this milestone and is graded by a build that must fail naming it

go doc ./loglist3 FindTiledLogByURL | head -2
# expected on stdout:
#   the signature of the finder that matches a tiled log by either of the two
#   URLs such a log is addressed by

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

Whatever step 12 left. If `ct_server` does not start, or the issuance-chain
storage selection from step 12 is not working, fix that first — both are
re-checked here under `inherited-` names, and the storage work at this step
sits directly on top of the step-12 interface.

Then treat the milestone as the list of independent changes it is, and get one
observable behaviour finished per change before improving any of them. The
cheapest are the ones that are pure command line: the four transport and
timeout flags, the three non-fresh flags, the body-size flag, the flags
`migrillian` no longer has, and the preloader's three.

Two need care. The parser change is a tightening that an inherited group can
catch you on, so read X509-FORK.md §5.4 for which parse entry point is exempt
before you touch either. And the deletion is a deletion: remove the tree, then
make sure nothing left in the workspace still imports it, because the build has
to succeed afterwards.

# WORKSPACE_HANDOFF — step 09 of 13 (certificate-transparency-go v1.1.2)

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

`/app` holds the stack through step 8 at certificate-transparency-go
v1.1.0 — the inverted admission rules, `reject_extensions`, frozen
STHs, `/healthz`, masked internal errors, the `lax` parsing tag with its three
relaxations, and the `ctpolicy`, `submission`, `schedule` and `loglist2`
packages.

Continue in place. This step is mostly **subtraction**: three programs, one
package, one client method and one flag leave the tree, and what replaces them
is smaller than what goes.

A deletion is only done when nothing imports the deleted thing. Budget for the
callers, not for the delete.

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
#   15   (three gossip programs are gone — see PROFILE.md)

/tmp/ck-bin/ct_server --by_range=true >/dev/null 2>&1; echo "exit=$?"
# expected on stdout:
#   exit=2   (the flag lived and died inside this milestone)

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

Whatever step 8 left. The admission path you rewrote there is where two of
this step's changes land, so if the inversion is incomplete, finish it first:
it is re-checked here under an `inherited-` name and the EKU filter sits
directly on top of it.

Do the removals early and the additions after, and count the callers before
you start — they are not where the names are. `dnsclient` is *imported* by
exactly two files, `ctutil/loginfo.go` and `client/ctclient/`, but the DNS
**flags** sit in three programs, and `sctcheck` and `sctscan` reach DNS
only through `ctutil`'s `…OverDNS…` constructors, so removing the package
without those three flag declarations leaves a tree that compiles and still
advertises `-dns`. `LogClient.AddJSON` is the opposite case: nothing in the
tree calls it, so deleting the method costs nothing — but `AddJSONPath` and
`AddJSONRequest` stay, and after the method goes nothing in the tree uses
them either, which is not a reason to remove them. `by_range` has exactly one
flag declaration and one call site, both in `trillian/ctfe/handlers.go`.

Then `align_getentries`, the EKU filter, the `Cache-Control` headers and the
binary-config fallback are four independent, small additions, and the Go
merge is a fifth that touches only the forked packages.

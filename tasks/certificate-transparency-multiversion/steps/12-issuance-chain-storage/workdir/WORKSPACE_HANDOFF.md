# WORKSPACE_HANDOFF — step 12 of 13 (certificate-transparency-go v1.2.0)

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

`/app` holds the stack through step 11 at certificate-transparency-go
v1.1.4 — the codec, the forked parsers, the RFC 6962 structures,
`ctutil`, the Cobra `ctclient`, the witness service, `ctpolicy`,
`submission`, `schedule`, `migrillian`, the front end with quota, a handler
prefix, a frozen STH, read-only mode and the inverted admission rules, and
**two** log-list packages where step 10 left three.

Continue in place. This step adds a subsystem beside what you have; it
replaces nothing.

Be careful with the inheritance here in a way that is easy to get backwards:
the leaf-construction path you are about to extend is the one step 01 built
and every step since has depended on, and its existing behaviour is the
default at v1.2.0, not the legacy. It is still graded, unchanged, under
`inherited-` names.

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
go doc ./trillian/ctfe/storage IssuanceChainStorage
# expected on stdout:
#   the two-method interface this milestone adds — one lookup by key, one add

go doc ./trillian/ctfe/cache NewIssuanceChainCache
# expected on stdout:
#   the factory that turns a cache type and an option into an
#   IssuanceChainCache, and refuses a type it does not know

go doc ./trillian/util ExtraDataForChainHash | head -2
# expected on stdout:
#   the out-of-line extra-data encoder, sitting beside ExtraDataForChain,
#   which must still exist and still behave exactly as it did

ls -d loglist2 2>/dev/null || echo NO-LOGLIST2
# expected on stdout:
#   NO-LOGLIST2   (the second log-list generation is gone at this milestone)

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

Whatever step 11 left, and it has to compile before anything here is worth
starting. Four milestones from the steps behind you are re-graded at this one
under `inherited-` names, each in a group of its own: the TLS
presentation-language codec, RFC 6962 Merkle leaf hashing, the forked X.509
parser's non-fatal error behaviour, and `client.LogClient`'s SCT signature
checking. Those four are the parts of the inherited stack this milestone is
most able to break from the inside, because the leaf work below runs straight
through the first two. If one of them is already broken when you arrive, fix
it before you add anything — no amount of new capability compensates, and the
rest of the inherited surface is carried only by the tree having to build.

Then go to the leaf-construction path and read it before you edit it. The
change is an **addition**: a second encoder that puts a hash where the chain
used to go, chosen per log, with the first encoder still the default and still
producing the same bytes it always did. Build the new path so the two can be
exercised against each other — same submission, same Merkle leaf, one field
different — because that is exactly how it is graded.

The storage and cache packages are separable and can be done first: two small
interfaces, a factory apiece, an LRU implementation and a no-op one, and the
configuration and flag validation that selects between them. None of that
needs a running server, and none of it needs the leaf work to be finished.

Deleting the second log-list package is independent of all of it, and is the
one place where finishing means the tree has *less* in it than it started
with. Move its callers, if any remain, before you delete it.

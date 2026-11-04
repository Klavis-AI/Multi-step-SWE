# Prefetch assets — ooxml-spreadsheet-engine-multiversion

The agent phase of this task runs against a network allowlist: the model's own
inference endpoint is the only reachable host. Everything the agent's build may
reach for must therefore already be in the image. This tree is what
`environment/Dockerfile` copies in and turns into a warm Go module cache.

## Why there is exactly one directory

`01/` holds the stand-in module for the one manifest the ladder has. The
dependency set of this task is a single pinned union, seeded into `/app` at
step 01 and never widened by a transition:

* Only `steps/01-opc-package-and-writer/workdir/` ships a dependency manifest
  (`go.mod`, `go.sum`, `vendor/`). Steps 02–10 carry documents only — they
  continue in the same `/app`, and their workdirs hold no manifest to read.
* All ten steps' verifier pins are byte-identical:
  `md5sum steps/*/tests/assets/pin/{go.mod,go.sum,vendor.tar.gz}` gives one
  hash per file across the ten directories
  (`go.mod db44f787…`, `go.sum dd7f80df…`, `vendor.tar.gz 9f771aed…`).
* Every step's `solution/solve.sh` asserts the same invariant in prose:
  "the dependency set is one pinned union for the whole ladder, seeded at step
  01 and never changed by a transition".
* Every step's `instruction.md` tells the agent the set is frozen: "Do not edit
  `go.mod` or `go.sum`, do not run `go get` or `go mod tidy`, and do not add a
  vendored package".

So a stand-in per step would be ten copies of the same two files. If a later
milestone ever does widen the set, add `NN/` here for the step that widens it
and the Dockerfile loop picks it up without change.

## What the stand-in is

`01/go.mod` and `01/go.sum` are copies of the manifests seeded into `/app`
(`md5 go.mod db44f78748054be3a291cf9a9ce931a0`, `md5 go.sum
dd7f80df3bdcf1a301ae69613b738a6d`); `01/doc.go` is a stub whose only job is to
make the directory a loadable module. Dependencies only, no path members.

Regenerate with:

    cp steps/01-*/workdir/go.mod steps/01-*/workdir/go.sum environment/assets/prefetch/01/

## What the cache ends up holding

The nine modules of the pinned manifest — `github.com/mohae/deepcopy`,
`github.com/richardlehane/mscfb`, `github.com/richardlehane/msoleps`,
`github.com/tiendc/go-deepcopy`, `github.com/xuri/efp`, `github.com/xuri/nfp`,
`golang.org/x/crypto`, `golang.org/x/net`, `golang.org/x/text` — plus the nine
modules `go.sum` pins for the wider module graph (`go-spew`, `go-difflib`,
`testify`, `x/mod`, `x/sync`, `x/sys`, `x/term`, `x/tools`, `yaml.v3`: eighteen
zip entries in all), which is what `go mod download all` resolves for this
manifest.

Those are the same bytes the seeded `/app/vendor/` tree already carries, so the
cache adds no capability the workspace did not have — it only removes the
offline failure. The one module that must never appear here is
`github.com/xuri/excelize/v2` (and its `qax-os` mirror): that is the upstream
implementation this task asks the agent to write, and it is not in the manifest,
so no fetch rooted in these files can reach it. See the report's honesty
section.

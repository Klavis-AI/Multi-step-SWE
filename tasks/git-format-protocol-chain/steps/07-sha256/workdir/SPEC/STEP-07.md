# Milestone 7 — the SHA-256 object format, and interoperating with SHA-1

`/app` holds a git implementation in PHP that does everything through the
reftable backend and the acceleration structures, over **SHA-1** repositories.
This milestone adds the **SHA-256 object format** (`git init
--object-format=sha256`) and the pieces that let SHA-256 and SHA-1 coexist.

Everything in `SPEC/STEP-01.md` … `SPEC/STEP-06.md` still holds for SHA-1
repositories; nothing about SHA-1 behaviour may change.

## 1. The SHA-256 object format (`technical/hash-function-transition.adoc`)

* `git init --object-format=sha256` sets `extensions.objectFormat = sha256` and
  `core.repositoryformatversion = 1`. Object names are 32-byte SHA-256, hex 64
  chars, everywhere: loose objects, the packfile and its index — git keeps
  **pack version 2** and **`.idx` version 2** for SHA-256, so what widens is the
  object names and the trailers, not the format version — the index (`DIRC`
  with its sha256 trailer), and the **32-byte trailer** on every file that
  carries one.
* Every command that produced or consumed a 40-hex name now does 64-hex in a
  sha256 repository. The ones this milestone is about are `hash-object`,
  `cat-file`, `rev-parse`, `commit`, `log`, `ls-tree`, `fsck`, `show-ref`,
  `for-each-ref`, and the pack path (`repack`, `verify-pack`).
* `git rev-parse --show-object-format` prints `sha256` in a sha256 repository
  and `sha1` in a sha1 one.
* A sha256 repository is identified by `extensions.objectFormat = sha256` at
  `core.repositoryformatversion = 1`. Your implementation must **accept**
  `extensions.compatObjectFormat` as a known extension key as well, rather than
  refusing to open a repository that names it: git's own suite sets both keys
  together when it exercises object naming, so rejecting the second makes every
  command in that repository fail. Accepting the key is required here;
  implementing the mapping it refers to is not (§2).

## 2. The transition mapping

* Git's `hash-function-transition` design describes tables mapping each sha256
  object name to its sha1 name, but the pinned git does **not** ship them: there
  is no `.idx` version 3 and no `objects/loose-object-idx`. A sha256 repository
  here is self-contained — it stores sha256 names only — and no sha1<->sha256
  mapping is required.
* The pack protocol negotiates `object-format`; a clone/fetch between two sha256
  peers uses sha256 throughout. (That negotiation is not a separate requirement
  here: the clone this milestone requires is one over git's local transport, so
  it depends on the repository format rather than the wire.) This milestone does
  **not** require live sha1↔sha256 translation on the wire (that is git's still-experimental compat
  mode) — only that a sha256 repository is self-consistent and fsck-clean.

## 3. Interoperability

Against the peers that implement sha256 (`/opt/gitpeer/v2.39.5`,
`/opt/gitpeer/v2.51.0`; v2.20.1 is SHA-1 only):

* a sha256 repository your git creates is read, cloned and `fsck --strict`-ed
  by the peers, with identical `log`, `cat-file`, `rev-parse` and `show-ref`
  output;
* a sha256 repository a peer creates is read by your git unchanged;
* SHA-1 repositories continue to interoperate exactly as in the earlier
  milestones — the two formats do not interfere.

## 4. Checking your work

git's own suite at `/opt/git-suite` under
`GIT_TEST_INSTALLED=/app/out/bin TEST_NO_MALLOC_CHECK=1`, and the resource
ceilings of `SPEC/STEP-02.md`
§5, as before. Beyond the suite, check the object-format behaviour directly: run
the commands of §1 in sha256 repositories and compare their output with real
git's, and open sha256 repositories that real git built, which your git has to
read.

## 5. What this milestone does *not* include

* live SHA-1↔SHA-256 interop on a single wire connection: git's
  `compatObjectFormat` mapping is experimental and computing it is not required.
  Note this is about the MAPPING — accepting the configuration key is required
  by §1.
* the general `core.repositoryformatversion` gate is out of scope for this
  milestone.

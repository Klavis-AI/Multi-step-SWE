# Milestone 2 — packfiles v2, `gc`/`repack`, and the v0/v1 wire protocol

The repository in `/app` already implements git's object model with loose
objects, the files ref backend, index v2 and the porcelain that drives them.
This milestone adds the two things that turn that into a git a network can use:
**packfiles** (pack version 2 with delta compression, and the `.idx` version 2 that
indexes them) and the **pack protocol** over `file://`, `git://` and smart HTTP
(git 1.6.6), on both sides — your git as the client, and your git as the server.

Everything below is defined by documents that are in this container. Nothing
here invents an interface.

## 1. The artifact (unchanged)

* Running `make` with `/app` as the working directory produces the executable
  **`/app/out/bin/git`**, and `git --exec-path` prints the directory holding the
  `git-<command>` programs.
* The implementation is **PHP**. Every file that participates in the build or is
  loaded at run time is PHP source, a `Makefile`, a shell wrapper or data; no
  compiled binaries, no implementation sources in another language, and
  `/app/out/bin/git` itself is a script that runs PHP.
* Your implementation does the work itself and never executes another git.
* Keep `/app` text-only: it must survive being copied as a plain tar over a text
  channel. Experiment on repositories in `/tmp`, not inside `/app`. `make` must
  rebuild `/app/out` from scratch.
* Everything the previous milestone required still has to work: the object
  model and byte-exact object ids, the files ref backend with `packed-refs` and
  reflogs, index v2, and the porcelain (`add`, `commit`, `status`, `diff`,
  `log`, `show`, `branch`, `tag`, `checkout`, `switch`, `restore`, `reset`,
  `merge`, `rm`, `mv`, `fsck`, `check-ignore`, `config`, and the plumbing
  around them).

## 2. Packfiles (gitformat-pack(5))

* `git gc`, `git repack` and `git pack-objects` write **pack version 2** files
  and **`.idx` version 2** indexes under `objects/pack/`, named
  `pack-<pack checksum>.pack` / `.idx`, together with the `.rev` reverse index
  current `git gc` writes alongside them. Everything in this milestone is
  "as the pinned `/opt/gitpeer/v2.51.0` peer implements it", not as some older
  release did.
* The pack carries `OBJ_COMMIT`/`OBJ_TREE`/`OBJ_BLOB`/`OBJ_TAG` entries and both
  delta encodings — `OBJ_OFS_DELTA` and `OBJ_REF_DELTA` — with the copy/insert
  instruction stream `gitformat-pack(5)` specifies. You must **read** both, and
  the packs you **write** must be readable by real git.
* `git index-pack` (including `--stdin`, `-o`, `--verify` and `--fix-thin`),
  `git unpack-objects`, `git verify-pack` (`-v`, `-s`), `git show-index` and
  `git count-objects -v` behave as their manual pages describe.
* After `git gc`/`git repack -a -d` the loose objects that are now packed are
  gone and every earlier behaviour still works unchanged: `log`, `cat-file`,
  `status`, `fsck`, `rev-list --objects`.
* `git repack -a` packs every object the database holds, reachable or not;
  `git gc` prunes unreachable *loose* objects only after `gc.pruneExpire`
  (default two weeks), while `git prune` prunes them now. `git gc` and
  `git repack` also write **cruft packs** (`--cruft`, `--cruft-expiration`,
  `--max-cruft-size`, `--expire-to`) as their manual pages describe.
* A pack whose bytes are damaged must be detected: `git fsck` and
  `git index-pack --verify` report a problem and exit non-zero.
* The `.idx` records the checksum of the packfile it describes (it is the
  second-to-last object-name-sized field of the index). **A pack whose trailing
  checksum does not match the one its index records must not be used**: report
  an error on stderr and treat the objects it claims to hold as absent, rather
  than reading offsets that no longer mean anything. A truncated or rewritten
  pack is exactly this case.
* Consequently a reference can point at an object the database can no longer
  produce. `git show-ref` lists references "along with the associated commit
  IDs" (git-show-ref(1)): a reference whose object is not present is a fatal
  error on stderr and a non-zero exit, not a line of output.

## 3. The pack protocol (gitprotocol-pack(5), gitprotocol-common(5), gitprotocol-capabilities(5))

* `git upload-pack` and `git receive-pack` speak the v0/v1 protocol on stdin and
  stdout, including `--advertise-refs` and `--stateless-rpc`. The reference
  advertisement is pkt-line framed, carries the capability list after a NUL on
  the first line, advertises `HEAD` (upload-pack only) and peeled tags
  (`<ref>^{}`, upload-pack only), and ends with a flush packet.
* The capability sets are the ones git advertises: for upload-pack
  `multi_ack thin-pack side-band side-band-64k ofs-delta shallow deepen-since
  deepen-not deepen-relative no-progress include-tag multi_ack_detailed no-done
  symref=HEAD:<ref> object-format=<algo> agent=<anything>`, with `filter`
  inserted just before `object-format` when `uploadpack.allowFilter` is set (the
  same switch that adds it to protocol v2's `fetch` capability), and for
  receive-pack
  `report-status report-status-v2 delete-refs side-band-64k quiet atomic
  ofs-delta object-format=<algo> agent=<anything>`.
* Negotiation: `want`/`have` lines (the client's capabilities follow the first
  `want` after a space), `ACK`/`NAK` with `multi_ack_detailed`, `done`, and the
  packfile delivered inside side-band 1 when the client asked for side-band.
* Push: the client sends `<old> <new> <ref>` command lines with its capabilities
  after a NUL on the first one, then the packfile; the server replies with the
  `report-status` result (`unpack ok`, then `ok <ref>` or `ng <ref> <reason>`).
* `git clone`, `git fetch`, `git push` and `git ls-remote` drive all of that as
  clients, over `file://` and plain paths, `git://` and `http://`, and honour
  `-c protocol.version=0` and `-c protocol.version=1`. The plumbing underneath
  them is required too: **`git fetch-pack`** and **`git send-pack`**, with the
  options their manual pages document.
* `git remote` manages the remotes those commands use: `add`, `rename`,
  `remove`, `set-url`, `get-url`, `set-branches`, `show`, `prune`, `update`,
  `-v`, and the `remote.<name>.*` configuration they read and write.
* `git push` also honours `--force-with-lease[=<ref>[:<expect>]]`, `--atomic`,
  `--delete`, `--tags`, `--mirror`, `--dry-run` and `--prune`.

* `git daemon` serves `git://` (`--listen`, `--port`, `--base-path`,
  `--export-all`, `--reuseaddr`, `--detach`, `--pid-file`, `--enable=`), and
  `git http-backend` is a CGI program that serves smart HTTP exactly as
  `git-http-backend(1)` documents: `GET $URL/info/refs?service=git-upload-pack`
  returns `application/x-git-upload-pack-advertisement` beginning with a
  `# service=…` pkt-line and a flush, and `POST $URL/git-upload-pack` (and
  `git-receive-pack`) return the corresponding `…-result` content type.
* Clone bookkeeping is git's: `remote.<name>.url` records `<cwd>/<url as given>`
  for a relative local path, a non-bare clone writes
  `+refs/heads/*:refs/remotes/<name>/*` and remote-tracking refs, a bare clone
  writes `+refs/heads/*:refs/heads/*` and no remote-tracking refs, and the
  checked-out branch gets `branch.<name>.remote`/`branch.<name>.merge`.
* `git push` reports each ref the way git does: ` * [new branch]`,
  ` * [new tag]`, `   <old>..<new>` for a fast-forward, ` + <old>...<new>` with
  `(forced update)`, ` - [deleted]`, and refuses a non-fast-forward without
  `--force`.

### 3.1 What this milestone does *not* include

The capability list above is advertised verbatim because real git compares it,
but honouring every capability it names is not part of this milestone. None of
the following is required, so do not spend your time budget on it:

* **shallow history** — honouring `deepen`, `deepen-since`, `deepen-not` or a
  client's `shallow` lines, `git clone --depth`, `git fetch --depth`,
  `--unshallow`, `--shallow-since`, and the `shallow` file;
* **protocol version 2** — `ls-refs`, the v2 `fetch` command, `GIT_PROTOCOL`
  negotiation to version 2; this milestone is v0/v1 only, and `protocol.version`
  above 1 may be refused;
* **object filters and partial clone** (`--filter=`, `promisor`,
  `partialclone`), and **bundle URIs**;
* push negotiation (`push.negotiate`) and `url.<base>.insteadOf` rewriting.

## 4. Interoperating with real git

Three real git builds are installed at `/opt/gitpeer/v2.20.1`,
`/opt/gitpeer/v2.39.5` and `/opt/gitpeer/v2.51.0` (v2.39.5 is also `git` on
`PATH`). They are the
reference in both directions, and they are *clients and servers* here:

* real git must `clone`, `fetch` and `push` against your repositories and your
  servers — over a local path, over `git://` against your `git daemon`, and over
  `http://` against your `git http-backend` — and `git fsck --strict` must be
  clean on what it gets;
* your git must `clone`, `fetch` and `push` against repositories real git built,
  including deltified packs written with `--window=50 --depth=50`, and against
  real git's own `git daemon` and `git http-backend`.

## 5. Checking your work
**Repository extensions you must tolerate.** git's own `t/` suite is
unmodified, and `t1006-cat-file.sh` sets up its main repository with three config
writes together -- `core.repositoryformatversion 1`, `extensions.objectFormat`
and `extensions.compatObjectFormat`. Opening such a repository must succeed:
accept both keys as known extensions, AND accept format version 1 on a repository
that carries them. (Repositories *you* create still default to version 0; this is
about not refusing one git's own suite sets up.) You are NOT asked to implement
the SHA-256 object format or the compat mapping here: a repository whose
`extensions.objectFormat` is actually `sha256` is out of scope for this
milestone, and refusing that one is fine. What must not be refused is the version-1 repository carrying
these keys that the suite sets up around SHA-1 objects. Nearly every assertion in
that file runs inside that repository, so refusing it fails all of them at
once.

* git's own test suite is at `/opt/git-suite`; run a file against your binary
  the way `t/README` documents:

      cd /opt/git-suite/t && \
      GIT_TEST_INSTALLED=/app/out/bin TEST_NO_MALLOC_CHECK=1 \
      TEST_OUTPUT_DIRECTORY=/tmp/t ./t5300-pack-object.sh --no-chain-lint

  `TEST_NO_MALLOC_CHECK=1` is required (test-lib otherwise sets
  `LD_PRELOAD=libc_malloc_debug.so.0`, which aborts the PHP binary).
* Commands must behave as git's do under a frozen identity and clock and the
  same environment as before: `GIT_AUTHOR_NAME`, `GIT_AUTHOR_EMAIL`,
  `GIT_COMMITTER_NAME`, `GIT_COMMITTER_EMAIL`, `GIT_AUTHOR_DATE`,
  `GIT_COMMITTER_DATE`, `TZ=UTC`, `LC_ALL=C`, `LANG=C`, `GIT_CONFIG_NOSYSTEM=1`,
  `GIT_ATTR_NOSYSTEM=1`, `GIT_TERMINAL_PROMPT=0`, `PAGER=cat`, `GIT_PAGER=cat`,
  `HOME`/`XDG_CONFIG_HOME` in a scratch directory, `GIT_CEILING_DIRECTORIES`,
  and where relevant `GIT_DIR`, `GIT_WORK_TREE`, `GIT_INDEX_FILE`,
  `GIT_OBJECT_DIRECTORY` and `GIT_PROTOCOL`.
* Standard output, standard error and exit status must all match git's.
* Resource ceilings: a single command must work within 4 GiB of address space
  (`ulimit -v`), a 300 s wall clock and a 600 s CPU limit (`ulimit -t`); a `t/`
  file, which runs without those per-command limits, must finish within a 900 s
  wall clock. Everything runs on 7 CPUs, so
  size any parallelism to that rather than to what `nproc` reports.
* Apache is installed; your `git http-backend` must serve smart HTTP under
  `git-http-backend(1)`'s own example configuration.
* `/tests` and `/logs` are not part of your workspace, and no code in `/app`
  may name them.

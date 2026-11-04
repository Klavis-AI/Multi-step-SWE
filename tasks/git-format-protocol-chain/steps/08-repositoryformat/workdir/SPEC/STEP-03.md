# Milestone 3 — wire protocol version 2, partial clone, and bundles

`/app` holds a git implementation in PHP that already has the object database,
the files ref backend, index v2, the porcelain, packfiles v2 with delta
compression, and the **version 0/1** pack protocol as client and server over
local paths, `file://`, `git://` and smart HTTP.

This milestone adds **protocol version 2** (`gitprotocol-v2(5)`), the object
**filters** and **partial clone** that v2's `fetch` command carries, and
**bundles** — including the `bundle-uri` command that v2 defines.

Everything in `SPEC/STEP-01.md` and `SPEC/STEP-02.md` still holds, unchanged:
the loose object store, the index, the porcelain, packfiles, `gc`,
and the v0/v1 protocol keep working exactly as they did. Protocol v2 is
*negotiated*, never assumed — a peer that does not ask for it must still get the
v0 advertisement it expects, byte for byte.

## 1. Version negotiation

* The client announces its willingness with `GIT_PROTOCOL=version=2` in the
  environment for local and ssh transports, `\0version=2\0` in the `git://`
  request line, and the `Git-Protocol: version=2` HTTP header. `protocol.version`
  in the configuration (and `GIT_PROTOCOL` in the environment) select it;
  `-c protocol.version=0` and `-c protocol.version=1` must still produce exactly
  the v0/v1 exchange of the previous milestone.
* A v2 server answers with `version 2` as the first pkt-line, then its
  capability advertisement, then a flush. A server that is asked for v2 by a
  client it cannot serve at v2 answers v0, and the client falls back.
* The capabilities a v2 `upload-pack` advertises are
  `agent=<anything>`, `ls-refs=unborn`, `fetch=shallow wait-for-done`,
  `server-option` and `object-format=sha1`. `fetch` gains a trailing ` filter`
  only when `uploadpack.allowFilter` is set, and `bundle-uri` appears only when
  `uploadpack.advertiseBundleURIs` is set -- the pinned git's
  bundle_uri_advertise() defaults to 0 and consults that key alone
  (bundle-uri.c:909-918). The bundle locations under `bundle.*` are what the
  bundle-uri COMMAND reports; they do not trigger the advertisement. `receive-pack` has no v2 form and keeps its v0 advertisement.

## 2. `ls-refs`

* Arguments: `peel`, `symrefs`, `unborn`, and any number of
  `ref-prefix <prefix>` lines. The response is one pkt-line per ref,
  `<oid> <refname>`, with ` peeled:<oid>` appended under `peel` and
  ` symref-target:<target>` under `symrefs`, terminated by a flush.
* Refs are listed in the same order `for-each-ref` gives, `HEAD` first when it
  matches a prefix. `unborn` reports `unborn HEAD symref-target:<ref>` for a
  repository whose `HEAD` does not resolve.
* `git ls-remote` and the ref discovery inside `clone`, `fetch` and `push` use
  `ls-refs` when the connection is v2, and must send the `ref-prefix` lines git
  sends, so that a server may legitimately return fewer refs.

## 3. `fetch`

* Arguments: `want <oid>`, `have <oid>`, `done`, `thin-pack`, `no-progress`,
  `include-tag`, `ofs-delta`, `deepen`/`deepen-relative`/`deepen-since`/
  `deepen-not` (accepted, see §6), `filter <spec>`, `want-ref <ref>`,
  `sideband-all`, `packfile-uris <protocols>` and `wait-for-done`.
* The response is a sequence of named sections, each introduced by a pkt-line
  holding its name: `acknowledgments` (`NAK`, or `ACK <oid>` lines followed by
  `ready`), `shallow-info`, `wanted-refs`, `packfile-uris`, and `packfile`,
  which carries the pack in side-band 1. Sections appear in that order and only
  when they have content; `0001` delimits them.
* Multi-round negotiation: the client sends `have` lines in batches and stops
  when the server says `ready`; with `done` the server sends the pack
  immediately.

## 4. Filters and partial clone

* `--filter=blob:none`, `--filter=blob:limit=<n>` (with `k`/`m`/`g` suffixes),
  `--filter=tree:<depth>` and `--filter=object:type=<type>`, on `clone`,
  `fetch`, `rev-list` and `pack-objects`. `rev-list --filter=… --missing=allow-any`
  and `--filter-print-omitted` report what a filter leaves out.
* A partial clone records `remote.<name>.promisor = true` and
  `remote.<name>.partialclonefilter = <spec>`, and sets
  `core.repositoryformatversion = 1`. It does **not** write
  `extensions.partialClone`: git documented that key historically, but 2.51.0
  records the promisor on the remote only. Packs fetched from a promisor
  remote get a `.promisor` file next to the `.pack`.
* An object that is missing because a filter omitted it is fetched **on demand**
  from the promisor remote when something needs it, and `git rev-list --objects
  --missing=print` names what is still absent. `git fsck` does not report a
  missing object that a promisor pack accounts for.
* `git cat-file --batch-all-objects`, `git log`, `git checkout` and `git blame`
  keep working in a partial clone, fetching what they need.

## 5. Bundles

* `git bundle create`, `verify`, `list-heads` and `unbundle`, in the **v2** and
  **v3** bundle formats (`gitformat-bundle(5)`): the `# v2 git bundle` /
  `# v3 git bundle` signature, `@<key>=<value>` capability lines in v3,
  `-<oid> <comment>` prerequisite lines, `<oid> <ref>` ref lines, a blank line,
  then a packfile.
* `git clone <file>.bundle` and `git fetch <file>.bundle` work, and a bundle
  whose prerequisites are missing is refused by `verify` with a non-zero exit.
* `bundle-uri`: setting `uploadpack.advertiseBundleURIs` makes a v2
  `upload-pack` advertise the `bundle-uri` capability. The bundle locations
  themselves live under `bundle.*` (`bundle.version`, `bundle.mode`,
  `bundle.<id>.uri`, `bundle.<id>.creationToken`) and are what the command
  reports once a client asks, and `transfer.bundleURI` is client configuration that
  reads back. Only that advertisement and this configuration surface are required
  — answering a `command=bundle-uri` request on the wire is not.

## 6. What this milestone does *not* include

* honouring shallow requests (`deepen*`, `--depth`, `--unshallow`): the
  arguments and capabilities are advertised and parsed, and a `deepen` request
  is answered with an error, not a truncated history;
* index versions above 2, sparse checkout, commit-graph, multi-pack-index,
  bitmaps, reftable and SHA-256 — out of scope for this milestone;
* `packfile-uris` fetching an actual URI (the section is advertised and an empty
  section is correct).

## 7. Interoperability

The pinned real gits at `/opt/gitpeer/{v2.20.1,v2.39.5,v2.51.0}` are the
reference. v2.20.1 predates protocol v2's default and must keep working at v0;
v2.39.5 and v2.51.0 default to v2. Both directions are required:

* real git clones, fetches and pushes against your repositories and servers at
  `-c protocol.version=2` and at `-c protocol.version=0`, and `git fsck --strict`
  is clean on what it gets;
* your git clones and fetches from real git's `upload-pack` at v2, including
  `--filter=blob:none` partial clones, and can then read the result;
* bundles your git writes are read by real git and the reverse.

## 8. Checking your work
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

Identical to the previous milestone: git's own suite at `/opt/git-suite` run
with `GIT_TEST_INSTALLED=/app/out/bin TEST_NO_MALLOC_CHECK=1`, output (stdout,
stderr and exit status) that matches git's under a frozen identity and clock,
and the resource ceilings of `SPEC/STEP-02.md` §5. `t5702-protocol-v2.sh`, `t5703-upload-pack-ref-in-want.sh`,
`t5616-partial-clone.sh`, `t5607-clone-bundle.sh` and `t6020-bundle-misc.sh` are
the files this milestone is about.

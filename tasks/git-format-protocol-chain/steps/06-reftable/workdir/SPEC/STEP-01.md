# Milestone 1 — object model, loose objects, refs, index v2, porcelain core

You are writing **git** — the program, not a wrapper around one. This milestone
covers the repository format git shipped with in 1.5-era releases and still uses
today at `core.repositoryformatversion = 0`: SHA-1 object names, zlib-compressed
loose objects, the files ref backend with `packed-refs` and reflogs, index
version 2, and the porcelain that drives them.

Everything below is defined by documents that are in this container. Nothing in
this file invents an interface: the command line is git's, the file formats are
git's, and the words used here are the words those documents use.

## 1. The artifact

* Running `make` with `/app` as the working directory must produce an executable
  at **`/app/out/bin/git`**.
* That executable must work when it is invoked as `git` through a directory
  placed on `PATH`, whether the entry named `git` there is the file itself or a
  **symlink** to `/app/out/bin/git`. So resolve your own location
  before you use it: a wrapper that derives its installation directory from `$0`
  without following symlinks will look for its sources next to the symlink and
  fail with every command. Dereference `$0` first —

      self=$0
      while [ -L "$self" ]; do self=$(readlink "$self"); done
      root=$(CDPATH= cd -- "$(dirname -- "$self")/../.." && pwd)

  — or resolve the path by any equivalent means. This is stated because nothing
  else in this specification implies it, and getting it wrong breaks every
  command that runs your `git` from `PATH` rather than by its full path.
* The implementation is **PHP** (PHP 8.2 is installed). Every file in the
  repository that takes part in the build or is loaded at run time must be PHP
  source, a `Makefile`, a shell wrapper, or data. The repository must not contain
  compiled objects or binaries, and must not contain implementation sources in
  another language. `/app/out/bin/git` itself must be a script that runs PHP,
  not a compiled program.
* Your implementation must do the work itself: it must never execute another
  git.
* Keep `/app` text-only. It must survive being copied as a plain tar over a
  text channel, and binary files left under `/app` — a `.git` directory,
  packfiles, loose objects — may be lost in transit. Experiment on
  repositories in `/tmp`, not inside `/app`. `make` must regenerate the build
  output under `/app/out` from scratch, so it never needs to be
  preserved.
* `/app/out/bin/git` must behave as `git(1)` describes:
  * `git` with no arguments prints a usage message and exits **1**;
  * `git --exec-path` prints a directory;
  * `git version` prints `git version <version>`;
  * `git version --build-options` additionally prints a line
    `default-hash: sha1`;
  * `git <command> -h` prints a usage line and exits **129**;
  * the global options `-C <path>`, `-c <name>=<value>`, `--git-dir=<path>`,
    `--work-tree=<path>`, `--bare`, `--no-pager`, `--literal-pathspecs` and
    `--config-env=<name>=<envvar>` work as documented.

## 2. Repository format for this milestone

* Object names are **SHA-1**; `git rev-parse --show-object-format` prints `sha1`.
* Objects are stored as **loose objects**: `objects/ab/cdef…`, zlib-deflated
  `"<type> <size>\0<content>"`, exactly as `gitrepository-layout(5)` and
  `gitformat-pack(5)`'s "Object storage" description say. Object ids are the
  SHA-1 of that uncompressed representation, so a correct implementation
  produces **the same object ids as git** for the same inputs.
* Trees store entries as `<mode> <name>\0<20-byte oid>`, sorted the way git
  sorts them (a directory sorts as if its name ended in `/`), with modes
  `100644`, `100755`, `120000`, `160000` and `40000`.
* Refs live in `refs/…` as loose files, in `packed-refs`, and `HEAD` may be a
  symbolic ref. Reflogs are `logs/<ref>` with git's line format.
* The index is **version 2** (`gitformat-index(5)`): the `DIRC` header, cache
  entries with the documented stat fields, the optional `TREE` extension, and
  the trailing checksum.
* `git init` creates the layout `gitrepository-layout(5)` describes, including
  `hooks/`, `info/exclude`, `description`, `objects/{info,pack}`,
  `refs/{heads,tags}` and `HEAD` pointing at the initial branch
  (`init.defaultBranch`, else `master`).

## 3. Command surface of this milestone

Implement these commands with the semantics of their manual pages
(`/opt/git-suite/Documentation/git-<command>.adoc`). Only the behaviour those
pages document is required — including exit codes, the wording of the messages
those pages quote, and which stream each message goes to.

    init          hash-object   cat-file      ls-tree       mktree
    mktag         commit-tree   ls-files      update-index  write-tree
    read-tree
    checkout-index  update-ref  symbolic-ref  show-ref      for-each-ref
    check-ref-format  pack-refs  reflog       rev-parse     rev-list
    merge-base    add           rm            mv            status
    commit        diff          diff-index    diff-files    diff-tree
    log           show          branch        tag           checkout
    switch        restore       reset         merge         fsck
    count-objects check-ignore  stripspace    config        var
    version

## 4. Required behaviour

* **Object model.** `hash-object`, `cat-file` (including `--batch`,
  `--batch-check` with `%(objectname)`, `%(objecttype)`, `%(objectsize)` and
  `%(rest)`, and `--batch-all-objects`), `mktree`, `mktag`, `commit-tree`,
  `ls-tree` and `write-tree` produce git's bytes and git's output formats.
  `hash-object` refuses malformed objects (and accepts them under
  `--literally`) the way `git-hash-object(1)` describes; `mktag` applies the
  tag checks `git-mktag(1)` describes.
* **Index.** `update-index` (`--add`, `--remove`, `--force-remove`,
  `--cacheinfo`, `--index-info`, `--chmod`, `--refresh`,
  `--assume-unchanged`), `ls-files` (`-s`, `-t`, `-u`, `-o`, `-i`,
  `--exclude-standard`, `--full-name`, `-z`, `--error-unmatch`), `read-tree`
  (plain, `-m` for one, two and three trees, `--reset`, `-u`, `--prefix=`,
  `--empty`) and `checkout-index`.
* **Refs.** `update-ref` (including `--stdin`, `-d`, old-value verification and
  `--no-deref`), `symbolic-ref`, `show-ref`, `for-each-ref` with `--format`,
  `check-ref-format`, `pack-refs --all`, reflog entries written by ref updates,
  and `<ref>@{n}` reflog lookups.
* **Working tree.** `add` (including `-A`, `-u`, `-N`, `--dry-run`), `rm`
  (`--cached`, `-r`, `-f`), `mv`, `status` (long, `--porcelain`, `-s`, `-b`,
  `-z`, `--untracked-files=`, `--ignored`), `commit` (`-m`, `-F`, `-a`,
  `--amend`, `--allow-empty`, `--author=`, `--date=`), `checkout`/`switch`/
  `restore`, and `reset` (`--soft`, `--mixed`, `--hard`, paths).
* **History.** `log` (default, `--oneline`, `--pretty=`, `--format=` with the
  placeholders `%H %T %P %an %ae %ad %cn %ce %cd %s %b %B`, `-n`, `--reverse`,
  `--grep`, `--author`), `show`, `rev-list` (`--all`, `--objects`, `--count`,
  `--max-count`, `--reverse`, `--topo-order`, `--no-walk`, ranges and `^`
  exclusions), `rev-parse` (the `gitrevisions(7)` syntax: `^`, `~`, `^{}`,
  `^{type}`, `:path`, `@{n}`, abbreviations, `--verify`, `--short`,
  `--abbrev-ref`, `--symbolic-full-name`, and the repository queries
  `--git-dir`, `--show-toplevel`, `--show-prefix`, `--show-cdup`,
  `--is-bare-repository`, `--is-inside-work-tree`, `--show-object-format`),
  `merge-base` (including `--is-ancestor`, `--all`, `--independent`).
* **Diff.** `diff`, `diff-index`, `diff-files`, `diff-tree`: the raw format, the
  patch format (`diff --git`, `index`, `---`/`+++`, `@@` hunks, `new file
  mode`, `deleted file mode`, `similarity index`/`rename from`/`rename to`,
  `Binary files … differ`, `\ No newline at end of file`), `--stat`,
  `--numstat`, `--shortstat`, `--name-only`, `--name-status`, `-U<n>`,
  `--cached`, and rename detection with `-M`.
* **Merge.** `merge` fast-forward and three-way, `<<<<<<< HEAD` /
  `=======` / `>>>>>>> <other>` conflict markers, unmerged index stages 1/2/3,
  `MERGE_HEAD`, and committing the result.
* **Ignores and attributes.** `gitignore(5)` pattern semantics (negation,
  directory-only, anchoring, `**`), `check-ignore` including `-v` and `--stdin`,
  `.gitattributes` `text`/`eol` and `core.autocrlf` conversion on the way into
  the object database.
* **Config.** `git config` reading and writing `.git/config` in the format
  `git-config(1)` documents, including subsections, multi-valued keys,
  `--get`, `--get-all`, `--get-regexp`, `--add`, `--unset`, `--replace-all`,
  `--list`, `--type=`/`--bool`/`--int`, `--file`, `--global`, and preserving the
  rest of the file when it edits one line.
* **Integrity.** `git fsck` reports dangling objects as
  `dangling <type> <oid>`, reports missing and corrupt objects, and uses the
  exit status `git-fsck(1)` documents; on a damaged repository git's outcome
  must be matched (a non-zero exit and which objects are still readable), but
  its wording need not be. `git count-objects` reports the number of loose
  objects and the disk space they consume, in KiB, and `-v` adds the
  `count`/`size`/`in-pack`/`packs`/`size-pack` breakdown.
* **Object enumeration.** `git rev-list --objects` prints `<oid>` for commits and
  `<oid> <path>` for the trees and blobs reachable from them, and names a tag
  object by its tag ref.

## 5. Interoperating with real git

Three real git builds are installed at `/opt/gitpeer/v2.20.1`,
`/opt/gitpeer/v2.39.5` and `/opt/gitpeer/v2.51.0`; the v2.39.5 build is also
`git` on `PATH`, for comparing behaviour while you work.
They are the reference for this task in both directions:

* Every repository your implementation writes must be a repository those gits
  accept: `git fsck --strict` clean, `git log`/`git ls-tree`/`git cat-file`
  agreeing with yours, `git clone` producing an identical history, and
  `git status` clean in a fresh checkout.
* Every repository those gits write must be one your implementation reads
  without conversion, including `packed-refs`, annotated tags, merge commits,
  symlinks, executable files and unusual path names.

## 6. Checking your work
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

* git's own test suite is installed, unmodified, at `/opt/git-suite`. Run a
  file against your binary the way `t/README` documents:

      cd /opt/git-suite/t && \
      GIT_TEST_INSTALLED=/app/out/bin TEST_NO_MALLOC_CHECK=1 \
      TEST_OUTPUT_DIRECTORY=/tmp/t ./t1007-hash-object.sh --no-chain-lint

  `TEST_NO_MALLOC_CHECK=1` is required because `test-lib.sh` otherwise sets
  `LD_PRELOAD=libc_malloc_debug.so.0` and `MALLOC_CHECK_=3`, which abort the PHP
  binary.
* Commands must behave as git's do when run with a frozen identity and clock,
  and with the environment git's own documentation defines: `GIT_AUTHOR_NAME`,
  `GIT_AUTHOR_EMAIL`, `GIT_COMMITTER_NAME`, `GIT_COMMITTER_EMAIL`,
  `GIT_AUTHOR_DATE`, `GIT_COMMITTER_DATE`, `TZ=UTC`, `LC_ALL=C`, `LANG=C`,
  `GIT_CONFIG_NOSYSTEM=1`, `GIT_ATTR_NOSYSTEM=1`, `GIT_TERMINAL_PROMPT=0`,
  `PAGER=cat`, `GIT_PAGER=cat`, `HOME` and `XDG_CONFIG_HOME` inside a scratch
  directory, and `GIT_CEILING_DIRECTORIES`. Your implementation must honour all
  of them, and `GIT_DIR`, `GIT_WORK_TREE`, `GIT_INDEX_FILE` and
  `GIT_OBJECT_DIRECTORY`.
* Standard output, standard error and the exit status of every command must
  match git's, so messages must go to the stream `git-<command>(1)`
  documents. The behaviour to match is that of the pinned reference build,
  `/opt/gitpeer/v2.51.0`, not the v2.39.5 `git` on `PATH`.
* Resource ceilings: a single command must work within a 4 GiB address-space
  limit (`ulimit -v`), a 300 s wall clock and a 600 s CPU limit (`ulimit -t`);
  a `t/` file, which runs without those per-command limits, must finish within a
  900 s wall clock.
  Everything runs on 7 CPUs, so size any parallelism you add to
  that, not to what `nproc` reports.
  Your container has **12 GiB of memory** (12288 MiB).
  That is what is guaranteed to you; whether you may briefly exceed it depends on
  headroom the operator may or may not provide, and when the ceiling is reached the
  container is killed OUTRIGHT -- no warning, and everything not already written
  to disk is lost with it, including this session. A `git` that allocates without
  bound on a large repository will take down the whole container rather than fail
  one command, so stream where you can and cap what you buffer. Stated for the
  same reason as the CPU count above: it is a fact about the machine, not a hint
  about the task.
* `/tests` and `/logs` are not part of your workspace, and no code in `/app`
  may name them.

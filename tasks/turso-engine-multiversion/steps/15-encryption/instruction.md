# Step 15 — Opt-in page-level encryption in the pager and the WAL

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R15.1** — Encryption is opt-in: with no cipher selected, and with a cipher selected but no key supplied, a given write workload produces the same database file it produced before encryption existed. Two independent runs of one workload write byte-identical files, and declaring a cipher without a key changes not one byte of them.
- **R15.2** — With encryption off, stock `sqlite3` opens the file and returns `ok` from `PRAGMA integrity_check`, so every earlier format guarantee still holds.
- **R15.3** — With encryption on, every page written past page 1 is transformed, so no page body after byte 100 matches its plaintext content, while the `SQLite format 3\0` magic at offset 0 and the standard page-1 fields at offsets 16-99 remain plaintext. Page 1's own body (bytes 100-4095) is not observed either way: the reference leaves it untransformed, and no case distinguishes that from transforming it.
- **R15.5** — With encryption on, the `SQLite format 3\0` magic at offsets 0-15 and the standard page-1 fields up to offset 99 remain plaintext, so the file is still recognisable as a database of the declared page size, while the reserved-space byte at offset 20 is nonzero to accommodate the per-page nonce and tag: 48 bytes per page for `aegis256`, 32 for `aegis128l` and 28 for `aes256gcm`.
- **R15.6** — An encrypted database reopened with the correct key returns exactly the rows that were written to it.
- **R15.7** — An encrypted database opened with a wrong or absent key fails outright rather than returning garbage rows: no row of the encrypted table is produced, and the failure is visible in the process's exit status.
- **R15.8** — Stock `sqlite3` opening an encrypted database does not return `ok` from `PRAGMA integrity_check`, the byte-level interoperability guarantee being explicitly suspended while encryption is on.
- **R15.9** — A `PRAGMA wal_checkpoint` run with encryption on copies frames correctly, and the rows remain readable through the encrypted pager afterwards. The checkpoint's result triple is checked by value at this step, not merely by shape: it is SQLite's `busy | frames-in-log | frames-copied`, `busy` is `0` because no case opens a second connection to the same file, a `TRUNCATE` checkpoint resets the log and answers `0|0|0`, and a `FULL` checkpoint taken after one row was inserted into an existing table answers `0|1|1`.
- **R15.10** — Recovery after a simulated crash with encryption on yields either the pre-transaction or the post-transaction state and never a mixture. Where the copy of the database and its log is taken after the writing process has exited, `spec/PROFILE.md`'s durability rule requires the committed transaction to be recoverable from that pair, so the post-transaction state is the only conforming outcome there.
- **R15.11** — `PRAGMA cipher = '<name>'` selects the session's cipher and `PRAGMA cipher` with no argument reports the selected name, in the engine's canonical spelling and as a single result row; with no cipher selected it reports no row at all. The recognised names are `aegis256`, `aes256gcm` and `aegis128l`; recognition of the name is case-insensitive and the reported spelling is always the lower-case one, so a name selected as `AEGIS256` reports back as `aegis256`. Any other name is refused under the `PROFILE.md` error contract, leaves no cipher selected, and does not end the session.
- **R15.12** — `PRAGMA hexkey = '<hex>'` supplies the session's key and `PRAGMA hexkey` with no argument reports whether a key is held, as one result row, without ever echoing the key; that row's text is fixed, exactly `encryption key is set for this session` while a key is held and exactly `encryption key is not set for this session` before one has been supplied, and after a key refused for its *format* -- not hex, or of a decoded length neither of the two recognised key sizes uses. A key refused by the length cross-check against the selected cipher is the one spot `TEST_INTERFACE.md` leaves free: whether such a key is still held afterwards is out of scope. The argument must be hexadecimal and must decode to a length some recognised cipher uses -- 16 bytes (`aegis128l`) or 32 bytes (`aegis256`, `aes256gcm`); anything else is refused. When a cipher and a key are both present the pair is cross-checked at the moment the second arrives, and a key whose length is not the selected cipher's key length is refused, leaving the session usable and able to take a correctly sized key. A key on its own does not engage the transform: with a key held and no cipher selected, the session writes and reads on the ordinary unencrypted path, so `CREATE`, `INSERT` and `SELECT` behave exactly as they do when no key was ever supplied.
- **R15.13** — The pair is set once. Once a session holds both a cipher and a key, re-selecting either -- even to another recognised cipher, even with another well-formed key of the right length -- is refused rather than re-keying the pager underneath open cursors, and the context already in force keeps working for the rest of the session and for a later process that reopens the file with the original pair. The rule binds only once both halves are held: while no key has been supplied, selecting a cipher again is allowed, is not refused, and the last selection is the one the query form reports and the one a key would later be cross-checked against.
- **R15.14** — `PRAGMA synchronous = <mode>` selects the pager's fsync discipline and `PRAGMA synchronous` reports the mode numerically, defaulting to 2. `OFF`/`FULL` and their numeric spellings both select, and the two pair up as SQLite pairs them: `OFF` and `0` are the same mode and the query form answers `0` for it, `FULL` and `2` are the same mode and the query form answers `2`, so the default of `2` is full. The mode changes when the pager fsyncs and never what it stores: rows written under either mode checkpoint into the main database, read back unchanged, and `PRAGMA wal_checkpoint` returns the same result triple it returns in full mode.

The numbering above skips `R15.4`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers nothing beyond them as new behaviour. One inherited obligation is in scope there as well, because one `.sh` case at this step reads the shell's raw standard output: the framing `R11.3` fixed at step 11's checkpoint -- result rows only, with no blank line between result blocks. Nothing is asked of you that step 11 did not already ask.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/pragma.html`
  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/walformat.html`
  - `/app/spec/turso/COMPAT.md` — the upstream compatibility matrix at exactly
    this release: statement by statement and function by function, what the
    project claimed to support at that tag. Where it disagrees with the requirements listed in this step, **the requirements win**: the matrix is a description of the release, the requirement list is the contract.
  - `/app/spec/turso/manual.md` — the project manual at this release. Its **Limitations** list in particular is a description of the release and not of what is in scope here: where a line in it denies something this step's requirements ask for, the requirements win, exactly as with the matrix.
  - `/app/spec/turso/CHANGELOG.md`, `/app/spec/turso/README.md` — what this release
    added, in the project's own words
  - `/app/spec/README.md` — how to navigate the rest of the corpus

Task documents:

  - `/app/TEST_INTERFACE.md` — the contract: the artefact path, the exact
    invocation, the output mode, how output and exit status are compared, what
    is in scope, and what is explicitly out of scope. **Read this before writing
    code.**
  - `/app/WORKSPACE_HANDOFF.md` — what you start with, what must be true when
    you stop, and how to check your work locally
  - `/app/spec/PROFILE.md` — where this engine deliberately differs from stock
    SQLite, and what the tests assert instead

## Meta requirements

- **Budget.** Run `bash /app/timer.sh` to see the time remaining in this
  step. The configured timeout is authoritative; the timer is an estimate.
- **The workspace is cumulative.** The next step continues in this same
  `/app`. Leave your sources in place, keep `/app/bin/` populated, and do not
  clean the tree. `/app/target/` and `/app/.git/` are not part of the
  deliverable; everything else under `/app` is.
- **Install the artefact.** `/app/bin/tursodb` must be a regular
  executable file when you stop. Nothing is built for you; if it
  is absent, no requirement group can be satisfied.
- **Network.** This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an online implementation of this task, or a task-specific answer,
  is prohibited.
- **Off-limits paths.** Do not inspect `/tests`, later steps, or any
  solution material, and do not write to them.
- **Check your own work.** Stock `sqlite3` is on this machine and is a second
  implementation of the same format. Use it to check any file you write and
  any answer you are unsure of. It is a development tool: nothing in `/app`
  may link to it, vendor it, or shell out to it at run time.

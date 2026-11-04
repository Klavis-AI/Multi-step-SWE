# SMB 2.0.2 and SMB 2.1

## 1. Implement this milestone

The file service in this workspace speaks the SMB1/CIFS dialect **NT LM 0.12**.
Teach it **SMB 2.0.2 (0x0202)** and **SMB 2.1 (0x0210)** as [MS-SMB2] defines
them, without losing anything it does today.

Nothing about how the service is started or configured changes:
`/app/SPEC/SERVICE_INTERFACE.md` is still the contract -- `/app/bin/smbd` with
`smbd(8)`'s own options (`--foreground --no-process-group --debug-stdout
--debuglevel=N --configfile=PATH --port=N`), configured only by an
`smb.conf(5)` file, accounts from an `smbpasswd(5)` file named by
`passdb backend = smbpasswd:<path>`. It is run under one fixed,
unprivileged OS account for every connection -- never as root -- so it cannot
change its process uid or the on-disk ownership of files, and it reports
ownership, group and access in terms of the authenticated SMB user and the share
configuration, as Samba does, not the account the process itself runs under. The
files already present in the share belong to the account the clients authenticate
as rather than to the service's OS account, so the service holds their ownership,
group and permission bits as metadata it maintains for the authenticated user and
does not depend on changing the on-disk ownership or mode of a file it did not
create: an owner operation on such a file is emulated, not performed on disk. It
is still a Node.js 20 program using
only Node's standard library, with no compiled code, no native addon and no
third-party runtime dependency directory in the workspace.

### The new dialects

- **A new message format.** The 64-byte `SMB2 Packet Header - SYNC`, the
  direct-TCP framing, `NextCommand` compounding including
  `SMB2_FLAGS_RELATED_OPERATIONS` (where a `FileId` of all `0xFF` means the one
  the previous `CREATE` in the chain returned), `SMB2_FLAGS_ASYNC_COMMAND` with
  `STATUS_PENDING` interim responses and `AsyncId`, and the 9-byte
  `SMB2 ERROR Response`.
- **The SMB2 command set.** `NEGOTIATE`, `SESSION_SETUP`, `LOGOFF`,
  `TREE_CONNECT`, `TREE_DISCONNECT`, `CREATE`, `CLOSE`, `FLUSH`, `READ`,
  `WRITE`, `LOCK`, `IOCTL`, `CANCEL`, `ECHO`, `QUERY_DIRECTORY`,
  `CHANGE_NOTIFY`, `QUERY_INFO`, `SET_INFO` and `OPLOCK_BREAK`, over the
  [MS-FSCC] information classes and the [MS-DTYP] security descriptor.
- **Dialect negotiation, including the revert.** An SMB2 `NEGOTIATE` selects the
  highest dialect the configuration allows out of the client's list, reports the
  security mode the signing policy implies and the capabilities the dialect
  implies -- `SMB2_GLOBAL_CAP_DFS` always, `SMB2_GLOBAL_CAP_LEASING` and
  `SMB2_GLOBAL_CAP_LARGE_MTU` from 0x0210; a client that offers only dialects
  outside `server min protocol`...`server max protocol` is refused. An **SMB1**
  `SMB_COM_NEGOTIATE` whose dialect list carries `SMB 2.???` or `SMB 2.002` is
  answered with an SMB2 `NEGOTIATE` response instead of an SMB1 one. `SMB 2.002`
  alone is answered with `DialectRevision` `0x0202` ([MS-SMB2] 3.3.5.3.2). The
  `SMB 2.???` wildcard is answered with `0x02FF` -- the "dialect revision
  negotiation" placeholder that asks the client to come back with a real SMB2
  `NEGOTIATE` -- only where a dialect above 0x0202 is actually on offer; a
  configuration whose `server max protocol` stops at `SMB2_02` has nothing to
  renegotiate and answers the wildcard with `0x0202` as well
  ([MS-SMB2] 3.3.5.3.1).
- **Signing.** HMAC-SHA256 over the whole PDU with the `Signature` field zeroed,
  truncated to 16 bytes, keyed with the session key: verified on every request
  that carries `SMB2_FLAGS_SIGNED`, and applied to the responses of a session
  that signs. [MS-SMB2] 3.3.5.2.4 lets a server answer a signature that does not
  verify either with `STATUS_ACCESS_DENIED` or by dropping the connection; the
  reference server answers `STATUS_ACCESS_DENIED` and keeps the connection. The
  sequence-number check of 3.3.5.2.3 comes before the signature check: a request
  whose `MessageId` has already been used, or whose `CreditCharge` reaches past
  the credits the client has been granted, ends the connection without an
  answer.
- **Credits.** `CreditRequest`/`CreditResponse` accounting, and from 0x0210 the
  `CreditCharge` a multi-credit request consumes.
- **Large MTU.** `MaxTransactSize`, `MaxReadSize` and `MaxWriteSize` of 65536 at
  0x0202 and 8388608 at 0x0210, with `SMB2_GLOBAL_CAP_LARGE_MTU` advertised at
  0x0210. A single read or write above 65536 bytes must therefore be served at
  0x0210, and a request whose payload spans more than one 65536-byte credit unit
  carries the `CreditCharge` [MS-SMB2] 3.2.4.1.5 computes for it. A `READ` or
  `WRITE` longer than the `MaxReadSize` or `MaxWriteSize` advertised for the
  connection is `STATUS_INVALID_PARAMETER`, once it has passed the
  sequence-number check above.
- **Create contexts.** `DHnQ`, `DHnC`, `DH2Q`, `DH2C`, `MxAc`, `QFid`, `RqLs`,
  `AlSi`, `ExtA` and `SecD` on the request. [MS-SMB2] 2.2.14.2 defines a
  *response* context for only some of them -- `DHnQ`, `DH2Q`, `MxAc`, `QFid` and
  `RqLs` -- and the others take effect without one: `AlSi` sets the allocation
  size, `ExtA` writes extended attributes, `SecD` applies a security descriptor,
  and none of the three is echoed back. An unrecognised context tag is ignored,
  not refused. `MxAc` reports what the object's security descriptor grants the
  authenticated user under the [MS-DTYP] 2.5.3 access check for
  `MAXIMUM_ALLOWED` (the owner's implicit `READ_CONTROL` and `WRITE_DAC`
  included), plus `FILE_READ_ATTRIBUTES` always and `DELETE` whenever the
  containing directory grants that user `FILE_DELETE_CHILD`.
- **Durable handles.** Both forms are granted from 0x0202 -- `DHnQ` and, since a
  server answers the request it is given rather than the one its dialect
  implies, `DH2Q` as well -- and neither is granted persistent. A granted handle
  survives the loss of its connection and is reclaimed by a `DHnC`/`DH2C`
  reconnect from the same client on a new connection.
- **Names, shares and control codes.** A `CREATE` name is share-relative and
  must not begin with a path separator; `TREE_CONNECT` reports the share type,
  its flags and the maximal access the share's `read only` setting implies --
  `FILE_ALL_ACCESS` (0x001F01FF) on a writable share, and on a `read only = yes`
  share the same less `FILE_WRITE_DATA`, `FILE_APPEND_DATA`, `FILE_WRITE_EA`,
  `FILE_DELETE_CHILD` and `FILE_WRITE_ATTRIBUTES`;
  `FSCTL_DFS_GET_REFERRALS` on a share that is not a DFS root
  (`STATUS_NOT_FOUND`), the pipe FSCTLs on a disk share (`FSCTL_PIPE_WAIT`
  `STATUS_INVALID_PARAMETER`, `FSCTL_PIPE_PEEK` `STATUS_INVALID_DEVICE_REQUEST`,
  `FSCTL_PIPE_TRANSCEIVE` `STATUS_NOT_SUPPORTED`), and an unknown control code
  (`STATUS_INVALID_DEVICE_REQUEST`) are each refused.
  `QUERY_DIRECTORY` on an open that is not a directory is `STATUS_NOT_SUPPORTED`
  (where 3.3.5.18 says `STATUS_INVALID_PARAMETER`, the reference server's
  answer is the required one), and one with an empty search pattern is `STATUS_INVALID_PARAMETER`
  rather than a match for everything. Renaming a file sets its
  `FILE_ATTRIBUTE_ARCHIVE`, and a name that is already a valid 8.3 name is its
  own alternate name, exactly as stored rather than upper-cased.
- **Leases.** `SMB2_GLOBAL_CAP_LEASING` at 0x0210 when `smb2 leases = yes`:
  read/handle/write leases requested with `RqLs`, granted per lease key, broken
  by a conflicting open, and a second open under the *same* lease key that does
  not break it.
- **Oplocks.** `SMB2_OPLOCK_LEVEL_II`, `_EXCLUSIVE` and `_BATCH` granted through
  `CREATE`, broken with an unsolicited `OPLOCK_BREAK` notification on the
  holder's connection (`MessageId` 0xFFFFFFFFFFFFFFFF) and acknowledged with
  `OPLOCK_BREAK`.

### What must keep working

The SMB1/CIFS service is not retired. With a client pinned to `NT LM 0.12` and
a configuration whose `server min protocol` allows it, every behaviour that
worked before must still work: negotiation, NTLMv2 session setup, tree connect,
the SMB1 command set, the TRANS2 and NT_TRANSACT surfaces, share modes,
delete-on-close, byte-range locks, oplocks and the error forms of
[MS-CIFS] 2.2.3.2. Both dialect families are served by the same process, chosen
by what the client negotiates, and `server min protocol` / `server max protocol`
decide which are on offer. Everything the previous milestone required of the
SMB1 surface -- its conformance tests and its field-for-field match with Samba
4.17.12 -- must still hold.

**Conformance.** Samba's `smbtorture` (installed here) publishes the
conformance suites: `smb2.*` for these dialects and `base.*` for NT LM 0.12.
The target is the tests in them that a Samba 4.17.12 file server itself passes
under the same configurations:

```
smbtorture //127.0.0.1/tmp -U smbuser%'smbpass1!' -p PORT --target=samba3 \
  --option='client min protocol=SMB2_02' --option='client max protocol=SMB2_10' smb2.<name>
```

The service's answers to scripted `smbclient` sessions and direct SMB2 and SMB1
exchanges must also match, field for field, what a real Samba 4.17.12 file
server answers. Values that are a property of the server or the host rather than
of the protocol are exempt from that match: time stamps, server GUID, challenge,
session key, identifiers (SessionId/TreeId/FileId/UID/TID/FID), free space,
directory enumeration order, and the content of a generated 8.3 alternate name.

**The reference implementation.** The interface this service adopts is Samba's,
and Samba 4.17.12 is the version whose behaviour it must match. Its *client*
tooling is installed here; its *server* is not, and shipping, wrapping or
invoking one as part of this service is the cheating this ticket forbids.
Where two answers would both satisfy the
prose of a Microsoft document -- which `NTSTATUS` a failure carries, whether a
condition is reported as an NTSTATUS or as a DOS error class and code, how many
bytes an information level occupies -- the required answer is the one Samba
4.17.12 gives under the same configuration. `smb.conf(5)` under
`/app/SPEC/samba-man/` is the normative description of every parameter's effect,
including how DOS attributes are carried in UNIX permission bits (`map archive`,
`map hidden`, `map system`, `store dos attributes`).

## 2. Read these local references

**Normative semantics:** `/app/SPEC/MS-SMB2.txt` for the new dialects (its
provenance is in `/app/SPEC/PROVENANCE-MS-SMB2.md`), `/app/SPEC/MS-CIFS.txt` and
`/app/SPEC/MS-SMB.txt` for the dialect already implemented,
`/app/SPEC/MS-NLMP.txt` and `/app/SPEC/MS-SPNG.txt` for authentication,
`/app/SPEC/MS-FSCC.txt` for information classes and file attributes,
`/app/SPEC/MS-DTYP.txt` for security descriptors and `FILETIME`, and
`/app/SPEC/MS-ERREF.txt` for NTSTATUS values.

**Standards those documents cite (local copies):** RFC 4178, the SPNEGO protocol
[MS-SPNG] extends, at `/opt/docs/ietf/rfc4178.txt`; RFC 2743 (GSS-API, including
the initial-token framing SPNEGO uses) at `/opt/docs/ietf/rfc2743.txt`.
`/opt/docs/SOURCES.txt` records where each came from.

**The service's own interface:** `/app/SPEC/SERVICE_INTERFACE.md`, with the
`smbd(8)`, `smb.conf(5)`, `smbpasswd(5)` and `smbclient(1)` manual pages under
`/app/SPEC/samba-man/`, and a representative configuration and account file in
`/app/SPEC/conf/`.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` and the existing sources in
`/app`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and handoff files. Do not create a prose answer or
  alternate entry point unless Section 1 explicitly requires one.
- Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; work only from this
  workspace, its documentation and `/opt/docs`, and do not search the system
  for other implementations or answers.
- Run the local checks named by the references and leave a reproducible
  handoff for the next fresh session.

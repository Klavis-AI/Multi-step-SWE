# SMB1/CIFS file server

## 1. Implement this milestone

Build a **CIFS file server** in this workspace that speaks the **NT LM 0.12**
dialect of SMB over TCP and exports directories of the local filesystem.

It is a Node.js 20 program: it runs under the `node` in this container using
only Node's own standard library. The workspace must contain no compiled
executable, shared object or native addon, and no third-party runtime
dependency directory (`node_modules`). Nothing is built or installed for it:
the service runs from what is in `/app`.

The service is started, configured and driven exactly as Samba's `smbd` is --
`/app/SPEC/SERVICE_INTERFACE.md` in this workspace is the contract, and it does
not change. In short: the build produces `/app/bin/smbd`, it takes `smbd(8)`'s
own options (`--foreground --no-process-group --debug-stdout --debuglevel=N
--configfile=PATH --port=N`), it is configured only by an `smb.conf(5)` file,
and its accounts come from an `smbpasswd(5)` file named by
`passdb backend = smbpasswd:<path>`. It is run under one fixed,
unprivileged OS account for every connection -- never as root -- so it cannot
change its process uid or the on-disk ownership of files, and it reports
ownership, group and access in terms of the authenticated SMB user and the share
configuration, as Samba does, not the account the process itself runs under. The
files already present in the share belong to the account the clients authenticate
as rather than to the service's OS account, so the service holds their ownership,
group and permission bits as metadata it maintains for the authenticated user and
does not depend on changing the on-disk ownership or mode of a file it did not
create: an owner operation on such a file is emulated, not performed on disk.

Over the wire it must implement, as [MS-CIFS] and [MS-SMB] define them:

- **Dialect negotiation.** `SMB_COM_NEGOTIATE` selecting `NT LM 0.12` out of the
  client's dialect list, advertising the capabilities and the security mode the
  configuration implies, and refusing a list that offers nothing usable.
- **Authentication.** `SMB_COM_SESSION_SETUP_ANDX` with extended security:
  SPNEGO ([MS-SPNG]) carrying NTLMSSP ([MS-NLMP]) with **NTLMv2**, validated
  against the NT hashes in the `smbpasswd` file. Several sessions per
  connection; `SMB_COM_LOGOFF_ANDX` ends one.
- **Tree connect.** `SMB_COM_TREE_CONNECT_ANDX` to the disk shares the
  configuration defines and to `IPC$`, with the share type in `Service`;
  `SMB_COM_TREE_DISCONNECT`.
- **File operations.** `SMB_COM_NT_CREATE_ANDX` and `NT_TRANSACT_CREATE` with
  the full access mask, share mode, create disposition and create options;
  `SMB_COM_OPEN_ANDX`; `READ_ANDX`/`WRITE_ANDX` including 64-bit offsets and the
  large read/write forms; `CLOSE`, `FLUSH`, `DELETE`, `RENAME`, `NT_RENAME`,
  `CREATE_DIRECTORY`, `DELETE_DIRECTORY`, `CHECK_DIRECTORY`, `SEEK`,
  `QUERY_INFORMATION`, `SET_INFORMATION`, `QUERY_INFORMATION2`,
  `QUERY_INFORMATION_DISK`, `PROCESS_EXIT`, `ECHO`.
- **The TRANS2 surface.** `FIND_FIRST2`/`FIND_NEXT2`/`FIND_CLOSE2` with DOS
  search attributes and wildcards, `QUERY_FS_INFORMATION`,
  `QUERY_PATH_INFORMATION`, `SET_PATH_INFORMATION`, `QUERY_FILE_INFORMATION`,
  `SET_FILE_INFORMATION` and `CREATE_DIRECTORY`, over both the SMB1 information
  levels of [MS-CIFS] 2.2.8 and the [MS-FSCC] pass-through classes. A
  `FIND_FIRST2` search pattern that ends in a path separator has it dropped
  before the directory and the pattern are split apart, so `\data\gamma\`
  searches `\data` for `gamma`.
- **NT semantics.** Share-mode conflicts, `FILE_DELETE_ON_CLOSE` (which needs
  `DELETE` in the same request's access mask) and `FileDispositionInformation`
  delete-pending behaviour, the DOS attributes `smb.conf(5)` describes under
  `map archive`, `map hidden`, `map system` and `store dos attributes`,
  case-insensitive but case-preserving name lookup, 8.3 alternate names (a name
  that is already a valid 8.3 name is its own alternate name, exactly as stored
  rather than upper-cased), `FILE_ATTRIBUTE_READONLY` enforcement, and the
  NTSTATUS values [MS-ERREF] defines for each failure. Renaming a file sets its
  `FILE_ATTRIBUTE_ARCHIVE`.
- **Byte-range locking.** `LOCKING_ANDX` (including the large-file form and
  timed, blocking requests), `LOCK_BYTE_RANGE`/`UNLOCK_BYTE_RANGE`.
- **Oplocks.** Exclusive, batch and level II oplocks requested through
  `SMB_COM_NT_CREATE_ANDX`, broken by a conflicting open, and acknowledged with
  `LOCKING_ANDX`.
- **Security descriptors.** `NT_TRANSACT_QUERY_SECURITY_DESC` returning a
  self-relative `SECURITY_DESCRIPTOR` as [MS-DTYP] 2.4.6 defines it, and
  `NT_TRANSACT_SET_SECURITY_DESC` accepting one. Both are subject to the
  handle's access mask: reading the descriptor needs `READ_CONTROL`, replacing
  the DACL needs `WRITE_DAC`, replacing the owner needs `WRITE_OWNER`. The
  descriptor is the one `nt acl support` (`smb.conf(5)`) produces with no
  ACL-storing VFS module:
  - The owner of an object that belongs to an account in the `smbpasswd(5)`
    file is that account's SID in the server's own machine domain,
    `S-1-5-21-<n1>-<n2>-<n3>-<RID>`, not the UNIX-user form `S-1-22-1-<uid>`:
    the three sub-authorities identify the server and their values are its own
    choice, and the RID is twice the account's uid plus `algorithmic rid base`
    (with the default base of 1000, uid 1000 is RID 3000). A gid maps to the
    UNIX-group SID `S-1-22-2-<gid>`. Every descriptor returned has
    `SE_SELF_RELATIVE` and `SE_DACL_PROTECTED` set in `Control`, plus
    `SE_DACL_PRESENT` whenever it carries a DACL.
  - An object whose DACL was never applied gets the descriptor its UNIX
    permission bits describe: one `ACCESS_ALLOWED_ACE` each for the owner, group
    and other triples, over the owner SID, the group SID and `S-1-1-0`. A triple
    that is `rwx` is full control (`acl map full control` defaults to `yes`):
    `FILE_ALL_ACCESS` for a directory, and `FILE_ALL_ACCESS` without `DELETE` for
    a file. Any other triple is the union of the `FILE_GENERIC_READ`,
    `FILE_GENERIC_WRITE` and `FILE_GENERIC_EXECUTE` sets its bits imply, plus
    `FILE_DELETE_CHILD` for a `w` on a directory; `DELETE` is in none of them.
  - A DACL that is set is not stored as sent: it is mapped down onto the
    object's POSIX permissions and the descriptor is re-synthesised from them
    when it is read back. An ACE contributes `r` if its mask carries
    `FILE_READ_DATA` or `FILE_READ_EA`, `w` for `FILE_WRITE_DATA` /
    `FILE_APPEND_DATA` / `FILE_WRITE_EA`, and `x` for `FILE_EXECUTE`; every
    other right is dropped. Only an ACE over the owner, the owning group or
    `S-1-1-0` maps at all, and a deny ACE subtracts its bits from that class. A
    missing `user::` entry keeps the object's current owner bits, a missing
    `group::` entry copies `other::`, and the owner is then forced to keep `r`
    on a file and `rwx` on a directory. What is read back is the **five-ACE**
    POSIX-ACL shape -- `user::`, `group::`, the named group entry, the named
    user entry, then `other::`, in that order, each masked by the rule above,
    the two named entries repeating the owning group and the owner with
    `group::`'s and `user::`'s masks. A DACL with no mappable ACE, or exactly
    the one the permission bits already imply, is not applied and leaves the
    previous descriptor unchanged.
- **Error reporting.** [MS-CIFS] 2.2.3.2: an NTSTATUS from [MS-ERREF] when the
  `SMB_FLAGS2_NT_STATUS` bit is set on the response, and the DOS error class
  and code for the errors [MS-CIFS] defines only in that form -- among them
  `ERRSRV`/`ERRbaduid`, `ERRSRV`/`ERRbadcmd` and `ERRDOS`/`ERRinvalidparam`.

Two shares are configured: a writable one and one with `read only = yes`, which
must refuse every modifying request -- the ones that name a path and the ones
that work through an open handle, the core command set and the TRANS2
equivalents alike. Not every refusal carries the same status, and [MS-CIFS] does
not settle which carries which; this is one of the places where the rule below
about two admissible answers decides it. The reference server refuses the core
command set (`DELETE`, `DELETE_DIRECTORY`, `CREATE_DIRECTORY`, `RENAME`,
`NT_RENAME`, `SET_INFORMATION`, `SET_INFORMATION2`) with
`STATUS_MEDIA_WRITE_PROTECTED`, and the TRANS2 layer (`SET_PATH_INFORMATION`,
`SET_FILE_INFORMATION`, `CREATE_DIRECTORY`) and any open or create that asks for
write access with `STATUS_ACCESS_DENIED`.

**Conformance.** Samba publishes the conformance suite for this dialect:
`smbtorture`'s `base.*` tests, and `smbtorture` is installed here. The target is
the tests among them that a Samba 4.17.12 file server itself passes under the
configuration above. Run them with

```
smbtorture //127.0.0.1/tmp -U smbuser%'smbpass1!' -p PORT --target=samba3 \
  --option='client min protocol=NT1' --option='client max protocol=NT1' base.<name>
```

Beyond that suite, the service's answers to scripted `smbclient` sessions and
direct SMB1 exchanges must match, field for field, what a real Samba 4.17.12
file server answers -- same requests, same directory tree, same configuration.
Values that are genuinely a property of the server or the host rather than of
the protocol are exempt from that match: time stamps, server GUID, challenge,
session key, identifiers (UID/TID/FID), buffer-size and credit advertisements,
free space, directory enumeration order, and the *content* of a generated 8.3
alternate name -- for the last, only its 8.3 form must match, since no
specification fixes how it is generated.

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

**Normative semantics:** `/app/SPEC/MS-CIFS.txt` and `/app/SPEC/MS-SMB.txt` for
the protocol, `/app/SPEC/MS-NLMP.txt` and `/app/SPEC/MS-SPNG.txt` for
authentication, `/app/SPEC/MS-FSCC.txt` for information classes and file
attributes, `/app/SPEC/MS-DTYP.txt` for security descriptors and `FILETIME`, and
`/app/SPEC/MS-ERREF.txt` for NTSTATUS values. `/app/SPEC/PROVENANCE.md` records
where each came from.

**Standards those documents cite (local copies):** RFC 4178, the SPNEGO protocol
[MS-SPNG] extends, at `/opt/docs/ietf/rfc4178.txt`; RFC 2743 (GSS-API, including
the initial-token framing SPNEGO uses) at `/opt/docs/ietf/rfc2743.txt`.
`/opt/docs/SOURCES.txt` records where each came from.

**The service's own interface:** `/app/SPEC/SERVICE_INTERFACE.md`, with the
`smbd(8)`, `smb.conf(5)`, `smbpasswd(5)` and `smbclient(1)` manual pages under
`/app/SPEC/samba-man/`, and a representative configuration and account file in
`/app/SPEC/conf/`.

**Additional files:** `/app/WORKSPACE_HANDOFF.md`.

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

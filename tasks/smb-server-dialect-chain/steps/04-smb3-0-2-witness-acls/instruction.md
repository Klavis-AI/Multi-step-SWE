# SMB 3.0.2, the witness protocol, and the security-descriptor model

## 1. Implement this milestone

The file service in this workspace speaks SMB1/CIFS (`NT LM 0.12`) and SMB2
through SMB 3.0 (0x0202, 0x0210, 0x0300). Add three things, without losing
anything it does today.

Nothing about how the service is started or configured changes:
`/app/SPEC/SERVICE_INTERFACE.md` is still the contract -- `/app/bin/smbd` with
`smbd(8)`'s own options, configured only by an `smb.conf(5)` file, accounts from
an `smbpasswd(5)` file named by `passdb backend = smbpasswd:<path>`. It is run under one fixed,
unprivileged OS account for every connection -- never as root -- so it cannot
change its process uid or the on-disk ownership of files, and it reports
ownership, group and access in terms of the authenticated SMB user and the share
configuration, as Samba does, not the account the process itself runs under. The
files already present in the share belong to the account the clients authenticate
as rather than to the service's OS account, so the service holds their ownership,
group and permission bits as metadata it maintains for the authenticated user and
does not depend on changing the on-disk ownership or mode of a file it did not
create: an owner operation on such a file is emulated, not performed on disk. It
is still
a Node.js 20 program using only Node's standard library, with no compiled code,
no native addon and no third-party runtime dependency directory in the
workspace.

### 1. Dialect 0x0302

- `NEGOTIATE` selects 0x0302 when the client offers it and the configuration
  allows it, and the highest allowed dialect otherwise.
- The read and write **channel flags** [MS-SMB2] 2.2.19/2.2.21 add at this
  dialect: `SMB2_READFLAG_READ_UNBUFFERED` and
  `SMB2_WRITEFLAG_WRITE_UNBUFFERED`.
- `SMB2_CREATE_APP_INSTANCE_ID` ([MS-SMB2] 2.2.13.2.13) is accepted on `CREATE`.
- Encryption, signing, secure negotiate and the network-interface FSCTL behave
  as they do at 0x0300.

### 2. The witness protocol

Serve **MS-SWN** on the named pipe `\pipe\witness` of the `IPC$` share, over
DCE/RPC as [MS-RPCE] defines it (it extends DCE 1.1 RPC, C706, local copy
under `/opt/docs/opengroup-c706/`): `bind`/`bind_ack` with the NDR transfer syntax,
`request`/`response`, and the interface
`ccd8c074-d0e5-4a40-92b4-d074faa6ba28` version 1.1 with its five operations --
`WitnessrGetInterfaceList` (0), `WitnessrRegister` (1), `WitnessrUnRegister` (2),
`WitnessrAsyncNotify` (3) and `WitnessrRegisterEx` (4) -- marshalled in
little-endian NDR. A `CREATE` on a pipe name the service does not serve is
`STATUS_OBJECT_NAME_NOT_FOUND`; a pipe is driven by `WRITE`+`READ` or by
`FSCTL_PIPE_TRANSCEIVE`. An opnum the interface does not define is a DCE/RPC
`fault` carrying `nca_s_op_rng_error`.

What each operation must answer:

- `WitnessrGetInterfaceList` returns one `WITNESS_INTERFACE_INFO` per interface
  the service is available on, and `WERR_OK`. A service whose interface list is
  empty returns `ERROR_NO_MORE_ITEMS` instead ([MS-SWN] 3.1.4.1) -- so a service
  that answers a client on an interface has to put that interface in the list.
- `WitnessrRegister` and `WitnessrRegisterEx` take a version, a NetName, an IP
  address and a client name -- `RegisterEx` a share name as well, and two
  trailing `DWORD`s -- and return a context handle and `WERR_OK`. A version that
  is neither `0x00010001` nor `0x00020000` is `WERR_REVISION_MISMATCH`
  ([MS-SWN] 3.1.4.2). A handle the service issues must be real: unregistering it
  succeeds once.
- `WitnessrUnRegister` takes a context handle. A handle with no matching entry is
  **`ERROR_INVALID_PARAMETER`**, which is what [MS-SWN] 3.1.4.3's processing rule
  says even though `ERROR_NOT_FOUND` is what its table of common error codes
  lists. Where a Microsoft document's error table and its processing rules
  disagree, the processing rules are the requirement.
- `WitnessrAsyncNotify` takes a context handle and, in the general case, does not
  answer until there is something to report. A handle with no matching entry is
  answered immediately and -- unlike `UnRegister`, and the document is explicit
  about each -- with `ERROR_NOT_FOUND` ([MS-SWN] 3.1.4.4).

Every response stub must be the NDR encoding [MS-SWN]'s IDL defines, byte for
byte: a stub that a conforming NDR decoder cannot parse and re-emit unchanged is
wrong even if its fields look right.

### 3. The security-descriptor and access-check model

Behind `nt acl support` in `smb.conf(5)`:

- `QUERY_INFO` and `SET_INFO` with `SMB2_0_INFO_SECURITY`, and the SMB1
  `NT_TRANSACT_QUERY_SECURITY_DESC` / `NT_TRANSACT_SET_SECURITY_DESC`, carrying
  a **self-relative `SECURITY_DESCRIPTOR`** as [MS-DTYP] 2.4.6 defines it, with
  the owner, group, DACL and control flags, honouring the
  `AdditionalInformation` selector. The owner of an object that belongs to an
  account in the `smbpasswd(5)` file is that account's SID in the server's own
  machine domain, `S-1-5-21-<n1>-<n2>-<n3>-<RID>`, not the UNIX-user form
  `S-1-22-1-<uid>`: the three sub-authorities identify the server and their
  values are its own choice, and the RID is twice the account's uid plus
  `algorithmic rid base` (`smb.conf(5)`; with the default base of 1000, uid 1000
  is RID 3000). A gid always maps to the UNIX-group SID `S-1-22-2-<gid>`. Every
  descriptor returned has `SE_SELF_RELATIVE` and
  `SE_DACL_PROTECTED` set in `Control`, plus `SE_DACL_PRESENT` whenever it
  carries a DACL.
- The access mask each operation needs: reading the descriptor needs
  `READ_CONTROL`, reading the SACL needs `ACCESS_SYSTEM_SECURITY`, replacing the
  DACL needs `WRITE_DAC`, replacing the owner needs `WRITE_OWNER`; an output
  buffer too small for the descriptor is `STATUS_BUFFER_TOO_SMALL`.
- The **access check** of [MS-DTYP] 2.5.3 on every `CREATE`: generic rights
  mapped to specific ones, `MAXIMUM_ALLOWED` resolved to what the descriptor
  actually grants, deny ACEs ahead of allow ACEs, the owner's implicit
  `READ_CONTROL`/`WRITE_DAC`, and `ACCESS_SYSTEM_SECURITY` refused without the
  privilege. The `MxAc` create context reports the same answer. As the reference
  server computes it, the maximal access also always includes
  `FILE_READ_ATTRIBUTES`, and includes `DELETE` whenever the containing
  directory grants the user `FILE_DELETE_CHILD`.
- A file that has no stored descriptor gets the one its UNIX permission bits
  describe. Each of the owner, group and other triples becomes one
  `ACCESS_ALLOWED_ACE`, over the owner SID, the `S-1-22-2-<gid>` group SID and
  `S-1-1-0` respectively, and the mask is built from the `FILE_GENERIC_READ`,
  `FILE_GENERIC_WRITE` and `FILE_GENERIC_EXECUTE` sets the triple's bits imply.
  That much is the shape; the exact mask is not, and two refinements of how
  `nt acl support` reads the bits decide it. First, `acl map full control` is in
  effect (its default is `yes`), so a triple that grants read, write and traverse
  together collapses to full control rather than the union of the three generic
  sets: `FILE_ALL_ACCESS` for a directory, and `FILE_ALL_ACCESS` without `DELETE`
  (so `FILE_DELETE_CHILD` is present, `DELETE` is not) for a file. This applies to
  whichever of the owner, group and other triples is `rwx`; it is not specific to
  the owner, and the owner's ACE carries no `WRITE_DAC` or `WRITE_OWNER` beyond
  what its own triple yields. Second, a triple that is not the full `rwx` is the
  union of the generic sets its bits imply, and for a **directory** a `w` there
  additionally carries `FILE_DELETE_CHILD`; `DELETE` itself is in none of them.
  The masks must be the ones the reference server produces, and `smb.conf(5)`'s
  description of `nt acl support`, `acl map full control` and the
  `create mask` / `directory mask` family is the normative account of how the
  bits are read.

- **A descriptor that is set is not a descriptor that is stored.** With
  `nt acl support = yes` and no ACL-storing VFS module there is nowhere to keep
  an NT ACL, so a DACL that arrives on `SET_INFO` or in a `SecD` create context is
  mapped down onto the object's POSIX permissions and a descriptor is
  **re-synthesised** from those permissions when it is read back, as `smb.conf(5)`'s
  `nt acl support` describes. The mapping is lossy and specific. An ACE contributes
  `r` if its mask carries `FILE_READ_DATA` or `FILE_READ_EA`, `w` for
  `FILE_WRITE_DATA` / `FILE_APPEND_DATA` / `FILE_WRITE_EA`, and `x` for
  `FILE_EXECUTE`; every other right -- `DELETE`, `READ_CONTROL`, `SYNCHRONIZE`, the
  attribute rights and `FILE_DELETE_CHILD` among them -- is dropped. Only an ACE
  whose trustee is the owner, the owning group or `S-1-1-0` maps at all; an ACE
  over any other trustee contributes nothing, and a deny ACE subtracts its bits
  from that class. A missing `user::` entry keeps the object's current owner bits,
  a missing `group::` entry copies `other::`, and the owner is then forced to keep
  `r` on a file and `rwx` on a directory. Because what is stored is a POSIX ACL, a
  client that sets a DACL does not read the same ACEs back: the descriptor that
  comes back is the **five-ACE** POSIX-ACL shape -- `user::`, `group::`, the named
  group entry, the named user entry, then `other::`, in that order, each masked by
  the rule in the bullet above, the two named entries repeating the owning group
  and the owner with `group::`'s and `user::`'s masks -- not the three-ACE
  mode-derived form, whenever a
  DACL is actually applied. Two DACLs are not applied and leave the object
  mode-derived with its previous descriptor unchanged: one with no mappable ACE at
  all, and one that is exactly the descriptor the object's permission bits already
  imply.

### What must keep working

Every earlier dialect is still served by the same process, chosen by what the
client negotiates and bounded by `server min protocol` / `server max protocol`:
0x0300 with its AES-128-CMAC signing and AES-128-CCM encryption, 0x0210 and
0x0202 with HMAC-SHA256 signing, and `NT LM 0.12` with the SMB1 command set.

Everything the earlier milestones required at those dialects -- the SMB 3.0 and
NT LM 0.12 conformance tests, and the field-for-field match with Samba 4.17.12
in 3.0, 2.1 and SMB1 exchanges and the SMB1 `smbclient` sessions -- must still
hold.

**Conformance.** The target is the tests in Samba's `smbtorture` `smb2.*` suites
that a Samba 4.17.12 file server itself passes, and the service's answers to
direct SMB exchanges and scripted `smbclient` sessions must match what a real
Samba 4.17.12 file server answers. Samba only runs its witness service on a
clustered server, so there is no `rpc.witness.*` torture suite to run against
this one: what the witness protocol must get right is its wire-level responses
on `\pipe\witness` over DCE/RPC. Each response stub must pass Samba's own NDR
decoder (`ndrdump witness <op> out --validate`), which re-marshals what it
parsed and compares byte-for-byte.

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

**Normative semantics:** `/app/SPEC/MS-SMB2.txt`, `/app/SPEC/MS-SWN.txt` and
`/app/SPEC/MS-RPCE.txt` (their provenance is in the matching
`/app/SPEC/PROVENANCE-*.md`), `/app/SPEC/MS-DTYP.txt` for security descriptors
and the access-check algorithm, `/app/SPEC/MS-CIFS.txt` and
`/app/SPEC/MS-SMB.txt`, `/app/SPEC/MS-NLMP.txt`, `/app/SPEC/MS-SPNG.txt`,
`/app/SPEC/MS-FSCC.txt` and `/app/SPEC/MS-ERREF.txt`.

**Standards those documents cite (local copies):** RFC 4178, the SPNEGO protocol
[MS-SPNG] extends, at `/opt/docs/ietf/rfc4178.txt`; RFC 2743 (GSS-API, including
the initial-token framing SPNEGO uses) at `/opt/docs/ietf/rfc2743.txt`; RFC 4493
(AES-CMAC) at `/opt/docs/ietf/rfc4493.txt`; NIST SP 800-108 (October 2009), the
KDF [MS-SMB2] 3.1.4.2 cites, at `/opt/docs/nist/sp800-108-2009.pdf` with a text
edition `/opt/docs/nist/sp800-108-2009.txt`; The Open Group's DCE 1.1: Remote
Procedure Call (C706), which [MS-RPCE] extends -- chapter 4 (IDL), chapter 12
(RPC PDU encodings), chapter 14 (NDR) and appendix N (IDL data type
declarations) -- under `/opt/docs/opengroup-c706/`. `/opt/docs/SOURCES.txt`
records where each came from.

**The service's own interface:** `/app/SPEC/SERVICE_INTERFACE.md`, the Samba
manual pages under `/app/SPEC/samba-man/`, the configuration in
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

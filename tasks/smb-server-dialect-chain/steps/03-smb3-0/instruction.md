# SMB 3.0

## 1. Implement this milestone

The file service in this workspace speaks SMB1/CIFS (`NT LM 0.12`) and SMB2
(0x0202, 0x0210). Teach it **SMB 3.0 (dialect 0x0300)** as [MS-SMB2] defines it,
without losing anything it does today.

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

### What 0x0300 changes

- **Signing changes algorithm.** Through 0x0210 a request is signed with
  HMAC-SHA256 over the PDU using the session key. From 0x0300 it is
  **AES-128-CMAC** (RFC 4493, local copy `/opt/docs/ietf/rfc4493.txt`), and the
  key is no longer the session key: it is derived with the SP800-108
  counter-mode KDF (local copy `/opt/docs/nist/sp800-108-2009.txt`) of
  [MS-SMB2] 3.1.4.2 -- label `SMB2AESCMAC`,
  context `SmbSign`. A request that still carries a 2.x signature is not
  authentic and must be refused.
- **Encryption.** `SMB2_GLOBAL_CAP_ENCRYPTION`, the
  `SMB2_TRANSFORM_HEADER` of [MS-SMB2] 2.2.41 and **AES-128-CCM**, with the
  encryption and decryption keys derived by the same KDF (label `SMB2AESCCM`,
  context `ServerIn ` for the client-to-server key and `ServerOut` for the
  server-to-client key -- the first has a trailing space and the second does
  not, and each string is NUL-terminated in the KDF input). `smb encrypt` in `smb.conf(5)` selects
  the policy globally and per share: a share with `smb encrypt = required`
  reports `SMB2_SHAREFLAG_ENCRYPT_DATA` in its `TREE_CONNECT` response, every
  message on that tree afterwards is wrapped, and a client that did not offer
  `SMB2_GLOBAL_CAP_ENCRYPTION` cannot connect to it.
- **Secure negotiate.** `FSCTL_VALIDATE_NEGOTIATE_INFO` ([MS-SMB2] 2.2.31.4,
  3.3.5.15.12): the server answers with the capabilities, server GUID, security
  mode and dialect it really negotiated, and refuses a request whose contents do
  not match what it saw.
- **Server-side copy.** `FSCTL_SRV_REQUEST_RESUME_KEY` ([MS-SMB2] 2.2.32.3)
  returns a 24-byte key for an open file, and `FSCTL_SRV_COPYCHUNK`
  ([MS-SMB2] 2.2.31.1) copies ranges from the file that key names into the file
  the request is made on, answering with `SMB2_SRV_COPYCHUNK_RESPONSE`. A resume
  key that names no open file is refused.
- **Sparse files and ranges.** `FSCTL_SET_SPARSE` ([MS-FSCC] 2.3.65) and
  `FSCTL_SET_ZERO_DATA` ([MS-FSCC] 2.3.67): a zeroed range reads back as zeroes.
  A control code the service does not implement is refused with the status the
  reference server uses, `STATUS_INVALID_DEVICE_REQUEST`.
- **One session per connection.** A `SESSION_SETUP` carrying
  `SMB2_SESSION_FLAG_BINDING` for a session already established on this
  connection is refused; binding a session across connections is a multichannel
  feature this configuration does not offer.
- **The interface list.** `FSCTL_QUERY_NETWORK_INTERFACE_INFO`
  ([MS-SMB2] 2.2.32.5), returning the interfaces the service is listening on.
- **Persistent handles are requested but not granted.** The
  `SMB2_DHANDLE_FLAG_PERSISTENT` flag of the `DH2Q` create context on a share
  that is not continuously available: the durable handle is granted, the
  persistence is not.
- **Leases gain a version.** The 52-byte `RqLs` context (lease v2) of
  [MS-SMB2] 2.2.13.2.10, with its parent lease key and epoch, is accepted and
  answered with a v2 context. The document defines the epoch field without
  fixing what a freshly granted lease carries in it or exactly what moves it;
  both are observable, and both must match the reference server. A lease requested on a
  **directory** handle is a separate question, answered by
  `SMB2_GLOBAL_CAP_DIRECTORY_LEASING`: a server
  that does not advertise that capability does not grant one, and answers the
  `CREATE` with no lease rather than refusing it.

  The capability bits are their own question, and the answer is the reference
  server's: which of `SMB2_GLOBAL_CAP_ENCRYPTION`,
  `SMB2_GLOBAL_CAP_DIRECTORY_LEASING`, `SMB2_GLOBAL_CAP_MULTI_CHANNEL` and
  `SMB2_GLOBAL_CAP_PERSISTENT_HANDLES` a `NEGOTIATE` response actually
  advertises at 0x0300 under a given `smb.conf` is decided by what that server
  advertises. A capability it does not advertise is one it does not offer: it
  must not then grant the thing the capability names, which is why a directory
  lease is answered with no lease rather than with one.

  One of the four is not a property of the server at all.
  `SMB2_GLOBAL_CAP_ENCRYPTION` is **mirrored**: [MS-SMB2] 3.3.5.4 has the server
  set it in its response only when the client set it in the request, so the same
  server under the same configuration advertises it to one client and not to
  another. A client that does not offer it gets a response without it, and gets
  no encrypted session -- which is what makes a share with
  `smb encrypt = required` unreachable for that client.

### What must keep working

Every earlier dialect is still served by the same process, chosen by what the
client negotiates and bounded by `server min protocol` / `server max protocol`.
With a client pinned to 0x0202 or 0x0210 the SMB2 behaviour is unchanged --
including HMAC-SHA256 signing, which is still the right algorithm for those
dialects -- and with a client pinned to `NT LM 0.12` the SMB1/CIFS behaviour is
unchanged.

Everything the earlier milestones required of the SMB 2.1 and SMB1 surfaces --
their conformance tests, and their field-for-field match with Samba 4.17.12 in
SMB exchanges and the SMB1 `smbclient` sessions -- must still hold.

**Conformance.** Samba's `smbtorture` (installed here) publishes the
conformance suites: `smb2.*` for the SMB2 family and `base.*` for NT LM 0.12.
The target is the tests in them that a Samba 4.17.12 file server itself passes
under the same configurations:

```
smbtorture //127.0.0.1/tmp -U smbuser%'smbpass1!' -p PORT --target=samba3 \
  --option='client min protocol=SMB2_02' --option='client max protocol=SMB3_00' smb2.<name>
```

The service's answers to direct SMB exchanges and scripted `smbclient` sessions
must also match, field for field, what a real Samba 4.17.12 file server answers.
Values that are a property of the server or the host rather than of the protocol
are exempt from that match: time stamps, server GUID, challenge, session key,
identifiers, free space, directory enumeration order, and the content of a
generated 8.3 alternate name.

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

**Normative semantics:** `/app/SPEC/MS-SMB2.txt` (dialects, the transform
header, the KDF, the FSCTLs, create contexts), `/app/SPEC/MS-CIFS.txt` and
`/app/SPEC/MS-SMB.txt` (NT LM 0.12), `/app/SPEC/MS-NLMP.txt` and
`/app/SPEC/MS-SPNG.txt` (authentication), `/app/SPEC/MS-FSCC.txt` (information
classes), `/app/SPEC/MS-DTYP.txt` (security descriptors, `FILETIME`) and
`/app/SPEC/MS-ERREF.txt` (NTSTATUS).

**Standards those documents cite (local copies):** RFC 4178, the SPNEGO protocol
[MS-SPNG] extends, at `/opt/docs/ietf/rfc4178.txt`; RFC 2743 (GSS-API, including
the initial-token framing SPNEGO uses) at `/opt/docs/ietf/rfc2743.txt`; RFC 4493
(AES-CMAC) at `/opt/docs/ietf/rfc4493.txt`; NIST SP 800-108 (October 2009), the
KDF [MS-SMB2] 3.1.4.2 cites, at `/opt/docs/nist/sp800-108-2009.pdf` with a text
edition `/opt/docs/nist/sp800-108-2009.txt`. `/opt/docs/SOURCES.txt` records
where each came from.

**The service's own interface:** `/app/SPEC/SERVICE_INTERFACE.md`, the Samba
manual pages under `/app/SPEC/samba-man/`, and the representative configuration
in `/app/SPEC/conf/`.

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

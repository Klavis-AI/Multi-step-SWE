# SMB 3.1.1

## 1. Implement this milestone

The file service in this workspace speaks SMB1/CIFS (`NT LM 0.12`) and SMB2
through SMB 3.0.2 (0x0202, 0x0210, 0x0300, 0x0302). Teach it **SMB 3.1.1
(0x0311)** as [MS-SMB2] defines it, without losing anything it does today.

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

### What 0x0311 changes

- **Negotiate contexts.** `NEGOTIATE` carries a context list ([MS-SMB2] 2.2.3.1,
  each context 8-byte aligned): `SMB2_PREAUTH_INTEGRITY_CAPABILITIES` (1),
  `SMB2_ENCRYPTION_CAPABILITIES` (2), `SMB2_COMPRESSION_CAPABILITIES` (3),
  `SMB2_NETNAME_NEGOTIATE_CONTEXT_ID` (5) and `SMB2_SIGNING_CAPABILITIES` (8).
  All five must be accepted on the request, and a context type the server does
  not know is ignored rather than refused. The response carries the pre-auth
  integrity context -- hash algorithm SHA-512 (id 1) and a 32-byte salt, freshly
  generated -- and, when the client offered ciphers and the configuration allows
  encryption, the encryption context naming the **one** cipher chosen. The choice
  is the server's own preference, not the order the client listed: AES-128-GCM is
  preferred over AES-128-CCM whichever order they arrive in, and a client that
  offers only a cipher the server does not know gets a context naming cipher 0.
  A `SMB2_SIGNING_CAPABILITIES` context is likewise answered with a context
  naming the one signing algorithm chosen -- AES-128-GMAC (2; RFC 4543, local
  copy `/opt/docs/ietf/rfc4543.txt`) where the client offers it, otherwise
  AES-128-CMAC (1) -- and a response carries no signing
  context at all when the client sent none. A 0x0311 negotiate with **no**
  pre-auth integrity context is `STATUS_INVALID_PARAMETER`; one whose hash
  algorithm list has nothing in common with the server's is
  `STATUS_SMB_NO_PREAUTH_INTEGRITY_HASH_OVERLAP`.

  Two cases in this area are places where [MS-SMB2]'s prose and the reference
  server part company, and the reference server's answer is the required one, as it is
  everywhere else in this ticket. A `NEGOTIATE` that carries the **same context
  type twice** is one; `FSCTL_VALIDATE_NEGOTIATE_INFO` **at 0x0311** is the other
  -- 3.3.5.15.12 says a server that has negotiated 3.1.1 should not process it,
  because the pre-auth integrity hash already protects the negotiate, and the
  reference server processes it anyway. Answer both the way the reference server
  answers them: a repeated context type is not refused -- the negotiate succeeds
  at 0x0311 as though the context had come once -- and
  `FSCTL_VALIDATE_NEGOTIATE_INFO` at 0x0311 succeeds.
- **Pre-authentication integrity.** The SHA-512 chain of [MS-SMB2] 3.3.5.4 over
  the `NEGOTIATE` request and response and every `SESSION_SETUP` request and
  interim response, and that hash becomes the KDF context: the signing,
  encryption, decryption and application keys are derived with the labels
  `SMBSigningKey`, `SMBC2SCipherKey`, `SMBS2CCipherKey` and `SMBAppKey` over the
  pre-auth hash, instead of 3.0's fixed `SmbSign`, `ServerIn ` and `ServerOut`
  contexts. Whether a request after the session setup *must* be signed is the
  signing policy's business, not the dialect's; what changes at 0x0311 is the key
  the signature is computed with, so a client that derived it from a different
  pre-auth hash than the server did cannot produce an acceptable one.
- **AES-128-GCM.** The `SMB2_TRANSFORM_HEADER` cipher chosen by the encryption
  context, with the 12-byte GCM nonce ([MS-SMB2] 3.1.4.3).
- **Compression.** `SMB2_COMPRESSION_CAPABILITIES`, whose `Flags` field is
  `SMB2_COMPRESSION_CAPABILITIES_FLAG_NONE` unless chained compression is on
  offer, and the
  `SMB2_COMPRESSION_TRANSFORM_HEADER` of [MS-SMB2] 2.2.42: a request that
  arrives compressed with any algorithm the server advertised must be
  decompressed and answered exactly as the same request would be answered
  uncompressed. The algorithms are those of [MS-XCA]: `LZNT1` (1), `LZ77` (2),
  `LZ77+Huffman` (3) and `Pattern_V1` (4).

### What must keep working

Every earlier dialect is still served by the same process, chosen by what the
client negotiates and bounded by `server min protocol` / `server max protocol`:
0x0302 and 0x0300 with AES-128-CMAC signing and their 3.0 key derivation,
0x0210 and 0x0202 with HMAC-SHA256 signing and the session key itself, and
`NT LM 0.12` with the SMB1 command set. A 0x0300 client must still derive the
same keys it derived before -- the 3.1.1 derivation applies only at 0x0311.

Everything the earlier milestones required at those dialects -- the SMB 3.0 and
NT LM 0.12 conformance tests, and the field-for-field match with Samba 4.17.12
in 3.0.2, 3.0 and 2.1 exchanges and the SMB1 `smbclient` sessions -- must still
hold.

**Conformance.** Samba's `smbtorture` publishes the conformance suites; the
target is the tests in them that a Samba 4.17.12 file server itself passes, and
the service's answers to direct SMB exchanges and scripted `smbclient` sessions
must match what a real Samba 4.17.12 file server answers. Samba 4.17 does not
negotiate compression, so for compression the requirement is that the
compressed form of a request produces the same answer as its uncompressed form,
and that the compression context carries the right algorithm list.

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

**Normative semantics:** `/app/SPEC/MS-SMB2.txt` (negotiate contexts, the
pre-auth chain, the transform and compression transform headers, the KDF) and
`/app/SPEC/MS-XCA.txt` (the compression algorithms; its provenance is in
`/app/SPEC/PROVENANCE-MS-XCA.md`), plus `/app/SPEC/MS-CIFS.txt`,
`/app/SPEC/MS-SMB.txt`, `/app/SPEC/MS-NLMP.txt`, `/app/SPEC/MS-SPNG.txt`,
`/app/SPEC/MS-FSCC.txt`, `/app/SPEC/MS-DTYP.txt` and `/app/SPEC/MS-ERREF.txt`.

**Standards those documents cite (local copies):** RFC 4178, the SPNEGO protocol
[MS-SPNG] extends, at `/opt/docs/ietf/rfc4178.txt`; RFC 2743 (GSS-API, including
the initial-token framing SPNEGO uses) at `/opt/docs/ietf/rfc2743.txt`; RFC 4493
(AES-CMAC) at `/opt/docs/ietf/rfc4493.txt`; NIST SP 800-108 (October 2009), the
KDF [MS-SMB2] 3.1.4.2 cites, at `/opt/docs/nist/sp800-108-2009.pdf` with a text
edition `/opt/docs/nist/sp800-108-2009.txt`; The Open Group's DCE 1.1: Remote
Procedure Call (C706), which [MS-RPCE] extends -- chapter 4 (IDL), chapter 12
(RPC PDU encodings), chapter 14 (NDR) and appendix N (IDL data type
declarations) -- under `/opt/docs/opengroup-c706/`; RFC 4543 (GMAC) at
`/opt/docs/ietf/rfc4543.txt`. `/opt/docs/SOURCES.txt` records where each came
from.

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

# Dialect and security policy

## 1. Implement this milestone

The file service in this workspace speaks every SMB dialect from `NT LM 0.12`
through SMB 3.1.1. This milestone is about **what it offers, and to whom** --
the `smb.conf(5)` policy surface, and the behaviour a Windows client expects
when a policy says no.

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

### The policies

- **SMB1 is off unless it is asked for.** `server min protocol` and
  `server max protocol` bound what is offered, and `server min protocol`'s
  default is `SMB2_02` -- the value Samba has shipped since 4.11. With the
  default in force an SMB1 `SMB_COM_NEGOTIATE` offering only pre-SMB2 dialects
  is answered with `DialectIndex` 0xFFFF; with `server min protocol = NT1` the
  same request is answered with the full `NT LM 0.12` negotiate response. Every
  dialect in between behaves the same way: a client is given the highest dialect
  inside the configured range, and nothing outside it.
- **Signing policy.** `server signing` = `auto` / `mandatory` / `disabled`.
  `mandatory` sets `SMB2_NEGOTIATE_SIGNING_REQUIRED` in the `NEGOTIATE`
  response's `SecurityMode` and refuses an unsigned request with
  `STATUS_ACCESS_DENIED`. The `SESSION_SETUP` itself is **not** refused for a
  client that advertised only `SMB2_NEGOTIATE_SIGNING_ENABLED`: the session is
  established and the refusal comes on the first request that arrives unsigned,
  which at 0x0210 is the `TREE_CONNECT`. `auto` advertises signing as
  enabled and accepts both signed and unsigned. `disabled` is a third accepted
  value whose effect on this server is not observable on the wire -- it still
  advertises `SMB2_NEGOTIATE_SIGNING_ENABLED` and still accepts both -- so match
  the reference server rather than reasoning from the parameter's name. At 0x0311
  a session that established a signing key signs everything after the session
  setup whatever the policy says.
- **Encryption policy.** `smb encrypt` = `off` / `if_required` / `desired` /
  `required`, globally and per share, with the per-share value overriding the
  global one for that share. A share whose policy is `required` reports
  `SMB2_SHAREFLAG_ENCRYPT_DATA` on `TREE_CONNECT`, every message on that tree is
  wrapped in an `SMB2_TRANSFORM_HEADER`, and a client that did not negotiate
  encryption is refused. When the *global* policy is `required` the session
  itself carries `SMB2_SESSION_FLAG_ENCRYPT_DATA`, every share behaves as though
  it were `required`, and a client that cannot encrypt is not merely refused a
  tree connect -- the reference server tears the session down.
- **Accounts.** `map to guest` = `never` / `bad user`: with `never` an unknown
  account is `STATUS_LOGON_FAILURE`; with `bad user` it becomes a guest session,
  reported with `SMB2_SESSION_FLAG_IS_GUEST`. A known account with the wrong
  password is `STATUS_LOGON_FAILURE` under either policy. A session setup that
  carries an empty user name and an empty password follows the same rule rather
  than becoming a null session: `STATUS_LOGON_FAILURE` under `never`, and under
  `bad user` it succeeds as a guest, with `SMB2_SESSION_FLAG_IS_GUEST` set and
  `SMB2_SESSION_FLAG_IS_NULL` clear.
- **Share policy.** `read only` and `guest ok` per share, with the maximal
  access the `TREE_CONNECT` response reports following from them.

### The client quirks the suite encodes

Samba's `smbtorture` carries the behaviours real Windows clients depend on --
among them the error forms of [MS-CIFS] 2.2.3.2, the multi-protocol negotiate
that reverts an SMB1 connection to SMB2, credit accounting across a session, the
handling of an invalid session or tree identifier, and the file-identifier
limits. The service must behave on each of them as the suite expects.

### What must keep working

Under a configuration that allows it, every dialect still behaves as it did:
`NT LM 0.12` with the SMB1 command set, 0x0202 and 0x0210 with HMAC-SHA256
signing, 0x0300 and 0x0302 with AES-128-CMAC and AES-128-CCM, and 0x0311 with
its pre-auth-bound keys and AES-128-GCM.

Under a configuration that allows SMB1, everything the earlier milestones
required of the SMB1 surface -- its conformance tests and its behaviour under
`smbclient` -- must still hold, alongside the policy surface itself.

**Conformance.** Under each configuration of the policy surface, the service's
answers must match, field for field, what a real Samba 4.17.12 file server
answers under the identical configuration, and the `smbtorture` tests that
Samba 4.17.12 passes there must pass as well.

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

**Normative semantics:** `/app/SPEC/MS-SMB2.txt`, `/app/SPEC/MS-CIFS.txt`,
`/app/SPEC/MS-SMB.txt`, `/app/SPEC/MS-NLMP.txt`, `/app/SPEC/MS-SPNG.txt`,
`/app/SPEC/MS-FSCC.txt`, `/app/SPEC/MS-DTYP.txt`, `/app/SPEC/MS-ERREF.txt`.

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

**The policy surface:** `/app/SPEC/samba-man/smb.conf.5.txt` -- the authority for
every parameter named above -- with `/app/SPEC/SERVICE_INTERFACE.md`,
`/app/SPEC/samba-man/smbd.8.txt`, `/app/SPEC/samba-man/smbpasswd.5.txt` and the
configuration in `/app/SPEC/conf/`.

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

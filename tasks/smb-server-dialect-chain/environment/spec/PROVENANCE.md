# Provenance of the documents in this directory

Every file here is Microsoft Open Specification text, retrieved unmodified on
2026-09-12 from the Microsoft Learn Open Specifications CDN and converted to
plain text with `pdftotext -layout` (poppler-utils 22.12.0, Debian bookworm).
Nothing in these files was written or edited by the task author.

Download URL pattern:
  https://winprotocoldocs-bhdugrdyduf5h2e4.b02.azurefd.net/<DOC>/%5b<DOC>%5d.pdf

| Document | Revision | Release | sha256 of the source PDF |
| --- | --- | --- | --- |
| MS-CIFS | v20251121 | November 21, 2025 | e9be193162f69930f349169beb45695deadeb956a8c99584c04ee4e3e9ba5a90 |
| MS-SMB | v20260114 | January 14, 2026 | ddb5ca3d8dedef8ad9b566595b1b63efdc127fa59d38e859fa5f3a38e8bb8437 |
| MS-NLMP | v20260330 | March 30, 2026 | 00fe14a058134aabf6a6db1bcc1c47b8d45f0656ef7de610c03712906a945a27 |
| MS-SPNG | v20260714 | July 14, 2026 | 725c875f450d15bfa4343a00c06881f43ab40af9c7b1e701f5d85a6f5e568669 |
| MS-DTYP | v20241119 | November 19, 2024 | 0f4a3744c74ebf69212e6c45f9adf182a0cfc7beacf33fa402b1f8d233ca83b3 |
| MS-FSCC | v20251121 | November 21, 2025 | 119d996aff0a1c877cfa476ac8959f91620d73ecaa3a53f9e5dc9ade374320db |
| MS-ERREF | v20241119 | November 19, 2024 | 2b1f552b0af0fb034add7f0fb498bb5a351393714c09da0a0f063684a62fe00a |

| File | Title |
| --- | --- |
| `MS-CIFS.txt` | Common Internet File System (CIFS) Protocol |
| `MS-SMB.txt` | Server Message Block (SMB) Protocol |
| `MS-NLMP.txt` | NT LAN Manager (NTLM) Authentication Protocol |
| `MS-SPNG.txt` | Simple and Protected GSS-API Negotiation Mechanism (SPNEGO) Extension |
| `MS-DTYP.txt` | Windows Data Types (security descriptors, ACLs, SIDs, FILETIME) |
| `MS-FSCC.txt` | File System Control Codes (information classes, FSCTLs) |
| `MS-ERREF.txt` | Windows Error Codes (NTSTATUS) |

## Samba manual pages -- `samba-man/`

The companion documents for the service's start command, configuration file,
account database and the reference client. Retrieved on 2026-09-12 from
<https://www.samba.org/samba/docs/current/man-html/> and rendered to plain text
with `html2text -width 100 -nobs`. `docs/current` is the current stable release
line, which is not necessarily 2:4.17.12+dfsg-0+deb12u4, the version this
container's `smbclient` and `smbtorture` come from: Debian's package strips
manual pages, so the release's own pages are not installable here. Prose may
therefore describe a later release in detail.

`smb.conf-defaults.txt` closes that gap for the part that matters most. It is
every `smb.conf(5)` parameter and the default value with `testparm -v` read out
of the **4.17.12-Debian build itself**, so where the manual page and the
reference build disagree about a default, this file is the build. Where either
document and the running reference server disagree about behaviour, the server
is authoritative.

| File | Manual page |
| --- | --- |
| `samba-man/smbd.8.txt` | `smbd(8)` -- the server's command line |
| `samba-man/smb.conf.5.txt` | `smb.conf(5)` -- every configuration parameter |
| `samba-man/smbpasswd.5.txt` | `smbpasswd(5)` -- the account database file format |
| `samba-man/smbclient.1.txt` | `smbclient(1)` -- the reference client |

## Author-written files in this directory

`SERVICE_INTERFACE.md` and `conf/` are written by the task author. They state
how the service is started and configured -- the `smbd(8)` option list, the
`smb.conf(5)` parameters in play and the `smbpasswd(5)` account file -- and add
nothing to the protocol itself. Every name in them comes from the Samba manual
pages in `samba-man/`.

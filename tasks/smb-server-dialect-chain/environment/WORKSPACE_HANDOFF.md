# Workspace handoff

This file is the running handover note for `/app`. Every session that works here
reads it first and rewrites it before finishing, so that the next session -- which
has seen none of this one's conversation -- can pick the work up from the
workspace alone.

## What is in this container

| Path | What it is |
| --- | --- |
| `/app` | the workspace; everything you create lives here and is carried forward |
| `/app/SPEC/` | normative documents, unmodified vendor text (see `SPEC/PROVENANCE.md`) |
| `/app/SPEC/samba-man/` | `smbd(8)`, `smb.conf(5)`, `smbpasswd(5)`, `smbclient(1)` manual pages |
| `/app/SPEC/samba-man/smb.conf-defaults.txt` | every `smb.conf(5)` parameter and the default the reference build compiles in |
| `/app/SPEC/conf/` | a representative `smb.conf` and `smbpasswd`, in the shape the service is started with |
| `/app/timer.sh` | advisory countdown for the current session's budget |
| `node`, `npm` | Node.js 20 LTS at `/usr/local/bin` |
| `smbclient`, `smbtorture` | Samba 4.17.12 client and test tooling, for driving whatever you build |
| `python3`, `git`, `rg`, `file`, `curl` | ordinary development tooling |

There is no SMB **server** in this image: `smbd` is not installed, and the
service cannot rely on one. The Samba *client* tooling is here, and the
service you build is the only thing that will answer the port. Shipping,
wrapping or invoking an SMB server as part of the service is not allowed.

## Handover contents

Rewrite the sections below before you finish.

### State of the service

*(nothing has been built yet)*

### How to build and start it

*(not yet defined)*

### How to check it

*(not yet defined)*

### Known gaps

*(none recorded yet)*

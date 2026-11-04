#!/usr/bin/env bash
# Reference implementation of the SMB1/CIFS milestone.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p /app/src /app/bin
cp "$here"/src/*.js /app/src/
cp "$here"/bin/smbd /app/bin/smbd
chmod +x /app/bin/smbd
for f in /app/src/*.js; do node --check "$f"; done
cat > /app/WORKSPACE_HANDOFF.md <<'HANDOFF'
# Workspace handoff

## State of the service

A CIFS file server speaking the NT LM 0.12 dialect, in `/app/src`, started
through `/app/bin/smbd`. Modules: `main.js` (the `smbd(8)` command line),
`server.js` (transport and connection state), `smbconf.js` / `passdb.js`
(`smb.conf(5)` and `smbpasswd(5)` parsing), `ntlm.js` + `asn1.js` + `crypt.js`
(SPNEGO/NTLMv2, with MD4, RC4, AES-CMAC and the SP800-108 KDF written by hand),
`vfs.js` + `fileops.js` (name resolution, opens, share modes, delete-on-close,
DOS attributes, byte-range locks), `info.js` ([MS-FSCC] information classes) and
`smb1.js` (the SMB1 command set, TRANS2 and NT_TRANSACT).

## How to build and start it

Nothing is compiled. Start it exactly as `smbd(8)` is started:

    /app/bin/smbd --foreground --no-process-group --debug-stdout \
                  --debuglevel=1 --configfile=<path> --port=<n>

## How to check it

    smbclient //127.0.0.1/tmp -U smbuser%'smbpass1!' -p PORT \
      --option='client min protocol=NT1' --option='client max protocol=NT1' -c ls
    smbtorture //127.0.0.1/tmp -U smbuser%'smbpass1!' -p PORT --target=samba3 \
      --option='client min protocol=NT1' --option='client max protocol=NT1' base.delete

## Known gaps

`SMB_COM_SEARCH`/`SMB_COM_FIND` (the pre-NT core search commands) and the DOS
`OPEN_ANDX` deny modes are not implemented, and there is no NT access check
against a per-file security descriptor: `NT_TRANSACT_QUERY_SECURITY_DESC`
returns a fixed descriptor.
HANDOFF

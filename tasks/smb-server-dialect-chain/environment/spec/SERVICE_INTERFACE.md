# Service interface

This document is the contract between the file service in this workspace and
anything that drives it. It is frozen: nothing in it changes.

## 1. The start command -- `smbd(8)`

The build produces an executable at **`/app/bin/smbd`**. It is started with
Samba's own `smbd(8)` options and no others:

```
/app/bin/smbd --foreground --no-process-group --debug-stdout \
              --debuglevel=<n> --configfile=<path> --port=<n>
```

`smbd(8)` (see `/app/SPEC/samba-man/smbd.8.txt`) defines all of them:

| Option | Meaning |
| --- | --- |
| `-F`, `--foreground` | run in the foreground, do not fork into the background |
| `--no-process-group` | do not create a new process group |
| `-S`, `--debug-stdout` | send debug output to standard output |
| `-d`, `--debuglevel=DEBUGLEVEL` | debug verbosity |
| `-s`, `--configfile=CONFIGFILE` | use this configuration file |
| `-p`, `--port=STRING` | listen on this space- or comma-separated port list |
| `-l`, `--log-basename=LOGFILEBASE` | base directory for log files |
| `--option=name=value` | set one `smb.conf` parameter from the command line |
| `-D`/`--daemon`, `-i`/`--interactive`, `-V`/`--version`, `-b`/`--build-options` | as `smbd(8)` documents them |

The process must stay in the foreground, accept TCP connections on every port
it was given, and exit on `SIGTERM`.

## 2. The configuration file -- `smb.conf(5)`

The service is configured only through an `smb.conf` in Samba's own format
(`/app/SPEC/samba-man/smb.conf.5.txt`): `[global]` and one section per share,
`key = value` lines, `#`/`;` comments, parameter names compared
case-insensitively and ignoring internal spaces.

A configuration may contain any `smb.conf(5)` parameter; unknown or irrelevant
parameters are ignored, exactly as Samba ignores them. `smb.conf(5)` itself --
`/app/SPEC/samba-man/smb.conf.5.txt` -- is the normative description of what each
parameter does, and `/app/SPEC/samba-man/smb.conf-defaults.txt` is every
parameter with the default the reference build compiles in. A configuration the
service is started with may set any parameter those documents describe; there is
no shorter list to work from, and a parameter this service does not understand
must be ignored rather than refused.

A representative configuration, in the exact shape the service will be started
with, is in `conf/smb.conf` next to this file. The paths, the port and the
protocol range in it are examples: they are supplied afresh every time the
service is started, and must be read from the configuration file rather than
assumed.

## 2a. Resource ceilings

The service runs inside a container with 7 CPUs and 16 GiB of memory, under a
memory ceiling: if the resident memory of the service and everything below it
passes **4096 MiB**, the process group is stopped and whatever it was serving
fails. A correct implementation of this service needs about two orders of
magnitude less than that, so the ceiling is a guard against a runaway, not a
budget to design against. No other limit applies to the service's resource
use.

## 3. The account database -- `smbpasswd(5)`

`passdb backend = smbpasswd:<path>` names a file in Samba's `smbpasswd(5)`
format (`/app/SPEC/samba-man/smbpasswd.5.txt`): one colon-separated line per
account,

```
name:uid:LANMAN hash:NT hash:[account flags]:LCT-<hex seconds>:
```

with the hashes as 32 hex digits, `XXXX...` in the LANMAN field meaning "no
LANMAN hash stored", and `[U          ]` an ordinary enabled user account. The
NT hash is the MD4 of the UTF-16LE password, as `smbpasswd(5)` describes. An
example file is in `conf/smbpasswd` next to this document.

## 4. What drives the service

Samba 4.17.12's own clients, installed in this container:

- `smbtorture` -- the protocol conformance suite. Its families are named for the
  dialect they drive: `base.*` runs over SMB1/CIFS, `smb2.*` over SMB2 and SMB3,
  and the `--option='client min protocol=...'` / `max protocol` pair decides what
  the client will negotiate. Which families a given milestone exercises is
  that milestone's own business; the invocation is always of this shape:
  `smbtorture //127.0.0.1/tmp -U user%password -p PORT --target=samba3
  --option='client min protocol=NT1' --option='client max protocol=NT1' base.<test>`
- `smbclient` -- the reference command-line client.
- and direct SMB exchanges on the loopback port.

Nothing else is added: there is no extra flag, environment variable, control
socket, status file or marker of any kind.

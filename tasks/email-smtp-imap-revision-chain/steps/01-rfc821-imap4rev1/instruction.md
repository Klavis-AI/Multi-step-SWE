# Step 1 — RFC 821/822 MTA and IMAP4rev1 mailbox

## Step time budget

This step has a two-hour (7,200-second) implementation budget. Check the
remaining budget with `bash /app/timer.sh` while working. The time limit is enforced; the timer is advisory and resets for each step.

## What to build

Implement, in **C++**, a mail stack whose *only* entry points are:

1. **Postfix 3.8.6** `postfix(1)` + `main.cf`/`master.cf` (see
   `/app/etc/postfix/`; the Postfix 3.8.6 manual pages, including `postfix(1)`
   and `postconf(5)`, are in `/opt/docs/postfix-3.8.6/`) speaking **SMTP on the wire** (RFC 821, with RFC 822
   messages) on loopback.
2. **Dovecot 2.3.21** `dovecot(1)` + `dovecot.conf` (see
   `/app/etc/dovecot/`; the Dovecot 2.3.21 documentation, including
   `dovecot(1)`, its wiki and the commented example configuration, is in
   `/opt/docs/dovecot-2.3.21/`) speaking **IMAP4rev1 on the wire** (RFC 3501) on
   loopback, over a **Maildir** store (cr.yp.to Maildir + Dovecot Maildir++
   folders and `dovecot-uidlist` version 3).

Do not wrap, exec, or reverse-proxy Postfix, Dovecot, Cyrus, Exim, OpenSMTPD,
Courier, Stalwart, Apache James, maddy, Haraka, or sendmail. The processes
that listen on the SMTP and IMAP ports must be ELF binaries produced from C++
sources under `/app`, dynamically linked against the C++ standard library
(`libstdc++` or `libc++`).

Normative texts are in `/app/SPEC/` (`rfc821.txt`, `rfc822.txt`, `rfc3501.txt`,
`rfc974.txt`, `maildir.html`). Configuration files under `/app/etc/` are part
of the interface: honor the keys they contain.

Do not cheat by using online solutions or hints specific to this task.

## Hooks (where, not what)

Where the interface knobs live:

| What | Where |
| --- | --- |
| Bind address | Postfix `inet_interfaces`, Dovecot `listen` / `inet_listener` `address` (`127.0.0.1`) |
| Listen ports | `master.cf` first field (decimal TCP port) and Dovecot `inet_listener` `port` (SMTP **2525**, IMAP **2143** in the shipped files) |
| Queue root | `queue_directory` = `/app/var/spool/postfix` |
| Maildir root | `virtual_mailbox_base` = `/app/var/mail`; Dovecot user `home` + `mail_location = maildir:~/Maildir` |
| TLS cert/key | `smtpd_tls_cert_file` / `smtpd_tls_key_file` and Dovecot `ssl_cert` / `ssl_key` at `/app/etc/ssl/server.certificate` and `/app/etc/ssl/server.private` |
| Frozen clock | environment `SOURCE_DATE_EPOCH` (Unix time); use it for `Date`, INTERNALDATE, and unique Maildir names when set |

No `--mode`, `--enable-*`, version banners, or marker headers.

## Start, stop, flush

Install:

- `/app/sbin/postfix`
- `/app/sbin/dovecot`

Build from `/app` with:

```sh
cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /app/build -j"$(nproc)"
```

This build and both servers must work when run as an unprivileged user that
owns `/app`; neither may need root.

The controller understands real `postfix(1)` verbs with `-c /app/etc/postfix`:

- `/app/sbin/postfix -c /app/etc/postfix check` — config is readable
- `/app/sbin/postfix -c /app/etc/postfix start-fg` — foreground master (Postfix ≥ 3.3); the verb used to start the stack
- `/app/sbin/postfix -c /app/etc/postfix start` / `stop` — daemonize using `{queue_directory}/pid/master.pid`
- `/app/sbin/postfix -c /app/etc/postfix flush` — force a deferred-queue run

IMAP:

```sh
/app/sbin/dovecot -F -c /app/etc/dovecot/dovecot.conf
```

(`-F` is foreground, Dovecot 2.3.)

## SMTP (RFC 821) on port 2525

Speak RFC 821 command/reply bytes, CRLF lines, 3-digit codes. The SMTP
greeting’s **code** is `220` (human-readable banner text is free-form).
Required commands:

- `HELO <domain>` — `250`; empty HELO is `501`
- `MAIL FROM:<reverse-path>` including the null path `<>` and RFC 821 source
  routes `@host,@host:user@domain`
- `RCPT TO:<forward-path>` including source routes
- `DATA` … `<CRLF>.<CRLF>` with dot-stuffing
- `RSET`, `NOOP`, `QUIT`
- `VRFY` — enabled (`disable_vrfy_command = no`): known local mailbox `252`,
  unknown `550` (Postfix 3.8.6 `smtpd` recorded behavior)
- Unrecognized commands `500` (this includes `HELP` and `EXPN`; they are not
  implemented and MUST NOT return `214` or `502`)

Sequence errors: nested `MAIL` is `503`; `RCPT`/`DATA` without `MAIL` is
`503`; `DATA` with no valid recipients is `554`.

`smtpd_reject_unlisted_recipient = yes`: `RCPT` to an address in
`virtual_mailbox_domains` that is **not** in `virtual_mailbox_maps`
(`texthash:/app/etc/postfix/vmailbox`) is `550`.

Local mail uses **local virtual delivery** into
`{virtual_mailbox_base}/{vmailbox map value}` as Maildir. After `DATA`, the
message MUST appear under that mailbox’s `new/` or `cur/` (not remain only in
`tmp/`). Rewrite unqualified envelope locals with `myorigin` when the path has
no `@`. `queue_directory` from `main.cf` is the Postfix queue root the
controller uses (`incoming` / `deferred` / `bounce` / `active`). Honor
`transport_maps` as a Postfix `texthash:` map. Recipients whose domain is
neither `mydestination` nor `virtual_mailbox_domains` are routed by MX
lookup (RFC 974); if MX lookup fails, generate a bounce to the reverse-path
mailbox (Postfix “Returned mail” to the envelope sender). Recipients routed
by `transport_maps` to an SMTP nexthop that replies `4xx` are **deferred**
under `queue_directory/deferred`; `/app/sbin/postfix -c /app/etc/postfix flush`
retries them (Postfix `flush` / SIGUSR1).

RFC 822: accept folded header fields on input (a folded `Subject` still
delivers). Reply **text** after the 3-digit SMTP code is free-form.

## IMAP4rev1 (RFC 3501) on port 2143

Greeting then tagged commands. Authenticate `LOGIN` against the Dovecot
passwd-file `/app/etc/dovecot/users` (`{PLAIN}` secrets). Mailbox `INBOX` is
`{home}/Maildir`; other names use Maildir++ (`.` + namespace `separator` `/`
mapped to dots) as Dovecot 2.3.21 does.

These IMAP commands MUST complete with the tagged `OK`/`NO`/`BAD` status
Dovecot 2.3.21 produces for the same session (what must match is the tagged
status, not untagged banner text): `CAPABILITY` (untagged data MUST include
`IMAP4rev1`), `NOOP`, `LOGOUT`, `LOGIN`, `SELECT`, `EXAMINE`, `CREATE`,
`DELETE`, `RENAME`, `LIST`, `LSUB`, `STATUS`, `SUBSCRIBE`, `UNSUBSCRIBE`,
`APPEND` (synchronizing literals `{n}` / `+`), `CHECK`, `CLOSE`, `EXPUNGE`,
`FETCH`, `STORE`, `COPY`, `SEARCH`, and the `UID` prefix for
`FETCH`/`STORE`/`COPY`/`SEARCH`.

`FETCH 1` (or any sequence of concrete message numbers that matches no
message) on an empty mailbox is tagged `BAD`. A set that uses `*` (`1:*`,
`*`) on an empty mailbox is a valid empty set and tagged `OK` (Dovecot
2.3.21). `CREATE INBOX` is tagged `NO`. `SELECT`/`FETCH` before `LOGIN` are
tagged `BAD`. Mailboxes use Maildir++ under the user’s `home` (`INBOX` is
`{home}/Maildir`).

Users in the shipped passwd-file: `alice` / `alice-secret`, `bob` /
`bob-secret`, `carol` / `carol-secret`, matching `alice@example.test` etc. in
`vmailbox`.

## Core requirements

The stack must build from the in-tree C++ sources, its listeners must answer,
and it must not wrap a stock MTA/IMAP. Refusals (SMTP `4xx`/`5xx` replies, IMAP
tagged `NO` or `BAD`) are required wherever they are stated, just as the
successful replies are; a stack that accepts every command does not meet these
requirements.

In particular, these must hold:

- SMTP `HELO` → `250`
- `RCPT TO:<bob@example.test>` after `MAIL FROM:<alice@example.test>` → `250`
- `RCPT TO:<nobody@example.test>` → `550`
- a `DATA` transaction is accepted (`354` then `250`) and the message appears
  under Bob’s Maildir `new/` or `cur/` (not left in `tmp/`)
- IMAP `LOGIN` with `bob` / `bob-secret` → tagged `OK`
- `SELECT INBOX` → tagged `OK`
- `APPEND` of a small RFC 822 message to `INBOX` then `FETCH 1` → tagged `OK`

The rest of the RFC 821 session behaviour (malformed commands, source routes,
VRFY, RSET, folding, dot-stuffing, multi-recipient) and the IMAP4rev1 command
matrix (LIST/CREATE/STATUS/SEARCH/STORE/UID; `FETCH 1` on empty → `BAD`,
`UID FETCH 1:*` on empty → `OK`) must hold as well. For SMTP, what must match
is the 3-digit reply code plus, for `DATA`, the message landing in Maildir
`new/`/`cur/`; for IMAP, the tagged `OK`/`NO`/`BAD` status and the
`IMAP4rev1` capability token.

## Checking the stack

No test suite ships with the workspace, so check this behaviour yourself on
the wire: build, start the services, and drive the ports this ticket lists with
a client of your own (the image has `python3`, `curl`, and `openssl`). Compare
the 3-digit SMTP replies and the tagged IMAP statuses you get back with the
values stated above; every value this ticket names is observable that way. A
delivery is done when the message file is under the recipient's Maildir `new/`
or `cur/`, not `tmp/`.

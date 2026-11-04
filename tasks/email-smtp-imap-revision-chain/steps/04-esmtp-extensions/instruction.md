# Step 4 — ESMTP extensions, STARTTLS, AUTH SCRAM

## Step time budget

This step has a two-hour (7,200-second) implementation budget. Check the
remaining budget with `bash /app/timer.sh` while working. The time limit is enforced; the timer is advisory and resets for each step.

## Interface (unchanged)

The only entry points remain:

- Postfix 3.8.6 (manual pages: `/opt/docs/postfix-3.8.6/`)
  `/app/sbin/postfix -c /app/etc/postfix` (`check`, `start-fg`, `start`, `stop`, `flush`)
  with SMTP on the `master.cf` inet ports (2525, and 2587 when that service is listed).
- Dovecot 2.3.21 (documentation: `/opt/docs/dovecot-2.3.21/`)
  `/app/sbin/dovecot -F -c /app/etc/dovecot/dovecot.conf` with IMAP on 2143.
- Maildir under `/app/var/mail`. Hooks are still only bind address, those ports,
  `queue_directory`, Maildir root, cert/key paths, and `SOURCE_DATE_EPOCH`.

**Liveness requirement:** Because `/app/SPEC/rfc6409.txt` is present in this
step, TCP port **2587** must accept connections within **20 seconds** of
starting the stack, in addition to 2525 and 2143. All three ports must be
bound within that window.

Build from C++ under `/app`:

```sh
cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /app/build -j"$(nproc)"
```

This build and both servers must work when run as an unprivileged user that
owns `/app`; neither may need root.

Binaries: `/app/sbin/postfix` and `/app/sbin/dovecot`, ELF executables built from
your C++ sources and dynamically linked against the C++ standard library
(`libstdc++` or `libc++`). Do not wrap a stock MTA/IMAP.

Do not cheat by using online solutions or hints specific to this task.

## Inherited behavior (restated)

RFC 821-era SMTP on 2525 and IMAP4rev1 on 2143 with the shipped users and
`virtual_mailbox_maps` MUST keep working except where this revision **flips**
a rule (those flips are stated below). 7-bit SMTP (`HELO` / `MAIL` / `RCPT` /
`DATA` with ASCII) and IMAP4rev1 `LOGIN`/`SELECT`/`FETCH` remain required.

Keep these already-stated details (they are still required):

- Unrecognized SMTP verbs, including `HELP` and `EXPN`, are `500`.
- `strict_rfc821_envelopes = no`: a space after the colon in `MAIL FROM:` /
  `RCPT TO:` is `250`.
- Unqualified envelope locals (no `@`) are rewritten with `myorigin` (`250`).
- From RFC 2821 onward, source routes (`@host:user@domain`) are `501`.
- `FETCH 1` on empty is `BAD`; `UID FETCH 1:*` / `UID STORE 1:*` on empty is `OK`.

Normative texts already in `/app/SPEC/` plus the new files this ticket adds
under `/app/SPEC/` (rfc1652.txt, rfc1870.txt, rfc2920.txt, rfc3461.txt, rfc3463.txt, rfc3030.txt, rfc3207.txt, rfc4954.txt, rfc5802.txt).

### Earlier requirements still in force

Everything below must still hold exactly as before; nothing in this list
changes in this revision:

- RFC 2821/2822 revision: `EHLO` is `250`; source routes
  (`@host:user@domain`, `@host,@host:user@domain`) are `501`; folded header
  fields are accepted on message input; a space after the colon and
  unqualified locals (rewritten with `myorigin`) stay `250`.
- RFC 6409 submission, on the `master.cf` service whose first field is
  **2587**: `MAIL` with no prior `EHLO`, or after `HELO` only, is `503`;
  `EHLO` then `MAIL` is `250`; `RSET` keeps the `EHLO` state and a later
  `HELO` clears it; `NOOP`/`QUIT` without `EHLO` are `250`/`221`; `RCPT`
  without `MAIL` and nested `MAIL` are `503`; `DATA` with no valid recipient
  is `554`; an unknown local `RCPT` is `550`; `VRFY` of bob is `252`;
  `HELP`/`EXPN` are `500`; `EHLO` advertises `SIZE`; a `DATA` transaction to
  bob is delivered. On 2525, `MAIL` with no `HELO`/`EHLO` stays `250`.

## New work

Advertise and implement, matching Postfix 3.8.6 `smtpd` recorded EHLO:

- Keywords `8BITMIME`, `PIPELINING`, `SIZE`, `DSN`, `CHUNKING`, `STARTTLS`,
  `AUTH` with `PLAIN`/`LOGIN`/`SCRAM-SHA-1`.
- `BDAT <n> LAST` (RFC 3030) queues the chunk and delivers like `DATA`.
- `STARTTLS` replies `220` then completes a TLS handshake on the hooked
  `smtpd_tls_cert_file` / `smtpd_tls_key_file`; a following `EHLO` is `250`
  and MUST NOT re-advertise `STARTTLS`. `STARTTLS` after `MAIL` is `503`.
- `AUTH PLAIN` success `235` / failure `535`. `AUTH SCRAM-SHA-1` (RFC 5802)
  completes with `235` for alice / alice-secret.
- `MAIL FROM` `SIZE=` / `BODY=8BITMIME` / `RET=` / `ENVID=` and `RCPT TO`
  `NOTIFY=` / `ORCPT=` are accepted (`250`).

## Core requirements

The stack must build from the in-tree C++ sources, its listeners must answer,
and it must not wrap a stock MTA/IMAP. Every requirement from earlier steps
must still hold, except where this revision flips a rule. Refusals (SMTP
`4xx`/`5xx` replies, IMAP tagged `NO` or `BAD`) are required wherever they are
stated, just as the successful replies are; a stack that accepts every command
does not meet these requirements.

In particular, these must hold:

- `EHLO` advertises `8BITMIME`, `PIPELINING`, `SIZE`, `CHUNKING`, `STARTTLS`, `AUTH`
- `BDAT … LAST` to bob is `250` and the message is in Maildir
- `STARTTLS` then `EHLO` over TLS is `220` then `250`
- `AUTH PLAIN` alice / alice-secret → `235`
- `AUTH SCRAM-SHA-1` alice / alice-secret → `235`
- 7-bit `HELO` → `250`
- IMAP `LOGIN` → tagged `OK`

## Checking the stack

No test suite ships with the workspace, so check this behaviour yourself on
the wire: build, start the services, and drive the ports this ticket lists with
a client of your own (the image has `python3`, `curl`, and `openssl`). Compare
the 3-digit SMTP replies and the tagged IMAP statuses you get back with the
values stated above; every value this ticket names is observable that way. A
delivery is done when the message file is under the recipient's Maildir `new/`
or `cur/`, not `tmp/`.

# Step 3 — RFC 5321/5322 + RFC 6409 submission

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
under `/app/SPEC/` (rfc5321.txt, rfc5322.txt, rfc6409.txt).

### Earlier requirements still in force

Everything below must still hold exactly as before; nothing in this list
changes in this revision:

- RFC 2821/2822 revision: `EHLO` is `250`; source routes
  (`@host:user@domain`, `@host,@host:user@domain`) are `501`; folded header
  fields are accepted on message input; a space after the colon and
  unqualified locals (rewritten with `myorigin`) stay `250`.

## New work

Adopt RFC 5321/5322 and message submission (RFC 6409) on the `master.cf` inet
service whose first field is **2587**:

- The process started by `/app/sbin/postfix … start-fg` listens on **2587** as
  well as 2525.
- On 2587, this server's submission policy requires **`EHLO` before `MAIL`**. `MAIL`
  with no prior `EHLO` is `503`. `HELO` (not `EHLO`) then `MAIL` is also
  `503`. `EHLO` then `MAIL FROM:<alice@example.test>` is `250`.
- `RSET` after `EHLO` does not require a second `EHLO` before the next
  `MAIL`. A later `HELO` on 2587 clears the extended state, so `MAIL` is
  `503` until `EHLO` again.
- On 2587, `NOOP`/`QUIT` without `EHLO` remain `250`/`221`. `RCPT` without
  `MAIL` is `503`. Nested `MAIL` is `503`. `DATA` with no valid recipient is
  `554`. Unknown local `RCPT` is `550`. Source routes are `501`. Null reverse
  path `<>` is `250`. Space after the colon and unqualified `MAIL FROM:<alice>`
  remain `250`. `VRFY` of bob is `252`. `HELP`/`EXPN` are `500`.
- `EHLO` on 2587 advertises `SIZE`. A `DATA` transaction to
  `bob@example.test` delivers into Bob’s Maildir `new/` or `cur/`. Folded
  RFC 5322 header fields (a folded `Subject`) are accepted on input.
- Port **2525** still accepts `HELO` then `MAIL` without a prior `EHLO`, and —
  with the shipped `main.cf` `smtpd_helo_required = no` — also accepts
  `MAIL FROM:` with no `HELO`/`EHLO` at all (`250`); the RFC 6409 greeting
  requirement above applies only to 2587. IMAP4rev1 `LOGIN`/`SELECT`/`APPEND`
  then `FETCH 1` remain as already stated. Empty-mailbox `FETCH 1` is `BAD`;
  `UID FETCH 1:*` is `OK`.

## Core requirements

The stack must build from the in-tree C++ sources, its listeners must answer,
and it must not wrap a stock MTA/IMAP. Every requirement from earlier steps
must still hold, except where this revision flips a rule. Refusals (SMTP
`4xx`/`5xx` replies, IMAP tagged `NO` or `BAD`) are required wherever they are
stated, just as the successful replies are; a stack that accepts every command
does not meet these requirements.

In particular, these must hold:

- On 2587, `MAIL` with no `EHLO` → `503`
- On 2587, `HELO` then `MAIL` → `503`
- On 2587, `EHLO` then `MAIL FROM:<alice@example.test>` → `250`
- On 2587, `DATA` to bob is accepted and the message is in Maildir `new/`/`cur/`
- On 2525, `HELO` / `MAIL` / `RCPT` to bob → `250`
- IMAP `LOGIN` / `SELECT INBOX` → tagged `OK`
- `APPEND` then `FETCH 1` → tagged `OK`

## Checking the stack

No test suite ships with the workspace, so check this behaviour yourself on
the wire: build, start the services, and drive the ports this ticket lists with
a client of your own (the image has `python3`, `curl`, and `openssl`). Compare
the 3-digit SMTP replies and the tagged IMAP statuses you get back with the
values stated above; every value this ticket names is observable that way. A
delivery is done when the message file is under the recipient's Maildir `new/`
or `cur/`, not `tmp/`.

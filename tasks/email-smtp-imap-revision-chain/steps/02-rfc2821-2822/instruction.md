# Step 2 — RFC 2821/2822 revision

## Step time budget

This step has a two-hour (7,200-second) implementation budget. Check the
remaining budget with `bash /app/timer.sh` while working. The time limit is enforced; the timer is advisory and resets for each step.

## Interface (unchanged)

The only entry points remain:

- Postfix 3.8.6 (manual pages: `/opt/docs/postfix-3.8.6/`)
  `/app/sbin/postfix -c /app/etc/postfix` (`check`, `start-fg`, `start`, `stop`, `flush`)
  with SMTP on the `master.cf` inet port 2525 in the shipped file.
- Dovecot 2.3.21 (documentation: `/opt/docs/dovecot-2.3.21/`)
  `/app/sbin/dovecot -F -c /app/etc/dovecot/dovecot.conf` with IMAP on 2143.
- Maildir under `/app/var/mail`. Hooks are still only bind address, those ports,
  `queue_directory`, Maildir root, cert/key paths, and `SOURCE_DATE_EPOCH`.

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

Keep these already-stated envelope and IMAP details (they are still required):

- Unrecognized SMTP verbs, including `HELP` and `EXPN`, are `500` (not `214` or
  `502`).
- `strict_rfc821_envelopes = no` in `main.cf`: a single space after the colon
  in `MAIL FROM:` / `RCPT TO:` is accepted (`250`).
- Unqualified envelope locals (no `@`) are rewritten with `myorigin` (`250`).
- `FETCH 1` on an empty mailbox is `BAD`; `UID FETCH 1:*` / `UID STORE 1:*` on
  an empty mailbox is tagged `OK`.

Normative texts already in `/app/SPEC/` plus the new files this ticket adds
under `/app/SPEC/` (rfc2821.txt, rfc2822.txt).

## New work

Adopt RFC 2821/2822.

**Flip:** RFC 821 source routes (`@host:user@domain` and
`@host,@host:user@domain`), which RFC 821 required and RFC 2821 lets a server
decline, MUST now be rejected with `501`.

`EHLO` is defined and MUST be accepted (`250`). Obsolete RFC 822 syntax
remains accepted on **message** input (folded headers). Envelope syntax stays
the Postfix 3.8.6 `smtpd` behavior selected by the shipped `main.cf` (space
after colon allowed; unqualified locals rewritten with `myorigin`); do not
tighten those to a 501 just because 2821’s ABNF is stricter.

## Core requirements

The stack must build from the in-tree C++ sources, its listeners must answer,
and it must not wrap a stock MTA/IMAP. Every requirement from earlier steps
must still hold, except where this revision flips a rule. Refusals (SMTP
`4xx`/`5xx` replies, IMAP tagged `NO` or `BAD`) are required wherever they are
stated, just as the successful replies are; a stack that accepts every command
does not meet these requirements.

The 7-bit SMTP and IMAP4rev1 behaviour restated above and the new-revision
behaviour in this ticket, including every stated flip, must all hold.

## Checking the stack

No test suite ships with the workspace, so check this behaviour yourself on
the wire: build, start the services, and drive the ports this ticket lists with
a client of your own (the image has `python3`, `curl`, and `openssl`). Compare
the 3-digit SMTP replies and the tagged IMAP statuses you get back with the
values stated above; every value this ticket names is observable that way. A
delivery is done when the message file is under the recipient's Maildir `new/`
or `cur/`, not `tmp/`.

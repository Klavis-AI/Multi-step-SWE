# Step 7 — SPF, DKIM, DMARC

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
under `/app/SPEC/` (rfc7208.txt, rfc6376.txt, rfc7489.txt).

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
- ESMTP extensions: `EHLO` advertises `8BITMIME`, `PIPELINING`, `SIZE`,
  `DSN`, `CHUNKING`, `STARTTLS`, and `AUTH` with `PLAIN`/`LOGIN`/`SCRAM-SHA-1`;
  `BDAT <n> LAST` (RFC 3030) delivers like `DATA`; `STARTTLS` replies `220`
  then completes a TLS handshake on the hooked cert/key, the following `EHLO`
  is `250` and does not re-advertise `STARTTLS`, and `STARTTLS` after `MAIL`
  is `503`; `AUTH PLAIN` is `235` on success and `535` on failure;
  `AUTH SCRAM-SHA-1` (RFC 5802) completes with `235` for alice / alice-secret;
  `MAIL FROM` `SIZE=` / `BODY=8BITMIME` / `RET=` / `ENVID=` and `RCPT TO`
  `NOTIFY=` / `ORCPT=` are `250`.
- IMAP extensions: `CAPABILITY` lists `IDLE`, `NAMESPACE`, `UIDPLUS`,
  `CONDSTORE`, `QRESYNC`, `MOVE`, `BINARY`, `LITERAL+`, and `ESEARCH`;
  `NAMESPACE` emits an untagged `NAMESPACE` then tagged `OK`; `IDLE` sends `+`
  and completes `OK` on `DONE`; `ENABLE CONDSTORE` is `OK`; `MOVE`,
  `SEARCH RETURN (ALL)`, and `APPEND` with a non-synchronizing `{n+}` literal
  complete with tagged `OK`.
- SMTPUTF8 and UTF8=ACCEPT: `EHLO` advertises `SMTPUTF8` (on 2525, and on
  2587 when that service exists); `MAIL FROM:<…> SMTPUTF8` is `250` for ASCII,
  UTF-8, and null reverse paths, and a UTF-8 reverse path without the
  parameter is also `250`; `RCPT TO:` of an unknown UTF-8 address is `550`;
  `DATA` with UTF-8 header bytes to bob is delivered; toward the 7-bit next hop
  `downgrade.test` (`smtp:[127.0.0.1]:2529`), UTF-8 header values on a 7-bit
  envelope are sent as RFC 2047 encoded-words (local copy:
  `/opt/docs/rfc/rfc2047.txt`) and a UTF-8 envelope mailbox is
  bounced into the reverse-path mailbox; IMAP `ENABLE UTF8=ACCEPT` is `OK`
  before and after `LOGIN`, a quoted UTF-8 `CREATE` is `NO`, and `APPEND` of
  a small UTF-8 message to `INBOX` is `OK`.

## New work

RFC 4954 **AUTH**, RFC 7208 **SPF**, RFC 6376 **DKIM**, and RFC 7489 **DMARC**,
using Postfix 3.8.6 `smtpd` + OpenDKIM file layout:

- `EHLO` advertises `AUTH` with `PLAIN`, `LOGIN`, and `SCRAM-SHA-1` (and still
  `SMTPUTF8`, `PIPELINING`, and `SIZE` when those RFCs are in `/app/SPEC/`).
- `AUTH PLAIN` with a one-line RFC 4954 initial response, or `AUTH PLAIN`
  then `334` then the payload, authenticates against the Dovecot passwd-file
  `{PLAIN}` secrets. Success is `235`; bad credentials are `535`; unknown
  mechanism is `504`; client `*` cancel after `334` is `501`.
  `AUTH SCRAM-SHA-1` (RFC 5802) completes with `235` for alice / alice-secret.
- After `DATA` to `bob@example.test` from `alice@example.test`, the delivered
  message includes `Authentication-Results:` (RFC 8601; local copy:
  `/opt/docs/rfc/rfc8601.txt`) with `spf=`, `dkim=`,
  and `dmarc=` result tokens, and a `DKIM-Signature:` field (`v=1`,
  `a=rsa-sha256`, `c=simple/simple`, `d=example.test`, `s=default`) that
  **verifies** under RFC 6376 with the OpenDKIM public key
  `/app/etc/opendkim/keys/example.test/default.txt` (rsa-sha256 over the
  canonicalized headers+body, not a stub `b=`). Private key
  `/app/etc/opendkim/keys/example.test/default.private`.
- RFC 7208 SPF is evaluated from the shipped BIND zone (RFC 1035 master-file
  format; local copy: `/opt/docs/rfc/rfc1035.txt`)
  `/app/etc/bind/example.test.zone` (`example.test` TXT `v=spf1 ip4:127.0.0.0/8 -all`).
  Loopback clients therefore get `spf=pass` for `example.test`. Domains with
  no SPF TXT (e.g. `elsewhere.test`) yield `spf=none` and `dkim=none`.
- RFC 7208 §4.6.1 / §5: an **unknown mechanism** is `permerror`. The zone includes
  `legacy4408.test` TXT `v=spf1 foo:bar ip4:127.0.0.0/8 -all`; `MAIL FROM`
  that domain from loopback yields `spf=permerror` in `Authentication-Results`
  (not `spf=pass` from the later `ip4` term).
- RFC 6376 signing uses `a=rsa-sha256` (not RFC 4871 `rsa-sha1`). The
  `DKIM-Signature` verifies under RFC 6376 with the OpenDKIM public key.
- RFC 7489: `_dmarc.example.test` TXT `v=DMARC1; p=none;
  rua=mailto:dmarc-rua@example.test`. After accepting `DATA` for a message
  whose `header.from` is `example.test`, an aggregate XML report (`<feedback>`
  with `<report_metadata>` and `<policy_evaluated>`) is delivered to
  `dmarc-rua@example.test` Maildir.
- RFC 6530 §8 / RFC 6533: `transport_maps` `downgrade.test
  smtp:[127.0.0.1]:2529`. A 7-bit next hop that does not advertise
  `SMTPUTF8` receives RFC 2047 encoded-words for UTF-8 headers on a 7-bit
  envelope; a UTF-8 envelope mailbox to that hop is bounced.
- 7-bit `HELO` / `MAIL` / `RCPT` to bob remain `250`. `HELP`/`EXPN` remain
  `500`. Space after colon and unqualified `MAIL FROM:<alice>` remain `250`.
  Source routes remain `501`. Nested `MAIL` remains `503`. `DATA` with no
  recipient remains `554`. `VRFY` of bob remains `252`. Unknown ASCII `RCPT`
  remains `550`.
- IMAP4rev1 `LOGIN`/`SELECT`, `ENABLE UTF8=ACCEPT`, empty-mailbox `FETCH 1`
  → `BAD` and `UID FETCH 1:*` → `OK`, `CREATE INBOX` → `NO`, and `APPEND`
  then `FETCH 1` remain as already stated.

## Core requirements

The stack must build from the in-tree C++ sources, its listeners must answer,
and it must not wrap a stock MTA/IMAP. Every requirement from earlier steps
must still hold, except where this revision flips a rule. Refusals (SMTP
`4xx`/`5xx` replies, IMAP tagged `NO` or `BAD`) are required wherever they are
stated, just as the successful replies are; a stack that accepts every command
does not meet these requirements.

In particular, these must hold:

- `EHLO` advertises `AUTH` / `PLAIN`
- `AUTH PLAIN` with alice / alice-secret → `235`
- `AUTH PLAIN` with a wrong password → `535`
- `AUTH SCRAM-SHA-1` with alice / alice-secret → `235`
- `DATA` to bob from alice produces `Authentication-Results` and a
  `DKIM-Signature` with non-empty `bh=` and `b=` in Bob’s Maildir `new/` or
  `cur/`
- that `DATA` also delivers an RFC 7489 aggregate XML report (`<feedback>`)
  to `dmarc-rua@example.test` Maildir
- 7-bit `HELO` / `MAIL` / `RCPT` to bob → `250`
- IMAP `LOGIN` / `SELECT INBOX` → tagged `OK`
- `APPEND` of a small message to `INBOX` then `FETCH 1` → tagged `OK`

## Checking the stack

No test suite ships with the workspace, so check this behaviour yourself on
the wire: build, start the services, and drive the ports this ticket lists with
a client of your own (the image has `python3`, `curl`, and `openssl`). Compare
the 3-digit SMTP replies and the tagged IMAP statuses you get back with the
values stated above; every value this ticket names is observable that way. A
delivery is done when the message file is under the recipient's Maildir `new/`
or `cur/`, not `tmp/`.

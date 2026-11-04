# Step 9 — MTA-STS, DANE, TLS-RPT, REQUIRETLS

## Step time budget

This step has a two-hour (7,200-second) implementation budget. Check the
remaining budget with `bash /app/timer.sh` while working. The time limit is enforced; the timer is advisory and resets for each step.

## Interface (unchanged)

The only entry points remain:

- Postfix 3.8.6 (manual pages: `/opt/docs/postfix-3.8.6/`)
  `/app/sbin/postfix -c /app/etc/postfix` (`check`, `start-fg`, `start`, `stop`, `flush`)
  with SMTP on the `master.cf` inet ports (2525, and 2587 when that service is listed).
- Dovecot 2.3.21 (documentation: `/opt/docs/dovecot-2.3.21/`)
  `/app/sbin/dovecot -F -c /app/etc/dovecot/dovecot.conf` with IMAP on 2143
  (and HTTP `/.well-known/mta-sts.txt` on 8080 when RFC 8461 is in `/app/SPEC/`).
- Maildir under `/app/var/mail`. Hooks are still only bind address, those ports,
  `queue_directory`, Maildir root, cert/key paths, and `SOURCE_DATE_EPOCH`.

**Liveness requirement:** Because `/app/SPEC/rfc6409.txt`, `/app/SPEC/rfc5804.txt`, and
`/app/SPEC/rfc8461.txt` are present in this step, TCP ports **2587**, **24190**,
and **8080** must accept connections within **20 seconds** of starting the
stack, in addition to 2525 and 2143. All five ports must be bound within that
window.

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
under `/app/SPEC/` (rfc8461.txt, rfc7672.txt, rfc8460.txt, rfc8689.txt).

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
- Mail authentication: `AUTH PLAIN` (one-line initial response, or after
  `334`) authenticates against the passwd-file secrets (`235` / `535`), an
  unknown mechanism is `504`, and a `*` cancel after `334` is `501`; delivered
  mail from alice to bob carries `Authentication-Results:` with `spf=`, `dkim=`,
  and `dmarc=` tokens and a `DKIM-Signature:` (`v=1`, `a=rsa-sha256`,
  `c=simple/simple`, `d=example.test`, `s=default`) that verifies under
  RFC 6376 with `/app/etc/opendkim/keys/example.test/default.txt`; SPF comes
  from `/app/etc/bind/example.test.zone` (`spf=pass` for `example.test` from
  loopback, `spf=none` and `dkim=none` for a domain without SPF,
  `spf=permerror` for `legacy4408.test`); an RFC 7489 aggregate XML report is
  delivered to `dmarc-rua@example.test`.
- ARC, List-* and Sieve: delivered mail carries `ARC-Authentication-Results`,
  `ARC-Message-Signature`, and `ARC-Seal` (`i=1`, `cv=none`, `d=example.test`)
  with non-empty `bh=` / `b=`, and `List-Id`, `List-Unsubscribe`, and
  `List-Post` are preserved; ManageSieve on **24190** greets with capability
  lines then `OK`, `AUTHENTICATE "PLAIN"` is `OK`, unauthenticated `PUTSCRIPT`
  is `NO`, `PUTSCRIPT` / `SETACTIVE` store the active script at
  `/app/var/sieve/<user>/.dovecot.sieve`, and `NOOP`, `CAPABILITY`,
  `HAVESPACE`, and `LOGOUT` are `OK`; an active `fileinto "Archive"` script
  files later mail into `<Maildir>/.Archive/`.

## New work

RFC 8461 **MTA-STS**, RFC 7672 **DANE** (outbound TLSA), RFC 8460
**TLS-RPT**, and RFC 8689 **REQUIRETLS**:

- `EHLO` advertises `STARTTLS` (RFC 3207) and `REQUIRETLS` (RFC 8689).
  `STARTTLS` with no mail transaction in progress replies `220` and then
  completes a TLS handshake using `smtpd_tls_cert_file` /
  `smtpd_tls_key_file`. A following `EHLO` is `250` and MUST NOT
  re-advertise `STARTTLS`. `STARTTLS` after `MAIL` is `503`.
- RFC 7672 outbound DANE: when `transport_maps` delivers to a domain that has
  a TLSA `3 0 1` RR `_25._tcp.<domain>` in `/app/etc/bind/example.test.zone`
  (RFC 6698, local copy `/opt/docs/rfc/rfc6698.txt`; usage 3 / selector 0 = full certificate / matching type 1 =
  SHA-256), the client MUST `STARTTLS` and compare the SHA-256 of the peer
  certificate's DER encoding to that RR. On match, DATA is delivered. On
  mismatch, DATA MUST NOT be delivered (permanent failure / bounce). RFC 8460:
  after that TLS attempt, a JSON TLS-RPT report (`application/tlsrpt+json`,
  naming the recipient domain it evaluated in the RFC 8460 §4.4
  `policy-domain` field, and with `dane-invalid` on mismatch) is delivered to
  the `rua=` mailbox from `_smtp._tls.example.test` TXT
  (`mailto:tlsrpt@example.test`).
- `MAIL FROM:<…> REQUIRETLS` is accepted (`250`) as an ESMTP parameter
  (RFC 8689).
- HTTP `GET /.well-known/mta-sts.txt` on loopback port **8080** (RFC 8461
  §3.2) returns `200` with `version: STSv1`, a `mode:` line, and
  `mx: mail.example.test`.
- Inherited `AUTH PLAIN` `235`, `HELP` `500`, source routes `501`,
  `SMTPUTF8`, RFC 6530 7-bit MX header downgrade to RFC 2047 encoded-words
  and the bounce to the reverse-path mailbox for a UTF-8 envelope mailbox,
  both via `downgrade.test`, RFC 7208
  `spf=permerror` for unknown mechanisms, RFC 7489 aggregate XML to
  `dmarc-rua@example.test`, 7-bit `HELO`/`MAIL`/`RCPT`, IMAP4rev1
  `LOGIN`/`SELECT`, `ENABLE UTF8=ACCEPT`, empty `FETCH 1` → `BAD`,
  `UID FETCH 1:*` → `OK`, and `APPEND` then `FETCH 1` remain as already
  stated.

## Core requirements

The stack must build from the in-tree C++ sources, its listeners must answer,
and it must not wrap a stock MTA/IMAP. Every requirement from earlier steps
must still hold, except where this revision flips a rule. Refusals (SMTP
`4xx`/`5xx` replies, IMAP tagged `NO` or `BAD`) are required wherever they are
stated, just as the successful replies are; a stack that accepts every command
does not meet these requirements.

In particular, these must hold:

- `EHLO` advertises `STARTTLS` and `REQUIRETLS`
- `STARTTLS` completes TLS; the next `EHLO` is `250` without `STARTTLS`
- Outbound DANE: mail to a domain with a matching TLSA `3 0 1` is delivered
  over STARTTLS and a TLS-RPT JSON report naming that policy domain is in the
  `rua` Maildir (`tlsrpt@example.test`)
- `MAIL FROM:<alice@example.test> REQUIRETLS` → `250`
- `GET /.well-known/mta-sts.txt` on 8080 is `200` with `version: STSv1` and
  `mx: mail.example.test`
- 7-bit `HELO` / `MAIL` / `RCPT` to bob → `250`
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

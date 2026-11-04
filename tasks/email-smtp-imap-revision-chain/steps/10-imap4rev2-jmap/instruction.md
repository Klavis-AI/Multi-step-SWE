# Step 10 — IMAP4rev2 and JMAP

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
  (and HTTP `/.well-known/jmap` on 8080 when RFC 8620 is in `/app/SPEC/`).
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
under `/app/SPEC/` (rfc9051.txt, rfc8620.txt, rfc8621.txt).

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
- MTA-STS, DANE, TLS-RPT, REQUIRETLS: `EHLO` advertises `STARTTLS` and
  `REQUIRETLS`; outbound delivery to a domain with a TLSA `3 0 1` RR at
  `_25._tcp.<domain>` in the zone uses `STARTTLS` and delivers only when the
  SHA-256 of the peer certificate's DER matches; a JSON TLS-RPT report
  (`application/tlsrpt+json`, naming that domain in `policy-domain`, with
  `dane-invalid` on mismatch) is delivered to `tlsrpt@example.test`;
  `MAIL FROM:<…> REQUIRETLS` is `250`; `GET /.well-known/mta-sts.txt` on
  loopback port **8080** returns `200` with `version: STSv1`, a `mode:` line,
  and `mx: mail.example.test`.

## New work

RFC 9051 **IMAP4rev2** and RFC 8620/8621 **JMAP**:

- IMAP `CAPABILITY` (before and after `LOGIN`) includes `IMAP4rev2` and still
  `IMAP4rev1`.
- After `LOGIN`, `ENABLE IMAP4rev2` emits untagged `ENABLED IMAP4rev2` then
  tagged `OK`. Once IMAP4rev2 is enabled, this server answers `SEARCH CHARSET …`
  with a tagged `BAD`. This is a deliberate local policy that departs from
  RFC 9051 §6.4.4 in `SPEC/` (which supports US-ASCII and UTF-8 and answers an
  unsupported charset with `NO`); where the two differ, this ticket governs.
- HTTP `GET /.well-known/jmap` on loopback port **8080** returns `200` with a
  JMAP session resource (RFC 8620 §2) containing `urn:ietf:params:jmap:core`,
  `urn:ietf:params:jmap:mail`, `apiUrl`, `downloadUrl`, and `uploadUrl`.
  The MTA-STS policy path `/.well-known/mta-sts.txt` on the same port still
  returns `version: STSv1`.
- `POST /jmap` with an RFC 8621 `Email/query` method call, using an
  `accountId` from the session resource, returns `200` with
  `methodResponses` containing `Email/query`. After a message has been
  delivered to bob, that query’s `ids` array is non-empty.
- RFC 8620 blob URLs work: `POST` to `uploadUrl` returns a JSON `blobId`;
  `GET` `downloadUrl` with that id returns the uploaded bytes. `GET`
  `eventSourceUrl` is `text/event-stream` with an `event:` line (push).
- Every JMAP request in this ticket (session resource, `POST /jmap`, upload,
  download, event source) carries HTTP Basic credentials (RFC 7617; local copy:
  `/opt/docs/rfc/rfc7617.txt`) for
  `bob` / `bob-secret`; a server that authenticates JMAP requests must accept
  them. `Email/query` uses the session's primary mail account
  (`primaryAccounts` for `urn:ietf:params:jmap:mail`), so for those
  credentials that is bob's account.
- IMAP4rev1 `LOGIN` / `SELECT INBOX`, `ENABLE UTF8=ACCEPT`, empty-mailbox
  `FETCH 1` → `BAD`, `UID FETCH 1:*` → `OK`, `CREATE INBOX` → `NO`,
  `NAMESPACE`, `LIST`, `STATUS`, `APPEND` then `FETCH 1`, unauthenticated
  `SELECT` → `BAD`, and bad-password `LOGIN` → `NO` remain as already stated.
- 7-bit SMTP `HELO`/`MAIL`/`RCPT`, `EHLO` `STARTTLS`/`SMTPUTF8`/`AUTH`,
  `AUTH PLAIN` `235`, `HELP` `500`, source routes `501`, `REQUIRETLS`,
  nested `MAIL` `503`, unknown `RCPT` `550`, RFC 6530 7-bit MX header
  downgrade to RFC 2047 encoded-words and the bounce to the reverse-path
  mailbox for a UTF-8 envelope mailbox, both via `downgrade.test`,
  RFC 7208 `spf=permerror` for unknown mechanisms,
  and RFC 7489 aggregate XML to `dmarc-rua@example.test` remain as already
  stated.

## Core requirements

The stack must build from the in-tree C++ sources, its listeners must answer,
and it must not wrap a stock MTA/IMAP. Every requirement from earlier steps
must still hold, except where this revision flips a rule. Refusals (SMTP
`4xx`/`5xx` replies, IMAP tagged `NO` or `BAD`) are required wherever they are
stated, just as the successful replies are; a stack that accepts every command
does not meet these requirements.

In particular, these must hold:

- `GET /.well-known/jmap` on 8080 is `200` with `urn:ietf:params:jmap:core`,
  `urn:ietf:params:jmap:mail`, and `apiUrl`
- IMAP `CAPABILITY` includes `IMAP4rev2`
- `ENABLE IMAP4rev2` after `LOGIN` is tagged `OK` with untagged `ENABLED`
- After IMAP4rev2 is enabled, `SEARCH CHARSET US-ASCII ALL` is tagged `BAD`
- `POST /jmap` `Email/query` (accountId from the session) is `200` with
  `Email/query` in `methodResponses`
- Blob `POST` upload then `GET` download round-trips bytes; EventSource
  `GET` is `text/event-stream` with `event:`
- IMAP `LOGIN` / `SELECT INBOX` → tagged `OK`
- 7-bit `HELO` / `MAIL` / `RCPT` to bob → `250`
- `APPEND` then `FETCH 1` → tagged `OK`

## Checking the stack

No test suite ships with the workspace, so check this behaviour yourself on
the wire: build, start the services, and drive the ports this ticket lists with
a client of your own (the image has `python3`, `curl`, and `openssl`). Compare
the 3-digit SMTP replies and the tagged IMAP statuses you get back with the
values stated above; every value this ticket names is observable that way. A
delivery is done when the message file is under the recipient's Maildir `new/`
or `cur/`, not `tmp/`.

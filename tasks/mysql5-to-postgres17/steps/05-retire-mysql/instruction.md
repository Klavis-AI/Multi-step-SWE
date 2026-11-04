# Step 5 — MySQL is gone

## 1. Implement this milestone

### The deployment as it stands

The deployment you inherit already runs on PostgreSQL 17. Every page is served
from it, every lesson's SQL is PostgreSQL's, and no request reads MySQL any
more.

What has not been done is removing the old server, and this deployment is
currently in the state that gap describes. `/var/lib/mysql` still holds the
Latin-1 dataset. `/etc/mysql` still configures a server to serve it. The tree
still carries two jframework adapters for MySQL back-ends, three adapter
self-tests that import them, seven MySQL-dialect files under `install/_db/`,
and thirteen MySQL and Drizzle files in the vendored Doctrine library under
`app/plugin/doctrine/`. Doctrine's driver registry still advertises three of
those drivers by name; its reserved-words command still offers a MySQL keyword
list and checks against it by default; the application's own Doctrine bootstrap
still falls back to `pdo_mysql` whenever the configuration does not name an
adapter. And `vagrant_bootstrap.sh` — the recipe that builds the machine —
still installs `mysql-server`, `phpmyadmin` and `php5-mysql`, and still
preseeds debconf with a MySQL root password.

"Nothing uses it" and "it is gone" are different states, and a deployment in
the first one is a single `vagrant up` away from having two databases again.

### What must change

**The MySQL server is retired.** `/var/lib/mysql` and `/etc/mysql` are gone.
`/var/lib/postgresql` still holds the live cluster and the application still
serves from it — removing the old server must not disturb the new one.

**Every file that exists only to speak MySQL is gone.** The jframework
adapters and their self-tests; the MySQL-dialect installer SQL; Doctrine's
MySQL and Drizzle drivers, platforms, keyword lists, schema managers and the
session-init listener that belongs to them.

The files that speak to *other* back-ends stay. The `pdo_pgsql`, `pdo_sqlite`
and `mssql` adapters and their installer SQL are not yours to remove, and
neither are Doctrine's other drivers and platforms — except a vendored file
that holds SQL PostgreSQL 17 would not run, which is ported or not kept
whichever server it is for (see *What must not change*). That exception goes
file by file: a file for another server that holds no SQL of its own — an
exception class, a keyword list, a driver class with no statement in it —
stays, even where that server's platform does not. Nor is the original
dump under `SQL/`, which is the project's record of where its schema came from
rather than part of the installer.

Two different things in this tree say "MySQL", and they are not answered the
same way. Some files only *mention* it — a docblock comparing behaviour, a
comment naming what a portability flag works around. Several of those sit in
the vendored Doctrine library; they are not MySQL files, and they stay exactly
as they are. Other files *select* a MySQL back-end, and the next section is
about them: some of that wiring is in files that go, and the rest is in files
that have to survive and therefore have to be edited. Retiring MySQL means
removing the driver — not removing the word, and not removing whatever file
the word turned up in.

**Every remaining reference that selects a MySQL back-end is gone.** This is
the part that cannot be done by deleting files, because these references live
in files that have to survive:

- Doctrine's driver registry, which maps a driver name to the class that
  implements it. Three of its entries name classes this step deletes. A key
  left pointing at a class that is no longer there is worse than the class
  being there.
- Doctrine's reserved-words command, which has both a keyword-list registry
  and a default list of the sets it checks against. MySQL is named in each.
  Both stay — the registry, and a default list that is still a literal,
  non-empty list of that registry's keys — with MySQL gone from each.
- The application's Doctrine bootstrap in `app/plugin/doctrine.php`, whose
  fallback fires whenever the configured adapter is absent or is one of the
  MySQL names. After this step there is no MySQL driver for it to fall back
  to. Deleting the fallback is not an answer to that: a configuration that
  names no adapter would then reach the rest of the bootstrap with nothing
  selected at all. It has to go on assigning a default, and the default it
  assigns has to be a driver this tree still implements — one whose class is
  still declared here after your deletions. It stays what it is today, a test
  of `$adapter` that assigns it a driver name, and the test no longer names a
  MySQL back-end.
- The adapter test harness at `_japp/test/lib/db/adapter/main.php`, which
  imports a test file per configured adapter. When you are done, every back-end
  it still selects on has to be one this tree still has, and the arm for each
  one has to import that back-end's own tests. The supplied `pdo_sqlite` arm
  does not: it imports a MySQL adapter's tests, which is harmless only for as
  long as that file exists. It stops being harmless in this step. The harness
  stays a `switch` on the configured adapter; `pdo_sqlite` is the one back-end
  whose own adapter tests this tree ships, so it is the one arm left, and no
  arm is a catch-all `default`.

Every one of these needs an edit to a file that stays. That is the work.

**The provisioning recipe builds a PostgreSQL machine.** After this step
`vagrant_bootstrap.sh` installs PostgreSQL and the PHP extension that talks to
it, and names none of MySQL's server or client, MariaDB, phpMyAdmin or
`php5-mysql` — not to install them and not to remove them — and preseeds no
database password. It still installs Apache and PHP: the recipe
has to go on producing a machine that runs the application.

### What must not change

**The application's behaviour.** This step asks for no behavioural change at
all. Every page that answered before answers the same way after, byte for
byte, and the database behind it is untouched — same rows, same encodings, same
sequences. A retirement that takes something live with it shows up as a page
that stops answering the way it answers today, and that is the failure to
avoid.

**The schema and the data.** Nothing in this step edits the PostgreSQL schema,
the installer's PostgreSQL SQL, or a single row. The rows to hand on are this
deployment's dataset as it stood before anyone exercised it — the rows its
installer data loads, and the sequences past them — not whatever the database
you inherit happens to hold. If testing in an earlier
session left writes behind in it, putting those rows back is restoring the
data, not editing it. Exercising the application writes to it too — page-view
statistics, accounts, lesson state — so if you drive it while testing, put the
data back afterwards, the rows and the sequences behind the generated keys with
them, and make the restore the last thing you do to the database.

**The tree's integrity.** Every `.php` file that remains still parses. No class
is left extending or implementing a parent whose declaration you removed.
Doctrine's driver registry and its keyword-list registry name only classes this
tree still declares. And no MySQL back-end name appears in more places than it
already appeared in — moving the wiring somewhere new is not retiring it.

**Every SQL statement the application's PHP builds is SQL PostgreSQL 17 would
run.** A statement qualifies when the PostgreSQL 17 grammar parses it and every
function and collation it names exists in the database this deployment uses,
wherever it is in the tree — vendored code for database servers this deployment
has never used included, where a file holding one that does not qualify is
ported or not kept. That is a requirement on the tree you hand on, not only a
description of the one you were handed: if you find a statement that does not
qualify, making it qualify is part of this milestone.

**And the server accepts what the application sends it.** Parsing is not
the whole of running: a statement that qualifies can still be refused when it
executes — a duplicate key it was counting on catching, a `NOT NULL` column it
leaves empty, a value its column's type will not take, an object the
application's role may not alter — and a connection can be refused before any
statement is sent. While the site serves requests, the server must report no
error to any of the application's sessions — at severity `ERROR`, `FATAL` or
`PANIC`, whatever its SQLSTATE class, valid SQL or not — with exactly three
exceptions. In these places a refusal is allowed, though never required:

- the lesson that demonstrates numeric SQL injection, when the station its
  request supplies is neither a number nor a well-formed condition, or is a
  condition that pattern-matches with `LIKE` under a non-deterministic
  collation;
- the business layer access control lesson's login, when the user id its
  request supplies is empty: the lesson compares it with an integer column,
  and PostgreSQL will not read an empty string as an integer;
- the installer (`/init`), run again over the database it has already
  installed: the accounts it creates exist already, so the role assignments it
  writes for them name no user.

## 2. Read these local references

**Normative semantics:** the PostgreSQL 17 documentation on *Server Setup and
Operation* and *Client Authentication*, for what the surviving cluster needs;
the Doctrine DBAL documentation on drivers, platforms and the `DriverManager`;
the PHP manual on PDO and its PostgreSQL driver; and the Debian/Ubuntu
packaging documentation for `postgresql`, `php5-pgsql` and `debconf-set-selections`.
**Local:** `/app/_japp/model/lib/db/adapter/`, which is the adapter family and
the contract its members keep; `/app/_japp/test/lib/db/adapter/`, the harness
that imports them; `/app/app/plugin/doctrine.php` and the vendored library
under `/app/app/plugin/doctrine/`; `/app/install/_db/`; `/app/vagrant_bootstrap.sh`;
the live PostgreSQL database; and the service wrapper at
`/usr/local/sbin/wg-services`, which starts whatever servers it finds and exits
0 either way — removing MySQL will not brick this container.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and in the two servers' data and
  configuration, and preserve the supplied source and data. Do not create a
  prose answer or an alternate entry point.
- What is kept from this session is `/app`,
  `/var/lib/mysql`, `/etc/mysql`, `/var/lib/postgresql` and `/etc/postgresql`.
  Nothing else is: the Apache and PHP configuration is supplied fresh, so a
  change made there does not carry over.
- Leave Apache and PostgreSQL running, or leave them so that
  `/usr/local/sbin/wg-services start` brings them up.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace, its servers and the references above; do not
  search the system for other implementations or answers.
- Leave the deployment so that it comes back up from what is kept.

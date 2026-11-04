# Step 4 — the application runs on PostgreSQL 17

## 1. Implement this milestone

### The deployment as it stands

WebGoatPHP is installed at `/app` and served by Apache 2.4 with PHP 7.4 on
`http://127.0.0.1`, `Host: localhost`. The application server's clock is frozen
at `2026-01-01 00:00:00 UTC`.

Three milestones are behind you and all three stay. The MySQL 5.7 server in
this container — `root` over `/var/run/mysqld/mysqld.sock`, no password —
rejects values it cannot store as given instead of repairing them, and the
application supplies what it had been relying on the server to invent. A
PostgreSQL 17 cluster under `/var/lib/postgresql`, on port 5432 and
the socket directory `/var/run/postgresql`, holds a `webgoatphp` database with
the whole schema in it and every row of the MySQL database in that schema, its
generated-key columns standing exactly one past the largest key they hold. And
`/app/install/_db/pdo_pgsql.schema.sql` and `/app/install/_db/pdo_pgsql.data.sql`
build and fill that database from nothing.

What has not moved is the application. It reads and writes MySQL on every
request. The framework chooses its database layer by name:
`app/config/application.php` constructs a `\jf\DatabaseSetting` whose first
argument is an adapter name, `_japp/model/lib/db.php` imports
`_japp/model/lib/db/adapter/<name>.php` from that string, and the two install
file names above are derived from it as well. Five adapters ship in that
directory. None of them is for PostgreSQL.

One command starts and stops the servers:

```sh
/usr/local/sbin/wg-services start        # also: stop, status
```

Three things about the PostgreSQL cluster are fixed: it listens on port 5432,
at `127.0.0.1`, with its socket directory at `/var/run/postgresql`. The
authentication rules in its configuration are the rules the application
connects under, so rules that shut something out shut the site out with it.
And everything that has to be true of the cluster has to be true of the files
it is made of, under `/var/lib/postgresql` and `/etc/postgresql`: a setting
that exists only in the session you typed it into is not part of what you hand
on. The cluster is started with TLS off, with statement logging set at
start-up, with WAL archiving and preloaded libraries off, and with a
connection limit of at least 50, so authentication rules that demand TLS shut
the site out. Everything else
about the cluster is yours.

### What must change

**The site must serve without MySQL.** Every page, every form, every lesson:
what is behind them comes out of PostgreSQL. That is this milestone, and it is
blunt — with the MySQL server stopped, the application answers what it answers
today. Nothing may open MySQL: not a fallback when a statement fails, not a
second connection kept for the one query that was hard to move, not a
shell-out. The site must serve from a PHP that has no MySQL extension loaded
at all — no `mysqli`, no `pdo_mysql`, no `mysqlnd` — and code that so much as
names one of their classes or functions on a request path fails under it.

That needs a database layer this framework will load and this server will talk
to, and it has to satisfy the whole of the contract the layer it replaces
satisfies — not the part that the pages you happen to click walk through. The
framework's model layer, the role-based access control library, the nested-set
library, the settings manager, the plugins and the individual lessons all go
through it, and between them they use more of it than a page view suggests: a
result handle that can be stepped through row by row, a row count, the key the
server generated for the row just inserted, a quoted literal, an escaped
identifier, transaction control, and the last error the server reported. A
method that returns nothing where the old one returned a value is a page that
breaks three screens away from the thing you changed.

**Every SQL statement this application's PHP builds must be SQL that
PostgreSQL 17 would run.** Not the statements on the request path — every one
of them. A statement qualifies when the real PostgreSQL 17 grammar parses it
and every function and collation it names exists in the database this
deployment now uses. The two dialects diverge in ways that are cheap to miss
precisely because MySQL took them for years: back-quoted identifiers,
`REPLACE INTO`, `GROUP_CONCAT(... SEPARATOR ...)`, a `LIMIT` on an `UPDATE` or
a `DELETE`, `LIMIT m,n`, `AUTO_INCREMENT` in a `CREATE TABLE`,
`ALTER TABLE ... AUTO_INCREMENT`, `LOCK TABLES` and `UNLOCK TABLES`,
`DATABASE()`. There are statements of most of those kinds in this repository.
Some of them are in vendored code for database servers this deployment has
never used, under the ORM plugin, and one is in the adapter for the server you
are leaving. They are in the repository, so they are in scope: such a file is
ported, or it is not kept.

Statements are not only in `.sql` files and not only on one line. Much of this
application's SQL is assembled — concatenated, interpolated, built up across
several statements of PHP — and what has to be PostgreSQL's is the statement
that reaches the driver, not the fragment that is legible in one place.

The `.sql` files under `/app/install/_db/` are a separate matter. The pair this
deployment installs from is covered under *What must not change*; the rest are
install data for servers this deployment does not use, and porting them is not
part of this.

**And the server must accept what the application sends it.** Parsing is not
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
  collation (see *The server's own words* below);
- the business layer access control lesson's login, when the user id its
  request supplies is empty: the lesson compares it with an integer column,
  and PostgreSQL will not read an empty string as an integer;
- the installer (`/init`), run again over the database it has already
  installed: the accounts it creates exist already, so the role assignments it
  writes for them name no user.

**The pages must not change.** The same requests produce the same status, the
same redirect target and the same page content — ignoring three things that are
random or arbitrary rather than behaviour: the session identifier, the source
listing each lesson echoes back out of its own file, and the order of the rows
in the one route that answers with a bare JSON array. The same holds of the
rows those requests leave behind, with the same three exemptions there: a
password hash, its salt, and the random token that names a pending activation.
Translating the grammar is the easy half. The two servers also disagree about
what a *correct* statement means, and every one of these disagreements is
somewhere in this application:

- **Order.** MySQL handed rows back in an order this application learned to
  live on — a list that reads in insertion order, a method that takes the first
  row of a result and treats it as the right one. PostgreSQL promises nothing
  without `ORDER BY`, and it will not hand you the same order. Where the content
  of a page depends on the order, the query has to say so.

- **Case.** MySQL compares text by the collation of the column, and this
  database holds three of them: the framework's own tables are `utf8_bin`, the
  seven the installer built for the lessons and the contest are
  `latin1_swedish_ci`, and the ORM's is `utf8_unicode_ci`. Only `utf8_bin`
  compares case-sensitively; the other two do not, and the PostgreSQL schema
  you were handed already reproduces all three. What is still open is that four
  of those lesson tables are dropped and rebuilt by the lessons themselves at
  reset, by a `CREATE TABLE` with no charset clause on it — so the copy the
  *application* builds inherits the database default, `utf8_bin`, and compares
  case-sensitively, where the installer's copy of the same table does not.
  Those statements are yours to port, and giving them the schema's collation to
  match would make the application start finding rows MySQL never found.

  Case also reaches the comparisons the application writes itself. Some of them
  fold both sides before testing them, and the two servers do not fold every
  letter the same way; where the old server's fold made two spellings the same
  name, the new one has to go on making them the same name.

- **Trailing blanks.** Every MySQL 5.7 collation is PAD SPACE: `'ny '` and
  `'ny'` are the same string to `=`, to `IN`, to a join. No PostgreSQL collation
  is, and `character(n)`'s storage padding is a different thing from this.
  Where this application compares a column against a value that can arrive with
  trailing blanks, the match MySQL made has to go on being made. That is a
  property of the application's own predicates and of nothing underneath them:
  a database layer that trims every value on its way past, or a column type
  that trims, changes comparisons this application never wrote — including the
  one immediately below, which must go on differing.

  One place it will not be, and must not be. The lesson that demonstrates
  numeric SQL injection takes a string out of the request and puts it into the
  statement; when what the request supplies is an operand carrying a trailing
  blank, the match was the collation's doing rather than the application's, and
  no rewrite of the application's own queries can reach inside it. That page
  comes back with one row fewer than it used to. That is the correct outcome
  here, not a regression to chase.

- **Generated keys.** What the application reads back after an insert must be
  the key the server actually assigned to the row it just inserted, on that
  connection — not the largest key in the table, and not a number some other
  request is also about to be given.

- **The server's own words.** That same lesson prints the database's error
  message when the statement it built does not run. The message is PostgreSQL's
  now and cannot be made into MySQL's, so those pages are expected to read
  differently. One of them is not about a malformed statement at all: the
  lesson's input can build a `LIKE` over a column the schema you were handed
  makes case-insensitive. Where it does that with a non-deterministic
  collation, PostgreSQL will not evaluate a pattern match under it: that page
  cannot come back with the rows it used to either, and it is expected to
  differ for that reason. Where it does it with `citext`, whose own `LIKE`
  folds case, the match still runs and the page may come back as it does
  today; that is correct too. What is not expected is a page reporting the
  failure of something other than the lesson's own query, or answering with
  your layer's stack trace in place of the lesson: the status and any redirect are still the ones this
  deployment produces today, and the rest of the site's error handling is
  unchanged. Nor is the failure going unsaid: a layer that answers a statement
  the server refused by handing its caller nothing leaves the lesson rendering
  as though the query had found no rows, and a page that has stopped reporting
  the failure is not this page with a different message on it.

**The bundled ORM must not take pages down with it.** The Doctrine inside this
repository is from 2013, and this deployment's run mode has it compare its
models against the live database on every request that loads it. Its PostgreSQL
schema manager reads a sequence by selecting columns from the sequence relation
that PostgreSQL stopped exposing there in version 10. Whatever you decide to do
about that, the pages that load it must go on rendering what they render now.

**The application needs an account on the PostgreSQL server.** The
configuration names a database, a user and a password, and those are a MySQL
account. PostgreSQL has its own roles, its own object ownership and its own
host-based authentication; the application connects under whatever you set up
there, as a role of its own — not the superuser, and not `postgres` or
`wg_install`, a name reserved for whoever installs the schema and data
files.

### What must not change

**Everything the first three milestones delivered.** The MySQL configuration
that rejects rather than repairs, and every place the application was changed to
write complete rows. The PostgreSQL schema: the same tables, columns, types,
nullability, defaults, keys, indexes and foreign keys, behaving the way they
behave now. The rows in them. The generated-key columns still standing exactly
one past the largest key they hold. And both install files still building and
filling that database from nothing, at a table prefix this deployment does not
use — which also fixes the name the database layer goes by, since the framework
derives those two file names from it. Each of them is still SQL and only SQL,
still applies top to bottom in one pass, in under 300 seconds, to an empty
database (the schema file) or to one whose tables have just been built or
emptied (the data file), with every statement succeeding,
when it is run by an ordinary role that owns that database rather than by a
superuser; and the data file still applies a second time to the database it
just filled without failing a statement or holding a row twice, and still
leaves the generated keys past the rows it loaded.

All of that is a requirement on the deployment you hand on, not only a
description of the one you were handed: where you find part of it does not
hold, making it hold is part of this milestone. In particular, every write the
application makes must supply what the MySQL server used to invent for it — a
value for every column the statement leaves unset or sends something
unstorable, of the type that column declares, though never a shortened copy of
a value too long for its column — and PostgreSQL repairs no value either. The
application does not check whether its writes succeed, so a statement the
server refuses can fail without a sign on the page that sent it. Where you
repair one, the pages that write belongs to go back to what they showed while
the MySQL server was still repairing values.

**The rows as they stand.** Exercising this application writes to it —
page-view statistics, accounts, lesson state, the tables the panel rebuilds. If
you drive it while testing, put the data back afterwards — the rows, and the
sequences behind the generated keys with them — and make the restore the last
thing you do to the database, after any final check that the deployment still
answers. The rows to hand on are this deployment's dataset as it stood before
anyone exercised it, not whatever the database you inherit happens to hold: if
testing in an earlier session left writes behind in it, putting those rows back
is restoring the data, not changing it.

**MySQL stays where it is.** `/var/lib/mysql` and `/etc/mysql` are handed on
whole: the data directory with its system tablespace and its redo logs, the
`mysql` system schema, the `webgoatphp` database and every table in it, and the
configuration that still sets the strict SQL mode the first milestone put
there. The application stops using that server. Nothing here removes it, empties
it, or takes it apart — a half-dismantled server is worse than one nobody is
talking to.

**The site itself.** The routes, the lessons, the challenges, and what each of
them teaches. This is a migration and not a rewrite: a lesson that no longer
demonstrates its vulnerability has been broken rather than fixed, and a query
made safe is a query whose page has stopped doing the thing the page is for.

## 2. Read these local references

**Normative semantics:** the PostgreSQL 17 documentation — *SQL Syntax*
(identifiers and key words), *Queries* (`ORDER BY`, `LIMIT`), `INSERT` and its
`ON CONFLICT` clause, *Functions and Operators* (string functions, aggregate
functions, sequence manipulation functions, system information functions),
*Data Definition → Identity Columns*, *Collation Support*, *Client
Authentication*, *Database Roles* and *Privileges*, and *Appendix A: Error
Codes*; plus the PHP manual on PDO and its PostgreSQL driver. The MySQL 5.7
Reference Manual on `REPLACE`, `GROUP_CONCAT`, `LIMIT` in `UPDATE` and
`DELETE`, `LAST_INSERT_ID()`, identifier quoting, and what its collations do
about letter case and trailing spaces. **Local:** `/app/_japp/model/lib/db/`,
which is the database layer, the adapters it loads and the nested-set library
built on top of it; `/app/_japp/model/lib/`, which is the rest of what talks to
it; `/app/app/config/`; `/app/app/plugin/`, which holds the ORM and statistics
plugins; `/app/install/_db/`; the lessons and challenges under `/app/challenges`
and `/app/app/`; the live PostgreSQL database; and the service wrapper at
`/usr/local/sbin/wg-services`.

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
- Leave Apache, MySQL and PostgreSQL running, or leave them so that
  `/usr/local/sbin/wg-services start` brings them up.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace, its servers and the references above; do not
  search the system for other implementations or answers.
- Leave the deployment so that it comes back up from what is kept.

# Step 2 — the schema exists in PostgreSQL 17

## 1. Implement this milestone

### The deployment as it stands

WebGoatPHP is installed at `/app` and served by Apache 2.4 with PHP 7.4 on
`http://127.0.0.1`, `Host: localhost`. Its data still lives in the `webgoatphp`
database on the MySQL 5.7 server in this container, reachable as `root` with an
empty password over the unix socket `/var/run/mysqld/mysqld.sock`. The
application server's clock is frozen at `2026-01-01 00:00:00 UTC`.

That server no longer repairs values it cannot store as given, and the
application supplies what it had been relying on the server to invent. That is
the previous step's work, it is part of this deployment, and it stays: the
configuration that makes the server reject those statements, and every place
the application was changed to keep writing complete rows, must still be there
and must still work when you are finished.

That is a requirement on the deployment you hand on, not only a description of
the one you were handed. Every write the application makes must still store the
row the server stored for it when it was repairing values — the value it
invented for a column the statement left unset or sent something unstorable, of
the type that column declares, and text the column can hold. The one exception
is a value too long for its column: it is not shortened, by the server or by
the application, and the write may fail instead. The application does not check
whether its writes succeed, so a statement the server refuses fails without a
sign on the page that sent it. If you find one, making the application supply
what the server used to invent for it is part of this milestone, and the pages
that write belongs to go back to what they showed while the server was still
repairing values.

PostgreSQL 17 is installed — server, client and contrib, with
`/usr/lib/postgresql/17/bin` on `PATH` — and there is no cluster. Debian's was
dropped when the image was built. Nothing in `/var/lib/postgresql` is yours
yet.

One command starts and stops the servers:

```sh
/usr/local/sbin/wg-services start        # also: stop, status
```

It starts what it finds and creates nothing. Read it: it is the only thing
run at a step boundary, and it documents how it looks for a cluster.

### Where the PostgreSQL side is expected to be

These locations are fixed, because your cluster has to start afresh from them,
not merely keep running as it is now:

- The data directory is **under `/var/lib/postgresql`** — either
  `/var/lib/postgresql/<version>/<name>`, which is what `pg_createcluster`
  makes, or a directory you create there yourself with `initdb -D`. Both are
  found; nothing outside that tree is.
- It answers on **port 5432**, on **`127.0.0.1`**, and on the unix socket
  directory **`/var/run/postgresql`**.
- The superuser role is **`postgres`**, and the cluster is owned on disk by the
  `postgres` user, which exists.
- The database is **`webgoatphp`**, the name the application already uses.

Those four settings are forced on the command line when your cluster is
started, together with a host-based authentication file that lets two other
accounts in over the local socket ahead of your own rules, so your own rules
cannot lock those accounts out. TLS is forced off too, because those
connections come over the loopback interface and the unix socket only. The
start also sets its own statement logging, turns WAL archiving and preloaded
libraries off, and raises the connection limit to at least 50.
Everything else — the configuration file, the locale and encoding you
initialise with, the roles you create, WAL, checksums — is yours.

### What must change

**The schema must exist in PostgreSQL**, in that database: every table the
`webgoatphp` database has today, with the same columns in it, and with the
constraints, defaults, indexes and keys those tables carry.

"The same" has two parts.

*What the catalogue says.* For each of the 27 tables: it exists, and it has a
primary key on the same columns as MySQL's — and no primary key where MySQL
has none. For each of its 155 columns: it exists, its type corresponds to the
MySQL type — an integer stays an integer, a character-string column stays a
character-string column that is no narrower than MySQL's, a float stays a
float — and it is nullable exactly where MySQL's is. Each column MySQL fills
in by itself on an `INSERT` has a PostgreSQL column that fills itself in the
same way. Each of the 34 indexes is there, on the same columns, unique exactly
where MySQL's is unique — an index that arrives `UNIQUE` when MySQL's was not
is a constraint this data never had to satisfy. Each of the 6 foreign keys is
there, pointing at the same table and columns.

*What the tables actually do.* A schema that declares the right things and
enforces nothing is not this schema. Written to directly, each table must
behave as the MySQL one behaves now — now, that is, with the previous step's
server settings in force. Every answer below is one the live MySQL server
gives today; ask it yourself if you are unsure:

- An ordinary row goes in.
- A value outside the range of a column's MySQL integer type is **rejected**,
  and a value inside it is accepted — including when it arrives as a quoted
  string, which is what a bound PHP string looks like by the time the server
  sees it.
- A string exactly as long as a bounded character column's declared length is
  accepted; one character longer is **rejected**, not shortened.
- A second row that collides on a unique index is **rejected**, and one that
  does not is accepted. Where MySQL indexes only a leading prefix of a column,
  two values that agree on that prefix and differ after it still collide.
- A row whose foreign key points at nothing is **rejected**.
- Comparing text is case-sensitive exactly where MySQL's is case-sensitive, and
  case-insensitive exactly where MySQL's is case-insensitive. The database has
  columns of both kinds and they differ by table; read each column's own
  collation rather than applying one answer to all of them — the collation the
  installer gave it. Four of the lesson tables are dropped and rebuilt by their
  lesson's own reset with no charset clause, which leaves a reset copy on the
  database default; that copy is not the schema.

**And the framework's installer must be able to build that schema from
nothing.** jFramework installs a schema by reading a file named for the
database adapter in use and running it statement by statement; the MySQL one is
`install/_db/mysqli.schema.sql`. The PostgreSQL one is

```
/app/install/_db/pdo_pgsql.schema.sql
```

and you are to write it. It is a separate deliverable from the live database:
run on a database that starts empty, it must leave behind the schema the two
lists above describe, behaving the way those lists say it behaves.

Six things about how the framework runs it:

- It is **SQL, and only SQL**. The framework hands each statement to a database
  driver, not to a terminal client: `psql`'s backslash commands are a feature of
  that program rather than statements a server understands, and
  `COPY ... FROM stdin` carries its rows in a protocol that driver does not
  speak. Both are ordinary things to find in a dump written by hand or by a
  tool, and neither survives being installed by the application.
- It is run **top to bottom in one pass**, on an **empty** database, in under
  300 seconds. It may not
  depend on anything already being there, and it may not require being run
  twice or in pieces.
- Failures are **ignored and not reported** — that is what the framework does,
  and it is why the shipped MySQL file quietly builds 26 of its 27 tables. So
  every statement in your file has to succeed. Check that by running it and
  reading the errors, because nothing else will tell you.
- The database it is run on is created with the **`C` locale** and `UTF8`
  encoding, from `template0`. Behaviour your live database gets from the
  cluster's own settings will not follow the file. Anything the schema needs in
  order to behave correctly — an extension, a collation — the file must create
  for itself.
- `PREFIX_` is the framework's placeholder for the configured table prefix, and
  it must keep working: `GetSQL()` substitutes it before the file is run. The
  tables that carry the deployment's prefix today must take a different prefix
  when the framework is configured with one, and the tables that carry no
  prefix today must not move.
- It is run by an **ordinary role that owns the empty database**, not by a
  superuser: the framework installs with the account the deployment connects
  under. A statement only a superuser may run, or one that hands an object to
  a role the installer is not, fails like any other.

### What must not change

**The application keeps running on MySQL, and keeps behaving as it does
today.** The same requests produce the same status, the same redirect target
and the same page content, ignoring the session identifier and the order of the
rows in the one route that answers with a bare JSON array, and the same rows
reach the same columns in MySQL. A refused write you repair, as described
above, is the one exception. Do not repoint the application at PostgreSQL
in this step: a half-moved deployment serves nothing, and the application's
configuration and its database adapter are not this step's work.

**The MySQL side stays as it is.** The same tables with the same columns,
types, lengths, character sets, collations and storage engines; the rows in the
`webgoatphp` database when you are finished must be the rows that are in it
now: this deployment's dataset as it stood before anyone exercised it. If
testing in an earlier session left writes behind in it, that dataset is still
what the rows must be, and putting them back is restoring the data, not
changing it. Exercising the application writes to
it — page-view statistics, accounts, lesson state — so if you drive it while
testing, put the data back afterwards.
Every request the front controller serves is logged, so one page fetch is one
row: make the restore the last thing you do to the database, after any final
check that the deployment still answers. `root` must still reach it over
`/var/run/mysqld/mysqld.sock` with an empty password.

**The rows stay where they are.** The PostgreSQL tables may be empty: this
milestone asks for the schema, not for the data.

**The names the application uses must still name one thing each.** The
application's SQL spells its tables and columns the way MySQL spells them, and
133 of the 155 columns and 4 of the 27 tables are spelled with capital letters
in them. PostgreSQL does not treat an unquoted name the way MySQL does. Settle
that however you like, but when you are done no two relations in the database
may differ only in case, and each of today's names must resolve to exactly one
table.

## 2. Read these local references

**Normative semantics:** the PostgreSQL 17 documentation — *Data Types*, *Data
Definition* (constraints, identity columns, generated columns), *Collation
Support*, and *SQL Syntax → Identifiers and Key Words* for how an unquoted name
is folded. The MySQL 5.7 Reference Manual for what the types you are converting
from mean. **Local:** the live MySQL database, which is the authority on what
the schema is; the application source under `/app`, in particular
`/app/install/_db/` and the framework's database layer under
`/app/_japp/model/lib/db/`; the service wrapper at `/usr/local/sbin/wg-services`.

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

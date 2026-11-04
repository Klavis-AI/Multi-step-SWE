# Step 3 — the data lives in PostgreSQL 17

## 1. Implement this milestone

### The deployment as it stands

WebGoatPHP is installed at `/app` and served by Apache 2.4 with PHP 7.4 on
`http://127.0.0.1`, `Host: localhost`. Its data still lives in the `webgoatphp`
database on the MySQL 5.7 server in this container, reachable as `root` with an
empty password over the unix socket `/var/run/mysqld/mysqld.sock`. The
application server's clock is frozen at `2026-01-01 00:00:00 UTC`.

Two things about this deployment were built before you arrived and both stay.
MySQL no longer repairs values it cannot store as given — it rejects them — and
the application supplies what it had been relying on the server to invent. And a
PostgreSQL 17 cluster exists under `/var/lib/postgresql`, on port 5432 and the
socket directory `/var/run/postgresql`, holding a `webgoatphp` database with the
whole schema in it, built by `/app/install/_db/pdo_pgsql.schema.sql`.

Those tables are empty. This step fills them.

One command starts and stops the servers:

```sh
/usr/local/sbin/wg-services start        # also: stop, status
```

Your PostgreSQL cluster has to start afresh, not merely keep running as it is
now. The start forces the port, the listen address, the socket directory
and a host-based authentication file that lets two other accounts in over the
local socket ahead of your own rules, with TLS off; it also sets its own
statement logging, turns WAL archiving and preloaded libraries off, and raises the
connection limit to at least 50. Everything else about the cluster is yours.

### What must change

**The rows must be in PostgreSQL.** Every row of every table in the MySQL
`webgoatphp` database, in the corresponding PostgreSQL table, and no rows that
are not in MySQL. The one exception is the session table: it is a MEMORY-engine
table on the MySQL side with server-generated identifiers, the application
writes a row to it on every request, including one made only to check that
the deployment is up, and MySQL empties it whenever it restarts. It is out of
scope. Everything else is in scope, and must match **per table**.

"The same rows" means the same set of rows per table, not a byte image of
either side, so several differences do not matter:

- the order rows come back in;
- how an integer is spelled, or how wide the column that holds it is;
- the trailing spaces on a fixed-length character column. MySQL does not return
  the padding it stored in a `CHAR`; PostgreSQL's `character(n)` does. Three
  tables here are affected and that difference does not matter — but a
  loader that turns those spaces into *leading* spaces, or into a different
  count of significant ones, is a real difference.

And several are not invisible at all:

- **Text must keep its meaning across the character-set change.** The MySQL
  database holds `utf8` tables and `latin1` tables side by side — read each
  table's and each column's own character set — and the PostgreSQL one is
  `UTF8`. MySQL's `latin1` is
  **not** ISO-8859-1: it is Windows-1252 plus five otherwise-unassigned control
  positions, so 27 of the 32 byte values from `0x80` to `0x9F` name printable
  characters — curly quotes, the euro sign, the em dash — where ISO-8859-1 has
  controls. Converting with the wrong one of those two produces text that is
  still valid UTF-8 and is silently wrong. This data contains such bytes; find
  out what resolves `latin1` how, and check the result rather than the exit
  status.

- **A value the application wrote must come back the way the application wrote
  it.** Including the ones that are not text at all. At least one column in this
  database holds a serialized PHP structure with embedded zero bytes, and
  PostgreSQL will not store a zero byte in `text`, `varchar` or `character` at
  any encoding — the `COPY` parser, the input function and the type itself each
  refuse it separately. A migration that drops those bytes, or stops at them, or
  replaces them, has lost the value.

  This is the one place the existing PostgreSQL schema may change: a column
  whose MySQL type is a character type may be given a **binary** type in
  PostgreSQL where no character type can hold what it holds. If you change one,
  change it in **both** places — the live database and
  `/app/install/_db/pdo_pgsql.schema.sql`. They are two separate objects held
  to the same rule, and the data file described below has to load into a
  database that schema file builds, so a file written for a binary column the
  schema file still declares as character text will not land. Everything else
  about the schema stays as it is, and a binary column that did not need to be
  binary is a defect: comparison in a binary column is byte-exact, and the columns
  that are case-insensitive today have to stay case-insensitive.

**The auto-numbered keys must carry on from where MySQL left off.** Every column
that MySQL fills in by itself on an `INSERT` has a PostgreSQL counterpart that
does the same, and loading rows with their keys already in them does not move
it. After the load, the first row the application inserts into such a table must
get a key that is free: the next value the column would generate has to be
exactly one more than the largest one the rows already hold, which is the key
MySQL itself numbers the next row with. This is state the rows
themselves do not carry, so nothing that only copies rows will get it right.

**Ship the file the framework installs data from.** jFramework builds both of
its install-time file names from the database adapter in use; the schema file
above is one of the pair, and the `data` one is

```
/app/install/_db/pdo_pgsql.data.sql
```

It is this step's second deliverable, and it is a separate thing from the live
database: it is what installs this application somewhere that has never seen it.
Seven things must be true of it.

- It is **SQL, and only SQL**. The framework hands each statement to a database
  driver, not to a terminal client: `psql`'s backslash commands are a feature of
  that program rather than statements a server understands, and
  `COPY ... FROM stdin` carries its rows in a protocol that driver does not
  speak. Both are ordinary things to find in a dump written by hand or by a
  tool, and neither survives being installed by the application.
- It is applied by an **ordinary role that owns the database**, not by a
  superuser: the framework installs with the account the deployment connects
  under. A statement only a superuser may run — switching off foreign-key
  triggers or replication roles, handing an object to another owner — fails
  like any other.
- It must apply **top to bottom in one pass**, to a database whose tables have
  just been built or emptied, in under 300 seconds. There are six foreign keys
  and PostgreSQL checks each of them on every `INSERT`, so the order rows go in
  matters.
- **Every statement in it must succeed.** The framework applies the file one
  statement at a time and throws away what each one returns, so a statement
  that fails part-way leaves a half-filled database and says nothing: a client
  given the whole file carries on past the failure and still exits 0. What a
  failed statement does leave behind is a message from the server. Check the
  file by running it and reading those, not `$?`. A statement ends where SQL
  says it ends, which is not at every `;` in the file: this data has
  semicolons inside values, and a semicolon inside a value is data.
- It must be **applicable twice**. Applied again to the database it just filled,
  it must leave that database holding the same rows once — not twice, and not
  with half the statements failing on a duplicate key.
- `PREFIX_` is the framework's placeholder for the configured table prefix and
  it must keep working, exactly as in the schema file: the tables that carry the
  deployment's prefix today must take a different prefix when the framework is
  configured with one, and the tables that carry no prefix today must not move.
  The substitution is textual and covers the whole file, comments included — so
  a placeholder that survives only inside a comment has not survived.
- Both files together must **install this application from nothing**. Into an
  empty database, with `PREFIX_` replaced by a prefix this deployment does not
  use, the schema file and then the data file must put these rows in the tables
  that prefix names. On such a database the auto-numbered keys are wherever
  `CREATE TABLE` left them and there is no earlier state for a loader to
  inherit, so the data file itself has to leave them past the rows it just
  loaded — the same requirement as above, where only the file can satisfy it.

### What must not change

**The application keeps running on MySQL, and keeps behaving as it does
today.** The same requests produce the same status, the same redirect target
and the same page content, ignoring the session identifier and the order of the
rows in the one route that answers with a bare JSON array, and the same rows
reach the same columns in MySQL. A refused write you repair, as described
below, is the one exception. Do not repoint the application at PostgreSQL:
its configuration and its database adapter are not part of this milestone, and a
half-moved deployment serves nothing.

**The MySQL side stays as it is.** It is the authority on what the data is.
The same tables with the same columns, types, lengths,
character sets, collations and storage engines; the rows in the `webgoatphp`
database when you are finished must be the rows that are in it now, and reading
it is not the same as driving the application through it. Those rows are this
deployment's dataset as it stood before anyone exercised it. If testing in an
earlier session left writes behind in it, that dataset is still what the rows
must be, and putting them back is restoring the data, not changing it. Exercising the
application writes to it — page-view statistics, accounts, lesson state — so if
you drive it while testing, put the data back afterwards, and make the restore
the last thing you do to the database, after any final check that the deployment
still answers. `root` must still reach it over `/var/run/mysqld/mysqld.sock`
with an empty password.

**The two things that were built before you arrived stay.** The MySQL
configuration that makes the server reject statements it used to repair, and
every place the application was changed to keep writing complete rows. The
PostgreSQL schema: the same tables, columns, types, nullability, defaults, keys,
indexes and foreign keys, behaving the way they behave now, and
`/app/install/_db/pdo_pgsql.schema.sql` still building it from an empty database
in one pass — with the single exception described above, applied to both.

Both are requirements on the deployment you hand on, not only descriptions of
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
repairing values. The same holds of the PostgreSQL schema: if you find that it,
or the schema file, does not do what the live MySQL tables do, bringing it up
to them is part of this milestone, and is the one other change either may take.

**The tables that are empty today are empty when you are finished.** Nine of
them. A loader that invents rows in a table nobody has rows in is a defect
whether or not anything else notices.

## 2. Read these local references

**Normative semantics:** the PostgreSQL 17 documentation — *Data Types*
(character types, binary data, numeric types), *Data Definition → Identity
Columns*, *Populating a Database*, `COPY`, and the `psql` and `pg_dump`
reference pages. The MySQL 5.7 Reference Manual on character sets and
collations, on `SELECT ... INTO OUTFILE` / `mysqldump`, and on what `latin1`
means to that server in particular. **Local:** the live MySQL database, which is
the authority on what the data is; the application source under `/app`, in
particular `/app/install/_db/` and the framework's database layer under
`/app/_japp/model/lib/db/`, which is what decides the file name above and what
is done with it; the service wrapper at `/usr/local/sbin/wg-services`.

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

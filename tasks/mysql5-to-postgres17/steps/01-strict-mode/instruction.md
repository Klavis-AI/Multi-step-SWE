# Step 1 — the database server stops repairing invalid values silently

## 1. Implement this milestone

### The deployment as it stands

WebGoatPHP is installed at `/app` and served by Apache 2.4 with PHP 7.4 on
`http://127.0.0.1`, `Host: localhost`. Its data lives in the `webgoatphp`
database on the MySQL 5.7 server in this container, reachable as `root` with an
empty password over the unix socket `/var/run/mysqld/mysqld.sock`. The `mysql`
client is on `PATH`. The application server's clock is frozen at
`2026-01-01 00:00:00 UTC`, which is where the timestamps in the data come from.

There is no build step. Apache and MySQL are both running, and one command
starts and stops them:

```sh
/usr/local/sbin/wg-services start        # also: stop, status
```

MySQL runs as the `mysql` user from the data directory `/var/lib/mysql`, on
the socket `/var/run/mysqld/mysqld.sock` and on port `3306` bound to
`127.0.0.1`. Those locations are fixed. Everything else about the server,
`sql_mode` included, comes from `/etc/mysql/my.cnf` and from any file that one
includes. Apache is started the same way, from the configuration supplied with
it.

### What must change

The server currently accepts data-change statements whose values it cannot
store as given: it adjusts the value and continues. A `NULL` sent to a column
that forbids one, a column left out of an `INSERT` that has no default, a
non-numeric value written to a numeric column, text a column's character set
cannot represent, a value longer than its column — each is quietly turned into
something storable and the statement reports success.

**Configure the server to reject those statements instead**, in
`/etc/mysql/my.cnf`, so that it comes up that way after a restart. No code the
application loads may put its connections back the other way, and no statement
the application sends may either. A scratch or diagnostic script of your own,
which opens its own connection, is yours: it is not code the application
loads.

The application does not check whether its writes succeed. Once the server
stops repairing them, some of its statements will stop writing their rows
altogether. **Find them and make the application supply what it had been
relying on the server to invent** — a value for every column it leaves unset,
of the type that column declares, and text each column can actually hold.

### What must not change

**The deployment must behave exactly as it does today**: the same requests
produce the same status, the same redirect target, and the same page content,
ignoring the session identifier and the order of the rows in the one route that
answers with a bare JSON array, and the same rows reach the same columns. In
particular:

- Text that a column's character set cannot represent is stored today with one
  `?` in place of each character the column cannot hold, and that is still
  what must be stored. Bytes that are not valid text on the connection at all
  reach such a column the same way, a `?` apiece, and that must not change
  either — but what the server does with those bytes is not the same for every
  column, so read what this deployment stores rather than carrying one
  column's answer to the next. Repairing such text is not this step's work,
  and doing it here changes what the deployment does.
- Every value the server invents today — for a column omitted from an
  `INSERT`, or sent a `NULL` it forbids — must still end up in that column,
  with the same value.

**Values too long for their column must not be silently shortened**, by the
server or by the application. A write that cannot store a value whole may
fail, and the request may be rejected; what it may not do is store a truncated
copy and report success.

**The schema stays as it is.** The same tables, with the same storage engines,
and the same columns: their types, lengths, character sets and collations
unchanged. Adding a `DEFAULT` to a column that lacks one, or making a column
`NOT NULL`, is allowed — widening a column, or moving one to a different
character set, is not.

**The data stays as it is.** The rows in the database when you are finished
must be the rows that are in it now. Exercising the application writes to it —
page-view statistics, accounts, lesson state — so if you drive it while
testing, put the data back afterwards; a copy of `/var/lib/mysql` taken with
the server stopped, or a dump reloaded, will do. Every request the front
controller serves is logged, so one page fetch is one row: make the restore the
last thing you do to the database, after any final check that the deployment
still answers.

`root` must still reach the `webgoatphp` database over
`/var/run/mysqld/mysqld.sock` with an empty password when you are done.

## 2. Read these local references

**Normative semantics:** the MySQL 5.7 Reference Manual, chapter *Server SQL
Modes*, defines what the server does with a value it cannot store and how that
is configured. **Local:** the application source under `/app`; the server's
configuration at `/etc/mysql/my.cnf`; the service wrapper at
`/usr/local/sbin/wg-services`, which documents what it starts and stops.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and in the server's configuration,
  and preserve the supplied source and data. Do not create a prose answer or
  an alternate entry point.
- What is kept from this session is `/app`, `/var/lib/mysql` and
  `/etc/mysql`. Nothing else is: the Apache and PHP configuration is supplied
  fresh, so a change made there does not carry over.
- Leave Apache and MySQL running, or leave them so that
  `/usr/local/sbin/wg-services start` brings them up.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace, its servers and the references above; do not
  search the system for other implementations or answers.
- Leave the deployment so that it comes back up from what is kept.

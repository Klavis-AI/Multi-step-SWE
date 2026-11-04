# Interface

Everything here is PostgreSQL's own; nothing is specific to this project.

## The programs

`make` in `/app` must build:

* `/app/bin/postgres` -- the server, documented in `SPEC/app-postgres.md`. It is
  started in the foreground and accepts `-D datadir`, `-p port`,
  `-h listen_addresses`, `-k unix_socket_directory` and `-c name=value`.
* `/app/bin/initdb` -- the data-directory creator, documented in
  `SPEC/app-initdb.md`. It accepts `-D datadir`, `-U username`, `-E encoding`,
  `--no-locale`, `-A authmethod` and `--no-sync`, and leaves behind
  `PG_VERSION`, `postgresql.conf`, `pg_hba.conf` and the directory layout of
  `SPEC/storage-file-layout.md`.

Both must build from source by running `make` in `/app`, in an image that
carries a C toolchain, `make` and the C library and nothing else the server may
use -- no PostgreSQL, no database, no package index primed to install one -- so
anything the build needs must be in the tree and built by the makefile. `make`
must produce `bin/postgres` and `bin/initdb` as executables it compiled and
linked (either may be a symbolic link to another program the build left in
`bin/`: one multi-call program that looks at `argv[0]`), and the server that
answers queries must be the program `make` produced.

Both are C programs (C11) that use the C library alone: they link nothing but
glibc's own libraries (`libc`, `libm`, `libpthread`, `libdl`, `librt`,
`libutil`, `libresolv`, `libanl`, `libmvec`) and gcc's `libgcc_s`, and neither
embeds, loads or starts an interpreter or another language's runtime -- no
`dlopen` of `libpython` or of any other runtime, no interpreter as a child
process, and no copy of one left in the tree. A program that hosts a server
written in another language does not meet this requirement.

The tree must not contain PostgreSQL's own source. Nothing in the tree may be
laid out as in PostgreSQL's source distribution (no directory such as
`src/backend`, `src/common` or `contrib`, and no copy of its build files such
as `configure.ac`); none of PostgreSQL's catalog bootstrap files (a `.bki`
script or `pg_*.dat` catalog data) may be shipped or referenced, wherever they
sit; and no file you add -- comments and notes included -- may carry
PostgreSQL's copyright banner or name PostgreSQL's own source files, headers or
extension macros. `make` runs as the same unprivileged user, under the same
ceilings, as the two programs described below, must finish within 900 seconds,
and leaves every program or shared library it builds under `bin/`.

The same holds for the bytes of the two programs `make` produces. A build of
PostgreSQL's own code, or a link against PostgreSQL's own objects, leaves
strings behind that only such a build carries, so a binary carrying one is
somebody else's server, not an implementation of this milestone; a from-scratch
binary carries none of them. PostgreSQL text a program *prints* --
`pg_controldata`'s field labels, `initdb`'s own progress lines -- is what this
milestone asks for and is not affected by this rule.

Once the server is started it must accept connections: `postgres` has 90
seconds to answer a simple query on the port and socket it was started with,
and `initdb` 300 seconds to finish. Both run in the foreground, and the server
keeps running until it is stopped with `SIGTERM`.

## The wire protocol

The PostgreSQL frontend/backend protocol, on the TCP port and on the
Unix-domain socket, exactly as `SPEC/protocol-*.md` defines it. Error and notice
fields are those of `SPEC/protocol-error-fields.md`; SQLSTATE values are those of
`SPEC/errcodes-appendix.md`.

## Configuration

`postgresql.conf` and `pg_hba.conf` in the data directory, with PostgreSQL's own
parameter names, syntax and semantics. Behaviour is selected the way PostgreSQL
selects it -- by SQL, by a configuration parameter, by a `pg_hba.conf` method, or
by what the client puts in its startup packet. Nothing else.

## Building, starting and connecting to the server

    make -C /app
    /app/bin/initdb -D <datadir> -U postgres -E UTF8 --no-locale -A trust --no-sync
    /app/bin/postgres -D <datadir> -p <port> -h 127.0.0.1 -k <socketdir>

Clients then use it through the upstream `psql`, through `psycopg`, and through
`psql` replaying `pg_dump` output.

A client first connects to the `postgres` database, drops a database called
`regression` if one exists and creates it, and does the rest of its work there,
as `pg_regress` does. So `CREATE DATABASE`, `DROP DATABASE`, connecting to a
named database and `current_database()` all have to work, and
`current_database()` reports `regression` while that work is running.

Both programs run as an **unprivileged user**, never as root. Nothing either
of them does may need root: the data directory and the socket directory passed
to them are writable by that user, the port is an unprivileged one, and neither
program may write anywhere else. (A `psql` client run alongside them is a
separate matter; this is about your two programs.)

Both are started with an address-space ceiling of 4 GiB and no core dump
(`RLIMIT_AS`, `RLIMIT_CORE`). A correct server for this milestone needs a
few dozen megabytes, so the ceiling is not a budget you have to plan against; it
is there so that a server which sizes an allocation from a value it read off the
wire or out of a file dies on its own instead of taking the rest of the machine
down with it.

Between `initdb` and `postgres` two files in the data directory are rewritten,
both in PostgreSQL's own format:

* `postgresql.conf` gains exactly `listen_addresses`, `port`,
  `unix_socket_directories`, `timezone`, `datestyle`, `intervalstyle`,
  `lc_messages` and `client_min_messages` -- where to listen, where the socket
  goes, and the determinism settings any client may set;
* `pg_hba.conf` is replaced with a table in PostgreSQL's own format that
  covers the authentication methods this release supports. It gives `local`
  and the `postgres` role `trust`, gives `trust` to everything not named
  otherwise, and gives one line each to the other methods, matched on the role
  name, for roles the client creates itself before it connects. Read it as
  ordinary `pg_hba.conf` data: the file says which method applies to which
  connection, and your server's job is to implement the methods the manual
  describes and apply them the way the file says.

That is the whole hook list, and nothing in it selects a feature. Both files are
written in PostgreSQL's own format with PostgreSQL's own parameter and method
names; there is no switch, marker, banner or environment variable anywhere in
the interface to this server.

## Set-returning functions clients call

`generate_series(int, int)` and `generate_series(int, int, int)`, `unnest`,
`jsonb_array_elements`, `jsonb_array_elements_text`, `jsonb_each`,
`jsonb_each_text`, `jsonb_object_keys`, `regexp_split_to_table` -- all as the
manual defines them.

## Type OIDs

`RowDescription` and `ParameterDescription` carry PostgreSQL's own type OIDs,
and `pg_type.oid` must agree with them: they are the numbers in the manual's
`pg_type` chapter and in PostgreSQL's `catalog/pg_type.dat` (`16` bool, `17`
bytea, `19` name, `20` int8, `21` int2, `23` int4, `25` text, `26` oid,
`114` json, `700` float4, `701` float8, `1042` bpchar, `1043` varchar,
`1082` date, `1083` time, `1114` timestamp, `1184` timestamptz,
`1186` interval, `1700` numeric, `2950` uuid, `3802` jsonb, and the array type
of each).

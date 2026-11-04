<!-- source: https://www.postgresql.org/docs/release/13.0/ -->

PostgreSQL: Release Notes

## Quick Links
Documentation
Manuals
Archive
Release Notes
Books
Tutorials & Other Resources
FAQ
Wiki

## Release Notes 

##  PostgreSQL 13.0 

 | E.24. Release 13
 | Prev | Up | Appendix E. Release Notes | Home | Next

## E.24. Release 13

E.24.1. Overview
E.24.2. Migration to Version 13
E.24.3. Changes
E.24.4. Acknowledgments
Release date: 2020-09-24

## E.24.1. Overview
PostgreSQL 13 contains many new features and enhancements, including:

Space savings and performance gains from de-duplication of B-tree index entries

Improved performance for queries that use aggregates or partitioned tables

Better query planning when using extended statistics

Parallelized vacuuming of indexes

Incremental sorting
The above items and other new features of PostgreSQL 13 are explained in more detail in the sections below.

## E.24.2. Migration to Version 13
A dump/restore using pg_dumpall or use of pg_upgrade or logical replication is required for those wishing to migrate data from any previous release. See Section 18.6 for general information on migrating to new major releases.
Version 13 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

Change SIMILAR TO ... ESCAPE NULL to return NULL (Tom Lane) Section 
This new behavior matches the SQL specification. Previously a null ESCAPE value was taken to mean using the default escape string (a backslash character). This also applies to substring(text FROM pattern ESCAPE text). The previous behavior has been retained in old views by keeping the original function unchanged.

Make json[b]_to_tsvector() fully check the spelling of its string option (Dominik Czarnota) Section 

Change the way non-default effective_io_concurrency values affect concurrency (Thomas Munro) Section 
Previously, this value was adjusted before setting the number of concurrent requests. The value is now used directly. Conversion of old values to new ones can be done using:
```

SELECT round(sum(OLDVALUE / n::float)) AS newvalue FROM generate_series(1, OLDVALUE) s(n);

```

Prevent display of auxiliary processes in pg_stat_ssl and pg_stat_gssapi system views (Euler Taveira) Section 
Queries that join these views to pg_stat_activity and wish to see auxiliary processes will need to use left joins.

Rename various wait events to improve consistency (Fujii Masao, Tom Lane) Section Section Section Section Section 

Fix ALTER FOREIGN TABLE ... RENAME COLUMN to return a more appropriate command tag (Fujii Masao) Section 
Previously it returned ALTER TABLE; now it returns ALTER FOREIGN TABLE.

Fix ALTER MATERIALIZED VIEW ... RENAME COLUMN to return a more appropriate command tag (Fujii Masao) Section 
Previously it returned ALTER TABLE; now it returns ALTER MATERIALIZED VIEW.

Rename configuration parameter wal_keep_segments to wal_keep_size (Fujii Masao) Section 
This determines how much WAL to retain for standby servers. It is specified in megabytes, rather than number of files as with the old parameter. If you previously used wal_keep_segments, the following formula will give you an approximately equivalent setting:
```

wal_keep_size = wal_keep_segments * wal_segment_size (typically 16MB)

```

Remove support for defining operator classes using pre-PostgreSQL 8.0 syntax (Daniel Gustafsson) Section 

Remove support for defining foreign key constraints using pre-PostgreSQL 7.3 syntax (Daniel Gustafsson) Section 

Remove support for "opaque" pseudo-types used by pre-PostgreSQL 7.3 servers (Daniel Gustafsson) Section 

Remove support for upgrading unpackaged (pre-9.1) extensions (Tom Lane) Section 
The FROM option of CREATE EXTENSION is no longer supported. Any installations still using unpackaged extensions should upgrade them to a packaged version before updating to PostgreSQL 13.

Remove support for posixrules files in the timezone database (Tom Lane) Section 
IANA's timezone group has deprecated this feature, meaning that it will gradually disappear from systems' timezone databases over the next few years. Rather than have a behavioral change appear unexpectedly with a timezone data update, we have removed PostgreSQL's support for this feature as of version 13. This affects only the behavior of POSIX-style time zone specifications that lack an explicit daylight savings transition rule; formerly the transition rule could be determined by installing a custom posixrules file, but now it is hard-wired. The recommended fix for any affected installations is to start using a geographical time zone name.

In ltree, when an lquery pattern contains adjacent asterisks with braces, e.g., *{2}.*{3}, properly interpret that as *{5} (Nikita Glukhov) Section 

Fix pageinspect's bt_metap() to return more appropriate data types that are less likely to overflow (Peter Geoghegan) Section 

## E.24.3. Changes
Below you will find a detailed account of the changes between PostgreSQL 13 and the previous major release.

## E.24.3.1. Server

E.24.3.1.1. Partitioning

Allow pruning of partitions to happen in more cases (Yuzuko Hosoya, Amit Langote, Alvaro Herrera) Section Section 

Allow partitionwise joins to happen in more cases (Ashutosh Bapat, Etsuro Fujita, Amit Langote, Tom Lane) Section Section 
For example, partitionwise joins can now happen between partitioned tables even when their partition bounds do not match exactly.

Support row-level BEFOREtriggers on partitioned tables (Alvaro Herrera) Section 
However, such a trigger is not allowed to change which partition is the destination.

Allow partitioned tables to be logically replicated via publications (Amit Langote) Section Section 
Previously, partitions had to be replicated individually. Now a partitioned table can be published explicitly, causing all its partitions to be published automatically. Addition/removal of a partition causes it to be likewise added to or removed from the publication. The CREATE PUBLICATION option publish_via_partition_root controls whether changes to partitions are published as their own changes or their parent's.

Allow logical replication into partitioned tables on subscribers (Amit Langote) Section 
Previously, subscribers could only receive rows into non-partitioned tables.

Allow whole-row variables (that is, table.*) to be used in partitioning expressions (Amit Langote) Section 

E.24.3.1.2. Indexes

More efficiently store duplicates in B-tree indexes (Anastasia Lubennikova, Peter Geoghegan) Section 
This allows efficient B-tree indexing of low-cardinality columns by storing duplicate keys only once. Users upgrading with pg_upgrade will need to use REINDEX to make an existing index use this feature.

Allow GiST and SP-GiST indexes on box columns to support ORDER BY box <-> point queries (Nikita Glukhov) Section Section 

Allow GIN indexes to more efficiently handle ! (NOT) clauses in tsquery searches (Nikita Glukhov, Alexander Korotkov, Tom Lane, Julien Rouhaud) Section 

Allow index operator classes to take parameters (Nikita Glukhov) Section 

Allow CREATE INDEX to specify the GiST signature length and maximum number of integer ranges (Nikita Glukhov) Section 
Indexes created on four and eight-byte integer array, tsvector, pg_trgm, ltree, and hstore columns can now control these GiST index parameters, rather than using the defaults.

Prevent indexes that use non-default collations from being added as a table's unique or primary key constraint (Tom Lane) Section 
The index's collation must match that of the underlying column, but ALTER TABLE previously failed to check this.

E.24.3.1.3. Optimizer

Improve the optimizer's selectivity estimation for containment/match operators (Tom Lane) Section 

Allow setting the statistics target for extended statistics (Tomas Vondra) Section 
This is controlled with the new command option ALTER STATISTICS ... SET STATISTICS. Previously this was computed based on more general statistics target settings.

Allow use of multiple extended statistics objects in a single query (Tomas Vondra) Section Section 

Allow use of extended statistics objects for OR clauses and IN/ANY constant lists (Pierre Ducroquet, Tomas Vondra) Section Section Section 

Allow functions in FROM clauses to be pulled up (inlined) if they evaluate to constants (Alexander Kuzmenkov, Aleksandr Parfenov) Section Section 

E.24.3.1.4. General Performance

Implement incremental sorting (James Coleman, Alexander Korotkov, Tomas Vondra) Section Section 
If an intermediate query result is known to be sorted by one or more leading keys of a required sort ordering, the additional sorting can be done considering only the remaining keys, if the rows are sorted in batches that have equal leading keys.
If necessary, this can be controlled using enable_incremental_sort.

Improve the performance of sorting inet values (Brandur Leach) Section 

Allow hash aggregation to use disk storage for large aggregation result sets (Jeff Davis) Section Section Section 
Previously, hash aggregation was avoided if it was expected to use more than work_mem memory. Now, a hash aggregation plan can be chosen despite that. The hash table will be spilled to disk if it exceeds work_mem times hash_mem_multiplier.
This behavior is normally preferable to the old behavior, in which once hash aggregation had been chosen, the hash table would be kept in memory no matter how large it got -- which could be very large if the planner had misestimated. If necessary, behavior similar to that can be obtained by increasing hash_mem_multiplier.

Allow inserts, not only updates and deletes, to trigger vacuuming activity in autovacuum (Laurenz Albe, Darafei Praliaskouski) Section 
Previously, insert-only activity would trigger auto-analyze but not auto-vacuum, on the grounds that there could not be any dead tuples to remove. However, a vacuum scan has other useful side-effects such as setting page-all-visible bits, which improves the efficiency of index-only scans. Also, allowing an insert-only table to receive periodic vacuuming helps to spread out the work of "freezing" old tuples, so that there is not suddenly a large amount of freezing work to do when the entire table reaches the anti-wraparound threshold all at once.
If necessary, this behavior can be adjusted with the new parameters autovacuum_vacuum_insert_threshold and autovacuum_vacuum_insert_scale_factor, or the equivalent table storage options.

Add maintenance_io_concurrency parameter to control I/O concurrency for maintenance operations (Thomas Munro) Section 

Allow WAL writes to be skipped during a transaction that creates or rewrites a relation, if wal_level is minimal (Kyotaro Horiguchi) Section 
Relations larger than wal_skip_threshold will have their files fsync'ed rather than generating WAL. Previously this was done only for COPY operations, but the implementation had a bug that could cause data loss during crash recovery.

Improve performance when replaying DROP DATABASE commands when many tablespaces are in use (Fujii Masao) Section 

Improve performance for truncation of very large relations (Kirk Jamison) Section 

Improve retrieval of the leading bytes of TOAST'ed values (Binguo Bao, Andrey Borodin) Section 
Previously, compressed out-of-line TOAST values were fully fetched even when it's known that only some leading bytes are needed. Now, only enough data to produce the result is fetched.

Improve performance of LISTEN/NOTIFY (Martijn van Oosterhout, Tom Lane) Section Section Section 

Speed up conversions of integers to text (David Fetter) Section 

Reduce memory usage for query strings and extension scripts that contain many SQL statements (Amit Langote) Section 

E.24.3.1.5. Monitoring

Allow EXPLAIN, auto_explain, autovacuum, and pg_stat_statements to track WAL usage statistics (Kirill Bychik, Julien Rouhaud) Section Section Section 

Allow a sample of SQL statements, rather than all statements, to be logged (Adrien Nayrat) Section 
A log_statement_sample_rate fraction of those statements taking more than log_min_duration_sample duration will be logged.

Add the backend type to csvlog and optionally log_line_prefix log output (Peter Eisentraut) Section 

Improve control of prepared statement parameter logging (Alexey Bashtanov, Alvaro Herrera) Section Section 
The GUC setting log_parameter_max_length controls the maximum length of parameter values output during logging of non-error statements, while log_parameter_max_length_on_error does the same for logging of statements with errors. Previously, prepared statement parameters were never logged during errors.

Allow function call backtraces to be logged after errors (Peter Eisentraut, Alvaro Herrera) Section Section 
The new parameter backtrace_functions specifies which C functions should generate backtraces on error.

Make vacuum buffer counters 64-bits wide to avoid overflow (Alvaro Herrera) Section 

E.24.3.1.6. System Views

Add leader_pid to pg_stat_activity to report a parallel worker's leader process (Julien Rouhaud) Section 

Add system view pg_stat_progress_basebackup to report the progress of streaming base backups (Fujii Masao) Section 

Add system view pg_stat_progress_analyze to report ANALYZE progress (Alvaro Herrera, Tatsuro Yamada, Vinayak Pokale) Section 

Add system view pg_shmem_allocations to display shared memory usage (Andres Freund, Robert Haas) Section 

Add system view pg_stat_slru to monitor internal SLRU caches (Tomas Vondra) Section 

Allow track_activity_query_size to be set as high as 1MB (Vyacheslav Makarov) Section 
The previous maximum was 100kB.

E.24.3.1.7. Wait Events

Report a wait event while creating a DSM segment with posix_fallocate() (Thomas Munro) Section 

Add wait event VacuumDelay to report on cost-based vacuum delay (Justin Pryzby) Section 

Add wait events for WAL archive and recovery pause (Fujii Masao) Section 
The new events are BackupWaitWalArchive and RecoveryPause.

Add wait events RecoveryConflictSnapshot and RecoveryConflictTablespace to monitor recovery conflicts (Masahiko Sawada) Section 

Improve performance of wait events on BSD-based systems (Thomas Munro) Section 

E.24.3.1.8. Authentication

Allow only superusers to view the ssl_passphrase_command setting (Insung Moon) Section 
This was changed as a security precaution.

Change the server's default minimum TLS version for encrypted connections from 1.0 to 1.2 (Peter Eisentraut) Section 
This choice can be controlled by ssl_min_protocol_version.

E.24.3.1.9. Server Configuration

Tighten rules on which utility commands are allowed in read-only transaction mode (Robert Haas) Section 
This change also increases the number of utility commands that can run in parallel queries.

Allow allow_system_table_mods to be changed after server start (Peter Eisentraut) Section 

Disallow non-superusers from modifying system tables when allow_system_table_mods is set (Peter Eisentraut) Section 
Previously, if allow_system_table_mods was set at server start, non-superusers could issue INSERT/UPDATE/DELETE commands on system tables.

Enable support for Unix-domain sockets on Windows (Peter Eisentraut) Section 

## E.24.3.2. Streaming Replication and Recovery

Allow streaming replication configuration settings to be changed by reload (Sergei Kornilov) Section 
Previously, a server restart was required to change primary_conninfo and primary_slot_name.

Allow WAL receivers to use a temporary replication slot when a permanent one is not specified (Peter Eisentraut, Sergei Kornilov) Section Section 
This behavior can be enabled using wal_receiver_create_temp_slot.

Allow WAL storage for replication slots to be limited by max_slot_wal_keep_size (Kyotaro Horiguchi) Section 
Replication slots that would require exceeding this value are marked invalid.

Allow standby promotion to cancel any requested pause (Fujii Masao) Section 
Previously, promotion could not happen while the standby was in paused state.

Generate an error if recovery does not reach the specified recovery target (Leif Gunnar Erlandsen, Peter Eisentraut) Section 
Previously, a standby would promote itself upon reaching the end of WAL, even if the target was not reached.

Allow control over how much memory is used by logical decoding before it is spilled to disk (Tomas Vondra, Dilip Kumar, Amit Kapila) Section 
This is controlled by logical_decoding_work_mem.

Allow recovery to continue even if invalid pages are referenced by WAL (Fujii Masao) Section 
This is enabled using ignore_invalid_pages.

## E.24.3.3. Utility Commands

Allow VACUUM to process a table's indexes in parallel (Masahiko Sawada, Amit Kapila) Section 
The new PARALLEL option controls this.

Allow FETCH FIRST to use WITH TIES to return any additional rows that match the last result row (Surafel Temesgen) Section 

Report planning-time buffer usage in EXPLAIN's BUFFER output (Julien Rouhaud) Section 

Make CREATE TABLE LIKE propagate a CHECK constraint's NO INHERIT property to the created table (Ildar Musin, Chris Travers) Section 

When using LOCK TABLE on a partitioned table, do not check permissions on the child tables (Amit Langote) Section 

Allow OVERRIDING USER VALUE on inserts into identity columns (Dean Rasheed) Section 

Add ALTER TABLE ... DROP EXPRESSION to allow removing the GENERATED property from a column (Peter Eisentraut) Section 

Fix bugs in multi-step ALTER TABLE commands (Tom Lane) Section 
IF NOT EXISTS clauses now work as expected, in that derived actions (such as index creation) do not execute if the column already exists. Also, certain cases of combining related actions into one ALTER TABLE now work when they did not before.

Add ALTER VIEW syntax to rename view columns (Fujii Masao) Section 
Renaming view columns was already possible, but one had to write ALTER TABLE RENAME COLUMN, which is confusing.

Add ALTER TYPE options to modify a base type's TOAST properties and support functions (Tomas Vondra, Tom Lane) Section 

Add CREATE DATABASELOCALE option (Peter Eisentraut) Section 
This combines the existing options LC_COLLATE and LC_CTYPE into a single option.

Allow DROP DATABASE to disconnect sessions using the target database, allowing the drop to succeed (Pavel Stehule, Amit Kapila) Section 
This is enabled by the FORCE option.

Add structure member tg_updatedcols to allow C-language update triggers to know which column(s) were updated (Peter Eisentraut) Section 

## E.24.3.4. Data Types

Add polymorphic data types for use by functions requiring compatible arguments (Pavel Stehule) Section 
The new data types are anycompatible, anycompatiblearray, anycompatiblenonarray, and anycompatiblerange.

Add SQL data type xid8 to expose FullTransactionId (Thomas Munro) Section 
The existing xid data type is only four bytes so it does not provide the transaction epoch.

Add data type regcollation and associated functions, to represent OIDs of collation objects (Julien Rouhaud) Section 

Use the glibc version in some cases as a collation version identifier (Thomas Munro) Section 
If the glibc version changes, a warning will be issued about possible corruption of collation-dependent indexes.

Add support for collation versions on Windows (Thomas Munro) Section 

Allow ROW expressions to have their members extracted with suffix notation (Tom Lane) Section 
For example, (ROW(4, 5.0)).f1 now returns 4.

## E.24.3.5. Functions

Add alternate version of jsonb_set() with improved NULL handling (Andrew Dunstan) Section 
The new function, jsonb_set_lax(), handles a NULL new value by either setting the specified key to a JSON null, deleting the key, raising an exception, or returning the jsonb value unmodified, as requested.

Add jsonpath .datetime() method (Nikita Glukhov, Teodor Sigaev, Oleg Bartunov, Alexander Korotkov) Section 
This function allows JSON values to be converted to timestamps, which can then be processed in jsonpath expressions. This change also adds jsonpath functions that support time-zone-aware output.

Add SQL functions NORMALIZE() to normalize Unicode strings, and IS NORMALIZED to check for normalization (Peter Eisentraut) Section 

Add min() and max() aggregates for pg_lsn (Fabrizio de Royes Mello) Section 
These are particularly useful in monitoring queries.

Allow Unicode escapes, e.g., E'\unnnn' or U&'\nnnn', to specify any character available in the database encoding, even when the database encoding is not UTF-8 (Tom Lane) Section 

Allow to_date() and to_timestamp() to recognize non-English month/day names (Juan Jose Santamaria Flecha, Tom Lane) Section 
The names recognized are the same as those output by to_char() with the same format patterns.

Add datetime format patterns FF1 - FF6 to specify input or output of 1 to 6 fractional-second digits (Alexander Korotkov, Nikita Glukhov, Teodor Sigaev, Oleg Bartunov) Section 
These patterns can be used by to_char(), to_timestamp(), and jsonpath's .datetime().

Add SSSSS datetime format pattern as an SQL-standard alias for SSSS (Nikita Glukhov, Alexander Korotkov) Section 

Add function gen_random_uuid() to generate version-4 UUIDs (Peter Eisentraut) Section 
Previously UUID generation functions were only available in the external modules uuid-ossp and pgcrypto.

Add greatest-common-denominator (gcd) and least-common-multiple (lcm) functions (Vik Fearing) Section 

Improve the performance and accuracy of the numeric type's square root (sqrt) and natural log (ln) functions (Dean Rasheed) Section Section 

Add function min_scale() that returns the number of digits to the right of the decimal point that are required to represent a numeric value with full accuracy (Pavel Stehule) Section 

Add function trim_scale() to reduce the scale of a numeric value by removing trailing zeros (Pavel Stehule) Section 

Add commutators of distance operators (Nikita Glukhov) Section 
For example, previously only point<->line was supported, now line<->point works too.

Create xid8 versions of all transaction ID functions (Thomas Munro) Section 
The old xid-based functions still exist, for backward compatibility.

Allow get_bit() and set_bit() to set bits beyond the first 256MB of a bytea value (Movead Li) Section 

Allow advisory-lock functions to be used in some parallel operations (Tom Lane) Section 

Add the ability to remove an object's dependency on an extension (Alvaro Herrera) Section 
The object can be a function, materialized view, index, or trigger. The syntax is ALTER .. NO DEPENDS ON.

## E.24.3.6. PL/pgSQL

Improve performance of simple PL/pgSQL expressions (Tom Lane, Amit Langote) Section 

Improve performance of PL/pgSQL functions that use immutable expressions (Konstantin Knizhnik) Section 

## E.24.3.7. Client Interfaces

Allow libpq clients to require channel binding for encrypted connections (Jeff Davis) Section 
Using the libpq connection parameter channel_binding forces the other end of the TLS connection to prove it knows the user's password. This prevents man-in-the-middle attacks.

Add libpq connection parameters to control the minimum and maximum TLS version allowed for an encrypted connection (Daniel Gustafsson) Section Section Section 
The settings are ssl_min_protocol_version and ssl_max_protocol_version. By default, the minimum TLS version is 1.2 (this represents a behavioral change from previous releases).

Allow use of passwords to unlock client certificates (Craig Ringer, Andrew Dunstan) Section 
This is enabled by libpq's sslpassword connection parameter.

Allow libpq to use DER-encoded client certificates (Craig Ringer, Andrew Dunstan) Section 

Fix ecpg's EXEC SQL elif directive to work correctly (Tom Lane) Section 
Previously it behaved the same as endif followed by ifdef, so that a successful previous branch of the same if construct did not prevent expansion of the elif branch or following branches.

## E.24.3.8. Client Applications

E.24.3.8.1. psql

Add transaction status (%x) to psql's default prompts (Vik Fearing) Section 

Allow the secondary psql prompt to be blank but the same width as the primary prompt (Thomas Munro) Section 
This is accomplished by setting PROMPT2 to %w.

Allow psql's \g and \gx commands to change \pset output options for the duration of that single command (Tom Lane) Section 
This feature allows syntax like \g (expand=on), which is equivalent to \gx.

Add psql commands to display operator classes and operator families (Sergey Cherkashin, Nikita Glukhov, Alexander Korotkov) Section 
The new commands are \dAc, \dAf, \dAo, and \dAp.

Show table persistence in psql's \dt+ and related commands (David Fetter) Section 
In verbose mode, the table/index/view shows if the object is permanent, temporary, or unlogged.

Improve output of psql's \d for TOAST tables (Justin Pryzby) Section 

Fix redisplay after psql's \e command (Tom Lane) Section 
When exiting the editor, if the query doesn't end with a semicolon or \g, the query buffer contents will now be displayed.

Add \warn command to psql (David Fetter) Section 
This is like \echo except that the text is sent to stderr instead of stdout.

Add the PostgreSQL home page to command-line --help output (Peter Eisentraut) Section 

E.24.3.8.2. pgbench

Allow pgbench to partition its "accounts" table (Fabien Coelho) Section 
This allows performance testing of partitioning.

Add pgbench command \aset, which behaves like \gset, but for multiple queries (Fabien Coelho) Section 

Allow pgbench to generate its initial data server-side, rather than client-side (Fabien Coelho) Section 

Allow pgbench to show script contents using option --show-script (Fabien Coelho) Section 

## E.24.3.9. Server Applications

Generate backup manifests for base backups, and verify them (Robert Haas) Section Section 
A new tool pg_verifybackup can verify backups.

Have pg_basebackup estimate the total backup size by default (Fujii Masao) Section 
This computation allows pg_stat_progress_basebackup to show progress. If that is not needed, it can be disabled by using the --no-estimate-size option. Previously, this computation happened only if the --progress option was used.

Add an option to pg_rewind to configure standbys (Paul Guo, Jimmy Yih, Ashwin Agrawal) Section 
This matches pg_basebackup's --write-recovery-conf option.

Allow pg_rewind to use the target cluster's restore_command to retrieve needed WAL (Alexey Kondratov) Section 
This is enabled using the -c/--restore-target-wal option.

Have pg_rewind automatically run crash recovery before rewinding (Paul Guo, Jimmy Yih, Ashwin Agrawal) Section 
This can be disabled by using --no-ensure-shutdown.

Increase the PREPARE TRANSACTION-related information reported by pg_waldump (Fujii Masao) Section 

Add pg_waldump option --quiet to suppress non-error output (Andres Freund, Robert Haas) Section 

Add pg_dump option --include-foreign-data to dump data from foreign servers (Luis Carril) Section 

Allow vacuum commands run by vacuumdb to operate in parallel mode (Masahiko Sawada) Section 
This is enabled with the new --parallel option.

Allow reindexdb to operate in parallel (Julien Rouhaud) Section 
Parallel mode is enabled with the new --jobs option.

Allow dropdb to disconnect sessions using the target database, allowing the drop to succeed (Pavel Stehule) Section 
This is enabled with the -f option.

Remove --adduser and --no-adduser from createuser (Alexander Lakhin) Section 
The long-supported preferred options for this are called --superuser and --no-superuser.

Use the directory of the pg_upgrade program as the default --new-bindir setting when running pg_upgrade (Daniel Gustafsson) Section 

## E.24.3.10. Documentation

Add a glossary to the documentation (Corey Huinker, Jurgen Purtz, Roger Harkavy, Alvaro Herrera) Section 

Reformat tables containing function and operator information for better clarity (Tom Lane) Section 

Upgrade to use DocBook 4.5 (Peter Eisentraut) Section 

## E.24.3.11. Source Code

Add support for building on Visual Studio 2019 (Haribabu Kommi) Section 

Add build support for MSYS2 (Peter Eisentraut) Section 

Add compare_exchange and fetch_add assembly language code for Power PC compilers (Noah Misch) Section 

Update Snowball stemmer dictionaries used by full text search (Panagiotis Mavrogiorgos) Section 
This adds Greek stemming and improves Danish and French stemming.

Remove support for Windows 2000 (Michael Paquier) Section 

Remove support for non-ELFBSD systems (Peter Eisentraut) Section 

Remove support for Python versions 2.5.X and earlier (Peter Eisentraut) Section 

Remove support for OpenSSL 0.9.8 and 1.0.0 (Michael Paquier) Section 

Remove configure options --disable-float8-byval and --disable-float4-byval (Peter Eisentraut) Section Section 
These were needed for compatibility with some version-zero C functions, but those are no longer supported.

Pass the query string to planner hook functions (Pascal Legrand, Julien Rouhaud) Section 

Add TRUNCATE command hook (Yuli Khodorkovskiy) Section 

Add TLS init hook (Andrew Dunstan) Section 

Allow building with no predefined Unix-domain socket directory (Peter Eisentraut) Section 

Reduce the probability of SysV resource key collision on Unix platforms (Tom Lane) Section 

Use operating system functions to reliably erase memory that contains sensitive information (Peter Eisentraut) Section 
For example, this is used for clearing passwords stored in memory.

Add headerscheck script to test C header-file compatibility (Tom Lane) Section 

Implement internal lists as arrays, rather than a chain of cells (Tom Lane) Section 
This improves performance for queries that access many objects.

Change the API for TS_execute() (Tom Lane, Pavel Borisov) Section Section 
TS_execute callbacks must now provide ternary (yes/no/maybe) logic. Calculating NOT queries accurately is now the default.

## E.24.3.12. Additional Modules

Allow extensions to be specified as trusted (Tom Lane) Section 
Such extensions can be installed in a database by users with database-level CREATE privileges, even if they are not superusers. This change also removes the pg_pltemplate system catalog.

Allow non-superusers to connect to postgres_fdw foreign servers without using a password (Craig Ringer) Section 
Specifically, allow a superuser to set password_required to false for a user mapping. Care must still be taken to prevent non-superusers from using superuser credentials to connect to the foreign server.

Allow postgres_fdw to use certificate authentication (Craig Ringer) Section 
Different users can use different certificates.

Allow sepgsql to control access to the TRUNCATE command (Yuli Khodorkovskiy) Section 

Add extension bool_plperl which transforms SQL booleans to/from PL/Perl booleans (Ivan Panchenko) Section 

Have pg_stat_statements treat SELECT ... FOR UPDATE commands as distinct from those without FOR UPDATE (Andrew Gierth, Vik Fearing) Section 

Allow pg_stat_statements to optionally track the planning time of statements (Julien Rouhaud, Pascal Legrand, Thomas Munro, Fujii Masao) Section Section 
Previously only execution time was tracked.

Overhaul ltree's lquery syntax to treat NOT (!) more logically (Filip Rembialkowski, Tom Lane, Nikita Glukhov) Section Section 
Also allow non-* queries to use a numeric range ({}) of matches.

Add support for binary I/O of ltree, lquery, and ltxtquery types (Nino Floris) Section 

Add an option to dict_int to ignore the sign of integers (Jeff Janes) Section 

Add adminpack function pg_file_sync() to allow fsync'ing a file (Fujii Masao) Section 

Add pageinspect functions to output t_infomask/t_infomask2 values in human-readable format (Craig Ringer, Sawada Masahiko, Michael Paquier) Section Section 

Add B-tree index de-duplication processing columns to pageinspect output (Peter Geoghegan) Section 

## E.24.4. Acknowledgments
The following individuals (in alphabetical order) have contributed to this release as patch authors, committers, reviewers, testers, or reporters of issues.

 | Abhijit Menon-Sen
 | Adam Lee
 | Adam Scott
 | Ade Heyward
 | Adrien Nayrat
 | Ahsan Hadi
 | Alastair McKinley
 | Aleksandr Parfenov
 | Alex Aktsipetrov
 | Alex Macy
 | Alex Shulgin
 | Alexander Korotkov
 | Alexander Kukushkin
 | Alexander Kuzmenkov
 | Alexander Lakhin
 | Alexey Bashtanov
 | Alexey Kondratov
 | Alvaro Herrera
 | Amit Kapila
 | Amit Khandekar
 | Amit Langote
 | Amul Sul
 | Anastasia Lubennikova
 | Andreas Joseph Krogh
 | Andreas Karlsson
 | Andreas Kunert
 | Andreas Seltenreich
 | Andrei Zubkov
 | Andres Freund
 | Andrew Bille
 | Andrew Dunstan
 | Andrew Gierth
 | Andrey Borodin
 | Andrey Klychkov
 | Andrey Lepikhov
 | Anna Akenteva
 | Anna Endo
 | Anthony Nowocien
 | Anton Vlasov
 | Antonin Houska
 | Ants Aasma
 | Arne Roland
 | Arnold Muller
 | Arseny Sher
 | Arthur Nascimento
 | Arthur Zakirov
 | Ashutosh Bapat
 | Ashutosh Sharma
 | Ashwin Agrawal
 | Asif Rehman
 | Asim Praveen
 | Atsushi Torikoshi
 | Augustinas Jokubauskas
 | Austin Drenski
 | Basil Bourque
 | Beena Emerson
 | Ben Cornett
 | Benjie Gillam
 | Benoit Lobreau
 | Bernd Helmle
 | Bharath Rupireddy
 | Bhargav Kamineni
 | Binguo Bao
 | Brad DeJong
 | Brandur Leach
 | Brent Bates
 | Brian Williams
 | Bruce Momjian
 | Cameron Ezell
 | Cary Huang
 | Chapman Flack
 | Charles Offenbacher
 | Chen Huajun
 | Chenyang Lu
 | Chris Bandy
 | Chris Travers
 | Christoph Berg
 | Christophe Courtois
 | Corey Huinker
 | Craig Ringer
 | Cuiping Lin
 | Dagfinn Ilmari Mannsaker
 | Daniel Fiori
 | Daniel Gustafsson
 | Daniel Verite
 | Daniel Westermann
 | Darafei Praliaskouski
 | Daryl Waycott
 | Dave Cramer
 | David Christensen
 | David Fetter
 | David G. Johnston
 | David Gilman
 | David Harper
 | David Rowley
 | David Steele
 | David Zhang
 | Davinder Singh
 | Dean Rasheed
 | Denis Stuchalin
 | Dent John
 | Didier Gautheron
 | Dilip Kumar
 | Dmitry Belyavsky
 | Dmitry Dolgov
 | Dmitry Ivanov
 | Dmitry Telpt
 | Dmitry Uspenskiy
 | Dominik Czarnota
 | Dongming Liu
 | Ed Morley
 | Edmund Horner
 | Emre Hasegeli
 | Eric Gillum
 | Erik Rijkers
 | Erwin Brandstetter
 | Ethan Waldo
 | Etsuro Fujita
 | Eugen Konkov
 | Euler Taveira
 | Fabien Coelho
 | Fabrizio de Royes Mello
 | Felix Lechner
 | Filip Janus
 | Filip Rembialkowski
 | Frank Gagnepain
 | Georgios Kokolatos
 | Gilles Darold
 | Greg Nancarrow
 | Grigory Smolkin
 | Guancheng Luo
 | Guillaume Lelarge
 | Hadi Moshayedi
 | Haiying Tang
 | Hamid Akhtar
 | Hans Buschmann
 | Hao Wu
 | Haribabu Kommi
 | Haruka Takatsuka
 | Heath Lord
 | Heikki Linnakangas
 | Himanshu Upadhyaya
 | Hironobu Suzuki
 | Hugh McMaster
 | Hugh Ranalli
 | Hugh Wang
 | Ian Barwick
 | Ibrar Ahmed
 | Ildar Musin
 | Insung Moon
 | Ireneusz Pluta
 | Isaac Morland
 | Ivan Kartyshov
 | Ivan Panchenko
 | Ivan Sergio Borgonovo
 | Jaime Casanova
 | James Coleman
 | James Gray
 | James Hunter
 | James Inform
 | James Lucas
 | Jan Mussler
 | Jaroslav Sivy
 | Jeevan Chalke
 | Jeevan Ladhe
 | Jeff Davis
 | Jeff Janes
 | Jehan-Guillaume de Rorthais
 | Jeremy Evans
 | Jeremy Schneider
 | Jeremy Smith
 | Jerry Sievers
 | Jesper Pedersen
 | Jesse Kinkead
 | Jesse Zhang
 | Jian Zhang
 | Jie Zhang
 | Jim Nasby
 | Jimmy Yih
 | Jobin Augustine
 | Joe Conway
 | John Hsu
 | John Naylor
 | Jon Jensen
 | Jonathan Katz
 | Jorge Gustavo Rocha
 | Josef Simanek
 | Joseph Nahmias
 | Juan Jose Santamaria Flecha
 | Julian Backes
 | Julien Rouhaud
 | Jurgen Purtz
 | Justin King
 | Justin Pryzby
 | Karl O. Pinc
 | Keisuke Kuroda
 | Keith Fiske
 | Kelly Min
 | Ken Tanzer
 | Kirill Bychik
 | Kirk Jamison
 | Konstantin Knizhnik
 | Kuntal Ghosh
 | Kyle Kingsbury
 | Kyotaro Horiguchi
 | Lars Kanis
 | Laurenz Albe
 | Leif Gunnar Erlandsen
 | Li Japin
 | Liudmila Mantrova
 | Lucas Viecelli
 | Luis M. Carril
 | Lukas Sobotka
 | Maciek Sakrejda
 | Magnus Hagander
 | Mahadevan Ramachandran
 | Mahendra Singh Thalor
 | Manuel Rigger
 | Marc Munro
 | Marcos David
 | Marina Polyakova
 | Mark Dilger
 | Mark Wong
 | Marko Tiikkaja
 | Markus Winand
 | Marti Raudsepp
 | Martijn van Oosterhout
 | Masahiko Sawada
 | Masahiro Ikeda
 | Masao Fujii
 | Mateusz Guzik
 | Matt Jibson
 | Matteo Beccati
 | Maxence Ahlouche
 | Melanie Plageman
 | Michael Banck
 | Michael Luo
 | Michael Meskes
 | Michael Paquier
 | Michail Nikolaev
 | Mike Palmiotto
 | Mithun Cy
 | Movead Li
 | Nathan Bossart
 | Nazli Ugur Koyluoglu
 | Neha Sharma
 | Nicola Contu
 | Nicolas Alvarez
 | Nikhil Sontakke
 | Nikita Glukhov
 | Nikolay Shaplov
 | Nino Floris
 | Noah Misch
 | Noriyoshi Shinoda
 | Oleg Bartunov
 | Oleg Samoilov
 | Oleksii Kliukin
 | Ondrej Jirman
 | Panagiotis Mavrogiorgos
 | Pascal Legrand
 | Patrick McHardy
 | Paul Guo
 | Paul Jungwirth
 | Paul Ramsey
 | Paul Sivash
 | Paul Spencer
 | Pavan Deolasee
 | Pavel Borisov
 | Pavel Luzanov
 | Pavel Stehule
 | Pavel Suderevsky
 | Peifeng Qiu
 | Pengzhou Tang
 | Peter Billen
 | Peter Eisentraut
 | Peter Geoghegan
 | Peter Smith
 | Petr Fedorov
 | Petr Jelinek
 | Phil Bayer
 | Philip Semanchuk
 | Philippe Beaudoin
 | Pierre Ducroquet
 | Pierre Giraud
 | Piotr Gabriel Kosinski
 | Piotr Wlodarczyk
 | Prabhat Sahu
 | Quan Zongliang
 | Quentin Rameau
 | Rafael Castro
 | Rafia Sabih
 | Raj Mohite
 | Rajkumar Raghuwanshi
 | Ramanarayana M
 | Ranier Vilela
 | Rares Salcudean
 | Raul Marin Rodriguez
 | Raymond Martin
 | Reijo Suhonen
 | Richard Guo
 | Robert Ford
 | Robert Haas
 | Robert Kahlert
 | Robert Treat
 | Robin Abbi
 | Robins Tharakan
 | Roger Harkavy
 | Roman Peshkurov
 | Rui DeSousa
 | Rui Hai Jiang
 | Rushabh Lathia
 | Ryan Lambert
 | Ryohei Takahashi
 | Scott Ribe
 | Sean Farrell
 | Sehrope Sarkuni
 | Sergei Agalakov
 | Sergei Kornilov
 | Sergey Cherkashin
 | Shawn Debnath
 | Shawn Wang
 | Shay Rojansky
 | Shenhao Wang
 | Simon Riggs
 | Slawomir Chodnicki
 | Soumyadeep Chakraborty
 | Stephane Lorek
 | Stephen Frost
 | Steve Rogerson
 | Steven Winfield
 | Surafel Temesgen
 | Suraj Kharage
 | Takanori Asaba
 | Takao Fujii
 | Takayuki Tsunakawa
 | Takuma Hoshiai
 | Tatsuhito Kasahara
 | Tatsuo Ishii
 | Tatsuro Yamada
 | Taylor Vesely
 | Teodor Sigaev
 | Tham Nguyen
 | Thibaut Madelaine
 | Thom Brown
 | Thomas Kellerer
 | Thomas Munro
 | Tiago Anastacio
 | Tim Clarke
 | Tim Mohlmann
 | Tom Ellis
 | Tom Gottfried
 | Tom Lane
 | Tomas Vondra
 | Tuomas Leikola
 | Tushar Ahuja
 | Victor Wagner
 | Victor Yegorov
 | Vignesh C
 | Vik Fearing
 | Vinay Banakar
 | Vladimir Leskov
 | Vladimir Sitnikov
 | Vyacheslav Makarov
 | Vyacheslav Shablistyy
 | Will Leinweber
 | William Crowell
 | Wyatt Alt
 | Yang Xiao
 | Yaroslav Schekin
 | Yi Huang
 | Yigong Hu
 | Yoann La Cancellera
 | Yoshikazu Imai
 | Yu Kimura
 | Yugo Nagata
 | Yuli Khodorkovskiy
 | Yusuke Egashira
 | Yuya Watari
 | Yuzuko Hosoya
 | ZhenHua Cai

 | Prev | Up | Next
 | E.23. Release 13.1  | Home |  E.25. Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 13.23 
 13.22 
 13.21 
 13.20 
 13.19 
 13.18 
 13.17 
 13.16 
 13.15 
 13.14 
 13.13 
 13.12 
 13.11 
 13.10 
 13.9 
 13.8 
 13.7 
 13.6 
 13.5 
 13.4 
 13.3 
 13.2 
 13.1 
 13.0

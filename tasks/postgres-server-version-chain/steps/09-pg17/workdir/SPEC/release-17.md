<!-- source: https://www.postgresql.org/docs/release/17.0/ -->

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

##  PostgreSQL 17.0 

 | E.12. Release 17
 | Prev | Up | Appendix E. Release Notes | Home | Next

## E.12. Release 17 #

E.12.1. Overview
E.12.2. Migration to Version 17
E.12.3. Changes
E.12.4. Acknowledgments
Release date: 2024-09-26

## E.12.1. Overview #
PostgreSQL 17 contains many new features and enhancements, including:

New memory management system for VACUUM, which reduces memory consumption and can improve overall vacuuming performance.

New SQL/JSON capabilities, including constructors, identity functions, and the JSON_TABLE() function, which converts JSON data into a table representation.

Various query performance improvements, including for sequential reads using streaming I/O, write throughput under high concurrency, and searches over multiple values in a btree index.

Logical replication enhancements, including:

Failover control

pg_createsubscriber, a utility that creates logical replicas from physical standbys

pg_upgrade now preserves logical replication slots on publishers and full subscription state on subscribers. This will allow upgrades to future major versions to continue logical replication without requiring copy to resynchronize.

New client-side connection option, sslnegotiation=direct, that performs a direct TLS handshake to avoid a round-trip negotiation.

pg_basebackup now supports incremental backup.

COPY adds a new option, ON_ERROR ignore, that allows a copy operation to continue in the event of an error.
The above items and other new features of PostgreSQL 17 are explained in more detail in the sections below.

## E.12.2. Migration to Version 17 #
A dump/restore using pg_dumpall or use of pg_upgrade or logical replication is required for those wishing to migrate data from any previous release. See Section 18.6 for general information on migrating to new major releases.
Version 17 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

Change functions to use a safe search_path during maintenance operations (Jeff Davis) Section Section 
This prevents maintenance operations (ANALYZE, CLUSTER, CREATE INDEX, CREATE MATERIALIZED VIEW, REFRESH MATERIALIZED VIEW, REINDEX, or VACUUM) from performing unsafe access. Functions used by expression indexes and materialized views that need to reference non-default schemas must specify a search path during function creation.

Restrict ago to only appear at the end in interval values (Joseph Koshakow) Section Section 
Also, prevent empty interval units from appearing multiple times.

Remove server variable old_snapshot_threshold (Thomas Munro) Section 
This variable allowed vacuum to remove rows that potentially could be still visible to running transactions, causing "snapshot too old" errors later if accessed. This feature might be re-added to PostgreSQL later if an improved implementation is found.

Change SET SESSION AUTHORIZATION handling of the initial session user's superuser status (Joseph Koshakow) Section 
The new behavior is based on the session user's superuser status at the time the SET SESSION AUTHORIZATION command is issued, rather than their superuser status at connection time.

Remove feature which simulated per-database users (Nathan Bossart) Section 
The feature, db_user_namespace, was rarely used.

Remove adminpack contrib extension (Daniel Gustafsson) Section 
This was used by now end-of-life pgAdmin III.

Remove wal_sync_method value fsync_writethrough on Windows (Thomas Munro) Section 
This value was the same as fsync on Windows.

Change file boundary handling of two WAL file name functions (Kyotaro Horiguchi, Andres Freund, Bruce Momjian) Section 
The functions pg_walfile_name() and pg_walfile_name_offset() used to report the previous LSN segment number when the LSN was on a file segment boundary; it now returns the current LSN segment.

Remove server variable trace_recovery_messages since it is no longer needed (Bharath Rupireddy) Section 

Remove information schema column element_types.domain_default (Peter Eisentraut) Section 

Change pgrowlocks lock mode output labels (Bruce Momjian) Section 

Remove buffers_backend and buffers_backend_fsync from pg_stat_bgwriter (Bharath Rupireddy) Section 
These fields are considered redundant to similar columns in pg_stat_io.

Rename I/O block read/write timing statistics columns of pg_stat_statements (Nazir Bilal Yavuz) Section 
This renames blk_read_time to shared_blk_read_time, and blk_write_time to shared_blk_write_time.

Change pg_attribute.attstattarget and pg_statistic_ext.stxstattarget to represent the default statistics target as NULL (Peter Eisentraut) Section Section 

Rename pg_collation.colliculocale to colllocale and pg_database.daticulocale to datlocale (Jeff Davis) Section 

Rename pg_stat_progress_vacuum column max_dead_tuples to max_dead_tuple_bytes, rename num_dead_tuples to num_dead_item_ids, and add dead_tuple_bytes (Masahiko Sawada) Section Section 

Rename SLRU columns in system view pg_stat_slru (Alvaro Herrera) Section 
The column names accepted by pg_stat_reset_slru() are also changed.

## E.12.3. Changes #
Below you will find a detailed account of the changes between PostgreSQL 17 and the previous major release.

## E.12.3.1. Server #

E.12.3.1.1. Optimizer #

Allow the optimizer to improve CTE plans by considering the statistics and sort order of columns referenced in earlier row output clauses (Jian Guo, Richard Guo, Tom Lane) Section Section 

Improve optimization of IS NOT NULL and IS NULL query restrictions (David Rowley, Richard Guo, Andy Fan) Section Section 
Remove IS NOT NULL restrictions from queries on NOT NULL columns and eliminate scans on NOT NULL columns if IS NULL is specified.

Allow partition pruning on boolean columns on IS [NOT] UNKNOWN conditionals (David Rowley) Section 

Improve optimization of range values when using containment operators <@ and @> (Kim Johan Andersson, Jian He) Section 

Allow correlated IN subqueries to be transformed into joins (Andy Fan, Tom Lane) Section 

Improve optimization of the LIMIT clause on partitioned tables, inheritance parents, and UNION ALL queries (Andy Fan, David Rowley) Section 

Allow query nodes to be run in parallel in more cases (Tom Lane) Section 

Allow GROUP BY columns to be internally ordered to match ORDER BY (Andrei Lepikhov, Teodor Sigaev) Section 
This can be disabled using server variable enable_group_by_reordering.

Allow UNION (without ALL) to use MergeAppend (David Rowley) Section 

Fix MergeAppend plans to more accurately compute the number of rows that need to be sorted (Alexander Kuzmenkov) Section 

Allow GiST and SP-GiST indexes to be part of incremental sorts (Miroslav Bendik) Section 
This is particularly useful for ORDER BY clauses where the first column has a GiST and SP-GiST index, and other columns do not.

Add columns to pg_stats to report range-type histogram information (Egor Rogov, Soumyadeep Chakraborty) Section 

E.12.3.1.2. Indexes #

Allow btree indexes to more efficiently find a set of values, such as those supplied by IN clauses using constants (Peter Geoghegan, Matthias van de Meent) Section 

Allow BRIN indexes to be created using parallel workers (Tomas Vondra, Matthias van de Meent) Section 

E.12.3.1.3. General Performance #

Allow vacuum to more efficiently remove and freeze tuples (Melanie Plageman, Heikki Linnakangas) Section 
WAL traffic caused by vacuum is also more compact.

Allow vacuum to more efficiently store tuple references (Masahiko Sawada, John Naylor) Section Section Section Section 
Additionally, vacuum is no longer silently limited to one gigabyte of memory when maintenance_work_mem or autovacuum_work_mem are higher.

Optimize vacuuming of relations with no indexes (Melanie Plageman) Section 

Increase default vacuum_buffer_usage_limit to 2MB (Thomas Munro) Section 

Improve performance when checking roles with many memberships (Nathan Bossart) Section 

Improve performance of heavily-contended WAL writes (Bharath Rupireddy) Section 

Improve performance when transferring large blocks of data to a client (Melih Mutlu) Section 

Allow the grouping of file system reads with the new system variable io_combine_limit (Thomas Munro, Andres Freund, Melanie Plageman, Nazir Bilal Yavuz) Section Section Section 

E.12.3.1.4. Monitoring #

Create system view pg_stat_checkpointer (Bharath Rupireddy, Anton A. Melnikov, Alexander Korotkov) Section Section Section 
Relevant columns have been removed from pg_stat_bgwriter and added to this new system view.

Improve control over resetting statistics (Atsushi Torikoshi, Bharath Rupireddy) Section Section Section 
Allow pg_stat_reset_shared() (with no arguments) and pg_stat_reset_shared(NULL) to reset all shared statistics. Allow pg_stat_reset_shared('slru') and pg_stat_reset_slru() (with no arguments) to reset SLRU statistics, which was already possible with pg_stat_reset_slru(NULL).

Add log messages related to WAL recovery from backups (Andres Freund) Section 

Add log_connections log line for trust connections (Jacob Champion) Section 

Add log message to report walsender acquisition and release of replication slots (Bharath Rupireddy) Section 
This is enabled by the server variable log_replication_commands.

Add system view pg_wait_events that reports wait event types (Bertrand Drouvot) Section 
This is useful for adding descriptions to wait events reported in pg_stat_activity.

Add wait events for checkpoint delays (Thomas Munro) Section 

Allow vacuum to report the progress of index processing (Sami Imseih) Section 
This appears in system view pg_stat_progress_vacuum columns indexes_total and indexes_processed.

E.12.3.1.5. Privileges #

Allow granting the right to perform maintenance operations (Nathan Bossart) Section 
The permission can be granted on a per-table basis using the MAINTAIN privilege and on a per-role basis via the pg_maintain predefined role. Permitted operations are VACUUM, ANALYZE, REINDEX, REFRESH MATERIALIZED VIEW, CLUSTER, and LOCK TABLE.

Allow roles with pg_monitor membership to execute pg_current_logfile() (Pavlo Golub, Nathan Bossart) Section 

E.12.3.1.6. Server Configuration #

Add system variable allow_alter_system to disallow ALTER SYSTEM (Jelte Fennema-Nio, Gabriele Bartolini) Section 

Allow ALTER SYSTEM to set unrecognized custom server variables (Tom Lane) Section 
This is also possible with GRANT ON PARAMETER.

Add server variable transaction_timeout to restrict the duration of transactions (Andrey Borodin, Japin Li, Junwang Zhao, Alexander Korotkov) Section Section Section 

Add a builtin platform-independent collation provider (Jeff Davis) Section Section Section Section 
This supports C and C.UTF-8 collations.

Add server variable huge_pages_status to report the use of huge pages by Postgres (Justin Pryzby) Section 
This is useful when huge_pages is set to try.

Add server variable to disable event triggers (Daniel Gustafsson) Section 
The setting, event_triggers, allows for the temporary disabling of event triggers for debugging.

Allow the SLRU cache sizes to be configured (Andrey Borodin, Dilip Kumar, Alvaro Herrera) Section 
The new server variables are commit_timestamp_buffers, multixact_member_buffers, multixact_offset_buffers, notify_buffers, serializable_buffers, subtransaction_buffers, and transaction_buffers. commit_timestamp_buffers, transaction_buffers, and subtransaction_buffers scale up automatically with shared_buffers.

E.12.3.1.7. Streaming Replication and Recovery #

Add support for incremental file system backup (Robert Haas, Jakub Wartak, Tomas Vondra) Section Section 
Incremental backups can be created using pg_basebackup's new --incremental option. The new application pg_combinebackup allows manipulation of base and incremental file system backups.

Allow the creation of WAL summarization files (Robert Haas, Nathan Bossart, Hubert Depesz Lubaczewski) Section Section Section Section 
These files record the block numbers that have changed within an LSN range and are useful for incremental file system backups. This is controlled by the server variables summarize_wal and wal_summary_keep_time, and introspected with pg_available_wal_summaries(), pg_wal_summary_contents(), and pg_get_wal_summarizer_state().

Add the system identifier to file system backup manifest files (Amul Sul) Section 
This helps detect invalid WAL usage.

Allow connection string value dbname to be written when pg_basebackup writes connection information to postgresql.auto.conf (Vignesh C, Hayato Kuroda) Section 

Add column pg_replication_slots.invalidation_reason to report the reason for invalid slots (Shveta Malik, Bharath Rupireddy) Section Section 

Add column pg_replication_slots.inactive_since to report slot inactivity duration (Bharath Rupireddy) Section Section Section 

Add function pg_sync_replication_slots() to synchronize logical replication slots (Hou Zhijie, Shveta Malik, Ajin Cherian, Peter Eisentraut) Section Section 

Add the failover property to the replication protocol (Hou Zhijie, Shveta Malik) Section 

E.12.3.1.8. Logical Replication#

Add application pg_createsubscriber to create a logical replica from a physical standby server (Euler Taveira) Section 

Have pg_upgrade migrate valid logical slots and subscriptions (Hayato Kuroda, Hou Zhijie, Vignesh C, Julien Rouhaud, Shlok Kyal) Section Section 
This allows logical replication to continue quickly after the upgrade. This only works for old PostgreSQL clusters that are version 17 or later.

Enable the failover of logical slots (Hou Zhijie, Shveta Malik, Ajin Cherian) Section 
This is controlled by an optional fifth argument to pg_create_logical_replication_slot().

Add server variable sync_replication_slots to enable failover logical slot synchronization (Shveta Malik, Hou Zhijie, Peter Smith) Section Section 

Add logical replication failover control to CREATE/ALTER SUBSCRIPTION (Shveta Malik, Hou Zhijie, Ajin Cherian) Section Section 

Allow the application of logical replication changes to use hash indexes on the subscriber (Hayato Kuroda) Section 
Previously only btree indexes could be used for this purpose.

Improve logical decoding performance in cases where there are many subtransactions (Masahiko Sawada) Section 

Restart apply workers if subscription owner's superuser privileges are revoked (Vignesh C) Section 
This forces reauthentication.

Add flush option to pg_logical_emit_message() (Michael Paquier) Section 
This makes the message durable.

Allow specification of physical standbys that must be synchronized before they are visible to subscribers (Hou Zhijie, Shveta Malik) Section Section 
The new server variable is synchronized_standby_slots.

Add worker type column to pg_stat_subscription (Peter Smith) Section 

## E.12.3.2. Utility Commands #

Add new COPY option ON_ERROR ignore to discard error rows (Damir Belyalov, Atsushi Torikoshi, Alex Shulgin, Jian He, Yugo Nagata) Section Section Section Section 
The default behavior is ON_ERROR stop.

Add new COPY option LOG_VERBOSITY which reports COPY FROM ignored error rows (Bharath Rupireddy) Section 

Allow COPY FROM to report the number of skipped rows during processing (Atsushi Torikoshi) Section 
This appears in system view column pg_stat_progress_copy.tuples_skipped.

In COPY FROM, allow easy specification that all columns should be forced null or not null (Zhang Mingli) Section 

Allow partitioned tables to have identity columns (Ashutosh Bapat) Section 

Allow exclusion constraints on partitioned tables (Paul A. Jungwirth) Section 
As long as exclusion constraints compare partition key columns for equality, other columns can use exclusion constraint-specific comparisons.

Add clearer ALTER TABLE method to set a column to the default statistics target (Peter Eisentraut) Section 
The new syntax is ALTER TABLE ... SET STATISTICS DEFAULT; using SET STATISTICS -1 is still supported.

Allow ALTER TABLE to change a column's generation expression (Amul Sul) Section 
The syntax is ALTER TABLE ... ALTER COLUMN ... SET EXPRESSION.

Allow specification of table access methods on partitioned tables (Justin Pryzby, Soumyadeep Chakraborty, Michael Paquier) Section Section 

Add DEFAULT setting for ALTER TABLE .. SET ACCESS METHOD (Michael Paquier) Section 

Add support for event triggers that fire at connection time (Konstantin Knizhnik, Mikhail Gribkov) Section 

Add event trigger support for REINDEX (Garrett Thornburg, Jian He) Section 

Allow parenthesized syntax for CLUSTER options if a table name is not specified (Nathan Bossart) Section 

E.12.3.2.1. EXPLAIN#

Allow EXPLAIN to report optimizer memory usage (Ashutosh Bapat) Section 
The option is called MEMORY.

Add EXPLAIN option SERIALIZE to report the cost of converting data for network transmission (Stepan Rutz, Matthias van de Meent) Section 

Add local I/O block read/write timing statistics to EXPLAIN's BUFFERS output (Nazir Bilal Yavuz) Section 

Improve EXPLAIN's display of SubPlan nodes and output parameters (Tom Lane, Dean Rasheed) Section 

Add JITdeform_counter details to EXPLAIN (Dmitry Dolgov) Section 

## E.12.3.3. Data Types #

Allow the interval data type to support +/-infinity values (Joseph Koshakow, Jian He, Ashutosh Bapat) Section 

Allow the use of an ENUM added via ALTER TYPE if the type was created in the same transaction (Tom Lane) Section 
This was previously disallowed.

## E.12.3.4. MERGE#

Allow MERGE to modify updatable views (Dean Rasheed) Section 

Add WHEN NOT MATCHED BY SOURCE to MERGE (Dean Rasheed) Section 
WHEN NOT MATCHED on target rows was already supported.

Allow MERGE to use the RETURNING clause (Dean Rasheed) Section 
The new RETURNING function merge_action() reports on the DML that generated the row.

## E.12.3.5. Functions #

Add function JSON_TABLE() to convert JSON data to a table representation (Nikita Glukhov, Teodor Sigaev, Oleg Bartunov, Alexander Korotkov, Andrew Dunstan, Amit Langote, Jian He) Section Section 
This function can be used in the FROM clause of SELECT queries as a tuple source.

Add SQL/JSON constructor functions JSON(), JSON_SCALAR(), and JSON_SERIALIZE() (Nikita Glukhov, Teodor Sigaev, Oleg Bartunov, Alexander Korotkov, Andrew Dunstan, Amit Langote) Section 

Add SQL/JSON query functions JSON_EXISTS(), JSON_QUERY(), and JSON_VALUE() (Nikita Glukhov, Teodor Sigaev, Oleg Bartunov, Alexander Korotkov, Andrew Dunstan, Amit Langote, Peter Eisentraut, Jian He) Section Section Section Section Section 

Add jsonpath methods to convert JSON values to other JSON data types (Jeevan Chalke) Section 
The jsonpath methods are .bigint(), .boolean(), .date(), .decimal([precision [, scale]]), .integer(), .number(), .string(), .time(), .time_tz(), .timestamp(), and .timestamp_tz().

Add to_timestamp() time zone format specifiers (Tom Lane) Section 
TZ accepts time zone abbreviations or numeric offsets, while OF accepts only numeric offsets.

Allow the session time zone to be specified by AT LOCAL (Vik Fearing) Section 
This is useful when converting adding and removing time zones from time stamps values, rather than specifying the literal session time zone.

Add functions uuid_extract_timestamp() and uuid_extract_version() to return UUID information (Andrey Borodin) Section 

Add functions to generate random numbers in a specified range (Dean Rasheed) Section 
The functions are random(min, max) and they take values of type integer, bigint, and numeric.

Add functions to convert integers to binary and octal strings (Eric Radman, Nathan Bossart) Section 
The functions are to_bin() and to_oct().

Add Unicode informational functions (Jeff Davis) Section 
Function unicode_version() returns the Unicode version, icu_unicode_version() returns the ICU version, and unicode_assigned() returns if the characters are assigned Unicode codepoints.

Add function xmltext() to convert text to a single XML text node (Jim Jones) Section 

Add function to_regtypemod() to return the type modifier of a type specification (David Wheeler, Erik Wienhold) Section 

Add pg_basetype() function to return a domain's base type (Steve Chavez) Section 

Add function pg_column_toast_chunk_id() to return a value's TOAST identifier (Yugo Nagata) Section 
This returns NULL if the value is not stored in TOAST.

## E.12.3.6. PL/pgSQL#

Allow plpgsql %TYPE and %ROWTYPE specifications to represent arrays of non-array types (Quan Zongliang, Pavel Stehule) Section 

Allow plpgsql %TYPE specification to reference composite column (Tom Lane) Section 

## E.12.3.7. libpq#

Add libpq function to change role passwords (Joe Conway) Section 
The new function, PQchangePassword(), hashes the new password before sending it to the server.

Add libpq functions to close portals and prepared statements (Jelte Fennema-Nio) Section 
The functions are PQclosePrepared(), PQclosePortal(), PQsendClosePrepared(), and PQsendClosePortal().

Add libpq API which allows for blocking and non-blocking cancel requests, with encryption if already in use (Jelte Fennema-Nio) Section 
Previously only blocking, unencrypted cancel requests were supported.

Add libpq function PQsocketPoll() to allow polling of network sockets (Tristan Partin, Tom Lane) Section Section 

Add libpq function PQsendPipelineSync() to send a pipeline synchronization point (Anton Kirilov) Section 
This is similar to PQpipelineSync() but it does not flush to the server unless the size threshold of the output buffer is reached.

Add libpq function PQsetChunkedRowsMode() to allow retrieval of results in chunks (Daniel Verite) Section 

Allow TLS connections without requiring a network round-trip negotiation (Greg Stark, Heikki Linnakangas, Peter Eisentraut, Michael Paquier, Daniel Gustafsson) Section Section Section Section Section Section Section Section 
This is enabled with the client-side option sslnegotiation=direct, requires ALPN, and only works on PostgreSQL 17 and later servers.

## E.12.3.8. psql#

Improve psql display of default and empty privileges (Erik Wienhold, Laurenz Albe) Section 
Command \dp now displays (none) for empty privileges; default still displays as empty.

Have backslash commands honor \pset null (Erik Wienhold, Laurenz Albe) Section 
Previously \pset null was ignored.

Allow psql's \watch to stop after a minimum number of rows returned (Greg Sabino Mullane) Section 
The parameter is min_rows.

Allow psql connection attempts to be canceled with control-C (Tristan Partin) Section 

Allow psql to honor FETCH_COUNT for non-SELECT queries (Daniel Verite) Section 

Improve psql tab completion (Dagfinn Ilmari Mannsaker, Gilles Darold, Christoph Heiss, Steve Chavez, Vignesh C, Pavel Borisov, Jian He) Section Section Section Section Section Section Section Section 

## E.12.3.9. Server Applications #

Add application pg_walsummary to dump WAL summary files (Robert Haas) Section 

Allow pg_dump's large objects to be restorable in batches (Tom Lane) Section 
This allows the restoration of many large objects to avoid transaction limits and to be restored in parallel.

Add pg_dump option --exclude-extension (Ayush Vatsa) Section 

Allow pg_dump, pg_dumpall, and pg_restore to specify include/exclude objects in a file (Pavel Stehule, Daniel Gustafsson) Section 
The option is called --filter.

Add the --sync-method parameter to several client applications (Justin Pryzby, Nathan Bossart) Section 
The applications are initdb, pg_basebackup, pg_checksums, pg_dump, pg_rewind, and pg_upgrade.

Add pg_restore option --transaction-size to allow object restores in transaction batches (Tom Lane) Section 
This allows the performance benefits of transaction batches without the problems of excessively large transaction blocks.

Change pgbench debug mode option from -d to --debug (Greg Sabino Mullane) Section 
Option -d is now used for the database name, and the new --dbname option can be used as well.

Add pgbench option --exit-on-abort to exit after any client aborts (Yugo Nagata) Section 

Add pgbench command \syncpipeline to allow sending of sync messages (Anthonin Bonnefoy) Section 

Allow pg_archivecleanup to remove backup history files (Atsushi Torikoshi) Section 
The option is --clean-backup-history.

Add some long options to pg_archivecleanup (Atsushi Torikoshi) Section 
The long options are --debug, --dry-run, and --strip-extension.

Allow pg_basebackup and pg_receivewal to use dbname in their connection specification (Jelte Fennema-Nio) Section 
This is useful for connection poolers that are sensitive to the database name.

Add pg_upgrade option --copy-file-range (Thomas Munro) Section 
This is supported on Linux and FreeBSD.

Allow reindexdb--index to process indexes from different tables in parallel (Maxim Orlov, Svetlana Derevyanko, Alexander Korotkov) Section 

Allow reindexdb, vacuumdb, and clusterdb to process objects in all databases matching a pattern (Nathan Bossart) Section Section Section 
The new option --all controls this behavior.

## E.12.3.10. Source Code #

Remove support for OpenSSL 1.0.1 (Michael Paquier) Section 

Allow tests to pass in OpenSSLFIPS mode (Peter Eisentraut) Section Section 

Use CPU AVX-512 instructions for bit counting (Paul Amonson, Nathan Bossart, Ants Aasma) Section Section 

Require LLVM version 10 or later (Thomas Munro) Section 

Use native CRC instructions on 64-bit LoongArch CPUs (Xudong Yang) Section 

Remove AIX support (Heikki Linnakangas) Section 

Remove the Microsoft Visual Studio-specific PostgreSQL build option (Michael Paquier) Section 
Meson is now the only available method for Visual Studio builds.

Remove configure option --disable-thread-safety (Thomas Munro, Heikki Linnakangas) Section Section 
We now assume all supported platforms have sufficient thread support.

Remove configure option --with-CC (Heikki Linnakangas) Section 
Setting the CC environment variable is now the only supported method for specifying the compiler.

User-defined data type receive functions will no longer receive their data null-terminated (David Rowley) Section 

Add incremental JSON parser for use with huge JSON documents (Andrew Dunstan) Section 

Convert top-level README file to Markdown (Nathan Bossart) Section 

Remove no longer needed top-level INSTALL file (Tom Lane) Section 

Remove make's distprep option (Peter Eisentraut) Section 

Add make support for Android shared libraries (Peter Eisentraut) Section 

Add backend support for injection points (Michael Paquier) Section Section Section Section 
This is used for server debugging and they must be enabled at server compile time.

Add dynamic shared memory registry (Nathan Bossart) Section 
This allows shared libraries which are not initialized at startup to coordinate dynamic shared memory access.

Fix emit_log_hook to use the same time value as other log records for the same query (Kambam Vinay, Michael Paquier) Section 

Improve documentation for using jsonpath for predicate checks (David Wheeler) Section 

## E.12.3.11. Additional Modules #

Allow joins with non-join qualifications to be pushed down to foreign servers and custom scans (Richard Guo, Etsuro Fujita) Section 
Foreign data wrappers and custom scans will need to be modified to handle these cases.

Allow pushdown of EXISTS and IN subqueries to postgres_fdw foreign servers (Alexander Pyhalov) Section 

Increase the default foreign data wrapper tuple cost (David Rowley, Umair Shahid) Section Section 
This value is used by the optimizer.

Allow dblink database operations to be interrupted (Noah Misch) Section 

Allow the creation of hash indexes on ltree columns (Tommy Pavlicek) Section 
This also enables hash join and hash aggregation on ltree columns.

Allow unaccent character translation rules to contain whitespace and quotes (Michael Paquier) Section 
The syntax for the unaccent.rules file has changed.

Allow amcheck to check for unique constraint violations using new option --checkunique (Anastasia Lubennikova, Pavel Borisov, Maxim Orlov) Section 

Allow citext tests to pass in OpenSSL FIPS mode (Peter Eisentraut) Section 

Allow pgcrypto tests to pass in OpenSSL FIPS mode (Peter Eisentraut) Section 

Remove some unused SPI macros (Bharath Rupireddy) Section 

Allow ALTER OPERATOR to set more optimization attributes (Tommy Pavlicek) Section 
This is useful for extensions.

Allow extensions to define custom wait events (Masahiro Ikeda) Section Section Section Section 
Custom wait events have been added to postgres_fdw and dblink.

Add pg_buffercache function pg_buffercache_evict() to allow shared buffer eviction (Palak Chaturvedi, Thomas Munro) Section 
This is useful for testing.

E.12.3.11.1. pg_stat_statements#

Replace CALL parameters in pg_stat_statements with placeholders (Sami Imseih) Section 

Replace savepoint names stored in pg_stat_statements with placeholders (Greg Sabino Mullane) Section 
This greatly reduces the number of entries needed to record SAVEPOINT, RELEASE SAVEPOINT, and ROLLBACK TO SAVEPOINT commands.

Replace the two-phase commit GIDs stored in pg_stat_statements with placeholders (Michael Paquier) Section 
This greatly reduces the number of entries needed to record PREPARE TRANSACTION, COMMIT PREPARED, and ROLLBACK PREPARED.

Track DEALLOCATE in pg_stat_statements (Dagfinn Ilmari Mannsaker, Michael Paquier) Section 
DEALLOCATE names are stored in pg_stat_statements as placeholders.

Add local I/O block read/write timing statistics columns of pg_stat_statements (Nazir Bilal Yavuz) Section Section 
The new columns are local_blk_read_time and local_blk_write_time.

Add JIT deform_counter details to pg_stat_statements (Dmitry Dolgov) Section 

Add optional fourth argument (minmax_only) to pg_stat_statements_reset() to allow for the resetting of only min/max statistics (Andrei Zubkov) Section 
This argument defaults to false.

Add pg_stat_statements columns stats_since and minmax_stats_since to track entry creation time and last min/max reset time (Andrei Zubkov) Section 

## E.12.4. Acknowledgments #
The following individuals (in alphabetical order) have contributed to this release as patch authors, committers, reviewers, testers, or reporters of issues.

 | Abhijit Menon-Sen
 | Adnan Dautovic
 | Aidar Imamov
 | Ajin Cherian
 | Akash Shankaran
 | Akshat Jaimini
 | Alaa Attya
 | Aleksander Alekseev
 | Aleksej Orlov
 | Alena Rybakina
 | Alex Hsieh
 | Alex Malek
 | Alex Shulgin
 | Alex Work
 | Alexander Korotkov
 | Alexander Kozhemyakin
 | Alexander Kuzmenkov
 | Alexander Lakhin
 | Alexander Pyhalov
 | Alexey Palazhchenko
 | Alfons Kemper
 | Alvaro Herrera
 | Amadeo Gallardo
 | Amit Kapila
 | Amit Langote
 | Amul Sul
 | Anastasia Lubennikova
 | Anatoly Zaretsky
 | Andreas Karlsson
 | Andreas Ulbrich
 | Andrei Lepikhov
 | Andrei Zubkov
 | Andres Freund
 | Andrew Alsup
 | Andrew Atkinson
 | Andrew Bille
 | Andrew Dunstan
 | Andrew Kane
 | Andrey Borodin
 | Andrey Rachitskiy
 | Andrey Sokolov
 | Andy Fan
 | Anthonin Bonnefoy
 | Anthony Hsu
 | Anton Kirilov
 | Anton Melnikov
 | Anton Voloshin
 | Antonin Houska
 | Ants Aasma
 | Antti Lampinen
 | Aramaki Zyake
 | Artem Anisimov
 | Artur Zakirov
 | Ashutosh Bapat
 | Ashutosh Sharma
 | Atsushi Torikoshi
 | Attila Gulyas
 | Ayush Tiwari
 | Ayush Vatsa
 | Bartosz Chrol
 | Benoit Ryder
 | Bernd Helmle
 | Bertrand Drouvot
 | Bharath Rupireddy
 | Bo Andreson
 | Boshomi Phenix
 | Bowen Shi
 | Boyu Yang
 | Bruce Momjian
 | Cameron Vogt
 | Cary Huang
 | Cedric Villemain
 | Changhong Fei
 | Chantal Keller
 | Chapman Flack
 | Chengxi Sun
 | Chris Travers
 | Christian Maurer
 | Christian Stork
 | Christoph Berg
 | Christoph Heiss
 | Christophe Courtois
 | Christopher Kline
 | Claudio Freire
 | Colin Caine
 | Corey Huinker
 | Curt Kolovson
 | Dag Lem
 | Dagfinn Ilmari Mannsaker
 | Damir Belyalov
 | Daniel Fredouille
 | Daniel Gustafsson
 | Daniel Shelepanov
 | Daniel Verite
 | Daniel Westermann
 | Darren Rush
 | Dave Cramer
 | Dave Page
 | David Christensen
 | David Cook
 | David G. Johnston
 | David Geier
 | David Hillman
 | David Perez
 | David Rowley
 | David Steele
 | David Wheeler
 | David Zhang
 | Dean Rasheed
 | Denis Erokhin
 | Denis Laxalde
 | Devrim Gunduz
 | Dilip Kumar
 | Dimitrios Apostolou
 | Dmitry Dolgov
 | Dmitry Koval
 | Dmitry Vasiliev
 | Dominique Devienne
 | Dong Wook Lee
 | Donghang Lin
 | Dongming Liu
 | Drew Callahan
 | Drew Kimball
 | Dzmitry Jachnik
 | Egor Chindyaskin
 | Egor Rogov
 | Ekaterina Kiryanova
 | Elena Indrupskaya
 | Elizabeth Christensen
 | Emre Hasegeli
 | Eric Cyr
 | Eric Mutta
 | Eric Radman
 | Eric Ridge
 | Erik Rijkers
 | Erik Wienhold
 | Erki Eessaar
 | Ethan Mertz
 | Etsuro Fujita
 | Eugen Konkov
 | Euler Taveira
 | Evan Macbeth
 | Evgeny Morozov
 | Fabien Coelho
 | Fabrizio de Royes Mello
 | Farias de Oliveira
 | Feliphe Pozzer
 | Fire Emerald
 | Flavien Guedez
 | Floris Van Nee
 | Francesco Degrassi
 | Frank Streitzig
 | Gabriele Bartolini
 | Garrett Thornburg
 | Gavin Flower
 | Gavin Panella
 | Gilles Darold
 | Gilles Parc
 | Grant Gryczan
 | Greg Nancarrow
 | Greg Sabino Mullane
 | Greg Stark
 | Gurjeet Singh
 | Haiying Tang
 | Hajime Matsunaga
 | Hal Takahara
 | Hanefi Onaldi
 | Hannu Krosing
 | Hans Buschmann
 | Hao Wu
 | Hao Zhang
 | Hayato Kuroda
 | Heikki Linnakangas
 | Hemanth Sandrana
 | Himanshu Upadhyaya
 | Hironobu Suzuki
 | Holger Reise
 | Hongxu Ma
 | Hongyu Song
 | Horst Reiterer
 | Hubert Lubaczewski
 | Hywel Carver
 | Ian Barwick
 | Ian Ilyasov
 | Ilya Nenashev
 | Isaac Morland
 | Israel Barth Rubio
 | Ivan Kartyshov
 | Ivan Kolombet
 | Ivan Lazarev
 | Ivan Panchenko
 | Ivan Trofimov
 | Jacob Champion
 | Jacob Speidel
 | Jacques Combrink
 | Jaime Casanova
 | Jakub Wartak
 | James Coleman
 | James Pang
 | Jani Rahkola
 | Japin Li
 | Jeevan Chalke
 | Jeff Davis
 | Jeff Janes
 | Jelte Fennema-Nio
 | Jeremy Schneider
 | Jian Guo
 | Jian He
 | Jim Jones
 | Jim Keener
 | Jim Nasby
 | Jingtang Zhang
 | Jingxian Li
 | Jingzhou Fu
 | Joe Conway
 | Joel Jacobson
 | John Ekins
 | John Hsu
 | John Morris
 | John Naylor
 | John Russell
 | Jonathan Katz
 | Jordi Gutierrez
 | Joseph Koshakow
 | Josh Kupershmidt
 | Joshua D. Drake
 | Joshua Uyehara
 | Jubilee Young
 | Julien Rouhaud
 | Junwang Zhao
 | Justin Pryzby
 | Kaido Vaikla
 | Kambam Vinay
 | Karen Talarico
 | Karina Litskevich
 | Karl O. Pinc
 | Kashif Zeeshan
 | Kim Johan Andersson
 | Kirill Reshke
 | Kirk Parker
 | Kirk Wolak
 | Kisoon Kwon
 | Koen De Groote
 | Kohei KaiGai
 | Kong Man
 | Konstantin Knizhnik
 | Kouhei Sutou
 | Krishnakumar R
 | Kuntal Ghosh
 | Kurt Roeckx
 | Kyotaro Horiguchi
 | Lang Liu
 | Lars Kanis
 | Laurenz Albe
 | Lauri Laanmets
 | Legs Mansion
 | Lukas Fittl
 | Magnus Hagander
 | Mahendrakar Srinivasarao
 | Maiquel Grassi
 | Manos Emmanouilidis
 | Marcel Hofstetter
 | Marcos Pegoraro
 | Marian Krucina
 | Marina Polyakova
 | Mark Dilger
 | Mark Guertin
 | Mark Sloan
 | Markus Winand
 | Marlene Reiterer
 | Martin Marques
 | Martin Nash
 | Martin Schlossarek
 | Masahiko Sawada
 | Masahiro Ikeda
 | Masaki Kuwamura
 | Masao Fujii
 | Mason Sharp
 | Matheus Alcantara
 | Mats Kindahl
 | Matthias Kuhn
 | Matthias van de Meent
 | Maxim Boguk
 | Maxim Orlov
 | Maxim Yablokov
 | Maxime Boyer
 | Melanie Plageman
 | Melih Mutlu
 | Merlin Moncure
 | Micah Gate
 | Michael Banck
 | Michael Bondarenko
 | Michael Paquier
 | Michael Wang
 | Michael Zhilin
 | Michail Nikolaev
 | Michal Bartak
 | Michal Kleczek
 | Mikhail Gribkov
 | Mingli Zhang
 | Miroslav Bendik
 | Mitsuru Hinata
 | Moaaz Assali
 | Muralikrishna Bandaru
 | Nathan Bossart
 | Nazir Bilal Yavuz
 | Neil Tiffin
 | Ngigi Waithaka
 | Nikhil Benesch
 | Nikhil Raj
 | Nikita Glukhov
 | Nikita Kalinin
 | Nikita Malakhov
 | Nikolay Samokhvalov
 | Nikolay Shaplov
 | Nisha Moond
 | Nishant Sharma
 | Nitin Jadhav
 | Noah Misch
 | Noriyoshi Shinoda
 | Ole Peder Brandtzg
 | Oleg Bartunov
 | Oleg Sibiryakov
 | Oleg Tselebrovskiy
 | Olleg Samoylov
 | Onder Kalaci
 | Ondrej Navratil
 | Pablo Kharo
 | Palak Chaturvedi
 | Pantelis Theodosiou
 | Paul Amonson
 | Paul Jungwirth
 | Pavel Borisov
 | Pavel Kulakov
 | Pavel Luzanov
 | Pavel Stehule
 | Pavlo Golub
 | Pedro Gallegos
 | Pete Storer
 | Peter Eisentraut
 | Peter Geoghegan
 | Peter Smith
 | Philip Warner
 | Philipp Salvisberg
 | Pierre Ducroquet
 | Pierre Fortin
 | Przemyslaw Sztoch
 | Quynh Tran
 | Raghuveer Devulapalli
 | Ranier Vilela
 | Reid Thompson
 | Rian McGuire
 | Richard Guo
 | Richard Vesely
 | Ridvan Korkmaz
 | Robert Haas
 | Robert Scott
 | Robert Treat
 | Roberto Mello
 | Robins Tharakan
 | Roman Lozko
 | Ronan Dunklau
 | Rui Zhao
 | Ryo Matsumura
 | Ryoga Yoshida
 | Sameer Kumar
 | Sami Imseih
 | Samuel Dussault
 | Sanjay Minni
 | Satoru Koizumi
 | Sebastian Skalacki
 | Sergei Glukhov
 | Sergei Kornilov
 | Sergey Prokhorenko
 | Sergey Sargsyan
 | Sergey Shinderuk
 | Shaozhong Shi
 | Shaun Thomas
 | Shay Rojansky
 | Shihao Zhong
 | Shinya Kato
 | Shlok Kyal
 | Shruthi Gowda
 | Shubham Khanna
 | Shulin Zhou
 | Shveta Malik
 | Simon Riggs
 | Soumyadeep Chakraborty
 | Sravan Velagandula
 | Stan Hu
 | Stepan Neretin
 | Stepan Rutz
 | Stephane Schildknecht
 | Stephane Tachoires
 | Stephen Frost
 | Steve Atkins
 | Steve Chavez
 | Suraj Khamkar
 | Suraj Kharage
 | Svante Richter
 | Svetlana Derevyanko
 | Sylvain Frandaz
 | Takayuki Tsunakawa
 | Tatsuo Ishii
 | Tatsuro Yamada
 | Tender Wang
 | Teodor Sigaev
 | Thom Brown
 | Thomas Munro
 | Tim Carey-Smith
 | Tim Needham
 | Tim Palmer
 | Tobias Bussmann
 | Tom Lane
 | Tomas Vondra
 | Tommy Pavlicek
 | Tomonari Katsumata
 | Tristan Partin
 | Tristen Raab
 | Tung Nguyen
 | Umair Shahid
 | Uwe Binder
 | Valerie Woolard
 | Vallimaharajan G
 | Vasya Boytsov
 | Victor Wagner
 | Victor Yegorov
 | Victoria Shepard
 | Vidushi Gupta
 | Vignesh C
 | Vik Fearing
 | Viktor Leis
 | Vinayak Pokale
 | Vitaly Burovoy
 | Vojtech Benes
 | Wei Sun
 | Wei Wang
 | Wenjiang Zhang
 | Will Mortensen
 | Willi Mann
 | Wolfgang Walther
 | Xiang Liu
 | Xiaoran Wang
 | Xing Guo
 | Xudong Yang
 | Yahor Yuzefovich
 | Yajun Hu
 | Yaroslav Saburov
 | Yong Li
 | Yongtao Huang
 | Yugo Nagata
 | Yuhang Qiu
 | Yuki Seino
 | Yura Sokolov
 | Yurii Rashkovskii
 | Yuuki Fujii
 | Yuya Watari
 | Yves Colin
 | Zhihong Yu
 | Zhijie Hou
 | Zongliang Quan
 | Zubeyr Eryilmaz
 | Zuming Jiang

 | Prev | Up | Next
 | E.11. Release 17.1  | Home |  E.13. Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 17.11 
 17.10 
 17.9 
 17.8 
 17.7 
 17.6 
 17.5 
 17.4 
 17.3 
 17.2 
 17.1 
 17.0

<!-- source: https://www.postgresql.org/docs/release/15.0/ -->

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

##  PostgreSQL 15.0 

 | E.20. Release 15
 | Prev | Up | Appendix E. Release Notes | Home | Next

## E.20. Release 15

E.20.1. Overview
E.20.2. Migration to Version 15
E.20.3. Changes
E.20.4. Acknowledgments
Release date: 2022-10-13

## E.20.1. Overview
PostgreSQL 15 contains many new features and enhancements, including:

Support for the SQLMERGE command.

Selective publication of tables' contents within logical replication publications, through the ability to specify column lists and row filter conditions.

More options for compression, including support for Zstandard (zstd) compression. This includes support for performing compression on the server side during pg_basebackup.

Support for structured server log output using the JSON format.

Performance improvements, particularly for in-memory and on-disk sorting.
The above items and other new features of PostgreSQL 15 are explained in more detail in the sections below.

## E.20.2. Migration to Version 15
A dump/restore using pg_dumpall or use of pg_upgrade or logical replication is required for those wishing to migrate data from any previous release. See Section 19.6 for general information on migrating to new major releases.
Version 15 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

Remove PUBLIC creation permission on the public schema (Noah Misch) Section 
The new default is one of the secure schema usage patterns that Section 5.9.6 has recommended since the security release for CVE-2018-1058. The change applies to new database clusters and to newly-created databases in existing clusters. Upgrading a cluster or restoring a database dump will preserve public's existing permissions.
For existing databases, especially those having multiple users, consider revoking CREATE permission on the public schema to adopt this new default. For new databases having no need to defend against insider threats, granting CREATE permission will yield the behavior of prior releases.

Change the owner of the public schema to be the new pg_database_owner role (Noah Misch) Section 
This allows each database's owner to have ownership privileges on the public schema within their database. Previously it was owned by the bootstrap superuser, so that non-superuser database owners could not do anything with it.
This change applies to new database clusters and to newly-created databases in existing clusters. Upgrading a cluster or restoring a database dump will preserve public's existing ownership specification.

Remove long-deprecated exclusive backup mode (David Steele, Nathan Bossart) Section 
If the database server stops abruptly while in this mode, the server could fail to start. The non-exclusive backup mode is considered superior for all purposes. Functions pg_start_backup()/pg_stop_backup() have been renamed to pg_backup_start()/pg_backup_stop(), and the functions pg_backup_start_time() and pg_is_in_backup() have been removed.

Increase hash_mem_multiplier default to 2.0 (Peter Geoghegan) Section 
This allows query hash operations to use more work_mem memory than other operations.

Remove server-side language plpython2u and generic Python language plpythonu (Andres Freund) Section 
Python 2.x is no longer supported. While the original intent of plpythonu was that it could eventually refer to plpython3u, changing it now seems more likely to cause problems than solve them, so it's just been removed.

Generate an error if array_to_tsvector() is passed an empty-string array element (Jean-Christophe Arnu) Section 
This is prohibited because lexemes should never be empty. Users of previous Postgres releases should verify that no empty lexemes are stored because they can lead to dump/restore failures and inconsistent results.

Generate an error when chr() is supplied with a negative argument (Peter Eisentraut) Section 

Prevent CREATE OR REPLACE VIEW from changing the collation of an output column (Tom Lane) Section 

Disallow zero-length Unicode identifiers, e.g., U&"" (Peter Eisentraut) Section 
Non-Unicode zero-length identifiers were already disallowed.

Prevent numeric literals from having non-numeric trailing characters (Peter Eisentraut) Section 
Previously, query text like 123abc would be interpreted as 123 followed by a separate token abc.

Adjust JSON numeric literal processing to match the SQL/JSON-standard (Peter Eisentraut) Section 
This accepts numeric formats like .1 and 1., and disallows trailing junk after numeric literals, like 1.type().

When interval input provides a fractional value for a unit greater than months, round to the nearest month (Bruce Momjian) Section 
For example, convert 1.99 years to 2 years, not 1 year 11 months as before.

Improve consistency of interval parsing with trailing periods (Tom Lane) Section 
Numbers with trailing periods were rejected on some platforms.

Mark the interval output function as stable, not immutable, since it depends on IntervalStyle (Tom Lane) Section 
This will, for example, cause creation of indexes relying on the text output of interval values to fail.

Detect integer overflow in interval justification functions (Joe Koshakow) Section 
The affected functions are justify_interval(), justify_hours(), and justify_days().

Change the I/O format of type "char" for non-ASCII characters (Tom Lane) Section 
Bytes with the high bit set are now output as a backslash and three octal digits, to avoid encoding issues.

Remove the default ADMIN OPTION privilege a login role has on its own role membership (Robert Haas) Section 
Previously, a login role could add/remove members of its own role, even without ADMIN OPTION privilege.

Allow logical replication to run as the owner of the subscription (Mark Dilger) Section 
Because row-level security policies are not checked, only superusers, roles with bypassrls, and table owners can replicate into tables with row-level security policies.

Prevent UPDATE and DELETElogical replication operations on tables where the subscription owner does not have SELECT permission on the table (Jeff Davis) Section 
UPDATE and DELETE commands typically involve reading the table as well, so require the subscription owner to have table SELECT permission.

When EXPLAIN references the session's temporary object schema, refer to it as pg_temp (Amul Sul) Section 
Previously the actual schema name was reported, leading to inconsistencies across sessions.

Fix pg_statio_all_tables to sum values for the rare case of TOAST tables with multiple indexes (Andrei Zubkov) Section 
Previously such cases would show one row for each index.

Disallow setting custom options that match the name of an installed extension, but are not one of the extension's declared variables (Florin Irion, Tom Lane) Section Section Section 
This change causes any such pre-existing variables to be deleted during extension load, and then prevents new ones from being created later in the session. The intent is to prevent confusion about whether a variable is associated with an extension or not.

Remove obsolete server variable stats_temp_directory (Andres Freund, Kyotaro Horiguchi) Section 

Improve the algorithm used to compute random() (Fabien Coelho) Section Section 
This will cause random()'s results to differ from what was emitted by prior versions, even for the same seed value.

libpq's PQsendQuery() function is no longer supported in pipeline mode (Alvaro Herrera) Section 
Applications that are using that combination will need to be modified to use PQsendQueryParams() instead.

On non-Windows platforms, consult the HOME environment variable to find the user's home directory (Anders Kaseorg) Section 
If HOME is empty or unset, fall back to the previous method of checking the <pwd.h> database. This change affects libpq (for example, while looking up ~/.pgpass) as well as various client application programs.

Remove pg_dump's --no-synchronized-snapshots option (Tom Lane) Section 
All still-supported server versions support synchronized snapshots, so there's no longer a need for this option.

After an error is detected in psql's --single-transaction mode, change the final COMMIT command to ROLLBACK only if ON_ERROR_STOP is set (Michael Paquier) Section 

Avoid unnecessary casting of constants in queries sent by postgres_fdw (Dian Fay) Section 
When column types are intentionally different between local and remote databases, such casts could cause errors.

Remove xml2's xml_is_well_formed() function (Tom Lane) Section 
This function has been implemented in the core backend since Postgres 9.1.

Allow custom scan providers to indicate if they support projections (Sven Klemm) Section 
The default is now that custom scan providers are assumed to not support projections; those that do will need to be updated for this release.

## E.20.3. Changes
Below you will find a detailed account of the changes between PostgreSQL 15 and the previous major release.

## E.20.3.1. Server

Record and check the collation version of each database (Peter Eisentraut) Section 
This feature is designed to detect collation version changes to avoid index corruption. Function pg_database_collation_actual_version() reports the underlying operating system collation version, and ALTER DATABASE ... REFRESH sets the recorded database collation version to match the operating system collation version.

Allow ICU collations to be set as the default for clusters and databases (Peter Eisentraut) Section 
Previously, only libc-based collations could be selected at the cluster and database levels. ICU collations could only be used via explicit COLLATE clauses.

Add system view pg_ident_file_mappings to report pg_ident.conf information (Julien Rouhaud) Section 

E.20.3.1.1. Partitioning

Improve planning time for queries referencing partitioned tables (David Rowley) Section 
This change helps when only a few of many partitions are relevant.

Allow ordered scans of partitions to avoid sorting in more cases (David Rowley) Section 
Previously, a partitioned table with a DEFAULT partition or a LIST partition containing multiple values could not be used for ordered partition scans. Now they can be used if such partitions are pruned during planning.

Improve foreign key behavior of updates on partitioned tables that move rows between partitions (Amit Langote) Section 
Previously, such updates ran a delete action on the source partition and an insert action on the target partition. PostgreSQL will now run an update action on the partition root, providing cleaner semantics.

Allow CLUSTER on partitioned tables (Justin Pryzby) Section Section 

Fix ALTER TRIGGER RENAME on partitioned tables to properly rename triggers on all partitions (Arne Roland, Alvaro Herrera) Section 
Also prohibit cloned triggers from being renamed.

E.20.3.1.2. Indexes

Allow btree indexes on system and TOAST tables to efficiently store duplicates (Peter Geoghegan) Section 
Previously de-duplication was disabled for these types of indexes.

Improve lookup performance of GiST indexes that were built using sorting (Aliaksandr Kalenik, Sergei Shoulbakov, Andrey Borodin) Section 

Allow unique constraints and indexes to treat NULL values as not distinct (Peter Eisentraut) Section 
Previously NULL entries were always treated as distinct values, but this can now be changed by creating constraints and indexes using UNIQUE NULLS NOT DISTINCT.

Allow the ^@ starts-with operator and the starts_with() function to use btree indexes if using the C collation (Tom Lane) Section 
Previously these could only use SP-GiST indexes.

E.20.3.1.3. Optimizer

Allow extended statistics to record statistics for a parent with all its children (Tomas Vondra, Justin Pryzby) Section 
Regular statistics already tracked parent and parent-plus-all-children statistics separately.

Add server variable recursive_worktable_factor to allow the user to specify the expected size of the working table of a recursive query (Simon Riggs) Section 

E.20.3.1.4. General Performance

Allow hash lookup for NOT IN clauses with many constants (David Rowley, James Coleman) Section 
Previously the code always sequentially scanned the list of values.

Allow SELECT DISTINCT to be parallelized (David Rowley) Section 

Speed up encoding validation of UTF-8 text by processing 16 bytes at a time (John Naylor, Heikki Linnakangas) Section 
This will improve text-heavy operations like COPY FROM.

Improve performance for sorts that exceed work_mem (Heikki Linnakangas) Section Section 
When the sort data no longer fits in work_mem, switch to a batch sorting algorithm that uses more output streams than before.

Improve performance and reduce memory consumption of in-memory sorts (Ronan Dunklau, David Rowley, Thomas Munro, John Naylor) Section Section Section 

Allow WALfull page writes to use LZ4 and Zstandard compression (Andrey Borodin, Justin Pryzby) Section Section 
This is controlled by the wal_compression server setting.

Add support for writing WAL using direct I/O on macOS (Thomas Munro) Section 
This only works if max_wal_senders = 0 and wal_level = minimal.

Allow vacuum to be more aggressive in setting the oldest frozen and multi transaction id (Peter Geoghegan) Section 

Allow a query referencing multiple foreign tables to perform parallel foreign table scans in more cases (Andrey Lepikhov, Etsuro Fujita) Section 

Improve the performance of window functions that use row_number(), rank(), dense_rank() and count() (David Rowley) Section 

Improve the performance of spinlocks on high-core-count ARM64 systems (Geoffrey Blake) Section 

E.20.3.1.5. Monitoring

Enable default logging of checkpoints and slow autovacuum operations (Bharath Rupireddy) Section 
This changes the default of log_checkpoints to on and that of log_autovacuum_min_duration to 10 minutes. This will cause even an idle server to generate some log output, which might cause problems on resource-constrained servers without log file rotation. These defaults should be changed in such cases.

Generate progress messages in the server log during slow server starts (Nitin Jadhav, Robert Haas) Section Section 
The messages report the cause of the delay. The time interval for notification is controlled by the new server variable log_startup_progress_interval.

Store cumulative statistics system data in shared memory (Kyotaro Horiguchi, Andres Freund, Melanie Plageman) Section Section 
Previously this data was sent to a statistics collector process via UDP packets, and could only be read by sessions after transferring it via the file system. There is no longer a separate statistics collector process.

Add additional information to VACUUM VERBOSE and autovacuum logging messages (Peter Geoghegan) Section Section Section 

Add EXPLAIN (BUFFERS) output for temporary file block I/O (Masahiko Sawada) Section 

Allow log output in JSON format (Sehrope Sarkuni, Michael Paquier) Section 
The new setting is log_destination = jsonlog.

Allow pg_stat_reset_single_table_counters() to reset the counters of relations shared across all databases (Sadhuprasad Patro) Section 

Add wait events for local shell commands (Fujii Masao) Section 
The new wait events are used when calling archive_command, archive_cleanup_command, restore_command and recovery_end_command.

E.20.3.1.6. Privileges

Allow table accesses done by a view to optionally be controlled by privileges of the view's caller (Christoph Heiss) Section 
Previously, view accesses were always treated as being done by the view's owner. That's still the default.

Allow members of the pg_write_server_files predefined role to perform server-side base backups (Dagfinn Ilmari Mannsaker) Section 
Previously only superusers could perform such backups.

Allow GRANT to grant permissions to change individual server variables via SET and ALTER SYSTEM (Mark Dilger) Section Section 
The new function has_parameter_privilege() reports on this privilege.

Add predefined role pg_checkpoint that allows members to run CHECKPOINT (Jeff Davis) Section Section 
Previously checkpoints could only be run by superusers.

Allow members of the pg_read_all_stats predefined role to access the views pg_backend_memory_contexts and pg_shmem_allocations (Bharath Rupireddy) Section 
Previously these views could only be accessed by superusers.

Allow GRANT to grant permissions on pg_log_backend_memory_contexts() (Jeff Davis) Section 
Previously this function could only be run by superusers.

E.20.3.1.7. Server Configuration

Add server variable shared_memory_size to report the size of allocated shared memory (Nathan Bossart) Section Section 

Add server variable shared_memory_size_in_huge_pages to report the number of huge memory pages required (Nathan Bossart) Section Section 
This is only supported on Linux.

Honor server variable shared_preload_libraries in single-user mode (Jeff Davis) Section 
This change supports use of shared_preload_libraries to load custom access methods and WAL resource managers, which would be essential for database access even in single-user mode.

On Solaris, make the default setting of dynamic_shared_memory_type be sysv (Thomas Munro) Section 
The previous default choice, posix, can result in spurious failures on this platform.

Allow postgres -C to properly report runtime-computed values (Nathan Bossart) Section 
Previously runtime-computed values data_checksums, wal_segment_size, and data_directory_mode would report values that would not be accurate on the running server. However, this does not work on a running server.

## E.20.3.2. Streaming Replication and Recovery

Add support for LZ4 and Zstandard compression of server-side base backups (Jeevan Ladhe, Robert Haas) Section Section Section 

Run the checkpointer and bgwriter processes during crash recovery (Thomas Munro) Section 
This helps to speed up long crash recoveries.

Allow WAL processing to pre-fetch needed file contents (Thomas Munro) Section 
This is controlled by the server variable recovery_prefetch.

Allow archiving via loadable modules (Nathan Bossart) Section 
Previously, archiving was only done by calling shell commands. The new server variable archive_library can be set to specify a library to be called for archiving.

No longer require IDENTIFY_SYSTEM to be run before START_REPLICATION (Jeff Davis) Section 

E.20.3.2.1. Logical Replication

Allow publication of all tables in a schema (Vignesh C, Hou Zhijie, Amit Kapila) Section Section Section 
For example, this syntax is now supported: CREATE PUBLICATION pub1 FOR TABLES IN SCHEMA s1,s2. ALTER PUBLICATION supports a similar syntax. Tables added later to the listed schemas will also be replicated.

Allow publication content to be filtered using a WHERE clause (Hou Zhijie, Euler Taveira, Peter Smith, Ajin Cherian, Tomas Vondra, Amit Kapila) Section Section Section 
Rows not satisfying the WHERE clause are not published.

Allow publication content to be restricted to specific columns (Tomas Vondra, Alvaro Herrera, Rahila Syed) Section 

Allow skipping of transactions on a subscriber using ALTER SUBSCRIPTION ... SKIP (Masahiko Sawada) Section 

Add support for prepared (two-phase) transactions to logical replication (Peter Smith, Ajin Cherian, Amit Kapila, Nikhil Sontakke, Stas Kelvich) Section Section Section 
The new CREATE_REPLICATION_SLOT option is called TWO_PHASE. pg_recvlogical now supports a new --two-phase option during slot creation.

Prevent logical replication of empty transactions (Ajin Cherian, Hou Zhijie, Euler Taveira) Section 
Previously, publishers would send empty transactions to subscribers if subscribed tables were not modified.

Add SQL functions to monitor the directory contents of logical replication slots (Bharath Rupireddy) Section 
The new functions are pg_ls_logicalsnapdir(), pg_ls_logicalmapdir(), and pg_ls_replslotdir(). They can be run by members of the predefined pg_monitor role.

Allow subscribers to stop the application of logical replication changes on error (Osumi Takamichi, Mark Dilger) Section 
This is enabled with the subscriber option disable_on_error and avoids possible infinite error loops during stream application.

Adjust subscriber server variables to match the publisher so datetime and float8 values are interpreted consistently (Japin Li) Section 
Some publishers might be relying on inconsistent behavior.

Add system view pg_stat_subscription_stats to report on subscriber activity (Masahiko Sawada) Section Section 
The new function pg_stat_reset_subscription_stats() allows resetting these statistics counters.

Suppress duplicate entries in the pg_publication_tables system view (Hou Zhijie) Section 
In some cases a partition could appear more than once.

## E.20.3.3. Utility Commands

Add SQLMERGE command to adjust one table to match another (Simon Riggs, Pavan Deolasee, Alvaro Herrera, Amit Langote) Section 
This is similar to INSERT ... ON CONFLICT but more batch-oriented.

Add support for HEADER option in COPY text format (Remi Lapeyre) Section Section 
The new option causes the column names to be output, and optionally verified on input.

Add new WAL-logged method for database creation (Dilip Kumar) Section 
This is the new default method for copying the template database, as it avoids the need for checkpoints during database creation. However, it might be slow if the template database is large, so the old method is still available.

Allow CREATE DATABASE to set the database OID (Shruthi Gowda, Antonin Houska) Section 

Prevent DROP DATABASE, DROP TABLESPACE, and ALTER DATABASE SET TABLESPACE from occasionally failing during concurrent use on Windows (Thomas Munro) Section 

Allow foreign key ON DELETE SET actions to affect only specified columns (Paul Martinez) Section 
Previously, all of the columns in the foreign key were always affected.

Allow ALTER TABLE to modify a table's ACCESS METHOD (Justin Pryzby, Jeff Davis) Section 

Properly call object access hooks when ALTER TABLE causes table rewrites (Michael Paquier) Section 

Allow creation of unlogged sequences (Peter Eisentraut) Section 

Track dependencies on individual columns in the results of functions returning composite types (Tom Lane) Section 
Previously, if a view or rule contained a reference to a specific column within the result of a composite-returning function, that was not noted as a dependency; the view or rule was only considered to depend on the composite type as a whole. This meant that dropping the individual column would be allowed, causing problems in later use of the view or rule. The column-level dependency is now also noted, so that dropping such a column will be rejected unless the view is changed or dropped.

## E.20.3.4. Data Types

Allow the scale of a numeric value to be negative, or greater than its precision (Dean Rasheed, Tom Lane) Section 
This allows rounding of values to the left of the decimal point, e.g., '1234'::numeric(4, -2) returns 1200.

Improve overflow detection when casting values to interval (Joe Koshakow) Section 

Change the I/O format of type "char" for non-ASCII characters (Tom Lane) Section 

Update the display width information of modern Unicode characters, like emojis (Jacob Champion) Section Section 
Also update from Unicode 5.0 to 14.0.0. There is now an automated way to keep Postgres updated with Unicode releases.

## E.20.3.5. Functions

Add multirange input to range_agg() (Paul Jungwirth) Section 

Add MIN() and MAX() aggregates for the xid8 data type (Ken Kato) Section 

Add regular expression functions for compatibility with other relational systems (Gilles Darold, Tom Lane) Section 
The new functions are regexp_count(), regexp_instr(), regexp_like(), and regexp_substr(). Some new optional arguments were also added to regexp_replace().

Add the ability to compute the distance between polygons (Tom Lane) Section 

Add to_char() format codes of, tzh, and tzm (Nitin Jadhav) Section 
The upper-case equivalents of these were already supported.

When applying AT TIME ZONE to a time with time zone value, use the transaction start time rather than wall clock time to determine whether DST applies (Aleksander Alekseev, Tom Lane) Section 
This allows the conversion to be considered stable rather than volatile, and it saves a kernel call per invocation.

Ignore NULL array elements in ts_delete() and setweight() functions with array arguments (Jean-Christophe Arnu) Section 
These functions effectively ignore empty-string array elements (since those could never match a valid lexeme). It seems consistent to let them ignore NULL elements too, instead of failing.

Add support for petabyte units to pg_size_pretty() and pg_size_bytes() (David Christensen) Section 

Change pg_event_trigger_ddl_commands() to output references to other sessions' temporary schemas using the actual schema name (Tom Lane) Section 
Previously this function reported all temporary schemas as pg_temp, but it's misleading to use that for any but the current session's temporary schema.

## E.20.3.6. PL/pgSQL

Fix enforcement of PL/pgSQL variable CONSTANT markings (Tom Lane) Section 
Previously, a variable could be used as a CALL output parameter or refcursor OPEN variable despite being marked CONSTANT.

## E.20.3.7. libpq

Allow IP address matching against a server certificate's Subject Alternative Name (Jacob Champion) Section 

Allow PQsslAttribute() to report the SSL library type without requiring a libpq connection (Jacob Champion) Section 

Change query cancellations sent by the client to use the same TCP settings as normal client connections (Jelte Fennema) Section 
This allows configured TCP timeouts to apply to query cancel connections.

Prevent libpq event callback failures from forcing an error result (Tom Lane) Section Section 

## E.20.3.8. Client Applications

Allow pgbench to retry after serialization and deadlock failures (Yugo Nagata, Marina Polyakova) Section 

E.20.3.8.1. psql

Improve performance of psql's \copy command, by sending data in larger chunks (Heikki Linnakangas) Section 

Add \dconfig command to report server variables (Mark Dilger, Tom Lane) Section Section Section 
This is similar to the server-side SHOW command, but it can process patterns to show multiple variables conveniently.

Add \getenv command to assign the value of an environment variable to a psql variable (Tom Lane) Section 

Add + option to the \lo_list and \dl commands to show large-object privileges (Pavel Luzanov) Section 

Add a pager option for the \watch command (Pavel Stehule, Thomas Munro) Section 
This is only supported on Unix and is controlled by the PSQL_WATCH_PAGER environment variable.

Make psql include intra-query double-hyphen comments in queries sent to the server (Tom Lane, Greg Nancarrow) Section Section 
Previously such comments were removed from the query before being sent. Double-hyphen comments that are before any query text are not sent, and are not recorded as separate psql history entries.

Adjust psql so that Readline's meta-# command will insert a double-hyphen comment marker (Tom Lane) Section 
Previously a pound marker was inserted, unless the user had taken the trouble to configure a non-default comment marker.

Make psql output all results when multiple queries are passed to the server at once (Fabien Coelho) Section 
Previously, only the last query result was displayed. The old behavior can be restored by setting the SHOW_ALL_RESULTSpsql variable to off.

After an error is detected in --single-transaction mode, change the final COMMIT command to ROLLBACK only if ON_ERROR_STOP is set (Michael Paquier) Section 
Previously, detection of an error in a -c command or -f script file would lead to issuing ROLLBACK at the end, regardless of the value of ON_ERROR_STOP.

Improve psql's tab completion (Shinya Kato, Dagfinn Ilmari Mannsaker, Peter Smith, Koyu Tanigawa, Ken Kato, David Fetter, Haiying Tang, Peter Eisentraut, Alvaro Herrera, Tom Lane, Masahiko Sawada) Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section 

Limit support of psql's backslash commands to servers running PostgreSQL 9.2 or later (Tom Lane) Section 
Remove code that was only used when running with an older server. Commands that do not require any version-specific adjustments compared to 9.2 will still work.

E.20.3.8.2. pg_dump

Make pg_dump dump public schema ownership changes and security labels (Noah Misch) Section Section 

Improve performance of dumping databases with many objects (Tom Lane) Section Section Section 
This will also improve the performance of pg_upgrade.

Improve parallel pg_dump's performance for tables with large TOAST tables (Tom Lane) Section 

Add dump/restore option --no-table-access-method to force restore to only use the default table access method (Justin Pryzby) Section 

Limit support of pg_dump and pg_dumpall to servers running PostgreSQL 9.2 or later (Tom Lane) Section 

## E.20.3.9. Server Applications

Add new pg_basebackup option --target to control the base backup location (Robert Haas) Section 
The new options are server to write the backup locally and blackhole to discard the backup (for testing).

Allow pg_basebackup to do server-side gzip, LZ4, and Zstandard compression and client-side LZ4 and Zstandard compression of base backup files (Dipesh Pandit, Jeevan Ladhe) Section Section Section 
Client-side gzip compression was already supported.

Allow pg_basebackup to compress on the server side and decompress on the client side before storage (Dipesh Pandit) Section 
This is accomplished by specifying compression on the server side and plain output format.

Allow pg_basebackup's --compress option to control the compression location (server or client), compression method, and compression options (Michael Paquier, Robert Haas) Section Section Section 

Add the LZ4 compression method to pg_receivewal (Georgios Kokolatos) Section Section 
This is enabled via --compress=lz4 and requires binaries to be built using --with-lz4.

Add additional capabilities to pg_receivewal's --compress option (Georgios Kokolatos) Section 

Improve pg_receivewal's ability to restart at the proper WAL location (Ronan Dunklau) Section 
Previously, pg_receivewal would start based on the WAL file stored in the local archive directory, or at the sending server's current WAL flush location. With this change, if the sending server is running Postgres 15 or later, the local archive directory is empty, and a replication slot is specified, the replication slot's restart point will be used.

Add pg_rewind option --config-file to simplify use when server configuration files are stored outside the data directory (Gunnar Bluth) Section 

E.20.3.9.1. pg_upgrade

Store pg_upgrade's log and temporary files in a subdirectory of the new cluster called pg_upgrade_output.d (Justin Pryzby) Section Section Section 
Previously such files were left in the current directory, requiring manual cleanup. Now they are automatically removed on successful completion of pg_upgrade.

Disable default status reporting during pg_upgrade operation if the output is not a terminal (Andres Freund) Section 
The status reporting output can be enabled for non-tty usage by using --verbose.

Make pg_upgrade report all databases with invalid connection settings (Jeevan Ladhe) Section 
Previously only the first database with an invalid connection setting was reported.

Make pg_upgrade preserve tablespace and database OIDs, as well as relation relfilenode numbers (Shruthi Gowda, Antonin Houska) Section Section Section 

Add a --no-sync option to pg_upgrade (Michael Paquier) Section 
This is recommended only for testing.

Limit support of pg_upgrade to old servers running PostgreSQL 9.2 or later (Tom Lane) Section 

E.20.3.9.2. pg_waldump

Allow pg_waldump output to be filtered by relation file node, block number, fork number, and full page images (David Christensen, Thomas Munro) Section Section 

Make pg_waldump report statistics before an interrupted exit (Bharath Rupireddy) Section 
For example, issuing a control-C in a terminal running pg_waldump --stats --follow will report the current statistics before exiting. This does not work on Windows.

Improve descriptions of some transaction WAL records reported by pg_waldump (Masahiko Sawada, Michael Paquier) Section 

Allow pg_waldump to dump information about multiple resource managers (Heikki Linnakangas) Section 
This is enabled by specifying the --rmgr option multiple times.

## E.20.3.10. Documentation

Add documentation for pg_encoding_to_char() and pg_char_to_encoding() (Ian Lawrence Barwick) Section 

Document the ^@ starts-with operator (Tom Lane) Section 

## E.20.3.11. Source Code

Add support for continuous integration testing using cirrus-ci (Andres Freund, Thomas Munro, Melanie Plageman) Section 

Add configure option --with-zstd to enable Zstandard builds (Jeevan Ladhe, Robert Haas, Michael Paquier) Section 

Add an ABI identifier field to the magic block in loadable libraries, allowing non-community PostgreSQL distributions to identify libraries that are not compatible with other builds (Peter Eisentraut) Section 
An ABI field mismatch will generate an error at load time.

Create a new pg_type.typcategory value for "char" (Tom Lane) Section 
Some other internal-use-only types have also been assigned to this category.

Add new protocol message TARGET to specify a new COPY method to be used for base backups (Robert Haas) Section 
pg_basebackup now uses this method.

Add new protocol message COMPRESSION and COMPRESSION_DETAIL to specify the compression method and options (Robert Haas) Section Section 

Remove server support for old BASE_BACKUP command syntax and base backup protocol (Robert Haas) Section Section 

Add support for extensions to set custom backup targets (Robert Haas) Section 

Allow extensions to define custom WAL resource managers (Jeff Davis) Section 

Add function pg_settings_get_flags() to get the flags of server variables (Justin Pryzby) Section 

On Windows, export all the server's global variables using PGDLLIMPORT markers (Robert Haas) Section 
Previously, only specific variables were accessible to extensions on Windows.

Require GNU make version 3.81 or later to build PostgreSQL (Tom Lane) Section 

Require OpenSSL to build the pgcrypto extension (Peter Eisentraut) Section 

Require Perl version 5.8.3 or later (Dagfinn Ilmari Mannsaker) Section 

Require Python version 3.2 or later (Andres Freund) Section 

## E.20.3.12. Additional Modules

Allow amcheck to check sequences (Mark Dilger) Section 

Improve amcheck sanity checks for TOAST tables (Mark Dilger) Section 

Add new module basebackup_to_shell as an example of a custom backup target (Robert Haas) Section Section 

Add new module basic_archive as an example of performing archiving via a library (Nathan Bossart) Section 

Allow btree_gist indexes on boolean columns (Emre Hasegeli) Section Section Section 
These can be used for exclusion constraints.

Fix pageinspect's page_header() to handle 32-kilobyte page sizes (Quan Zongliang) Section 
Previously, improper negative values could be returned in certain cases.

Add counters for temporary file block I/O to pg_stat_statements (Masahiko Sawada) Section 

Add JIT counters to pg_stat_statements (Magnus Hagander) Section 

Add new module pg_walinspect (Bharath Rupireddy) Section 
This gives SQL-level output similar to pg_waldump.

Indicate the permissive/enforcing state in sepgsql log messages (Dave Page) Section 

E.20.3.12.1. postgres_fdw

Allow postgres_fdw to push down CASE expressions (Alexander Pyhalov) Section 

Add server variable postgres_fdw.application_name to control the application name of postgres_fdw connections (Hayato Kuroda) Section Section Section 
Previously the remote session's application_name could only be set on the remote server or via a postgres_fdw connection specification. postgres_fdw.application_name supports some escape sequences for customization, making it easier to tell such connections apart on the remote server.

Allow parallel commit on postgres_fdw servers (Etsuro Fujita) Section 
This is enabled with the CREATE SERVER option parallel_commit.

## E.20.4. Acknowledgments
The following individuals (in alphabetical order) have contributed to this release as patch authors, committers, reviewers, testers, or reporters of issues.

 | Abhijit Menon-Sen
 | Adam Brusselback
 | Adam Mackler
 | Adrian Ho
 | Ahsan Hadi
 | Ajin Cherian
 | Alastair McKinley
 | Aleksander Alekseev
 | Ales Zeleny
 | Alex Kingsborough
 | Alex Kozhemyakin
 | Alexander Korotkov
 | Alexander Kukushkin
 | Alexander Lakhin
 | Alexander Nawratil
 | Alexander Pyhalov
 | Alexey Borzov
 | Alexey Ermakov
 | Aliaksandr Kalenik
 | Alvaro Herrera
 | Amit Kapila
 | Amit Khandekar
 | Amit Langote
 | Amul Sul
 | Anastasia Lubennikova
 | Anders Kaseorg
 | Andreas Dijkman
 | Andreas Grob
 | Andreas Seltenreich
 | Andrei Zubkov
 | Andres Freund
 | Andrew Alsup
 | Andrew Bille
 | Andrew Dunstan
 | Andrew Gierth
 | Andrew Kesper
 | Andrey Borodin
 | Andrey Lepikhov
 | Andrey Sokolov
 | Andy Fan
 | Anton Melnikov
 | Anton Voloshin
 | Antonin Houska
 | Arjan van de Ven
 | Arne Roland
 | Arthur Zakirov
 | Ashutosh Bapat
 | Ashutosh Sharma
 | Ashwin Agrawal
 | Asif Rehman
 | Asim Praveen
 | Atsushi Torikoshi
 | Aya Iwata
 | Bauyrzhan Sakhariyev
 | Benoit Lobreau
 | Bernd Dorn
 | Bertrand Drouvot
 | Bharath Rupireddy
 | Bjorn Harrtell
 | Boris Kolpackov
 | Boris Korzun
 | Brad Nicholson
 | Brar Piening
 | Bruce Momjian
 | Bruno da Silva
 | Bryn Llewellyn
 | Carl Sopchak
 | Cary Huang
 | Chapman Flack
 | Chen Jiaoqian
 | Chris Bandy
 | Chris Lowder
 | Christian Quest
 | Christoph Berg
 | Christoph Heiss
 | Christophe Pettus
 | Christopher Painter-Wakefield
 | Claudio Freire
 | Clemens Zeidler
 | Corey Huinker
 | Dag Lem
 | Dagfinn Ilmari Mannsaker
 | Dan Kubb
 | Daniel Cherniy
 | Daniel Gustafsson
 | Daniel Polski
 | Daniel Verite
 | Daniel Westermann
 | Daniele Varrazzo
 | Daniil Anisimov
 | Danny Shemesh
 | Darafei Praliaskouski
 | Daria Lepikhova
 | Dave Cramer
 | Dave Page
 | David Christensen
 | David Fetter
 | David G. Johnston
 | David Rowley
 | David Steele
 | David Zhang
 | Dean Rasheed
 | Dian Fay
 | Dilip Kumar
 | Dipesh Pandit
 | Dmitry Dolgov
 | Dmitry Koval
 | Dmitry Marakasov
 | Dominique Devienne
 | Dong Wook
 | Drew DeVault
 | Eduard Catala
 | Egor Chindyaskin
 | Egor Rogov
 | Ekaterina Kiryanova
 | Elena Indrupskaya
 | Elvis Pranskevichus
 | Emmanuel Quincerot
 | Emre Hasegeli
 | Eric Mutta
 | Erica Zhang
 | Erik Rijkers
 | Erki Eessaar
 | Etsuro Fujita
 | Euler Taveira
 | Fabien Coelho
 | Fabrice Chapuis
 | Fabrice Fontaine
 | Fabrizio de Royes Mello
 | Feike Steenbergen
 | Filip Gospodinov
 | Florin Irion
 | Floris Van Nee
 | Frederic Yhuel
 | Gabriela Serventi
 | Gaurab Dey
 | Geoff Winkless
 | Geoffrey Blake
 | Georgios Kokolatos
 | Gilles Darold
 | Greg Nancarrow
 | Greg Rychlewski
 | Greg Sabino Mullane
 | Greg Stark
 | Gregory Smith
 | Guillaume Lelarge
 | Gunnar Bluth
 | Gurjeet Singh
 | Haiyang Wang
 | Haiying Tang
 | Hannu Krosing
 | Hans Buschmann
 | Hayato Kuroda
 | Heath Lord
 | Heikki Linnakangas
 | Herwig Goemans
 | Himanshu Upadhyaya
 | Holly Roberts
 | Hou Zhijie
 | Hubert Lubaczewski
 | Ian Barwick
 | Ian Campbell
 | Ibrar Ahmed
 | Ildus Kurbangaliev
 | Ilya Anfimov
 | Itamar Gafni
 | Jacob Champion
 | Jaime Casanova
 | Jakub Wartak
 | James Coleman
 | James Hilliard
 | James Inform
 | Jan Piotrowski
 | Japin Li
 | Jason Harvey
 | Jason Kim
 | Jean-Christophe Arnu
 | Jeevan Ladhe
 | Jeff Davis
 | Jeff Janes
 | Jehan-Guillaume de Rorthais
 | Jelte Fennema
 | Jeremy Evans
 | Jeremy Schneider
 | Jian Guo
 | Jian He
 | Jimmy Yih
 | Jiri Fejfar
 | Jitka Plesnikova
 | Joe Conway
 | Joe Wildish
 | Joel Jacobson
 | Joey Bodoia
 | John Naylor
 | Jonathan Katz
 | Josef Simanek
 | Joseph Koshakow
 | Josh Soref
 | Joshua Brindle
 | Juan Jose Santamaria Flecha
 | Julien Rouhaud
 | Julien Roze
 | Junwang Zhao
 | Jurgen Purtz
 | Justin Pryzby
 | Ken Kato
 | Kevin Burke
 | Kevin Grittner
 | Kevin Humphreys
 | Kevin McKibbin
 | Kevin Sweet
 | Kevin Zheng
 | Klaudie Willis
 | Konstantin Knizhnik
 | Konstantina Skovola
 | Kosei Masumura
 | Kotaro Kawamoto
 | Koyu Tanigawa
 | Kuntal Ghosh
 | Kyotaro Horiguchi
 | Lars Kanis
 | Lauren Fliksteen
 | Laurent Hasson
 | Laurenz Albe
 | Leslie Lemaire
 | Liam Bowen
 | Lingjie Qiang
 | Liu Huailing
 | Louis Jachiet
 | Lukas Fittl
 | Ma Liangzhu
 | Maciek Sakrejda
 | Magnus Hagander
 | Mahendra Singh Thalor
 | Maksim Milyutin
 | Marc Bachmann
 | Marcin Krupowicz
 | Marcus Gartner
 | Marek Szuba
 | Marina Polyakova
 | Mario Emmenlauer
 | Mark Dilger
 | Mark Murawski
 | Mark Wong
 | Markus Wanner
 | Markus Winand
 | Martijn van Oosterhout
 | Martin Jurca
 | Martin Kalcher
 | Martin Marques
 | Masahiko Sawada
 | Masahiro Ikeda
 | Masao Fujii
 | Masaya Kawamoto
 | Masayuki Hirose
 | Matthias van de Meent
 | Matthijs van der Vleuten
 | Maxim Orlov
 | Maxim Yablokov
 | Melanie Plageman
 | Michael Banck
 | Michael Harris
 | Michael J. Sullivan
 | Michael Meskes
 | Michael Muhlbeyer
 | Michael Paquier
 | Michael Powers
 | Mike Fiedler
 | Mike Oh
 | Mikhail Kulagin
 | Miles Delahunty
 | Naoki Okano
 | Nathan Bossart
 | Nathan Long
 | Nazir Bilal Yavuz
 | Neha Sharma
 | Neil Chen
 | Nicola Contu
 | Nicolas Lutic
 | Nikhil Benesch
 | Nikhil Shetty
 | Nikhil Sontakke
 | Nikita Glukhov
 | Nikolai Berkoff
 | Nikolay Samokhvalov
 | Nikolay Shaplov
 | Nitin Jadhav
 | Noah Misch
 | Noboru Saito
 | Noriyoshi Shinoda
 | Olaf Bohlen
 | Olly Betts
 | Onder Kalaci
 | Oskar Stenberg
 | Otto Kekalainen
 | Paul Guo
 | Paul Jungwirth
 | Paul Martinez
 | Pavan Deolasee
 | Pavel Borisov
 | Pavel Luzanov
 | Pavel Stehule
 | Peter Eisentraut
 | Peter Geoghegan
 | Peter Slavov
 | Peter Smith
 | Petr Jelinek
 | Phil Florent
 | Phil Krylov
 | Pierre-Aurelien Georges
 | Prabhat Sahu
 | Quan Zongliang
 | Rachel Heaton
 | Rahila Syed
 | Rajakavitha Kodhandapani
 | Rajkumar Raghuwanshi
 | Ranier Vilela
 | Rei Kamigishi
 | Reid Thompson
 | Remi Lapeyre
 | Renan Soares Lopes
 | Richard Guo
 | Richard Wesley
 | RKN Sai Krishna
 | Robert Haas
 | Robert Treat
 | Roberto Mello
 | Robins Tharakan
 | Roger Mason
 | Roman Zharkov
 | Ronan Dunklau
 | Rui Zhao
 | Ryan Kelly
 | Ryo Matsumura
 | Ryohei Takahashi
 | Sadhuprasad Patro
 | Sait Talha Nisanci
 | Sami Imseih
 | Sandeep Thakkar
 | Sebastian Kemper
 | Sehrope Sarkuni
 | Sergei Kornilov
 | Sergei Shoulbakov
 | Sergey Shinderuk
 | Shay Rojansky
 | Shenhao Wang
 | Shi Yu
 | Shinya Kato
 | Shruthi Gowda
 | Simon Perepelitsa
 | Simon Riggs
 | Sirisha Chamarthi
 | Soumyadeep Chakraborty
 | Stan Hu
 | Stas Kelvich
 | Stefen Hillman
 | Stephen Frost
 | Steve Chavez
 | Sumanta Mukherjee
 | Suraj Khamkar
 | Suraj Kharage
 | Sven Klemm
 | Takamichi Osumi
 | Takayuki Tsunakawa
 | Takeshi Ideriha
 | Tatsuhiro Nakamori
 | Tatsuhito Kasahara
 | Tatsuo Ishii
 | Tatsuro Yamada
 | Teja Mupparti
 | Teodor Sigaev
 | Thibaud Walkowiak
 | Thom Brown
 | Thomas McKay
 | Thomas Munro
 | Tim McNamara
 | Timo Stolz
 | Timur Khanjanov
 | Tom Lane
 | Tomas Barton
 | Tomas Vondra
 | Tony Reix
 | Troy Frericks
 | Tushar Ahuja
 | Victor Wagner
 | Victor Yegorov
 | Vignesh C
 | Vik Fearing
 | Vincas Dargis
 | Vitaly Burovoy
 | Vitaly Voronov
 | Vladimir Sitnikov
 | Wang Ke
 | Wei Sun
 | Wei Wang
 | Whale Song
 | Will Mortensen
 | Wolfgang Walther
 | Yanliang Lei
 | Yaoguang Chen
 | Yogendra Suralkar
 | YoungHwan Joo
 | Yugo Nagata
 | Yukun Wang
 | Yura Sokolov
 | Yusuke Egashira
 | Yuzuko Hosoya
 | Zhang Mingli
 | Zhang Wenjie
 | Zhihong Yu
 | Zhiyong Wu

 | Prev | Up | Next
 | E.19. Release 15.1  | Home |  E.21. Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 15.19 
 15.18 
 15.17 
 15.16 
 15.15 
 15.14 
 15.13 
 15.12 
 15.11 
 15.10 
 15.9 
 15.8 
 15.7 
 15.6 
 15.5 
 15.4 
 15.3 
 15.2 
 15.1 
 15.0

<!-- source: https://www.postgresql.org/docs/release/12.0/ -->

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

##  PostgreSQL 12.0 

 | E.23. Release 12
 | Prev | Up | Appendix E. Release Notes | Home | Next

## E.23. Release 12

E.23.1. Overview
E.23.2. Migration to Version 12
E.23.3. Changes
E.23.4. Acknowledgments
Release date: 2019-10-03

## E.23.1. Overview
Major enhancements in PostgreSQL 12 include:

General performance improvements, including:

Optimizations to space utilization and read/write performance for B-tree indexes

Partitioning performance enhancements, including improved query performance on tables with thousands of partitions, improved insertion performance with INSERT and COPY, and the ability to execute ALTER TABLE ATTACH PARTITION without blocking queries

Automatic (but overridable) inlining of common table expressions (CTEs)

Reduction of WAL overhead for creation of GiST, GIN, and SP-GiST indexes

Support for covering GiST indexes, via the INCLUDE clause

Multi-column most-common-value (MCV) statistics can be defined via CREATE STATISTICS, to support better plans for queries that test several non-uniformly-distributed columns

Enhancements to administrative functionality, including:

REINDEX CONCURRENTLY can rebuild an index without blocking writes to its table

pg_checksums can enable/disable page checksums (used for detecting data corruption) in an offline cluster

Progress reporting statistics for CREATE INDEX, REINDEX, CLUSTER, VACUUM FULL, and pg_checksums

Support for the SQL/JSON path language

Stored generated columns

Nondeterministic ICU collations, enabling case-insensitive and accent-insensitive grouping and ordering

New authentication features, including:

Encryption of TCP/IP connections when using GSSAPI authentication

Discovery of LDAP servers using DNS SRV records

Multi-factor authentication, using the clientcert=verify-full option combined with an additional authentication method in pg_hba.conf
The above items are explained in more detail in the sections below.

## E.23.2. Migration to Version 12
A dump/restore using pg_dumpall or use of pg_upgrade or logical replication is required for those wishing to migrate data from any previous release. See Section 18.6 for general information on migrating to new major releases.
Version 12 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

Remove the special behavior of oid columns (Andres Freund, John Naylor) Section 
Previously, a normally-invisible oid column could be specified during table creation using WITH OIDS; that ability has been removed. Columns can still be explicitly declared as type oid. Operations on tables that have columns created using WITH OIDS will need adjustment.
The system catalogs that previously had hidden oid columns now have ordinary oid columns. Hence, SELECT * will now output those columns, whereas previously they would be displayed only if selected explicitly.

Remove data types abstime, reltime, and tinterval (Andres Freund) Section 
These are obsoleted by SQL-standard types such as timestamp.

Remove the timetravel extension (Andres Freund) Section 

Move recovery.conf settings into postgresql.conf (Masao Fujii, Simon Riggs, Abhijit Menon-Sen, Sergei Kornilov) Section 
recovery.conf is no longer used, and the server will not start if that file exists. recovery.signal and standby.signal files are now used to switch into non-primary mode. The trigger_file setting has been renamed to promote_trigger_file. The standby_mode setting has been removed.

Do not allow multiple conflicting recovery_target* specifications (Peter Eisentraut) Section 
Specifically, only allow one of recovery_target, recovery_target_lsn, recovery_target_name, recovery_target_time, and recovery_target_xid. Previously, multiple different instances of these parameters could be specified, and the last one was honored. Now, only one can be specified, though the same one can be specified multiple times and the last specification is honored.

Cause recovery to advance to the latest timeline by default (Peter Eisentraut) Section 
Specifically, recovery_target_timeline now defaults to latest. Previously, it defaulted to current.

Refactor code for geometric functions and operators (Emre Hasegeli) Section Section Section 
This could lead to more accurate, but slightly different, results compared to previous releases. Notably, cases involving NaN, underflow, overflow, and division by zero are handled more consistently than before.

Improve performance by using a new algorithm for output of real and double precision values (Andrew Gierth) Section Section 
Previously, displayed floating-point values were rounded to 6 (for real) or 15 (for double precision) digits by default, adjusted by the value of extra_float_digits. Now, whenever extra_float_digits is more than zero (as it now is by default), only the minimum number of digits required to preserve the exact binary value are output. The behavior is the same as before when extra_float_digits is set to zero or less.
Also, formatting of floating-point exponents is now uniform across platforms: two digits are used unless three are necessary. In previous releases, Windows builds always printed three digits.

random() and setseed() now behave uniformly across platforms (Tom Lane) Section Section 
The sequence of random() values generated following a setseed() call with a particular seed value is likely to be different now than before. However, it will also be repeatable, which was not previously guaranteed because of interference from other uses of random numbers inside the server. The SQL random() function now has its own private per-session state to forestall that.

Change SQL-style substring() to have standard-compliant greediness behavior (Tom Lane) Section 
In cases where the pattern can be matched in more than one way, the initial sub-pattern is now treated as matching the least possible amount of text rather than the greatest; for example, a pattern such as %#"aa*#"% now selects the first group of a's from the input, not the last group.

Do not pretty-print the result of xpath() or the XMLTABLE construct (Tom Lane) Section 
In some cases, these functions would insert extra whitespace (newlines and/or spaces) in nodeset values. This is undesirable since depending on usage, the whitespace might be considered semantically significant.

Rename command-line tool pg_verify_checksums to pg_checksums (Michael Paquier) Section 

In pg_restore, require specification of -f - to send the dump contents to standard output (Euler Taveira) Section 
Previously, this happened by default if no destination was specified, but that was deemed to be unfriendly.

Disallow non-unique abbreviations in psql's \pset format command (Daniel Verite) Section Section 
Previously, for example, \pset format a chose aligned; it will now fail since that could equally well mean asciidoc.

In new btree indexes, the maximum index entry length is reduced by eight bytes, to improve handling of duplicate entries (Peter Geoghegan) Section 
This means that a REINDEX operation on an index pg_upgrade'd from a previous release could potentially fail.

Cause DROP IF EXISTS FUNCTION/PROCEDURE/AGGREGATE/ROUTINE to generate an error if no argument list is supplied and there are multiple matching objects (David Rowley) Section 
Also improve the error message in such cases.

Split the pg_statistic_ext catalog into two catalogs, and add the pg_stats_ext view of it (Dean Rasheed, Tomas Vondra) Section Section 
This change supports hiding potentially-sensitive statistics data from unprivileged users.

Remove obsolete pg_constraint.consrc column (Peter Eisentraut) Section 
This column has been deprecated for a long time, because it did not update in response to other catalog changes (such as column renamings). The recommended way to get a text version of a check constraint's expression from pg_constraint is pg_get_expr(conbin, conrelid). pg_get_constraintdef() is also a useful alternative.

Remove obsolete pg_attrdef.adsrc column (Peter Eisentraut) Section 
This column has been deprecated for a long time, because it did not update in response to other catalog changes (such as column renamings). The recommended way to get a text version of a default-value expression from pg_attrdef is pg_get_expr(adbin, adrelid).

Mark table columns of type name as having "C" collation by default (Tom Lane, Daniel Verite) Section Section 
The comparison operators for data type name can now use any collation, rather than always using "C" collation. To preserve the previous semantics of queries, columns of type name are now explicitly marked as having "C" collation. A side effect of this is that regular-expression operators on name columns will now use the "C" collation by default, not the database collation, to determine the behavior of locale-dependent regular expression patterns (such as \w). If you want non-C behavior for a regular expression on a name column, attach an explicit COLLATE clause. (For user-defined name columns, another possibility is to specify a different collation at table creation time; but that just moves the non-backwards-compatibility to the comparison operators.)

Treat object-name columns in the information_schema views as being of type name, not varchar (Tom Lane) Section Section Section 
Per the SQL standard, object-name columns in the information_schema views are declared as being of domain type sql_identifier. In PostgreSQL, the underlying catalog columns are really of type name. This change makes sql_identifier be a domain over name, rather than varchar as before. This eliminates a semantic mismatch in comparison and sorting behavior, which can greatly improve the performance of queries on information_schema views that restrict an object-name column. Note however that inequality restrictions, for example
```

SELECT ... FROM information_schema.tables WHERE table_name < 'foo';

```

will now use "C"-locale comparison semantics by default, rather than the database's default collation as before. Sorting on these columns will also follow "C" ordering rules. The previous behavior (and inefficiency) can be enforced by adding a COLLATE "default" clause.

Remove the ability to disable dynamic shared memory (Kyotaro Horiguchi) Section 
Specifically, dynamic_shared_memory_type can no longer be set to none.

Parse libpq integer connection parameters more strictly (Fabien Coelho) Section 
In previous releases, using an incorrect integer value for connection parameters connect_timeout, keepalives, keepalives_count, keepalives_idle, keepalives_interval and port resulted in libpq either ignoring those values or failing with incorrect error messages.

## E.23.3. Changes
Below you will find a detailed account of the changes between PostgreSQL 12 and the previous major release.

## E.23.3.1. Server

E.23.3.1.1. Partitioning

Improve performance of many operations on partitioned tables (Amit Langote, David Rowley, Tom Lane, Alvaro Herrera) Section Section Section Section Section 
Allow tables with thousands of child partitions to be processed efficiently by operations that only affect a small number of partitions.

Allow foreign keys to reference partitioned tables (Alvaro Herrera) Section 

Improve speed of COPY into partitioned tables (David Rowley) Section 

Allow partition bounds to be any expression (Kyotaro Horiguchi, Tom Lane, Amit Langote) Section 
Such expressions are evaluated at partitioned-table creation time. Previously, only simple constants were allowed as partition bounds.

Allow CREATE TABLE's tablespace specification for a partitioned table to affect the tablespace of its children (David Rowley, Alvaro Herrera) Section 

Avoid sorting when partitions are already being scanned in the necessary order (David Rowley) Section 

ALTER TABLE ATTACH PARTITION is now performed with reduced locking requirements (Robert Haas) Section 

Add partition introspection functions (Michael Paquier, Alvaro Herrera, Amit Langote) Section Section Section 
The new function pg_partition_root() returns the top-most parent of a partition tree, pg_partition_ancestors() reports all ancestors of a partition, and pg_partition_tree() displays information about partitions.

Include partitioned indexes in the system view pg_indexes (Suraj Kharage) Section 

Add psql command \dP to list partitioned tables and indexes (Pavel Stehule) Section 

Improve psql\d and \z display of partitioned tables (Pavel Stehule, Michael Paquier, Alvaro Herrera) Section Section Section 

Fix bugs that could cause ALTER TABLE DETACH PARTITION to leave behind incorrect dependency state, allowing subsequent operations to misbehave, for example by not dropping a former partition child index when its table is dropped (Tom Lane) Section 

E.23.3.1.2. Indexes

Improve performance and space utilization of btree indexes with many duplicates (Peter Geoghegan, Heikki Linnakangas) Section Section Section 
Previously, duplicate index entries were stored unordered within their duplicate groups. This caused overhead during index inserts, wasted space due to excessive page splits, and it reduced VACUUM's ability to recycle entire pages. Duplicate index entries are now sorted in heap-storage order.
Indexes pg_upgrade'd from previous releases will not have these benefits.

Allow multi-column btree indexes to be smaller (Peter Geoghegan, Heikki Linnakangas)
Internal pages and min/max leaf page indicators now only store index keys until the change key, rather than all indexed keys. This also improves the locality of index access.
Indexes pg_upgrade'd from previous releases will not have these benefits.

Improve speed of btree index insertions by reducing locking overhead (Alexander Korotkov) Section 

Support INCLUDE columns in GiST indexes (Andrey Borodin) Section 

Add support for nearest-neighbor (KNN) searches of SP-GiST indexes (Nikita Glukhov, Alexander Korotkov, Vlad Sterzhanov) Section 

Reduce the WAL write overhead of GiST, GIN, and SP-GiST index creation (Anastasia Lubennikova, Andrey V. Lepikhov) Section 

Allow index-only scans to be more efficient on indexes with many columns (Konstantin Knizhnik) Section 

Improve the performance of vacuum scans of GiST indexes (Andrey Borodin, Konstantin Kuznetsov, Heikki Linnakangas) Section 

Delete empty leaf pages during GiSTVACUUM (Andrey Borodin) Section 

Reduce locking requirements for index renaming (Peter Eisentraut) Section 

E.23.3.1.3. Optimizer

Allow CREATE STATISTICS to create most-common-value statistics for multiple columns (Tomas Vondra) Section Section 
This improves optimization for queries that test several columns, requiring an estimate of the combined effect of several WHERE clauses. If the columns are correlated and have non-uniform distributions then multi-column statistics will allow much better estimates.

Allow common table expressions (CTEs) to be inlined into the outer query (Andreas Karlsson, Andrew Gierth, David Fetter, Tom Lane) Section Section 
Specifically, CTEs are automatically inlined if they have no side-effects, are not recursive, and are referenced only once in the query. Inlining can be prevented by specifying MATERIALIZED, or forced for multiply-referenced CTEs by specifying NOT MATERIALIZED. Previously, CTEs were never inlined and were always evaluated before the rest of the query.

Allow control over when generic plans are used for prepared statements (Pavel Stehule) Section 
This is controlled by the plan_cache_mode server parameter.

Improve optimization of partition and UNION ALL queries that have only a single child (David Rowley) Section 

Improve processing of domains that have no check constraints (Tom Lane) Section 
Domains that are being used purely as type aliases no longer cause optimization difficulties.

Pre-evaluate calls of LEAST and GREATEST when their arguments are constants (Vik Fearing) Section 

Improve optimizer's ability to verify that partial indexes with IS NOT NULL conditions are usable in queries (Tom Lane, James Coleman) Section Section 
Usability can now be recognized in more cases where the calling query involves casts or large x IN (array) clauses.

Compute ANALYZE statistics using the collation defined for each column (Tom Lane) Section 
Previously, the database's default collation was used for all statistics. This potentially gives better optimizer behavior for columns with non-default collations.

Improve selectivity estimates for inequality comparisons on ctid columns (Edmund Horner) Section 

Improve optimization of joins on columns of type tid (Tom Lane) Section Section 
These changes primarily improve the efficiency of self-joins on ctid columns.

Fix the leakproofness designations of some btree comparison operators and support functions (Tom Lane) Section Section 
This allows some optimizations that previously would not have been applied in the presence of security barrier views or row-level security.

E.23.3.1.4. General Performance

Enable Just-in-Time (JIT) compilation by default, if the server has been built with support for it (Andres Freund) Section 
Note that this support is not built by default, but has to be selected explicitly while configuring the build.

Speed up keyword lookup (John Naylor, Joerg Sonnenberger, Tom Lane) Section Section 

Improve search performance for multi-byte characters in position() and related functions (Heikki Linnakangas) Section 

Allow toasted values to be minimally decompressed (Paul Ramsey) Section 
This is useful for routines that only need to examine the initial portion of a toasted field.

Allow ALTER TABLE ... SET NOT NULL to avoid unnecessary table scans (Sergei Kornilov) Section 
This can be optimized when the table's column constraints can be recognized as disallowing nulls.

Allow ALTER TABLE ... SET DATA TYPE changing between timestamp and timestamptz to avoid a table rewrite when the session time zone is UTC (Noah Misch) Section 
In the UTC time zone, these two data types are binary compatible.

Improve speed in converting strings to int2 or int4 integers (Andres Freund) Section 

Allow parallelized queries when in SERIALIZABLE isolation mode (Thomas Munro) Section 
Previously, parallelism was disabled when in this mode.

Use pread() and pwrite() for random I/O (Oskari Saarenmaa, Thomas Munro) Section Section 
This reduces the number of system calls required for I/O.

Improve the speed of setting the process title on FreeBSD (Thomas Munro) Section 

E.23.3.1.5. Monitoring

Allow logging of statements from only a percentage of transactions (Adrien Nayrat) Section 
The parameter log_transaction_sample_rate controls this.

Add progress reporting to CREATE INDEX and REINDEX operations (Alvaro Herrera, Peter Eisentraut) Section Section 
Progress is reported in the pg_stat_progress_create_index system view.

Add progress reporting to CLUSTER and VACUUM FULL (Tatsuro Yamada) Section 
Progress is reported in the pg_stat_progress_cluster system view.

Add progress reporting to pg_checksums (Michael Banck, Bernd Helmle) Section 
This is enabled with the option --progress.

Add counter of checksum failures to pg_stat_database (Magnus Hagander) Section 

Add tracking of global objects in system view pg_stat_database (Julien Rouhaud) Section 
Global objects are shown with a pg_stat_database.datid value of zero.

Add the ability to list the contents of the archive directory (Christoph Moench-Tegeder) Section 
The function is pg_ls_archive_statusdir().

Add the ability to list the contents of temporary directories (Nathan Bossart) Section 
The function, pg_ls_tmpdir(), optionally allows specification of a tablespace.

Add information about the client certificate to the system view pg_stat_ssl (Peter Eisentraut) Section 
The new columns are client_serial and issuer_dn. Column clientdn has been renamed to client_dn for clarity.

Restrict visibility of rows in pg_stat_ssl for unprivileged users (Peter Eisentraut) Section 

At server start, emit a log message including the server version number (Christoph Berg) Section 

Prevent logging "incomplete startup packet" if a new connection is immediately closed (Tom Lane) Section 
This avoids log spam from certain forms of monitoring.

Include the application_name, if set, in log_connections log messages (Don Seiler) Section 

Make the walreceiver set its application name to the cluster name, if set (Peter Eisentraut) Section 

Add the timestamp of the last received standby message to pg_stat_replication (Lim Myungkyu) Section 

Add a wait event for fsync of WAL segments (Konstantin Knizhnik) Section 

E.23.3.1.6. Authentication

Add GSSAPI encryption support (Robbie Harwood, Stephen Frost) Section 
This feature allows TCP/IP connections to be encrypted when using GSSAPI authentication, without having to set up a separate encryption facility such as SSL. In support of this, add hostgssenc and hostnogssenc record types in pg_hba.conf for selecting connections that do or do not use GSSAPI encryption, corresponding to the existing hostssl and hostnossl record types. There is also a new gssencmode libpq option, and a pg_stat_gssapi system view.

Allow the clientcertpg_hba.conf option to check that the database user name matches the client certificate's common name (Julian Markwort, Marius Timmer) Section 
This new check is enabled with clientcert=verify-full.

Allow discovery of an LDAP server using DNS SRV records (Thomas Munro) Section 
This avoids the requirement of specifying ldapserver. It is only supported if PostgreSQL is compiled with OpenLDAP.

E.23.3.1.7. Server Configuration

Add ability to enable/disable cluster checksums using pg_checksums (Michael Banck, Michael Paquier) Section 
The cluster must be shut down for these operations.

Reduce the default value of autovacuum_vacuum_cost_delay to 2ms (Tom Lane) Section 
This allows autovacuum operations to proceed faster by default.

Allow vacuum_cost_delay to specify sub-millisecond delays, by accepting fractional values (Tom Lane) Section 

Allow time-based server parameters to use units of microseconds (us) (Tom Lane) Section 

Allow fractional input for integer server parameters (Tom Lane) Section 
For example, SET work_mem = '30.1GB' is now allowed, even though work_mem is an integer parameter. The value will be rounded to an integer after any required units conversion.

Allow units to be defined for floating-point server parameters (Tom Lane) Section 

Add wal_recycle and wal_init_zero server parameters to control WAL file recycling (Jerry Jelinek) Section 
Avoiding file recycling can be beneficial on copy-on-write file systems like ZFS.

Add server parameter tcp_user_timeout to control the server's TCP timeout (Ryohei Nagaura) Section 

Allow control of the minimum and maximum SSL protocol versions (Peter Eisentraut) Section 
The server parameters are ssl_min_protocol_version and ssl_max_protocol_version.

Add server parameter ssl_library to report the SSL library version used by the server (Peter Eisentraut) Section 

Add server parameter shared_memory_type to control the type of shared memory to use (Andres Freund) Section 
This allows selection of System V shared memory, if desired.

## E.23.3.2. Streaming Replication and Recovery

Allow some recovery parameters to be changed with reload (Peter Eisentraut) Section 
These parameters are archive_cleanup_command, promote_trigger_file, recovery_end_command, and recovery_min_apply_delay.

Allow the streaming replication timeout (wal_sender_timeout) to be set per connection (Takayuki Tsunakawa) Section 
Previously, this could only be set cluster-wide.

Add function pg_promote() to promote standbys to primaries (Laurenz Albe, Michael Paquier) Section Section 
Previously, this operation was only possible by using pg_ctl or creating a trigger file.

Allow replication slots to be copied (Masahiko Sawada) Section 
The functions for this are pg_copy_physical_replication_slot() and pg_copy_logical_replication_slot().

Make max_wal_senders not count as part of max_connections (Alexander Kukushkin) Section 

Add an explicit value of current for recovery_target_timeline (Peter Eisentraut) Section 

Make recovery fail if a two-phase transaction status file is corrupt (Michael Paquier) Section 
Previously, a warning was logged and recovery continued, allowing the transaction to be lost.

## E.23.3.3. Utility Commands

Add REINDEXCONCURRENTLY option to allow reindexing without locking out writes (Michael Paquier, Andreas Karlsson, Peter Eisentraut) Section 
This is also controlled by the reindexdb application's --concurrently option.

Add support for generated columns (Peter Eisentraut) Section 
The content of generated columns are computed from expressions (including references to other columns in the same table) rather than being specified by INSERT or UPDATE commands.

Add a WHERE clause to COPY FROM to control which rows are accepted (Surafel Temesgen) Section 
This provides a simple way to filter incoming data.

Allow enumerated values to be added more flexibly (Andrew Dunstan, Tom Lane, Thomas Munro) Section 
Previously, ALTER TYPE ... ADD VALUE could not be called in a transaction block, unless it was part of the same transaction that created the enumerated type. Now it can be called in a later transaction, so long as the new enumerated value is not referenced until after it is committed.

Add commands to end a transaction and start a new one (Peter Eisentraut) Section 
The commands are COMMIT AND CHAIN and ROLLBACK AND CHAIN.

Add VACUUM and CREATE TABLE options to prevent VACUUM from truncating trailing empty pages (Takayuki Tsunakawa) Section Section 
These options are vacuum_truncate and toast.vacuum_truncate. Use of these options reduces VACUUM's locking requirements, but prevents returning disk space to the operating system.

Allow VACUUM to skip index cleanup (Masahiko Sawada) Section Section 
This change adds a VACUUM command option INDEX_CLEANUP as well as a table storage option vacuum_index_cleanup. Use of this option reduces the ability to reclaim space and can lead to index bloat, but it is helpful when the main goal is to freeze old tuples.

Add the ability to skip VACUUM and ANALYZE operations on tables that cannot be locked immediately (Nathan Bossart) Section 
This option is called SKIP_LOCKED.

Allow VACUUM and ANALYZE to take optional Boolean argument specifications (Masahiko Sawada) Section 

Prevent TRUNCATE, VACUUM and ANALYZE from requesting a lock on tables for which the user lacks permission (Michael Paquier) Section Section 
This prevents unauthorized locking, which could interfere with user queries.

Add EXPLAIN option SETTINGS to output non-default optimizer settings (Tomas Vondra) Section 
This output can also be obtained when using auto_explain by setting auto_explain.log_settings.

Add OR REPLACE option to CREATE AGGREGATE (Andrew Gierth) Section 

Allow modifications of system catalogs' options using ALTER TABLE (Peter Eisentraut) Section 
Modifications of catalogs' reloptions and autovacuum settings are now supported. (Setting allow_system_table_mods is still required.)

Use all key columns' names when selecting default constraint names for foreign keys (Peter Eisentraut) Section 
Previously, only the first column name was included in the constraint name, resulting in ambiguity for multi-column foreign keys.

## E.23.3.4. Data Types

Update assorted knowledge about Unicode to match Unicode 12.1.0 (Peter Eisentraut) Section Section 
This fixes, for example, cases where psql would misformat output involving combining characters.

Update Snowball stemmer dictionaries with support for new languages (Arthur Zakirov) Section 
This adds word stemming support for Arabic, Indonesian, Irish, Lithuanian, Nepali, and Tamil to full text search.

Allow creation of collations that report string equality for strings that are not bit-wise equal (Peter Eisentraut) Section 
This feature supports "nondeterministic" collations that can define case- and accent-agnostic equality comparisons. Thus, for example, a case-insensitive uniqueness constraint on a text column can be made more easily than before. This is only supported for ICU collations.

Add support for ICU collation attributes on older ICU versions (Peter Eisentraut) Section 
This allows customization of the collation rules in a consistent way across all ICU versions.

Allow data type name to more seamlessly be compared to other text types (Tom Lane) Section 
Type name now behaves much like a domain over type text that has default collation "C". This allows cross-type comparisons to be processed more efficiently.

## E.23.3.5. Functions

Add support for the SQL/JSONpath language (Nikita Glukhov, Teodor Sigaev, Alexander Korotkov, Oleg Bartunov, Liudmila Mantrova) Section Section Section 
This allows execution of complex queries on JSON values using an SQL-standard language.

Add support for hyperbolic functions (Ltitia Avrot) Section 
Also add log10() as an alias for log(), for standards compliance.

Improve the accuracy of statistical aggregates like variance() by using more precise algorithms (Dean Rasheed) Section 

Allow date_trunc() to have an additional argument to control the time zone (Vik Fearing, Tom Lane) Section 
This is faster and simpler than using the AT TIME ZONE clause.

Adjust to_timestamp()/to_date() functions to be more forgiving of template mismatches (Artur Zakirov, Alexander Korotkov, Liudmila Mantrova) Section 
This new behavior more closely matches the Oracle functions of the same name.

Fix assorted bugs in XML functions (Pavel Stehule, Markus Winand, Chapman Flack) Section 
Specifically, in XMLTABLE, xpath(), and xmlexists(), fix some cases where nothing was output for a node, or an unexpected error was thrown, or necessary escaping of XML special characters was omitted.

Allow the BY VALUE clause in XMLEXISTS and XMLTABLE (Chapman Flack) Section 
This SQL-standard clause has no effect in PostgreSQL's implementation, but it was unnecessarily being rejected.

Prevent current_schema() and current_schemas() from being run by parallel workers, as they are not parallel-safe (Michael Paquier) Section 

Allow RECORD and RECORD[] to be used as column types in a query's column definition list for a table function that is declared to return RECORD (Elvis Pranskevichus) Section 

## E.23.3.6. PL/pgSQL

Allow SQL commands and variables with the same names as those commands to be used in the same PL/pgSQL function (Tom Lane) Section 
For example, allow a variable called comment to exist in a function that calls the COMMENTSQL command. Previously this combination caused a parse error.

Add new optional warning and error checks to PL/pgSQL (Pavel Stehule) Section 
The new checks allow for run-time validation of INTO column counts and single-row results.

## E.23.3.7. Client Interfaces

Add connection parameter tcp_user_timeout to control libpq's TCP timeout (Ryohei Nagaura) Section 

Allow libpq (and thus psql) to report only the SQLSTATE value in error messages (Didier Gautheron) Section 

Add libpq function PQresultMemorySize() to report the memory used by a query result (Lars Kanis, Tom Lane) Section 

Remove the no-display/debug flag from libpq's options connection parameter (Peter Eisentraut) Section 
This allows this parameter to be set by postgres_fdw.

Allow ecpg to create variables of data type bytea (Ryo Matsumura) Section 
This allows ECPG clients to interact with bytea data directly, rather than using an encoded form.

Add PREPARE AS support to ECPG (Ryo Matsumura) Section 

## E.23.3.8. Client Applications

Allow vacuumdb to select tables for vacuum based on their wraparound horizon (Nathan Bossart) Section 
The options are --min-xid-age and --min-mxid-age.

Allow vacuumdb to disable waiting for locks or skipping all-visible pages (Nathan Bossart) Section 
The options are --skip-locked and --disable-page-skipping.

Add colorization to the output of command-line utilities (Peter Eisentraut) Section 
This is enabled by setting the environment variable PG_COLOR to always or auto. The specific colors used can be adjusted by setting the environment variable PG_COLORS, using ANSI escape codes for colors. For example, the default behavior is equivalent to PG_COLORS="error=01;31:warning=01;35:locus=01".

E.23.3.8.1. psql

Add CSV table output mode in psql (Daniel Verite) Section 
This is controlled by \pset format csv or the command-line --csv option.

Show the manual page URL in psql's \help output for a SQL command (Peter Eisentraut) Section 

Display the IP address in psql's \conninfo (Fabien Coelho) Section Section 

Improve tab completion of CREATE TABLE, CREATE TRIGGER, CREATE EVENT TRIGGER, ANALYZE, EXPLAIN, VACUUM, ALTER TABLE, ALTER INDEX, ALTER DATABASE, and ALTER INDEX ALTER COLUMN (Dagfinn Ilmari Mannsaker, Tatsuro Yamada, Michael Paquier, Tom Lane, Justin Pryzby) Section Section Section Section Section Section Section Section Section 

E.23.3.8.2. pgbench

Allow values produced by queries to be assigned to pgbench variables (Fabien Coelho, Alvaro Herrera) Section Section 
The command for this is \gset.

Improve precision of pgbench's --rate option (Tom Lane) Section 

Improve pgbench's error reporting with clearer messages and return codes (Peter Eisentraut) Section 

## E.23.3.9. Server Applications

Allow control of log file rotation via pg_ctl (Kyotaro Horiguchi, Alexander Kuzmenkov, Alexander Korotkov) Section 
Previously, this was only possible via an SQL function or a process signal.

Properly detach the new server process during pg_ctl start (Paul Guo) Section 
This prevents the server from being shut down if the shell script that invoked pg_ctl is interrupted later.

Allow pg_upgrade to use the file system's cloning feature, if there is one (Peter Eisentraut) Section 
The --clone option has the advantages of --link, while preventing the old cluster from being changed after the new cluster has started.

Allow specification of the socket directory to use in pg_upgrade (Daniel Gustafsson) Section 
This is controlled by --socketdir; the default is the current directory.

Allow pg_checksums to disable fsync operations (Michael Paquier) Section 
This is controlled by the --no-sync option.

Allow pg_rewind to disable fsync operations (Michael Paquier) Section 

Fix pg_test_fsync to report accurate open_datasync durations on Windows (Laurenz Albe) Section Section 

E.23.3.9.1. pg_dump, pg_dumpall, pg_restore

When pg_dump emits data with INSERT commands rather than COPY, allow more than one data row to be included in each INSERT (Surafel Temesgen, David Rowley) Section Section 
The option controlling this is --rows-per-insert.

Allow pg_dump to emit INSERT ... ON CONFLICT DO NOTHING (Surafel Temesgen) Section 
This avoids conflict failures during restore. The option is --on-conflict-do-nothing.

Decouple the order of operations in a parallel pg_dump from the order used by a subsequent parallel pg_restore (Tom Lane) Section 
This allows pg_restore to perform more-fully-parallelized parallel restores, especially in cases where the original dump was not done in parallel. Scheduling of a parallel pg_dump is also somewhat improved.

Allow the extra_float_digits setting to be specified for pg_dump and pg_dumpall (Andrew Dunstan) Section 
This is primarily useful for making dumps that are exactly comparable across different source server versions. It is not recommended for normal use, as it may result in loss of precision when the dump is restored.

Add --exclude-database option to pg_dumpall (Andrew Dunstan) Section 

## E.23.3.10. Source Code

Add CREATE ACCESS METHOD command to create new table types (Andres Freund, Haribabu Kommi, Alvaro Herrera, Alexander Korotkov, Dmitry Dolgov) Section Section Section Section Section Section Section Section Section 
This enables the development of new table access methods, which can optimize storage for different use cases. The existing heap access method remains the default.

Add planner support function interfaces to improve optimizer estimates, inlining, and indexing for functions (Tom Lane) Section Section Section 
This allows extensions to create planner support functions that can provide function-specific selectivity, cost, and row-count estimates that can depend on the function's arguments. Support functions can also supply simplified representations and index conditions, greatly expanding optimization possibilities.

Simplify renumbering manually-assigned OIDs, and establish a new project policy for management of such OIDs (John Naylor, Tom Lane) Section Section 
Patches that manually assign OIDs for new built-in objects (such as new functions) should now randomly choose OIDs in the range 8000--9999. At the end of a development cycle, the OIDs used by committed patches will be renumbered down to lower numbers, currently somewhere in the 4xxx range, using the new renumber_oids.pl script. This approach should greatly reduce the odds of OID collisions between different in-process patches.
While there is no specific policy reserving any OIDs for external use, it is recommended that forks and other projects needing private manually-assigned OIDs use numbers in the high 7xxx range. This will avoid conflicts with recently-merged patches, and it should be a long time before the core project reaches that range.

Build Cygwin binaries using dynamic instead of static libraries (Marco Atzeri) Section 

Remove configure switch --disable-strong-random (Michael Paquier) Section 
A strong random-number source is now required.

printf-family functions, as well as strerror and strerror_r, now behave uniformly across platforms within Postgres code (Tom Lane) Section Section Section Section 
Notably, printf understands %m everywhere; on Windows, strerror copes with Winsock error codes (it used to do so in backend but not frontend code); and strerror_r always follows the GNU return convention.

Require a C99-compliant compiler, and MSVC 2013 or later on Windows (Andres Freund) Section 

Use pandoc, not lynx, for generating plain-text documentation output files (Peter Eisentraut) Section Section 
This affects only the INSTALL file generated during make dist and the seldom-used plain-text postgres.txt output file. Pandoc produces better output than lynx and avoids some locale/encoding issues. Pandoc version 1.13 or later is required.

Support use of images in the PostgreSQL documentation (Jurgen Purtz) Section 

## E.23.3.11. Additional Modules

Allow ORDER BY sorts and LIMIT clauses to be pushed to postgres_fdw foreign servers in more cases (Etsuro Fujita) Section Section 

Improve optimizer cost accounting for postgres_fdw queries (Etsuro Fujita) Section Section Section 

Properly honor WITH CHECK OPTION on views that reference postgres_fdw tables (Etsuro Fujita) Section 
While CHECK OPTIONs on postgres_fdw tables are ignored (because the reference is foreign), views on such tables are considered local, so this change enforces CHECK OPTIONs on them. Previously, only INSERTs and UPDATEs with RETURNING clauses that returned CHECK OPTION values were validated.

Allow pg_stat_statements_reset() to be more granular (Haribabu Kommi, Amit Kapila) Section 
The function now allows reset of statistics for specific databases, users, and queries.

Allow control of the auto_explain log level (Tom Dunstan, Andrew Dunstan) Section 
The default is LOG.

Update unaccent rules with new punctuation and symbols (Hugh Ranalli, Michael Paquier) Section 

Allow unaccent to handle some accents encoded as combining characters (Hugh Ranalli) Section 

Allow unaccent to remove accents from Greek characters (Tasos Maschalidis) Section 

Add a parameter to amcheck's bt_index_parent_check() function to check each index tuple from the root of the tree (Peter Geoghegan) Section 

Improve oid2name and vacuumlo option handling to match other commands (Tatsuro Yamada) Section Section 

## E.23.4. Acknowledgments
The following individuals (in alphabetical order) have contributed to this release as patch authors, committers, reviewers, testers, or reporters of issues.

 | Abhijit Menon-Sen
 | Achilleas Mantzios
 | Adam Berlin
 | Adam Bielanski
 | Aditya Toshniwal
 | Adrien Nayrat
 | Alan Jackson
 | Albert Schabhuetl
 | Aleksander Alekseev
 | Alex Aktsipetrov
 | Alex Kliukin
 | Alex Macy
 | Alexander Korotkov
 | Alexander Kukushkin
 | Alexander Kuzmenkov
 | Alexander Lakhin
 | Alexandra Ryzhevich
 | Alexey Bashtanov
 | Alexey Ermakov
 | Alexey Kondratov
 | Alexey Kryuchkov
 | Alexey Stepanov
 | Allison Kaptur
 | Alvaro Herrera
 | Alyssa Ross
 | Amit Kapila
 | Amit Khandekar
 | Amit Langote
 | Amul Sul
 | Anastasia Lubennikova
 | Andre Hansel
 | Andrea Gelmini
 | Andreas Joseph Krogh
 | Andreas Karlsson
 | Andreas Kunert
 | Andreas Scherbaum
 | Andreas Seltenreich
 | Andrei Yahorau
 | Andres Freund
 | Andrew Dunstan
 | Andrew Fletcher
 | Andrew Gierth
 | Andrew Krasichkov
 | Andrey Borodin
 | Andrey Klychkov
 | Andrey Lepikhov
 | Andy Abelisto
 | Anthony Greene
 | Anthony Skorski
 | Antonin Houska
 | Arne Roland
 | Arseny Sher
 | Arthur Zakirov
 | Ash Marath
 | Ashutosh Bapat
 | Ashutosh Sharma
 | Ashwin Agrawal
 | Aya Iwata
 | Bartosz Polnik
 | Basil Bourque
 | Bernd Helmle
 | Brad DeJong
 | Brigitte Blanc-Lafay
 | Bruce Klein
 | Bruce Momjian
 | Bruno Wolff
 | Chapman Flack
 | Chen Huajun
 | Chris Travers
 | Chris Wilson
 | Christian Hofstaedtler
 | Christoph Berg
 | Christoph Moench-Tegeder
 | Clemens Ladisch
 | Colm McHugh
 | Corey Huinker
 | Craig Ringer
 | Dagfinn Ilmari Mannsaker
 | Daisuke Higuchi
 | Daniel Fiori
 | Daniel Gustafsson
 | Daniel Verite
 | Daniel Westermann
 | Daniel Wilches
 | Darafei Praliaskouski
 | Daryl Waycott
 | Dave Cramer
 | David Binderman
 | David Fetter
 | David G. Johnston
 | David Rowley
 | David Steele
 | Davy Machado
 | Dean Rasheed
 | Derek Hans
 | Derek Nelson
 | Devrim Gunduz
 | Dian Fay
 | Didier Gautheron
 | Dilip Kumar
 | Dmitry Dolgov
 | Dmitry Marakasov
 | Dmitry Molotkov
 | Dmitry Shalashov
 | Don Seiler
 | Donald Dong
 | Doug Rady
 | Edmund Horner
 | Eduards Bezverhijs
 | Elvis Pranskevichus
 | Emanuel Araujo
 | Emre Hasegeli
 | Eric Cyr
 | Erik Rijkers
 | Ertugrul Kahveci
 | Etsuro Fujita
 | Eugen Konkov
 | Euler Taveira
 | Fabien Coelho
 | Fabrizio de Royes Mello
 | Feike Steenbergen
 | Filip Rembialkowski
 | Gaby Schilders
 | Geert Lobbestael
 | George Tarasov
 | Georgios Kokolatos
 | Gianni Ciolli
 | Gilles Darold
 | Greg Stark
 | Grigory Smolkin
 | Guillaume Lelarge
 | Gunnlaugur Thor Briem
 | Gurjeet Singh
 | Hadi Moshayedi
 | Hailong Li
 | Hans Buschmann
 | Haribabu Kommi
 | Haruka Takatsuka
 | Hayato Kuroda
 | Heikki Linnakangas
 | Hironobu Suzuki
 | Hubert Lubaczewski
 | Hugh Ranalli
 | Ian Barwick
 | Ibrar Ahmed
 | Ildar Musin
 | Insung Moon
 | Ioseph Kim
 | Isaac Morland
 | Ivan Panchenko
 | Jack Kelly
 | Jacob Champion
 | Jaime Casanova
 | Jakob Egger
 | Jakub Glapa
 | Jakub Janecek
 | James Coleman
 | James Inform
 | James Robinson
 | James Sewell
 | James Tomson
 | Jan Chochol
 | Jaroslav Sivy
 | Jean-Christophe Arnu
 | Jean-Marc Voillequin
 | Jean-Pierre Pelletier
 | Jeevan Chalke
 | Jeevan Ladhe
 | Jeff Davis
 | Jeff Janes
 | Jeremy Evans
 | Jeremy Schneider
 | Jeremy Smith
 | Jerry Jelinek
 | Jesper Pedersen
 | Jianing Yang
 | Jie Zhang
 | Jim Nasby
 | Jimmy Yih
 | Joe Conway
 | Joe Wildish
 | Joerg Sonnenberger
 | John Klann
 | John Naylor
 | Jonah Harris
 | Jonathan S. Katz
 | Jorge Gustavo Rocha
 | Jose Arthur Benetasso Villanova
 | Joshua D. Drake
 | Juan Jose Santamaria Flecha
 | Julian Hsiao
 | Julian Markwort
 | Julian Schauder
 | Julien Rouhaud
 | Jurgen Purtz
 | Jurgen Strobel
 | Justin Pryzby
 | Kaiting Chen
 | Karen Huddleston
 | Karl Czajkowski
 | Karl O. Pinc
 | Keiichi Hirobe
 | Keith Fiske
 | Ken Tanzer
 | Kenji Uno
 | Kevin Grittner
 | Kevin Hale Boyes
 | Kieran McCusker
 | Kirk Jamison
 | Kohei KaiGai
 | Konstantin Knizhnik
 | Konstantin Kuznetsov
 | Kristjan Tammekivi
 | Kuntal Ghosh
 | Kyle Samson
 | Kyotaro Horiguchi
 | Ltitia Avrot
 | Lars Kanis
 | Laurenz Albe
 | Lim Myungkyu
 | Liu Huailing
 | Liudmila Mantrova
 | Lloyd Albin
 | Luca Ferrari
 | Luis M. Carril
 | Lukas Eder
 | Lukas Fittl
 | Madelaine Thibaut
 | Madeleine Thompson
 | Magnus Hagander
 | Mahendra Singh
 | Mai Peng
 | Maksim Milyutin
 | Maksym Boguk
 | Malthe Borch
 | Manuel Rigger
 | Marco Atzeri
 | Marco Slot
 | Marina Polyakova
 | Mario De Frutos Dieguez
 | Marius Timmer
 | Mark Chambers
 | Mark Dilger
 | Marko Tiikkaja
 | Markus Winand
 | Martin Marques
 | Masahiko Sawada
 | Masao Fujii
 | Mateusz Guzik
 | Mathias Brossard
 | Matt Williams
 | Matthias Otterbach
 | Matvey Arye
 | Melanie Plageman
 | Mi Tar
 | Michael Banck
 | Michael Davidson
 | Michael Meskes
 | Michael Paquier
 | Michael Vitale
 | Michel Pelletier
 | Mikalai Keida
 | Mike Palmiotto
 | Mithun Cy
 | Morgan Owens
 | Murat Kabilov
 | Nathan Bossart
 | Nawaz Ahmed
 | Neeraj Kumar
 | Nick Barnes
 | Nico Williams
 | Nikita Glukhov
 | Nikolay Shaplov
 | Ning Yu
 | Nishant Fnu
 | Noah Misch
 | Norbert Benkocs
 | Noriyoshi Shinoda
 | Oleg Bartunov
 | Oleg Samoilov
 | Oleksii Kliukin
 | Ondrej Bouda
 | Oskari Saarenmaa
 | Pan Bian
 | Patrick Francelle
 | Patrick McHardy
 | Paul A. Jungwirth
 | Paul Bonaud
 | Paul Guo
 | Paul Martinez
 | Paul Ramsey
 | Paul Schaap
 | Paul van der Linden
 | Pavan Deolasee
 | Pavel Oskin
 | Pavel Raiskup
 | Pavel Stehule
 | Peifeng Qiu
 | Peter Billen
 | Peter Eisentraut
 | Peter Geoghegan
 | Peter Neave
 | Petr Fedorov
 | Petr Jelinek
 | Petr Slavov
 | Petru-Florin Mihancea
 | Phil Bayer
 | Phil Florent
 | Philip Dube
 | Pierre Ducroquet
 | Piotr Gabriel Kosinski
 | Piotr Stefaniak
 | Piotr Wlodarczyk
 | Prabhat Sahu
 | Quentin Rameau
 | Rafael Castro
 | Rafia Sabih
 | Rahila Syed
 | Rajkumar Raghuwanshi
 | Rares Salcudean
 | Raul Marin Rodriguez
 | Regina Obe
 | Renaud Navarro
 | Richard Guo
 | Rick Otten
 | Rikard Falkeborn
 | RK Korlapati
 | Robbie Harwood
 | Robert Haas
 | Robert Treat
 | Robert Vollmert
 | Roger Curley
 | Roman Zharkov
 | Ronan Dunklau
 | Rui Hai Jiang
 | Rushabh Lathia
 | Ryan Lambert
 | Ryo Matsumura
 | Ryohei Nagaura
 | Ryohei Takahashi
 | Samuel Williams
 | Sand Stone
 | Sanyo Capobiango
 | Satoru Koizumi
 | Sean Johnston
 | Serge Latyntsev
 | Sergei Kornilov
 | Sergey Pashkov
 | Sergio Conde Gomez
 | Shawn Debnath
 | Shay Rojansky
 | Sho Kato
 | Shohei Mochizuki
 | Shouyu Luo
 | Simon Riggs
 | Sivasubramanian Ramasubramanian
 | Slawomir Chodnicki
 | Stas Kelvish
 | Stefan Kadow
 | Stepan Yankevych
 | Stephen Amell
 | Stephen Frost
 | Steve Rogerson
 | Steve Singer
 | Steven Winfield
 | Surafel Temesgen
 | Suraj Kharage
 | Suresh Kumar R
 | Takayuki Tsunakawa
 | Takeshi Ideriha
 | Takuma Hoshiai
 | Tasos Maschalidis
 | Tatsuo Ishii
 | Tatsuro Yamada
 | Teodor Sigaev
 | Thom Brown
 | Thomas Munro
 | Thomas Poty
 | Tillmann Schulz
 | Tim Mohlmann
 | Timur Birsh
 | Tobias Bussmann
 | Tom Cassidy
 | Tom Dunstan
 | Tom Gottfried
 | Tom Lane
 | Tomas Vondra
 | Tushar Ahuja
 | Ulf Adams
 | Vaishnavi Prabakaran
 | Victor Petrovykh
 | Victor Wagner
 | Victor Yegorov
 | Vijaykumar Jain
 | Vik Fearing
 | Vlad Sterzhanov
 | Vladimir Baranoff
 | Vladimir Kriukov
 | Wu Fei
 | Yaroslav Schekin
 | Yi Huang
 | Yoshikazu Imai
 | Yugo Nagata
 | Yulian Khodorkovskiy
 | Yuming Wang
 | YunQiang Su
 | Yuri Kurenkov
 | Yusuke Egashira
 | Yuzuko Hosoya
 | Zhou Digoal

 | Prev | Up | Next
 | E.22. Release 12.1  | Home |  E.24. Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 12.22 
 12.21 
 12.20 
 12.19 
 12.18 
 12.17 
 12.16 
 12.15 
 12.14 
 12.13 
 12.12 
 12.11 
 12.10 
 12.9 
 12.8 
 12.7 
 12.6 
 12.5 
 12.4 
 12.3 
 12.2 
 12.1 
 12.0

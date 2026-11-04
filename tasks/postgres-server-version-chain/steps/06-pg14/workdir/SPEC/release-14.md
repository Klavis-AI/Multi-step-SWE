<!-- source: https://www.postgresql.org/docs/release/14.0/ -->

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

##  PostgreSQL 14.0 

 | E.25. Release 14
 | Prev | Up | Appendix E. Release Notes | Home | Next

## E.25. Release 14

E.25.1. Overview
E.25.2. Migration to Version 14
E.25.3. Changes
E.25.4. Acknowledgments
Release date: 2021-09-30

## E.25.1. Overview
PostgreSQL 14 contains many new features and enhancements, including:

Stored procedures can now return data via OUT parameters.

The SQL-standard SEARCH and CYCLE options for common table expressions have been implemented.

Subscripting can now be applied to any data type for which it is a useful notation, not only arrays. In this release, the jsonb and hstore types have gained subscripting operators.

Range types have been extended by adding multiranges, allowing representation of noncontiguous data ranges.

Numerous performance improvements have been made for parallel queries, heavily-concurrent workloads, partitioned tables, logical replication, and vacuuming.

B-tree index updates are managed more efficiently, reducing index bloat.

VACUUM automatically becomes more aggressive, and skips inessential cleanup, if the database starts to approach a transaction ID wraparound condition.

Extended statistics can now be collected on expressions, allowing better planning results for complex queries.

libpq now has the ability to pipeline multiple queries, which can boost throughput over high-latency connections.
The above items and other new features of PostgreSQL 14 are explained in more detail in the sections below.

## E.25.2. Migration to Version 14
A dump/restore using pg_dumpall or use of pg_upgrade or logical replication is required for those wishing to migrate data from any previous release. See Section 19.6 for general information on migrating to new major releases.
Version 14 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

User-defined objects that reference certain built-in array functions along with their argument types must be recreated (Tom Lane) Section 
Specifically, array_append(), array_prepend(), array_cat(), array_position(), array_positions(), array_remove(), array_replace(), and width_bucket() used to take anyarray arguments but now take anycompatiblearray. Therefore, user-defined objects like aggregates and operators that reference those array function signatures must be dropped before upgrading, and recreated once the upgrade completes.

Remove deprecated containment operators @ and ~ for built-in geometric data types and contrib modules cube, hstore, intarray, and seg (Justin Pryzby) Section Section 
The more consistently named <@ and @> have been recommended for many years.

Fix to_tsquery() and websearch_to_tsquery() to properly parse query text containing discarded tokens (Alexander Korotkov) Section 
Certain discarded tokens, like underscore, caused the output of these functions to produce incorrect tsquery output, e.g., both websearch_to_tsquery('"pg_class pg"') and to_tsquery('pg_class <-> pg') used to output ( 'pg' & 'class' ) <-> 'pg', but now both output 'pg' <-> 'class' <-> 'pg'.

Fix websearch_to_tsquery() to properly parse multiple adjacent discarded tokens in quotes (Alexander Korotkov) Section 
Previously, quoted text that contained multiple adjacent discarded tokens was treated as multiple tokens, causing incorrect tsquery output, e.g., websearch_to_tsquery('"aaa: bbb"') used to output 'aaa' <2> 'bbb', but now outputs 'aaa' <-> 'bbb'.

Change EXTRACT() to return type numeric instead of float8 (Peter Eisentraut) Section 
This avoids loss-of-precision issues in some usages. The old behavior can still be obtained by using the old underlying function date_part().
Also, EXTRACT(date) now throws an error for units that are not part of the date data type.

Change var_samp() and stddev_samp() with numeric parameters to return NULL when the input is a single NaN value (Tom Lane) Section 
Previously NaN was returned.

Return false for has_column_privilege() checks on non-existent or dropped columns when using attribute numbers (Joe Conway) Section 
Previously such attribute numbers returned an invalid-column error.

Fix handling of infinite window function ranges (Tom Lane) Section 
Previously window frame clauses like 'inf' PRECEDING AND 'inf' FOLLOWING returned incorrect results.

Remove factorial operators ! and !!, as well as function numeric_fac() (Mark Dilger) Section 
The factorial() function is still supported.

Disallow factorial() of negative numbers (Peter Eisentraut) Section 
Previously such cases returned 1.

Remove support for postfix (right-unary) operators (Mark Dilger) Section 
pg_dump and pg_upgrade will warn if postfix operators are being dumped.

Allow \D and \W shorthands to match newlines in regular expression newline-sensitive mode (Tom Lane) Section 
Previously they did not match newlines in this mode, but that disagrees with the behavior of other common regular expression engines. [^[:digit:]] or [^[:word:]] can be used to get the old behavior.

Disregard constraints when matching regular expression back-references (Tom Lane) Section 
For example, in (^\d+).*\1, the ^ constraint should be applied at the start of the string, but not when matching \1.

Disallow \w as a range start or end in regular expression character classes (Tom Lane) Section 
This previously was allowed but produced unexpected results.

Require custom server parameter names to use only characters that are valid in unquoted SQL identifiers (Tom Lane) Section Section 

Change the default of the password_encryption server parameter to scram-sha-256 (Peter Eisentraut) Section 
Previously it was md5. All new passwords will be stored as SHA256 unless this server setting is changed or the password is specified in MD5 format. Also, the legacy (and undocumented) Boolean-like values which were previously synonyms for md5 are no longer accepted.

Remove server parameter vacuum_cleanup_index_scale_factor (Peter Geoghegan) Section Section 
This setting was ignored starting in PostgreSQL version 13.3.

Remove server parameter operator_precedence_warning (Tom Lane) Section 
This setting was used for warning applications about PostgreSQL 9.5 changes.

Overhaul the specification of clientcert in pg_hba.conf (Kyotaro Horiguchi) Section 
Values 1/0/no-verify are no longer supported; only the strings verify-ca and verify-full can be used. Also, disallow verify-ca if cert authentication is enabled since cert requires verify-full checking.

Remove support for SSL compression (Daniel Gustafsson, Michael Paquier) Section Section 
This was already disabled by default in previous PostgreSQL releases, and most modern OpenSSL and TLS versions no longer support it.

Remove server and libpq support for the version 2 wire protocol (Heikki Linnakangas) Section 
This was last used as the default in PostgreSQL 7.3 (released in 2002).

Disallow single-quoting of the language name in the CREATE/DROP LANGUAGE command (Peter Eisentraut) Section 

Remove the composite types that were formerly created for sequences and toast tables (Tom Lane) Section 

Process doubled quote marks in ecpgSQL command strings correctly (Tom Lane) Section Section 
Previously 'abc''def' was passed to the server as 'abc'def', and "abc""def" was passed as "abc"def", causing syntax errors.

Prevent the containment operators (<@ and @>) for intarray from using GiST indexes (Tom Lane) Section 
Previously a full GiST index scan was required, so just avoid that and scan the heap, which is faster. Indexes created for this purpose should be removed.

Remove contrib program pg_standby (Justin Pryzby) Section 

Prevent tablefunc's function normal_rand() from accepting negative values (Ashutosh Bapat) Section 
Negative values produced undesirable results.

## E.25.3. Changes
Below you will find a detailed account of the changes between PostgreSQL 14 and the previous major release.

## E.25.3.1. Server

Add predefined roles pg_read_all_data and pg_write_all_data (Stephen Frost) Section 
These non-login roles can be used to give read or write permission to all tables, views, and sequences.

Add predefined role pg_database_owner that contains only the current database's owner (Noah Misch) Section 
This is especially useful in template databases.

Remove temporary files after backend crashes (Euler Taveira) Section 
Previously, such files were retained for debugging purposes. If necessary, deletion can be disabled with the new server parameter remove_temp_files_after_crash.

Allow long-running queries to be canceled if the client disconnects (Sergey Cherkashin, Thomas Munro) Section 
The server parameter client_connection_check_interval allows control over whether loss of connection is checked for intra-query. (This is supported on Linux and a few other operating systems.)

Add an optional timeout parameter to pg_terminate_backend() (Magnus Hagander) Section 

Allow wide tuples to be always added to almost-empty heap pages (John Naylor, Floris van Nee) Section 
Previously tuples whose insertion would have exceeded the page's fill factor were instead added to new pages.

Add Server Name Indication (SNI) in SSL connection packets (Peter Eisentraut) Section 
This can be disabled by turning off client connection option sslsni.

E.25.3.1.1. Vacuuming

Allow vacuum to skip index vacuuming when the number of removable index entries is insignificant (Masahiko Sawada, Peter Geoghegan) Section Section 
The vacuum parameter INDEX_CLEANUP has a new default of auto that enables this optimization.

Allow vacuum to more eagerly add deleted btree pages to the free space map (Peter Geoghegan) Section 
Previously vacuum could only add pages to the free space map that were marked as deleted by previous vacuums.

Allow vacuum to reclaim space used by unused trailing heap line pointers (Matthias van de Meent, Peter Geoghegan) Section 

Allow vacuum to be more aggressive in removing dead rows during minimal-locking index operations (Alvaro Herrera) Section Section Section 
Specifically, CREATE INDEX CONCURRENTLY and REINDEX CONCURRENTLY no longer limit the dead row removal of other relations.

Speed up vacuuming of databases with many relations (Tatsuhito Kasahara) Section 

Reduce the default value of vacuum_cost_page_miss to better reflect current hardware capabilities (Peter Geoghegan) Section 

Add ability to skip vacuuming of TOAST tables (Nathan Bossart) Section 
VACUUM now has a PROCESS_TOAST option which can be set to false to disable TOAST processing, and vacuumdb has a --no-process-toast option.

Have COPY FREEZE appropriately update page visibility bits (Anastasia Lubennikova, Pavan Deolasee, Jeff Janes) Section 

Cause vacuum operations to be more aggressive if the table is near xid or multixact wraparound (Masahiko Sawada, Peter Geoghegan) Section 
This is controlled by vacuum_failsafe_age and vacuum_multixact_failsafe_age.

Increase warning time and hard limit before transaction id and multi-transaction wraparound (Noah Misch) Section 
This should reduce the possibility of failures that occur without having issued warnings about wraparound.

Add per-index information to autovacuum logging output (Masahiko Sawada) Section 

E.25.3.1.2. Partitioning

Improve the performance of updates and deletes on partitioned tables with many partitions (Amit Langote, Tom Lane) Section Section Section 
This change greatly reduces the planner's overhead for such cases, and also allows updates/deletes on partitioned tables to use execution-time partition pruning.

Allow partitions to be detached in a non-blocking manner (Alvaro Herrera) Section 
The syntax is ALTER TABLE ... DETACH PARTITION ... CONCURRENTLY, and FINALIZE.

Ignore COLLATE clauses in partition boundary values (Tom Lane) Section 
Previously any such clause had to match the collation of the partition key; but it's more consistent to consider that it's automatically coerced to the collation of the partition key.

E.25.3.1.3. Indexes

Allow btree index additions to remove expired index entries to prevent page splits (Peter Geoghegan) Section Section 
This is particularly helpful for reducing index bloat on tables whose indexed columns are frequently updated.

Allow BRIN indexes to record multiple min/max values per range (Tomas Vondra) Section 
This is useful if there are groups of values in each page range.

Allow BRIN indexes to use bloom filters (Tomas Vondra) Section 
This allows BRIN indexes to be used effectively with data that is not well-localized in the heap.

Allow some GiST indexes to be built by presorting the data (Andrey Borodin) Section 
Presorting happens automatically and allows for faster index creation and smaller indexes.

Allow SP-GiST indexes to contain INCLUDE'd columns (Pavel Borisov) Section 

E.25.3.1.4. Optimizer

Allow hash lookup for IN clauses with many constants (James Coleman, David Rowley) Section 
Previously the code always sequentially scanned the list of values.

Increase the number of places extended statistics can be used for OR clause estimation (Tomas Vondra, Dean Rasheed) Section Section Section 

Allow extended statistics on expressions (Tomas Vondra) Section 
This allows statistics on a group of expressions and columns, rather than only columns like previously. System view pg_stats_ext_exprs reports such statistics.

Allow efficient heap scanning of a range of TIDs (Edmund Horner, David Rowley) Section 
Previously a sequential scan was required for non-equality TID specifications.

Fix EXPLAIN CREATE TABLE AS and EXPLAIN CREATE MATERIALIZED VIEW to honor IF NOT EXISTS (Bharath Rupireddy) Section 
Previously, if the object already existed, EXPLAIN would fail.

E.25.3.1.5. General Performance

Improve the speed of computing MVCCvisibility snapshots on systems with many CPUs and high session counts (Andres Freund) Section Section Section Section Section Section 
This also improves performance when there are many idle sessions.

Add executor method to memoize results from the inner side of a nested-loop join (David Rowley) Section 
This is useful if only a small percentage of rows is checked on the inner side. It can be disabled via server parameter enable_memoize.

Allow window functions to perform incremental sorts (David Rowley) Section 

Improve the I/O performance of parallel sequential scans (Thomas Munro, David Rowley) Section 
This was done by allocating blocks in groups to parallel workers.

Allow a query referencing multiple foreign tables to perform foreign table scans in parallel (Robert Haas, Kyotaro Horiguchi, Thomas Munro, Etsuro Fujita) Section 
postgres_fdw supports this type of scan if async_capable is set.

Allow analyze to do page prefetching (Stephen Frost) Section 
This is controlled by maintenance_io_concurrency.

Improve performance of regular expression searches (Tom Lane) Section Section Section Section Section Section Section Section Section Section Section 

Dramatically improve Unicode normalization performance (John Naylor) Section Section 
This speeds normalize() and IS NORMALIZED.

Add ability to use LZ4 compression on TOAST data (Dilip Kumar) Section 
This can be set at the column level, or set as a default via server parameter default_toast_compression. The server must be compiled with --with-lz4 to support this feature. The default setting is still pglz.

E.25.3.1.6. Monitoring

If server parameter compute_query_id is enabled, display the query id in pg_stat_activity, EXPLAIN VERBOSE, csvlog, and optionally in log_line_prefix (Julien Rouhaud) Section Section Section Section Section 
A query id computed by an extension will also be displayed.

Improve logging of auto-vacuum and auto-analyze (Stephen Frost, Jakub Wartak) Section 
This reports I/O timings for auto-vacuum and auto-analyze if track_io_timing is enabled. Also, report buffer read and dirty rates for auto-analyze.

Add information about the original user name supplied by the client to the output of log_connections (Jacob Champion) Section 

E.25.3.1.7. System Views

Add system view pg_stat_progress_copy to report COPY progress (Josef Simanek, Matthias van de Meent) Section Section 

Add system view pg_stat_wal to report WAL activity (Masahiro Ikeda) Section Section Section 

Add system view pg_stat_replication_slots to report replication slot activity (Masahiko Sawada, Amit Kapila, Vignesh C) Section Section Section Section 
The function pg_stat_reset_replication_slot() resets slot statistics.

Add system view pg_backend_memory_contexts to report session memory usage (Atsushi Torikoshi, Fujii Masao) Section Section 

Add function pg_log_backend_memory_contexts() to output the memory contexts of arbitrary backends (Atsushi Torikoshi) Section 

Add session statistics to the pg_stat_database system view (Laurenz Albe) Section 

Add columns to pg_prepared_statements to report generic and custom plan counts (Atsushi Torikoshi, Kyotaro Horiguchi) Section 

Add lock wait start time to pg_locks (Atsushi Torikoshi) Section 

Make the archiver process visible in pg_stat_activity (Kyotaro Horiguchi) Section 

Add wait event WalReceiverExit to report WAL receiver exit wait time (Fujii Masao) Section 

Implement information schema view routine_column_usage to track columns referenced by function and procedure default expressions (Peter Eisentraut) Section 

E.25.3.1.8. Authentication

Allow an SSL certificate's distinguished name (DN) to be matched for client certificate authentication (Andrew Dunstan) Section 
The new pg_hba.conf option clientname=DN allows comparison with certificate attributes beyond the CN and can be combined with ident maps.

Allow pg_hba.conf and pg_ident.conf records to span multiple lines (Fabien Coelho) Section 
A backslash at the end of a line allows record contents to be continued on the next line.

Allow the specification of a certificate revocation list (CRL) directory (Kyotaro Horiguchi) Section 
This is controlled by server parameter ssl_crl_dir and libpq connection option sslcrldir. Previously only single CRL files could be specified.

Allow passwords of an arbitrary length (Tom Lane, Nathan Bossart) Section 

E.25.3.1.9. Server Configuration

Add server parameter idle_session_timeout to close idle sessions (Li Japin) Section 
This is similar to idle_in_transaction_session_timeout.

Change checkpoint_completion_target default to 0.9 (Stephen Frost) Section 
The previous default was 0.5.

Allow %P in log_line_prefix to report the parallel group leader's PID for a parallel worker (Justin Pryzby) Section 

Allow unix_socket_directories to specify paths as individual, comma-separated quoted strings (Ian Lawrence Barwick) Section 
Previously all the paths had to be in a single quoted string.

Allow startup allocation of dynamic shared memory (Thomas Munro) Section 
This is controlled by min_dynamic_shared_memory. This allows more use of huge pages.

Add server parameter huge_page_size to control the size of huge pages used on Linux (Odin Ugedal) Section 

## E.25.3.2. Streaming Replication and Recovery

Allow standby servers to be rewound via pg_rewind (Heikki Linnakangas) Section 

Allow the restore_command setting to be changed during a server reload (Sergei Kornilov) Section 
You can also set restore_command to an empty string and reload to force recovery to only read from the pg_wal directory.

Add server parameter log_recovery_conflict_waits to report long recovery conflict wait times (Bertrand Drouvot, Masahiko Sawada) Section Section 

Pause recovery on a hot standby server if the primary changes its parameters in a way that prevents replay on the standby (Peter Eisentraut) Section 
Previously the standby would shut down immediately.

Add function pg_get_wal_replay_pause_state() to report the recovery state (Dilip Kumar) Section 
It gives more detailed information than pg_is_wal_replay_paused(), which still exists.

Add new read-only server parameter in_hot_standby (Haribabu Kommi, Greg Nancarrow, Tom Lane) Section 
This allows clients to easily detect whether they are connected to a hot standby server.

Speed truncation of small tables during recovery on clusters with a large number of shared buffers (Kirk Jamison) Section 

Allow file system sync at the start of crash recovery on Linux (Thomas Munro) Section 
By default, PostgreSQL opens and fsyncs each data file in the database cluster at the start of crash recovery. A new setting, recovery_init_sync_method=syncfs, instead syncs each filesystem used by the cluster. This allows for faster recovery on systems with many database files.

Add function pg_xact_commit_timestamp_origin() to return the commit timestamp and replication origin of the specified transaction (Movead Li) Section 

Add the replication origin to the record returned by pg_last_committed_xact() (Movead Li) Section 

Allow replication origin functions to be controlled using standard function permission controls (Martin Marques) Section 
Previously these functions could only be executed by superusers, and this is still the default.

E.25.3.2.1. Logical Replication

Allow logical replication to stream long in-progress transactions to subscribers (Dilip Kumar, Amit Kapila, Ajin Cherian, Tomas Vondra, Nikhil Sontakke, Stas Kelvich) Section Section Section Section 
Previously transactions that exceeded logical_decoding_work_mem were written to disk until the transaction completed.

Enhance the logical replication API to allow streaming large in-progress transactions (Tomas Vondra, Dilip Kumar, Amit Kapila) Section 
The output functions begin with stream. test_decoding also supports these.

Allow multiple transactions during table sync in logical replication (Peter Smith, Amit Kapila, Takamichi Osumi) Section 

Immediately WAL-log subtransaction and top-level XID association (Tomas Vondra, Dilip Kumar, Amit Kapila) Section 
This is useful for logical decoding.

Enhance logical decoding APIs to handle two-phase commits (Ajin Cherian, Amit Kapila, Nikhil Sontakke, Stas Kelvich) Section Section Section 
This is controlled via pg_create_logical_replication_slot().

Add cache invalidation messages to the WAL during command completion when using logical replication (Dilip Kumar, Tomas Vondra, Amit Kapila) Section 
This allows logical streaming of in-progress transactions. When logical replication is disabled, invalidation messages are generated only at transaction completion.

Allow logical decoding to more efficiently process cache invalidation messages (Dilip Kumar) Section 
This allows logical decoding to work efficiently in presence of a large amount of DDL.

Allow control over whether logical decoding messages are sent to the replication stream (David Pirotte, Euler Taveira) Section 

Allow logical replication subscriptions to use binary transfer mode (Dave Cramer) Section 
This is faster than text mode, but slightly less robust.

Allow logical decoding to be filtered by xid (Markus Wanner) Section 

## E.25.3.3. SELECT, INSERT

Reduce the number of keywords that can't be used as column labels without AS (Mark Dilger) Section 
There are now 90% fewer restricted keywords.

Allow an alias to be specified for JOIN's USING clause (Peter Eisentraut) Section 
The alias is created by writing AS after the USING clause. It can be used as a table qualification for the merged USING columns.

Allow DISTINCT to be added to GROUP BY to remove duplicate GROUPING SET combinations (Vik Fearing) Section 
For example, GROUP BY CUBE (a,b), CUBE (b,c) will generate duplicate grouping combinations without DISTINCT.

Properly handle DEFAULT entries in multi-row VALUES lists in INSERT (Dean Rasheed) Section 
Such cases used to throw an error.

Add SQL-standard SEARCH and CYCLE clauses for common table expressions (Peter Eisentraut) Section Section 
The same results could be accomplished using existing syntax, but much less conveniently.

Allow column names in the WHERE clause of ON CONFLICT to be table-qualified (Tom Lane) Section 
Only the target table can be referenced, however.

## E.25.3.4. Utility Commands

Allow REFRESH MATERIALIZED VIEW to use parallelism (Bharath Rupireddy) Section 

Allow REINDEX to change the tablespace of the new index (Alexey Kondratov, Michael Paquier, Justin Pryzby) Section Section 
This is done by specifying a TABLESPACE clause. A --tablespace option was also added to reindexdb to control this.

Allow REINDEX to process all child tables or indexes of a partitioned relation (Justin Pryzby, Michael Paquier) Section 

Allow index commands using CONCURRENTLY to avoid waiting for the completion of other operations using CONCURRENTLY (Alvaro Herrera) Section Section Section 

Improve the performance of COPY FROM in binary mode (Bharath Rupireddy, Amit Langote) Section 

Preserve SQL standard syntax for SQL-defined functions in view definitions (Tom Lane) Section 
Previously, calls to SQL-standard functions such as EXTRACT() were shown in plain function-call syntax. The original syntax is now preserved when displaying a view or rule.

Add the SQL-standard clause GRANTED BY to GRANT and REVOKE (Peter Eisentraut) Section 

Add OR REPLACE option for CREATE TRIGGER (Takamichi Osumi) Section 
This allows pre-existing triggers to be conditionally replaced.

Allow TRUNCATE to operate on foreign tables (Kazutaka Onishi, Kohei KaiGai) Section 
The postgres_fdw module also now supports this.

Allow publications to be more easily added to and removed from a subscription (Japin Li) Section 
The new syntax is ALTER SUBSCRIPTION ... ADD/DROP PUBLICATION. This avoids having to specify all publications to add/remove entries.

Add primary keys, unique constraints, and foreign keys to system catalogs (Peter Eisentraut) Section Section 
These changes help GUI tools analyze the system catalogs. The existing unique indexes of catalogs now have associated UNIQUE or PRIMARY KEY constraints. Foreign key relationships are not actually stored or implemented as constraints, but can be obtained for display from the function pg_get_catalog_foreign_keys().

Allow CURRENT_ROLE every place CURRENT_USER is accepted (Peter Eisentraut) Section 

## E.25.3.5. Data Types

Allow extensions and built-in data types to implement subscripting (Dmitry Dolgov) Section 
Previously subscript handling was hard-coded into the server, so that subscripting could only be applied to array types. This change allows subscript notation to be used to extract or assign portions of a value of any type for which the concept makes sense.

Allow subscripting of JSONB (Dmitry Dolgov) Section Section Section 
JSONB subscripting can be used to extract and assign to portions of JSONB documents.

Add support for multirange data types (Paul Jungwirth, Alexander Korotkov) Section Section Section Section 
These are like range data types, but they allow the specification of multiple, ordered, non-overlapping ranges. An associated multirange type is automatically created for every range type.

Add support for the stemming of languages Armenian, Basque, Catalan, Hindi, Serbian, and Yiddish (Peter Eisentraut) Section Section Section 

Allow tsearch data files to have unlimited line lengths (Tom Lane) Section 
The previous limit was 4K bytes. Also remove function t_readline().

Add support for Infinity and -Infinity values in the numeric data type (Tom Lane) Section 
Floating-point data types already supported these.

Add point operators<<| and |>> representing strictly above/below tests (Emre Hasegeli) Section 
Previously these were called >^ and <^, but that naming is inconsistent with other geometric data types. The old names remain available, but may someday be removed.

Add operators to add and subtract LSN and numeric (byte) values (Fujii Masao) Section 

Allow binary data transfer to be more forgiving of array and record OID mismatches (Tom Lane) Section 

Create composite array types for system catalogs (Wenjing Zeng) Section 
User-defined relations have long had composite types associated with them, and also array types over those composite types. System catalogs now do as well. This change also fixes an inconsistency that creating a user-defined table in single-user mode would fail to create a composite array type.

## E.25.3.6. Functions

Allow SQL-language functions and procedures to use SQL-standard function bodies (Peter Eisentraut) Section 
Previously only string-literal function bodies were supported. When writing a function or procedure in SQL-standard syntax, the body is parsed immediately and stored as a parse tree. This allows better tracking of function dependencies, and can have security benefits.

Allow procedures to have OUT parameters (Peter Eisentraut) Section Section 

Allow some array functions to operate on a mix of compatible data types (Tom Lane) Section 
The functions array_append(), array_prepend(), array_cat(), array_position(), array_positions(), array_remove(), array_replace(), and width_bucket() now take anycompatiblearray instead of anyarray arguments. This makes them less fussy about exact matches of argument types.

Add SQL-standard trim_array() function (Vik Fearing) Section 
This could already be done with array slices, but less easily.

Add bytea equivalents of ltrim() and rtrim() (Joel Jacobson) Section 

Support negative indexes in split_part() (Nikhil Benesch) Section 
Negative values start from the last field and count backward.

Add string_to_table() function to split a string on delimiters (Pavel Stehule) Section 
This is similar to the regexp_split_to_table() function.

Add unistr() function to allow Unicode characters to be specified as backslash-hex escapes in strings (Pavel Stehule) Section 
This is similar to how Unicode can be specified in literal strings.

Add bit_xor() XOR aggregate function (Alexey Bashtanov) Section 

Add function bit_count() to return the number of bits set in a bit or byte string (David Fetter) Section 

Add date_bin() function (John Naylor) Section Section 
This function "bins" input timestamps, grouping them into intervals of a uniform length aligned with a specified origin.

Allow make_timestamp()/make_timestamptz() to accept negative years (Peter Eisentraut) Section 
Negative values are interpreted as BC years.

Add newer regular expression substring() syntax (Peter Eisentraut) Section 
The new SQL-standard syntax is SUBSTRING(text SIMILAR pattern ESCAPE escapechar). The previous standard syntax was SUBSTRING(text FROM pattern FOR escapechar), which is still accepted by PostgreSQL.

Allow complemented character class escapes \D, \S, and \W within regular expression brackets (Tom Lane) Section 

Add [[:word:]] as a regular expression character class, equivalent to \w (Tom Lane) Section 

Allow more flexible data types for default values of lead() and lag() window functions (Vik Fearing) Section 

Make non-zero floating-point values divided by infinity return zero (Kyotaro Horiguchi) Section 
Previously such operations produced underflow errors.

Make floating-point division of NaN by zero return NaN (Tom Lane) Section 
Previously this returned an error.

Cause exp() and power() for negative-infinity exponents to return zero (Tom Lane) Section Section Section 
Previously they often returned underflow errors.

Improve the accuracy of geometric computations involving infinity (Tom Lane) Section 

Mark built-in type coercion functions as leakproof where possible (Tom Lane) Section 
This allows more use of functions that require type conversion in security-sensitive situations.

Change pg_describe_object(), pg_identify_object(), and pg_identify_object_as_address() to always report helpful error messages for non-existent objects (Michael Paquier) Section 

## E.25.3.7. PL/pgSQL

Improve PL/pgSQL's expression and assignment parsing (Tom Lane) Section 
This change allows assignment to array slices and nested record fields.

Allow plpgsql's RETURN QUERY to execute its query using parallelism (Tom Lane) Section 

Improve performance of repeated CALLs within plpgsql procedures (Pavel Stehule, Tom Lane) Section 

## E.25.3.8. Client Interfaces

Add pipeline mode to libpq (Craig Ringer, Matthieu Garrigues, Alvaro Herrera) Section 
This allows multiple queries to be sent, only waiting for completion when a specific synchronization message is sent.

Enhance libpq's target_session_attrs parameter options (Haribabu Kommi, Greg Nancarrow, Vignesh C, Tom Lane) Section Section 
The new options are read-only, primary, standby, and prefer-standby.

Improve the output format of libpq's PQtrace() (Aya Iwata, Alvaro Herrera) Section 

Allow an ECPG SQL identifier to be linked to a specific connection (Hayato Kuroda) Section 
This is done via DECLARE ... STATEMENT.

## E.25.3.9. Client Applications

Allow vacuumdb to skip index cleanup and truncation (Nathan Bossart) Section 
The options are --no-index-cleanup and --no-truncate.

Allow pg_dump to dump only certain extensions (Guillaume Lelarge) Section 
This is controlled by option --extension.

Add pgbenchpermute() function to randomly shuffle values (Fabien Coelho, Hironobu Suzuki, Dean Rasheed) Section 

Include disconnection times in the reconnection overhead measured by pgbench with -C (Yugo Nagata) Section 

Allow multiple verbose option specifications (-v) to increase the logging verbosity (Tom Lane) Section 
This behavior is supported by pg_dump, pg_dumpall, and pg_restore.

E.25.3.9.1. psql

Allow psql's \df and \do commands to specify function and operator argument types (Greg Sabino Mullane, Tom Lane) Section 
This helps reduce the number of matches printed for overloaded names.

Add an access method column to psql's \d[i|m|t]+ output (Georgios Kokolatos) Section 

Allow psql's \dt and \di to show TOAST tables and their indexes (Justin Pryzby) Section 

Add psql command \dX to list extended statistics objects (Tatsuro Yamada) Section 

Fix psql's \dT to understand array syntax and backend grammar aliases, like int for integer (Greg Sabino Mullane, Tom Lane) Section 

When editing the previous query or a file with psql's \e, or using \ef and \ev, ignore the results if the editor exits without saving (Laurenz Albe) Section 
Previously, such edits would load the previous query into the query buffer, and typically execute it immediately. This was deemed to be probably not what the user wants.

Improve tab completion (Vignesh C, Michael Paquier, Justin Pryzby, Georgios Kokolatos, Julien Rouhaud) Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section Section 

## E.25.3.10. Server Applications

Add command-line utility pg_amcheck to simplify running contrib/amcheck tests on many relations (Mark Dilger) Section 

Add --no-instructions option to initdb (Magnus Hagander) Section 
This suppresses the server startup instructions that are normally printed.

Stop pg_upgrade from creating analyze_new_cluster script (Magnus Hagander) Section 
Instead, give comparable vacuumdb instructions.

Remove support for the postmaster-o option (Magnus Hagander) Section 
This option was unnecessary since all passed options could already be specified directly.

## E.25.3.11. Documentation

Rename "Default Roles" to "Predefined Roles" (Bruce Momjian, Stephen Frost) Section 

Add documentation for the factorial() function (Peter Eisentraut) Section 
With the removal of the ! operator in this release, factorial() is the only built-in way to compute a factorial.

## E.25.3.12. Source Code

Add configure option --with-ssl={openssl} to allow future choice of the SSL library to use (Daniel Gustafsson, Michael Paquier) Section 
The spelling --with-openssl is kept for compatibility.

Add support for abstract Unix-domain sockets (Peter Eisentraut) Section 
This is currently supported on Linux and Windows.

Allow Windows to properly handle files larger than four gigabytes (Juan Jose Santamaria Flecha) Section 
For example this allows COPY,WAL files, and relation segment files to be larger than four gigabytes.

Add server parameter debug_discard_caches to control cache flushing for test purposes (Craig Ringer) Section Section Section 
Previously this behavior could only be set at compile time. To invoke it during initdb, use the new option --discard-caches.

Various improvements in valgrind error detection ability (Alvaro Herrera, Peter Geoghegan) Section Section Section 

Add a test module for the regular expression package (Tom Lane) Section 

Add support for LLVM version 12 (Andres Freund) Section 

Change SHA1, SHA2, and MD5 hash computations to use the OpenSSLEVP API (Michael Paquier) Section Section Section Section 
This is more modern and supports FIPS mode.

Remove separate build-time control over the choice of random number generator (Daniel Gustafsson) Section 
This is now always determined by the choice of SSL library.

Add direct conversion routines between EUC_TW and Big5 encodings (Heikki Linnakangas) Section 

Add collation version support for FreeBSD (Thomas Munro) Section 

Add amadjustmembers to the index access method API (Tom Lane) Section 
This allows an index access method to provide validity checking during creation of a new operator class or family.

Provide feature-test macros in libpq-fe.h for recently-added libpq features (Tom Lane, Alvaro Herrera) Section 
Historically, applications have usually used compile-time checks of PG_VERSION_NUM to test whether a feature is available. But that's normally the server version, which might not be a good guide to libpq's version. libpq-fe.h now offers #define symbols denoting application-visible features added in v14; the intent is to keep adding symbols for such features in future versions.

## E.25.3.13. Additional Modules

Allow subscripting of hstore values (Tom Lane, Dmitry Dolgov) Section 

Allow GiST/GIN pg_trgm indexes to do equality lookups (Julien Rouhaud) Section 
This is similar to LIKE except no wildcards are honored.

Allow the cube data type to be transferred in binary mode (KaiGai Kohei) Section 

Allow pgstattuple_approx() to report on TOAST tables (Peter Eisentraut) Section 

Add contrib module pg_surgery which allows changes to row visibility (Ashutosh Sharma) Section 
This is useful for correcting database corruption.

Add contrib module old_snapshot to report the XID/time mapping used by an active old_snapshot_threshold (Robert Haas) Section 

Allow amcheck to also check heap pages (Mark Dilger) Section 
Previously it only checked B-Tree index pages.

Allow pageinspect to inspect GiST indexes (Andrey Borodin, Heikki Linnakangas) Section Section 

Change pageinspect block numbers to be bigints (Peter Eisentraut) Section 

Mark btree_gist functions as parallel safe (Steven Winfield) Section 

E.25.3.13.1. pg_stat_statements

Move query hash computation from pg_stat_statements to the core server (Julien Rouhaud) Section 
The new server parameter compute_query_id's default of auto will automatically enable query id computation when this extension is loaded.

Cause pg_stat_statements to track top and nested statements separately (Julien Rohaud) Section 
Previously, when tracking all statements, identical top and nested statements were tracked as a single entry; but it seems more useful to separate such usages.

Add row counts for utility commands to pg_stat_statements (Fujii Masao, Katsuragi Yuta, Seino Yuki) Section Section 

Add pg_stat_statements_info system view to show pg_stat_statements activity (Katsuragi Yuta, Yuki Seino, Naoki Nakamichi) Section Section 

E.25.3.13.2. postgres_fdw

Allow postgres_fdw to INSERT rows in bulk (Takayuki Tsunakawa, Tomas Vondra, Amit Langote) Section Section 

Allow postgres_fdw to import table partitions if specified by IMPORT FOREIGN SCHEMA ... LIMIT TO (Matthias van de Meent) Section 
By default, only the root of a partitioned table is imported.

Add postgres_fdw function postgres_fdw_get_connections() to report open foreign server connections (Bharath Rupireddy) Section 

Allow control over whether foreign servers keep connections open after transaction completion (Bharath Rupireddy) Section 
This is controlled by keep_connections and defaults to on.

Allow postgres_fdw to reestablish foreign server connections if necessary (Bharath Rupireddy) Section Section 
Previously foreign server restarts could cause foreign table access errors.

Add postgres_fdw functions to discard cached connections (Bharath Rupireddy) Section 

## E.25.4. Acknowledgments
The following individuals (in alphabetical order) have contributed to this release as patch authors, committers, reviewers, testers, or reporters of issues.

 | Abhijit Menon-Sen
 | Adam Balogh
 | Adrian Ho
 | Ahsan Hadi
 | Ajin Cherian
 | Aleksander Alekseev
 | Alessandro Gherardi
 | Alex Kozhemyakin
 | Alexander Korotkov
 | Alexander Lakhin
 | Alexander Nawratil
 | Alexander Pyhalov
 | Alexandra Wang
 | Alexey Bashtanov
 | Alexey Bulgakov
 | Alexey Kondratov
 | Alvaro Herrera
 | Amit Kapila
 | Amit Khandekar
 | Amit Langote
 | Amul Sul
 | Anastasia Lubennikova
 | Andreas Grob
 | Andreas Kretschmer
 | Andreas Seltenreich
 | Andreas Wicht
 | Andres Freund
 | Andrew Bille
 | Andrew Dunstan
 | Andrew Gierth
 | Andrey Borodin
 | Andrey Lepikhov
 | Andy Fan
 | Anton Voloshin
 | Antonin Houska
 | Arne Roland
 | Arseny Sher
 | Arthur Nascimento
 | Arthur Zakirov
 | Ashutosh Bapat
 | Ashutosh Sharma
 | Ashwin Agrawal
 | Asif Rehman
 | Asim Praveen
 | Atsushi Torikoshi
 | Aya Iwata
 | Barry Pederson
 | Bas Poot
 | Bauyrzhan Sakhariyev
 | Beena Emerson
 | Benoit Lobreau
 | Bernd Helmle
 | Bernhard M. Wiedemann
 | Bertrand Drouvot
 | Bharath Rupireddy
 | Boris Kolpackov
 | Brar Piening
 | Brian Ye
 | Bruce Momjian
 | Bryn Llewellyn
 | Cameron Daniel
 | Chapman Flack
 | Charles Samborski
 | Charlie Hornsby
 | Chen Jiaoqian
 | Chris Wilson
 | Christian Quest
 | Christoph Berg
 | Christophe Courtois
 | Corey Huinker
 | Craig Ringer
 | Dagfinn Ilmari Mannsaker
 | Dana Burd
 | Daniel Cherniy
 | Daniel Gustafsson
 | Daniel Verite
 | Daniel Westermann
 | Daniele Varrazzo
 | Dar Alathar-Yemen
 | Darafei Praliaskouski
 | Dave Cramer
 | David Christensen
 | David Fetter
 | David G. Johnston
 | David Geier
 | David Gilman
 | David Pirotte
 | David Rowley
 | David Steele
 | David Turon
 | David Zhang
 | Dean Rasheed
 | Denis Patron
 | Dian Fay
 | Dilip Kumar
 | Dimitri Nuscheler
 | Dmitriy Kuzmin
 | Dmitry Dolgov
 | Dmitry Marakasov
 | Domagoj Smoljanovic
 | Dong Wook
 | Douglas Doole
 | Duncan Sands
 | Edmund Horner
 | Edson Richter
 | Egor Rogov
 | Ekaterina Kiryanova
 | Elena Indrupskaya
 | Emil Iggland
 | Emre Hasegeli
 | Eric Thinnes
 | Erik Rijkers
 | Erwin Brandstetter
 | Etienne Stalmans
 | Etsuro Fujita
 | Eugen Konkov
 | Euler Taveira
 | Fabien Coelho
 | Fabrizio de Royes Mello
 | Federico Caselli
 | Felix Lechner
 | Filip Gospodinov
 | Floris Van Nee
 | Frank Gagnepain
 | Frits Jalvingh
 | Georgios Kokolatos
 | Greg Nancarrow
 | Greg Rychlewski
 | Greg Sabino Mullane
 | Gregory Smith
 | Grigory Smolkin
 | Guillaume Lelarge
 | Guy Burgess
 | Guyren Howe
 | Haiying Tang
 | Hamid Akhtar
 | Hans Buschmann
 | Hao Wu
 | Haribabu Kommi
 | Harisai Hari
 | Hayato Kuroda
 | Heath Lord
 | Heikki Linnakangas
 | Henry Hinze
 | Herwig Goemans
 | Himanshu Upadhyaya
 | Hironobu Suzuki
 | Hiroshi Inoue
 | Hisanori Kobayashi
 | Honza Horak
 | Hou Zhijie
 | Hubert Lubaczewski
 | Hubert Zhang
 | Ian Barwick
 | Ibrar Ahmed
 | Ildus Kurbangaliev
 | Isaac Morland
 | Israel Barth
 | Itamar Gafni
 | Jacob Champion
 | Jaime Casanova
 | Jaime Soler
 | Jakub Wartak
 | James Coleman
 | James Hilliard
 | James Hunter
 | James Inform
 | Jan Mussler
 | Japin Li
 | Jasen Betts
 | Jason Harvey
 | Jason Kim
 | Jeevan Ladhe
 | Jeff Davis
 | Jeff Janes
 | Jelte Fennema
 | Jeremy Evans
 | Jeremy Finzel
 | Jeremy Smith
 | Jesse Kinkead
 | Jesse Zhang
 | Jie Zhang
 | Jim Doty
 | Jim Nasby
 | Jimmy Angelakos
 | Jimmy Yih
 | Jiri Fejfar
 | Joe Conway
 | Joel Jacobson
 | John Naylor
 | John Thompson
 | Jonathan Katz
 | Josef Simanek
 | Joseph Nahmias
 | Josh Berkus
 | Juan Jose Santamaria Flecha
 | Julien Rouhaud
 | Junfeng Yang
 | Jurgen Purtz
 | Justin Pryzby
 | Kazutaka Onishi
 | Keisuke Kuroda
 | Kelly Min
 | Kensuke Okamura
 | Kevin Sweet
 | Kevin Yeap
 | Kirk Jamison
 | Kohei KaiGai
 | Konstantin Knizhnik
 | Kota Miyake
 | Krzysztof Gradek
 | Kuntal Ghosh
 | Kyle Kingsbury
 | Kyotaro Horiguchi
 | Laurent Hasson
 | Laurenz Albe
 | Lee Dong Wook
 | Li Japin
 | Liu Huailing
 | Luc Vlaming
 | Ludovic Kuty
 | Luis Roberto
 | Lukas Eder
 | Ma Liangzhu
 | Maciek Sakrejda
 | Madan Kumar
 | Magnus Hagander
 | Mahendra Singh Thalor
 | Maksim Milyutin
 | Marc Boeren
 | Marcin Krupowicz
 | Marco Atzeri
 | Marek Szuba
 | Marina Polyakova
 | Mario Emmenlauer
 | Mark Dilger
 | Mark Wong
 | Mark Zhao
 | Markus Wanner
 | Martin Marques
 | Martin Visser
 | Masahiko Sawada
 | Masahiro Ikeda
 | Masao Fujii
 | Mathis Rudolf
 | Matthias van de Meent
 | Matthieu Garrigues
 | Matthijs van der Vleuten
 | Maxim Orlov
 | Melanie Plageman
 | Merlin Moncure
 | Michael Banck
 | Michael Brown
 | Michael Meskes
 | Michael Paquier
 | Michael Paul Killian
 | Michael Powers
 | Michael Vastola
 | Michail Nikolaev
 | Michal Albrycht
 | Mikael Gustavsson
 | Movead Li
 | Muhammad Usama
 | Nagaraj Raj
 | Naoki Nakamichi
 | Nathan Bossart
 | Nathan Long
 | Nazli Ugur Koyluoglu
 | Neha Sharma
 | Neil Chen
 | Nick Cleaton
 | Nico Williams
 | Nikhil Benesch
 | Nikhil Sontakke
 | Nikita Glukhov
 | Nikita Konev
 | Nikolai Berkoff
 | Nikolay Samokhvalov
 | Nikolay Shaplov
 | Nitin Jadhav
 | Noah Misch
 | Noriyoshi Shinoda
 | Odin Ugedal
 | Oleg Bartunov
 | Oleg Samoilov
 | Onder Kalaci
 | Pascal Legrand
 | Paul Forster
 | Paul Guo
 | Paul Jungwirth
 | Paul Martinez
 | Paul Sivash
 | Pavan Deolasee
 | Pavel Boev
 | Pavel Borisov
 | Pavel Luzanov
 | Pavel Stehule
 | Pengcheng Liu
 | Peter Eisentraut
 | Peter Geoghegan
 | Peter Smith
 | Peter Vandivier
 | Petr Fedorov
 | Petr Jelinek
 | Phil Krylov
 | Philipp Gramzow
 | Philippe Beaudoin
 | Phillip Menke
 | Pierre Giraud
 | Prabhat Sahu
 | Quan Zongliang
 | Rafi Shamim
 | Rahila Syed
 | Rajkumar Raghuwanshi
 | Ranier Vilela
 | Regina Obe
 | Remi Lapeyre
 | Robert Foggia
 | Robert Grange
 | Robert Haas
 | Robert Kahlert
 | Robert Sosinski
 | Robert Treat
 | Robin Abbi
 | Robins Tharakan
 | Roger Mason
 | Rohit Bhogate
 | Roman Zharkov
 | Ron L. Johnson
 | Ronan Dunklau
 | Ryan Lambert
 | Ryo Matsumura
 | Saeed Hubaishan
 | Sait Talha Nisanci
 | Sandro Mani
 | Santosh Udupi
 | Scott Ribe
 | Sehrope Sarkuni
 | Sergei Kornilov
 | Sergey Bernikov
 | Sergey Cherkashin
 | Sergey Koposov
 | Sergey Shinderuk
 | Sergey Zubkovsky
 | Shawn Wang
 | Shay Rojansky
 | Shi Yu
 | Shinya Kato
 | Shinya Okano
 | Sigrid Ehrenreich
 | Simon Norris
 | Simon Riggs
 | Sofoklis Papasofokli
 | Soumyadeep Chakraborty
 | Stas Kelvich
 | Stephan Springl
 | Stephane Lorek
 | Stephen Frost
 | Steven Winfield
 | Surafel Temesgen
 | Suraj Kharage
 | Sven Klemm
 | Takamichi Osumi
 | Takashi Menjo
 | Takayuki Tsunakawa
 | Tang Haiying
 | Tatsuhito Kasahara
 | Tatsuo Ishii
 | Tatsuro Yamada
 | Theodor Arsenij Larionov-Trichkin
 | Thomas Kellerer
 | Thomas Munro
 | Thomas Trenz
 | Tijs van Dam
 | Tom Ellis
 | Tom Gottfried
 | Tom Lane
 | Tom Vijlbrief
 | Tomas Barton
 | Tomas Vondra
 | Tomohiro Hiramitsu
 | Tony Reix
 | Vaishnavi Prabakaran
 | Valentin Gatien-Baron
 | Victor Wagner
 | Victor Yegorov
 | Vignesh C
 | Vik Fearing
 | Vitaly Ustinov
 | Vladimir Sitnikov
 | Vyacheslav Shablistyy
 | Wang Shenhao
 | Wei Wang
 | Wells Oliver
 | Wenjing Zeng
 | Wolfgang Walther
 | Yang Lin
 | Yanliang Lei
 | Yaoguang Chen
 | Yaroslav Pashinsky
 | Yaroslav Schekin
 | Yasushi Yamashita
 | Yoran Heling
 | YoungHwan Joo
 | Yugo Nagata
 | Yuki Seino
 | Yukun Wang
 | Yulin Pei
 | Yura Sokolov
 | Yuta Katsuragi
 | Yuta Kondo
 | Yuzuko Hosoya
 | Zhihong Yu
 | Zhiyong Wu
 | Zsolt Ero

 | Prev | Up | Next
 | E.24. Release 14.1  | Home |  E.26. Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 14.24 
 14.23 
 14.22 
 14.21 
 14.20 
 14.19 
 14.18 
 14.17 
 14.16 
 14.15 
 14.14 
 14.13 
 14.12 
 14.11 
 14.10 
 14.9 
 14.8 
 14.7 
 14.6 
 14.5 
 14.4 
 14.3 
 14.2 
 14.1 
 14.0

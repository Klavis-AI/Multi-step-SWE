<!-- source: https://www.postgresql.org/docs/release/11.0/ -->

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

##  PostgreSQL 11.0 

 | E.23. Release 11
 | Prev | Up | Appendix E. Release Notes | Home | Next

## E.23. Release 11

E.23.1. Overview
E.23.2. Migration to Version 11
E.23.3. Changes
E.23.4. Acknowledgments
Release date: 2018-10-18

## E.23.1. Overview
Major enhancements in PostgreSQL 11 include:

Improvements to partitioning functionality, including:

Add support for partitioning by a hash key

Add support for PRIMARY KEY, FOREIGN KEY, indexes, and triggers on partitioned tables

Allow creation of a "default" partition for storing data that does not match any of the remaining partitions

UPDATE statements that change a partition key column now cause affected rows to be moved to the appropriate partitions

Improve SELECT performance through enhanced partition elimination strategies during query planning and execution

Improvements to parallelism, including:

CREATE INDEX can now use parallel processing while building a B-tree index

Parallelization is now possible in CREATE TABLE ... AS, CREATE MATERIALIZED VIEW, and certain queries using UNION

Parallelized hash joins and parallelized sequential scans now perform better

SQL stored procedures that support embedded transactions

Optional Just-in-Time (JIT) compilation for some SQL code, speeding evaluation of expressions

Window functions now support all framing options shown in the SQL:2011 standard, including RANGE distance PRECEDING/FOLLOWING, GROUPS mode, and frame exclusion options

Covering indexes can now be created, using the INCLUDE clause of CREATE INDEX

Many other useful performance improvements, including the ability to avoid a table rewrite for ALTER TABLE ... ADD COLUMN with a non-null column default
The above items are explained in more detail in the sections below.

## E.23.2. Migration to Version 11
A dump/restore using pg_dumpall or use of pg_upgrade or logical replication is required for those wishing to migrate data from any previous release. See Section 18.6 for general information on migrating to new major releases.
Version 11 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

Make pg_dump dump the properties of a database, not just its contents (Haribabu Kommi)
Previously, attributes of the database itself, such as database-level GRANT/REVOKE permissions and ALTER DATABASE SET variable settings, were only dumped by pg_dumpall. Now pg_dump --create and pg_restore --create will restore these database properties in addition to the objects within the database. pg_dumpall -g now only dumps role- and tablespace-related attributes. pg_dumpall's complete output (without -g) is unchanged.
pg_dump and pg_restore, without --create, no longer dump/restore database-level comments and security labels; those are now treated as properties of the database.
pg_dumpall's output script will now always create databases with their original locale and encoding, and hence will fail if the locale or encoding name is unknown to the destination system. Previously, CREATE DATABASE would be emitted without these specifications if the database locale and encoding matched the old cluster's defaults.
pg_dumpall --clean now restores the original locale and encoding settings of the postgres and template1 databases, as well as those of user-created databases.

Consider syntactic form when disambiguating function versus column references (Tom Lane)
When x is a table name or composite column, PostgreSQL has traditionally considered the syntactic forms f(x) and x.f to be equivalent, allowing tricks such as writing a function and then using it as though it were a computed-on-demand column. However, if both interpretations are feasible, the column interpretation was always chosen, leading to surprising results if the user intended the function interpretation. Now, if there is ambiguity, the interpretation that matches the syntactic form is chosen.

Fully enforce uniqueness of table and domain constraint names (Tom Lane)
PostgreSQL expects the names of a table's constraints to be distinct, and likewise for the names of a domain's constraints. However, there was not rigid enforcement of this, and previously there were corner cases where duplicate names could be created.

Make power(numeric, numeric) and power(float8, float8) handle NaN inputs according to the POSIX standard (Tom Lane, Dang Minh Huong)
POSIX says that NaN ^ 0 = 1 and 1 ^ NaN = 1, but all other cases with NaN input(s) should return NaN. power(numeric, numeric) just returned NaN in all such cases; now it honors the two exceptions. power(float8, float8) followed the standard if the C library does; but on some old Unix platforms the library doesn't, and there were also problems on some versions of Windows.

Prevent to_number() from consuming characters when the template separator does not match (Oliver Ford)
Specifically, SELECT to_number('1234', '9,999') used to return 134. It will now return 1234. L and TH now only consume characters that are not digits, positive/negative signs, decimal points, or commas.

Fix to_date(), to_number(), and to_timestamp() to skip a character for each template character (Tom Lane)
Previously, they skipped one byte for each byte of template character, resulting in strange behavior if either string contained multibyte characters.

Adjust the handling of backslashes inside double-quotes in template strings for to_char(), to_number(), and to_timestamp().
Such a backslash now escapes the character after it, particularly a double-quote or another backslash.

Correctly handle relative path expressions in xmltable(), xpath(), and other XML-handling functions (Markus Winand)
Per the SQL standard, relative paths start from the document node of the XML input document, not the root node as these functions previously did.

In the extended query protocol, make statement_timeout apply to each Execute message separately, not to all commands before Sync (Tatsuo Ishii, Andres Freund)

Remove the relhaspkey column from system catalog pg_class (Peter Eisentraut)
Applications needing to check for a primary key should consult pg_index.

Replace system catalog pg_proc's proisagg and proiswindow columns with prokind (Peter Eisentraut)
This new column more clearly distinguishes functions, procedures, aggregates, and window functions.

Correct information schema column tables.table_type to return FOREIGN instead of FOREIGN TABLE (Peter Eisentraut)
This new output matches the SQL standard.

Change the ps process display labels for background workers to match the pg_stat_activity.backend_type labels (Peter Eisentraut)

Cause large object permission checks to happen during large object open, lo_open(), not when a read or write is attempted (Tom Lane, Michael Paquier)
If write access is requested and not available, an error will now be thrown even if the large object is never written to.

Prevent non-superusers from reindexing shared catalogs (Michael Paquier, Robert Haas)
Previously, database owners were also allowed to do this, but now it is considered outside the bounds of their privileges.

Remove deprecated adminpack functions pg_file_read(), pg_file_length(), and pg_logfile_rotate() (Stephen Frost)
Equivalent functionality is now present in the core backend. Existing adminpack installs will continue to have access to these functions until they are updated via ALTER EXTENSION ... UPDATE.

Honor the capitalization of double-quoted command options (Daniel Gustafsson)
Previously, option names in certain SQL commands were forcibly lower-cased even if entered with double quotes; thus for example "FillFactor" would be accepted as an index storage option, though properly its name is lower-case. Such cases will now generate an error.

Remove server parameter replacement_sort_tuples (Peter Geoghegan)
Replacement sorts were determined to be no longer useful.

Remove WITH clause in CREATE FUNCTION (Michael Paquier)
PostgreSQL has long supported a more standard-compliant syntax for this capability.

In PL/pgSQL trigger functions, the OLD and NEW variables now read as NULL when not assigned (Tom Lane)
Previously, references to these variables could be parsed but not executed.

## E.23.3. Changes
Below you will find a detailed account of the changes between PostgreSQL 11 and the previous major release.

## E.23.3.1. Server

E.23.3.1.1. Partitioning

Allow the creation of partitions based on hashing a key column (Amul Sul)

Support indexes on partitioned tables (Alvaro Herrera, Amit Langote)
An "index" on a partitioned table is not a physical index across the whole partitioned table, but rather a template for automatically creating similar indexes on each partition of the table.
If the partition key is part of the index's column set, a partitioned index may be declared UNIQUE. It will represent a valid uniqueness constraint across the whole partitioned table, even though each physical index only enforces uniqueness within its own partition.
The new command ALTER INDEX ATTACH PARTITION causes an existing index on a partition to be associated with a matching index template for its partitioned table. This provides flexibility in setting up a new partitioned index for an existing partitioned table.

Allow foreign keys on partitioned tables (Alvaro Herrera)

Allow FOR EACH ROW triggers on partitioned tables (Alvaro Herrera)
Creation of a trigger on a partitioned table automatically creates triggers on all existing and future partitions. This also allows deferred unique constraints on partitioned tables.

Allow partitioned tables to have a default partition (Jeevan Ladhe, Beena Emerson, Ashutosh Bapat, Rahila Syed, Robert Haas)
The default partition will store rows that don't match any of the other defined partitions, and is searched accordingly.

UPDATE statements that change a partition key column now cause affected rows to be moved to the appropriate partitions (Amit Khandekar)

Allow INSERT, UPDATE, and COPY on partitioned tables to properly route rows to foreign partitions (Etsuro Fujita, Amit Langote)
This is supported by postgres_fdw foreign tables. Since the ExecForeignInsert callback function is called for this in a different way than it used to be, foreign data wrappers must be modified to cope with this change.

Allow faster partition elimination during query processing (Amit Langote, David Rowley, Dilip Kumar)
This speeds access to partitioned tables with many partitions.

Allow partition elimination during query execution (David Rowley, Beena Emerson)
Previously, partition elimination only happened at planning time, meaning many joins and prepared queries could not use partition elimination.

In an equality join between partitioned tables, allow matching partitions to be joined directly (Ashutosh Bapat)
This feature is disabled by default but can be enabled by changing enable_partitionwise_join.

Allow aggregate functions on partitioned tables to be evaluated separately for each partition, subsequently merging the results (Jeevan Chalke, Ashutosh Bapat, Robert Haas)
This feature is disabled by default but can be enabled by changing enable_partitionwise_aggregate.

Allow postgres_fdw to push down aggregates to foreign tables that are partitions (Jeevan Chalke)

E.23.3.1.2. Parallel Queries

Allow parallel building of a btree index (Peter Geoghegan, Rushabh Lathia, Heikki Linnakangas)

Allow hash joins to be performed in parallel using a shared hash table (Thomas Munro)

Allow UNION to run each SELECT in parallel if the individual SELECTs cannot be parallelized (Amit Khandekar, Robert Haas, Amul Sul)

Allow partition scans to more efficiently use parallel workers (Amit Khandekar, Robert Haas, Amul Sul)

Allow LIMIT to be passed to parallel workers (Robert Haas, Tom Lane)
This allows workers to reduce returned results and use targeted index scans.

Allow single-evaluation queries, e.g., WHERE clause aggregate queries, and functions in the target list to be parallelized (Amit Kapila, Robert Haas)

Add server parameter parallel_leader_participation to control whether the leader also executes subplans (Thomas Munro)
The default is enabled, meaning the leader will execute subplans.

Allow parallelization of commands CREATE TABLE ... AS, SELECT INTO, and CREATE MATERIALIZED VIEW (Haribabu Kommi)

Improve performance of sequential scans with many parallel workers (David Rowley)

Add reporting of parallel workers' sort activity in EXPLAIN (Robert Haas, Tom Lane)

E.23.3.1.3. Indexes

Allow B-tree indexes to include columns that are not part of the search key or unique constraint, but are available to be read by index-only scans (Anastasia Lubennikova, Alexander Korotkov, Teodor Sigaev)
This is enabled by the new INCLUDE clause of CREATE INDEX. It facilitates building "covering indexes" that optimize specific types of queries. Columns can be included even if their data types don't have B-tree support.

Improve performance of monotonically increasing index additions (Pavan Deolasee, Peter Geoghegan)

Improve performance of hash index scans (Ashutosh Sharma)

Add predicate locking for hash, GiST and GIN indexes (Shubham Barai)
This reduces the likelihood of serialization conflicts in serializable-mode transactions.

E.23.3.1.3.1. SP-Gist

Add prefix-match operator text^@text, which is supported by SP-GiST (Ildus Kurbangaliev)
This is similar to using varLIKE 'word%' with a btree index, but it is more efficient.

Allow polygons to be indexed with SP-GiST (Nikita Glukhov, Alexander Korotkov)

Allow SP-GiST to use lossy representation of leaf keys (Teodor Sigaev, Heikki Linnakangas, Alexander Korotkov, Nikita Glukhov)

E.23.3.1.4. Optimizer

Improve selection of the most common values for statistics (Jeff Janes, Dean Rasheed)
Previously, the most common values (MCVs) were identified based on their frequency compared to all column values. Now, MCVs are chosen based on their frequency compared to the non-MCV values. This improves the robustness of the algorithm for both uniform and non-uniform distributions.

Improve selectivity estimates for >= and <= (Tom Lane)
Previously, such cases used the same selectivity estimates as > and <, respectively, unless the comparison constants are MCVs. This change is particularly helpful for queries involving BETWEEN with small ranges.

Reduce var=var to varIS NOT NULL where equivalent (Tom Lane)
This leads to better selectivity estimates.

Improve optimizer's row count estimates for EXISTS and NOT EXISTS queries (Tom Lane)

Make the optimizer account for evaluation costs and selectivity of HAVING clauses (Tom Lane)

E.23.3.1.5. General Performance

Add Just-in-Time (JIT) compilation of some parts of query plans to improve execution speed (Andres Freund)
This feature requires LLVM to be available. It is not currently enabled by default, even in builds that support it.

Allow bitmap scans to perform index-only scans when possible (Alexander Kuzmenkov)

Update the free space map during VACUUM (Claudio Freire)
This allows free space to be reused more quickly.

Allow VACUUM to avoid unnecessary index scans (Masahiko Sawada, Alexander Korotkov)

Improve performance of committing multiple concurrent transactions (Amit Kapila)

Reduce memory usage for queries using set-returning functions in their target lists (Andres Freund)

Improve the speed of aggregate computations (Andres Freund)

Allow postgres_fdw to push UPDATEs and DELETEs using joins to foreign servers (Etsuro Fujita)
Previously, only non-join UPDATEs and DELETEs were pushed.

Add support for large pages on Windows (Takayuki Tsunakawa, Thomas Munro)
This is controlled by the huge_pages configuration parameter.

E.23.3.1.6. Monitoring

Show memory usage in output from log_statement_stats, log_parser_stats, log_planner_stats, and log_executor_stats (Justin Pryzby, Peter Eisentraut)

Add column pg_stat_activity.backend_type to show the type of a background worker (Peter Eisentraut)
The type is also visible in ps output.

Make log_autovacuum_min_duration log skipped tables that are concurrently being dropped (Nathan Bossart)

E.23.3.1.6.1. Information Schema

Add information_schema columns related to table constraints and triggers (Peter Eisentraut)
Specifically, triggers.action_order, triggers.action_reference_old_table, and triggers.action_reference_new_table are now populated, where before they were always null. Also, table_constraints.enforced now exists but is not yet usefully populated.

E.23.3.1.7. Authentication

Allow the server to specify more complex LDAP specifications in search+bind mode (Thomas Munro)
Specifically, ldapsearchfilter allows pattern matching using combinations of LDAP attributes.

Allow LDAP authentication to use encrypted LDAP (Thomas Munro)
We already supported LDAP over TLS by using ldaptls=1. This new TLSLDAP method for encrypted LDAP is enabled with ldapscheme=ldaps or ldapurl=ldaps://.

Improve logging of LDAP errors (Thomas Munro)

E.23.3.1.8. Permissions

Add default roles that enable file system access (Stephen Frost)
Specifically, the new roles are: pg_read_server_files, pg_write_server_files, and pg_execute_server_program. These roles now also control who can use server-side COPY and the file_fdw extension. Previously, only superusers could use these functions, and that is still the default behavior.

Allow access to file system functions to be controlled by GRANT/REVOKE permissions, rather than superuser checks (Stephen Frost)
Specifically, these functions were modified: pg_ls_dir(), pg_read_file(), pg_read_binary_file(), pg_stat_file().

Use GRANT/REVOKE to control access to lo_import() and lo_export() (Michael Paquier, Tom Lane)
Previously, only superusers were granted access to these functions.
The compile-time option ALLOW_DANGEROUS_LO_FUNCTIONS has been removed.

Use view owner not session owner when preventing non-password access to postgres_fdw tables (Robert Haas)
PostgreSQL only allows superusers to access postgres_fdw tables without passwords, e.g., via peer. Previously, the session owner had to be a superuser to allow such access; now the view owner is checked instead.

Fix invalid locking permission check in SELECT FOR UPDATE on views (Tom Lane)

E.23.3.1.9. Server Configuration

Add server setting ssl_passphrase_command to allow supplying of the passphrase for SSL key files (Peter Eisentraut)
Also add ssl_passphrase_command_supports_reload to specify whether the SSL configuration should be reloaded and ssl_passphrase_command called during a server configuration reload.

Add storage parameter toast_tuple_target to control the minimum tuple length before TOAST storage will be considered (Simon Riggs)
The default TOAST threshold has not been changed.

Allow server options related to memory and file sizes to be specified in units of bytes (Beena Emerson)
The new unit suffix is "B". This is in addition to the existing units "kB", "MB", "GB" and "TB".

E.23.3.1.10. Write-Ahead Log (WAL)

Allow the WAL file size to be set during initdb (Beena Emerson)
Previously, the 16MB default could only be changed at compile time.

Retain WAL data for only a single checkpoint (Simon Riggs)
Previously, WAL was retained for two checkpoints.

Fill the unused portion of force-switched WAL segment files with zeros for improved compressibility (Chapman Flack)

## E.23.3.2. Base Backup and Streaming Replication

Replicate TRUNCATE activity when using logical replication (Simon Riggs, Marco Nenciarini, Peter Eisentraut)

Pass prepared transaction information to logical replication subscribers (Nikhil Sontakke, Stas Kelvich)

Exclude unlogged tables, temporary tables, and pg_internal.init files from streaming base backups (David Steele)
There is no need to copy such files.

Allow checksums of heap pages to be verified during streaming base backup (Michael Banck)

Allow replication slots to be advanced programmatically, rather than be consumed by subscribers (Petr Jelinek)
This allows efficient advancement of replication slots when the contents do not need to be consumed. This is performed by pg_replication_slot_advance().

Add timeline information to the backup_label file (Michael Paquier)
Also add a check that the WAL timeline matches the backup_label file's timeline.

Add host and port connection information to the pg_stat_wal_receiver system view (Haribabu Kommi)

## E.23.3.3. Utility Commands

Allow ALTER TABLE to add a column with a non-null default without doing a table rewrite (Andrew Dunstan, Serge Rielau)
This is enabled when the default value is a constant.

Allow views to be locked by locking the underlying tables (Yugo Nagata)

Allow ALTER INDEX to set statistics-gathering targets for expression indexes (Alexander Korotkov, Adrien Nayrat)
In psql, \d+ now shows the statistics target for indexes.

Allow multiple tables to be specified in one VACUUM or ANALYZE command (Nathan Bossart)
Also, if any table mentioned in VACUUM uses a column list, then the ANALYZE keyword must be supplied; previously, ANALYZE was implied in such cases.

Add parenthesized options syntax to ANALYZE (Nathan Bossart)
This is similar to the syntax supported by VACUUM.

Add CREATE AGGREGATE option to specify the behavior of the aggregate's finalization function (Tom Lane)
This is helpful for allowing user-defined aggregate functions to be optimized and to work as window functions.

## E.23.3.4. Data Types

Allow the creation of arrays of domains (Tom Lane)
This also allows array_agg() to be used on domains.

Support domains over composite types (Tom Lane)
Also allow PL/Perl, PL/Python, and PL/Tcl to handle composite-domain function arguments and results. Also improve PL/Python domain handling.

Add casts from JSONB scalars to numeric and boolean data types (Anastasia Lubennikova)

## E.23.3.5. Functions

Add all window function framing options specified by SQL:2011 (Oliver Ford, Tom Lane)
Specifically, allow RANGE mode to use PRECEDING and FOLLOWING to select rows having grouping values within plus or minus the specified offset. Add GROUPS mode to include plus or minus the number of peer groups. Frame exclusion syntax was also added.

Add SHA-2 family of hash functions (Peter Eisentraut)
Specifically, sha224(), sha256(), sha384(), sha512() were added.

Add support for 64-bit non-cryptographic hash functions (Robert Haas, Amul Sul)

Allow to_char() and to_timestamp() to specify the time zone's offset from UTC in hours and minutes (Nikita Glukhov, Andrew Dunstan)
This is done with format specifications TZH and TZM.

Add text search function websearch_to_tsquery() that supports a query syntax similar to that used by web search engines (Victor Drobny, Dmitry Ivanov)

Add functions json(b)_to_tsvector() to create a text search query for matching JSON/JSONB values (Dmitry Dolgov)

## E.23.3.6. Server-Side Languages

Add SQL-level procedures, which can start and commit their own transactions (Peter Eisentraut)
They are created with the new CREATE PROCEDURE command and invoked via CALL.
The new ALTER/DROP ROUTINE commands allow altering/dropping of all routine-like objects, including procedures, functions, and aggregates.
Also, writing FUNCTION is now preferred over writing PROCEDURE in CREATE OPERATOR and CREATE TRIGGER, because the referenced object must be a function not a procedure. However, the old syntax is still accepted for compatibility.

Add transaction control to PL/pgSQL, PL/Perl, PL/Python, PL/Tcl, and SPI server-side languages (Peter Eisentraut)
Transaction control is only available within top-transaction-level procedures and nested DO and CALL blocks that only contain other DO and CALL blocks.

Add the ability to define PL/pgSQL composite-type variables as not null, constant, or with initial values (Tom Lane)

Allow PL/pgSQL to handle changes to composite types (e.g., record, row) that happen between the first and later function executions in the same session (Tom Lane)
Previously, such circumstances generated errors.

Add extension jsonb_plpython to transform JSONB to/from PL/Python types (Anthony Bykov)

Add extension jsonb_plperl to transform JSONB to/from PL/Perl types (Anthony Bykov)

## E.23.3.7. Client Interfaces

Change libpq to disable compression by default (Peter Eisentraut)
Compression is already disabled in modern OpenSSL versions, so that the libpq setting had no effect with such libraries.

Add DO CONTINUE option to ecpg's WHENEVER statement (Vinayak Pokale)
This generates a C continue statement, causing a return to the top of the contained loop when the specified condition occurs.

Add an ecpg mode to enable Oracle Pro*C-style handling of char arrays.
This mode is enabled with -C.

## E.23.3.8. Client Applications

E.23.3.8.1. psql

Add psql command \gdesc to display the names and types of the columns in a query result (Pavel Stehule)

Add psql variables to report query activity and errors (Fabien Coelho)
Specifically, the new variables are ERROR, SQLSTATE, ROW_COUNT, LAST_ERROR_MESSAGE, and LAST_ERROR_SQLSTATE.

Allow psql to test for the existence of a variable (Fabien Coelho)
Specifically, the syntax :{?variable_name} allows a variable's existence to be tested in an \if statement.

Allow environment variable PSQL_PAGER to control psql's pager (Pavel Stehule)
This allows psql's default pager to be specified as a separate environment variable from the pager for other applications. PAGER is still honored if PSQL_PAGER is not set.

Make psql's \d+ command always show the table's partitioning information (Amit Langote, Ashutosh Bapat)
Previously, partition information would not be displayed for a partitioned table if it had no partitions. Also indicate which partitions are themselves partitioned.

Ensure that psql reports the proper user name when prompting for a password (Tom Lane)
Previously, combinations of -U and a user name embedded in a URI caused incorrect reporting. Also suppress the user name before the password prompt when --password is specified.

Allow quit and exit to exit psql when given with no prior input (Bruce Momjian)
Also print hints about how to exit when quit and exit are used alone on a line while the input buffer is not empty. Add a similar hint for help.

Make psql hint at using control-D when \q is entered alone on a line but ignored (Bruce Momjian)
For example, \q does not exit when supplied in character strings.

Improve tab completion for ALTER INDEX RESET/SET (Masahiko Sawada)

Add infrastructure to allow psql to adapt its tab completion queries based on the server version (Tom Lane)
Previously, tab completion queries could fail against older servers.

E.23.3.8.2. pgbench

Add pgbench expression support for NULLs, booleans, and some functions and operators (Fabien Coelho)

Add \if conditional support to pgbench (Fabien Coelho)

Allow the use of non-ASCII characters in pgbench variable names (Fabien Coelho)

Add pgbench option --init-steps to control the initialization steps performed (Masahiko Sawada)

Add an approximately Zipfian-distributed random generator to pgbench (Alik Khilazhev)

Allow the random seed to be set in pgbench (Fabien Coelho)

Allow pgbench to do exponentiation with pow() and power() (Raul Marin Rodriguez)

Add hashing functions to pgbench (Ildar Musin)

Make pgbench statistics more accurate when using --latency-limit and --rate (Fabien Coelho)

## E.23.3.9. Server Applications

Add an option to pg_basebackup that creates a named replication slot (Michael Banck)
The option --create-slot creates the named replication slot (--slot) when the WAL streaming method (--wal-method=stream) is used.

Allow initdb to set group read access to the data directory (David Steele)
This is accomplished with the new initdb option --allow-group-access. Administrators can also set group permissions on the empty data directory before running initdb. Server variable data_directory_mode allows reading of data directory group permissions.

Add pg_verify_checksums tool to verify database checksums while offline (Magnus Hagander)

Allow pg_resetwal to change the WAL segment size via --wal-segsize (Nathan Bossart)

Add long options to pg_resetwal and pg_controldata (Nathan Bossart, Peter Eisentraut)

Add pg_receivewal option --no-sync to prevent synchronous WAL writes, for testing (Michael Paquier)

Add pg_receivewal option --endpos to specify when WAL receiving should stop (Michael Paquier)

Allow pg_ctl to send the SIGKILL signal to processes (Andres Freund)
This was previously unsupported due to concerns over possible misuse.

Reduce the number of files copied by pg_rewind (Michael Paquier)

Prevent pg_rewind from running as root (Michael Paquier)

E.23.3.9.1. pg_dump, pg_dumpall, pg_restore

Add pg_dumpall option --encoding to control output encoding (Michael Paquier)
pg_dump already had this option.

Add pg_dump option --load-via-partition-root to force loading of data into the partition's root table, rather than the original partition (Rushabh Lathia)
This is useful if the system to be loaded to has different collation definitions or endianness, possibly requiring rows to be stored in different partitions than previously.

Add an option to suppress dumping and restoring database object comments (Robins Tharakan)
The new pg_dump, pg_dumpall, and pg_restore option is --no-comments.

## E.23.3.10. Source Code

Add PGXS support for installing include files (Andrew Gierth)
This supports creating extension modules that depend on other modules. Formerly there was no easy way for the dependent module to find the referenced one's include files. Several existing contrib modules that define data types have been adjusted to install relevant files. Also, PL/Perl and PL/Python now install their include files, to support creation of transform modules for those languages.

Install errcodes.txt to allow extensions to access the list of error codes known to PostgreSQL (Thomas Munro)

Convert documentation to DocBook XML (Peter Eisentraut, Alexander Lakhin, Jurgen Purtz)
The file names still use an sgml extension for compatibility with back branches.

Use stdbool.h to define type bool on platforms where it's suitable, which is most (Peter Eisentraut)
This eliminates a coding hazard for extension modules that need to include stdbool.h.

Overhaul the way that initial system catalog contents are defined (John Naylor)
The initial data is now represented in Perl data structures, making it much easier to manipulate mechanically.

Prevent extensions from creating custom server parameters that take a quoted list of values (Tom Lane)
This cannot be supported at present because knowledge of the parameter's property would be required even before the extension is loaded.

Add ability to use channel binding when using SCRAM authentication (Michael Paquier)
Channel binding is intended to prevent man-in-the-middle attacks, but SCRAM cannot prevent them unless it can be forced to be active. Unfortunately, there is no way to do that in libpq. Support for it is expected in future versions of libpq and in interfaces not built using libpq, e.g., JDBC.

Allow background workers to attach to databases that normally disallow connections (Magnus Hagander)

Add support for hardware CRC calculations on ARMv8 (Yuqi Gu, Heikki Linnakangas, Thomas Munro)

Speed up lookups of built-in functions by OID (Andres Freund)
The previous binary search has been replaced by a lookup array.

Speed up construction of query results (Andres Freund)

Improve speed of access to system caches (Andres Freund)

Add a generational memory allocator which is optimized for serial allocation/deallocation (Tomas Vondra)
This reduces memory usage for logical decoding.

Make the computation of pg_class.reltuples by VACUUM consistent with its computation by ANALYZE (Tomas Vondra)

Update to use perltidy version 20170521 (Tom Lane, Peter Eisentraut)

## E.23.3.11. Additional Modules

Allow extension pg_prewarm to restore the previous shared buffer contents on startup (Mithun Cy, Robert Haas)
This is accomplished by having pg_prewarm store the shared buffers' relation and block number data to disk occasionally during server operation, and at shutdown.

Add pg_trgm function strict_word_similarity() to compute the similarity of whole words (Alexander Korotkov)
The function word_similarity() already existed for this purpose, but it was designed to find similar parts of words, while strict_word_similarity() computes the similarity to whole words.

Allow btree_gin to index bool, bpchar, name and uuid data types (Matheus Oliveira)

Allow cube and seg extensions to perform index-only scans using GiST indexes (Andrey Borodin)

Allow retrieval of negative cube coordinates using the ~> operator (Alexander Korotkov)
This is useful for KNN-GiST searches when looking for coordinates in descending order.

Add Vietnamese letter handling to the unaccent extension (Dang Minh Huong, Michael Paquier)

Enhance amcheck to check that each heap tuple has an index entry (Peter Geoghegan)

Have adminpack use the new default file system access roles (Stephen Frost)
Previously, only superusers could call adminpack functions; now role permissions are checked.

Widen pg_stat_statement's query ID to 64 bits (Robert Haas)
This greatly reduces the chance of query ID hash collisions. The query ID can now potentially display as a negative value.

Remove the contrib/start-scripts/osx scripts since they are no longer recommended (use contrib/start-scripts/macos instead) (Tom Lane)

Remove the chkpass extension (Peter Eisentraut)
This extension is no longer considered to be a usable security tool or example of how to write an extension.

## E.23.4. Acknowledgments
The following individuals (in alphabetical order) have contributed to this release as patch authors, committers, reviewers, testers, or reporters of issues.

 | Abhijit Menon-Sen
 | Adam Bielanski
 | Adam Brightwell
 | Adam Brusselback
 | Aditya Toshniwal
 | Adrian Escoms
 | Adrien Nayrat
 | Akos Vandra
 | Aleksander Alekseev
 | Aleksandr Parfenov
 | Alexander Korotkov
 | Alexander Kukushkin
 | Alexander Kuzmenkov
 | Alexander Lakhin
 | Alexandre Garcia
 | Alexey Bashtanov
 | Alexey Chernyshov
 | Alexey Kryuchkov
 | Alik Khilazhev
 | Alvaro Herrera
 | Amit Kapila
 | Amit Khandekar
 | Amit Langote
 | Amul Sul
 | Anastasia Lubennikova
 | Andreas Joseph Krogh
 | Andreas Karlsson
 | Andreas Seltenreich
 | Andre Hansel
 | Andrei Gorita
 | Andres Freund
 | Andrew Dunstan
 | Andrew Fletcher
 | Andrew Gierth
 | Andrew Grossman
 | Andrew Krasichkov
 | Andrey Borodin
 | Andrey Lizenko
 | Andy Abelisto
 | Anthony Bykov
 | Antoine Scemama
 | Anton Dignos
 | Antonin Houska
 | Arseniy Sharoglazov
 | Arseny Sher
 | Arthur Zakirov
 | Ashutosh Bapat
 | Ashutosh Sharma
 | Ashwin Agrawal
 | Asim Praveen
 | Atsushi Torikoshi
 | Badrul Chowdhury
 | Balazs Szilfai
 | Basil Bourque
 | Beena Emerson
 | Ben Chobot
 | Benjamin Coutu
 | Bernd Helmle
 | Blaz Merela
 | Brad DeJong
 | Brent Dearth
 | Brian Cloutier
 | Bruce Momjian
 | Catalin Iacob
 | Chad Trabant
 | Chapman Flack
 | Christian Duta
 | Christian Ullrich
 | Christoph Berg
 | Christoph Dreis
 | Christophe Courtois
 | Christopher Jones
 | Claudio Freire
 | Clayton Salem
 | Craig Ringer
 | Dagfinn Ilmari Mannsaker
 | Dan Vianello
 | Dan Watson
 | Dang Minh Huong
 | Daniel Gustafsson
 | Daniel Verite
 | Daniel Westermann
 | Daniel Wood
 | Darafei Praliaskouski
 | Dave Cramer
 | Dave Page
 | David Binderman
 | David Carlier
 | David Fetter
 | David G. Johnston
 | David Gould
 | David Hinkle
 | David Pereiro Lagares
 | David Rader
 | David Rowley
 | David Steele
 | Davy Machado
 | Dean Rasheed
 | Dian Fay
 | Dilip Kumar
 | Dmitriy Sarafannikov
 | Dmitry Dolgov
 | Dmitry Ivanov
 | Dmitry Shalashov
 | Don Seiler
 | Doug Doole
 | Doug Rady
 | Edmund Horner
 | Eiji Seki
 | Elvis Pranskevichus
 | Emre Hasegeli
 | Erik Rijkers
 | Erwin Brandstetter
 | Etsuro Fujita
 | Euler Taveira
 | Everaldo Canuto
 | Fabien Coelho
 | Fabrizio de Royes Mello
 | Feike Steenbergen
 | Frits Jalvingh
 | Fujii Masao
 | Gao Zengqi
 | Gianni Ciolli
 | Greg Stark
 | Gunnlaugur Thor Briem
 | Guo Xiang Tan
 | Hadi Moshayedi
 | Hailong Li
 | Haribabu Kommi
 | Heath Lord
 | Heikki Linnakangas
 | Hugo Mercier
 | Igor Korot
 | Igor Neyman
 | Ildar Musin
 | Ildus Kurbangaliev
 | Ioseph Kim
 | Jacob Champion
 | Jaime Casanova
 | Jakob Egger
 | Jean-Pierre Pelletier
 | Jeevan Chalke
 | Jeevan Ladhe
 | Jeff Davis
 | Jeff Janes
 | Jeremy Evans
 | Jeremy Finzel
 | Jeremy Schneider
 | Jesper Pedersen
 | Jim Nasby
 | Jimmy Yih
 | Jing Wang
 | Jobin Augustine
 | Joe Conway
 | John Gorman
 | John Naylor
 | Jon Nelson
 | Jon Wolski
 | Jonathan Allen
 | Jonathan S. Katz
 | Julien Rouhaud
 | Jurgen Purtz
 | Justin Pryzby
 | KaiGai Kohei
 | Kaiting Chen
 | Karl Lehenbauer
 | Keith Fiske
 | Kevin Bloch
 | Kha Nguyen
 | Kim Rose Carlsen
 | Konstantin Knizhnik
 | Kuntal Ghosh
 | Kyle Samson
 | Kyotaro Horiguchi
 | Ltitia Avrot
 | Lars Kanis
 | Laurenz Albe
 | Leonardo Cecchi
 | Liudmila Mantrova
 | Lixian Zou
 | Lloyd Albin
 | Luca Ferrari
 | Lucas Fairchild
 | Lukas Eder
 | Lukas Fittl
 | Magnus Hagander
 | Mai Peng
 | Maksim Milyutin
 | Maksym Boguk
 | Mansur Galiev
 | Marc Dilger
 | Marco Nenciarini
 | Marina Polyakova
 | Mario de Frutos Dieguez
 | Mark Cave-Ayland
 | Mark Dilger
 | Mark Wood
 | Marko Tiikkaja
 | Markus Winand
 | Martin Marques
 | Masahiko Sawada
 | Matheus Oliveira
 | Matthew Stickney
 | Metin Doslu
 | Michael Banck
 | Michael Meskes
 | Michael Paquier
 | Michail Nikolaev
 | Mike Blackwell
 | Minh-Quan Tran
 | Mithun Cy
 | Morgan Owens
 | Nathan Bossart
 | Nathan Wagner
 | Neil Conway
 | Nick Barnes
 | Nicolas Thauvin
 | Nikhil Sontakke
 | Nikita Glukhov
 | Nikolay Shaplov
 | Noah Misch
 | Noriyoshi Shinoda
 | Oleg Bartunov
 | Oleg Samoilov
 | Oliver Ford
 | Pan Bian
 | Pascal Legrand
 | Patrick Hemmer
 | Patrick Krecker
 | Paul Bonaud
 | Paul Guo
 | Paul Ramsey
 | Pavan Deolasee
 | Pavan Maddamsetti
 | Pavel Golub
 | Pavel Stehule
 | Peter Eisentraut
 | Peter Geoghegan
 | Petr Jelinek
 | Petru-Florin Mihancea
 | Phil Florent
 | Philippe Beaudoin
 | Pierre Ducroquet
 | Piotr Stefaniak
 | Prabhat Sahu
 | Pu Qun
 | QL Zhuo
 | Rafia Sabih
 | Rahila Syed
 | Rainer Orth
 | Rajkumar Raghuwanshi
 | Raul Marin Rodriguez
 | Regina Obe
 | Richard Yen
 | Robert Haas
 | Robins Tharakan
 | Rod Taylor
 | Rushabh Lathia
 | Ryan Murphy
 | Sahap Asci
 | Samuel Horwitz
 | Scott Ure
 | Sean Johnston
 | Shao Bret
 | Shay Rojansky
 | Shubham Barai
 | Simon Riggs
 | Simone Gotti
 | Sivasubramanian Ramasubramanian
 | Stas Kelvich
 | Stefan Kaltenbrunner
 | Stephen Froehlich
 | Stephen Frost
 | Steve Singer
 | Steven Winfield
 | Sven Kunze
 | Taiki Kondo
 | Takayuki Tsunakawa
 | Takeshi Ideriha
 | Tatsuo Ishii
 | Tatsuro Yamada
 | Teodor Sigaev
 | Thom Brown
 | Thomas Kellerer
 | Thomas Munro
 | Thomas Reiss
 | Tobias Bussmann
 | Todd A. Cook
 | Tom Kazimiers
 | Tom Lane
 | Tomas Vondra
 | Tomonari Katsumata
 | Torsten Grust
 | Tushar Ahuja
 | Vaishnavi Prabakaran
 | Vasundhar Boddapati
 | Victor Drobny
 | Victor Wagner
 | Victor Yegorov
 | Vik Fearing
 | Vinayak Pokale
 | Vincent Lachenal
 | Vitaliy Garnashevich
 | Vitaly Burovoy
 | Vladimir Baranoff
 | Xin Zhang
 | Yi Wen Wong
 | Yorick Peterse
 | Yugo Nagata
 | Yuqi Gu
 | Yura Sokolov
 | Yves Goergen
 | Zhou Digoal

 | Prev | Up | Next
 | E.22. Release 11.1  | Home |  E.24. Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 11.22 
 11.21 
 11.20 
 11.19 
 11.18 
 11.17 
 11.16 
 11.15 
 11.14 
 11.13 
 11.12 
 11.11 
 11.10 
 11.9 
 11.8 
 11.7 
 11.6 
 11.5 
 11.4 
 11.3 
 11.2 
 11.1 
 11.0

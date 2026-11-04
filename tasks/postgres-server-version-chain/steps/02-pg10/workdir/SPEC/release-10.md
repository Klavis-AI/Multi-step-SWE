<!-- source: https://www.postgresql.org/docs/release/10.0/ -->

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

##  PostgreSQL 10.0 

 | E.24. Release 10
 | Prev | Up | Appendix E. Release Notes | Home | Next

## E.24. Release 10

E.24.1. Overview
E.24.2. Migration to Version 10
E.24.3. Changes
E.24.4. Acknowledgments
Release date: 2017-10-05

## E.24.1. Overview
Major enhancements in PostgreSQL 10 include:

Logical replication using publish/subscribe

Declarative table partitioning

Improved query parallelism

Significant general performance improvements

Stronger password authentication based on SCRAM-SHA-256

Improved monitoring and control
The above items are explained in more detail in the sections below.

## E.24.2. Migration to Version 10
A dump/restore using pg_dumpall or use of pg_upgrade or logical replication is required for those wishing to migrate data from any previous release. See Section 18.6 for general information on migrating to new major releases.
Version 10 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

Hash indexes must be rebuilt after pg_upgrade-ing from any previous major PostgreSQL version (Mithun Cy, Robert Haas, Amit Kapila)
Major hash index improvements necessitated this requirement. pg_upgrade will create a script to assist with this.

Rename write-ahead log directory pg_xlog to pg_wal, and rename transaction status directory pg_clog to pg_xact (Michael Paquier)
Users have occasionally thought that these directories contained only inessential log files, and proceeded to remove write-ahead log files or transaction status files manually, causing irrecoverable data loss. These name changes are intended to discourage such errors in future.

Rename SQL functions, tools, and options that reference "xlog" to "wal" (Robert Haas)
For example, pg_switch_xlog() becomes pg_switch_wal(), pg_receivexlog becomes pg_receivewal, and --xlogdir becomes --waldir. This is for consistency with the change of the pg_xlog directory name; in general, the "xlog" terminology is no longer used in any user-facing places.

Rename WAL-related functions and views to use lsn instead of location (David Rowley)
There was previously an inconsistent mixture of the two terminologies.

Change the implementation of set-returning functions appearing in a query's SELECT list (Andres Freund)
Set-returning functions are now evaluated before evaluation of scalar expressions in the SELECT list, much as though they had been placed in a LATERAL FROM-clause item. This allows saner semantics for cases where multiple set-returning functions are present. If they return different numbers of rows, the shorter results are extended to match the longest result by adding nulls. Previously the results were cycled until they all terminated at the same time, producing a number of rows equal to the least common multiple of the functions' periods. In addition, set-returning functions are now disallowed within CASE and COALESCE constructs. For more information see Section 37.4.8.

Use standard row constructor syntax in UPDATE ... SET (column_list) = row_constructor (Tom Lane)
The row_constructor can now begin with the keyword ROW; previously that had to be omitted. If just one column name appears in the column_list, then the row_constructor now must use the ROW keyword, since otherwise it is not a valid row constructor but just a parenthesized expression. Also, an occurrence of table_name.* within the row_constructor is now expanded into multiple columns, as occurs in other uses of row_constructors.

When ALTER TABLE ... ADD PRIMARY KEY marks columns NOT NULL, that change now propagates to inheritance child tables as well (Michael Paquier)

Prevent statement-level triggers from firing more than once per statement (Tom Lane)
Cases involving writable CTEs updating the same table updated by the containing statement, or by another writable CTE, fired BEFORE STATEMENT or AFTER STATEMENT triggers more than once. Also, if there were statement-level triggers on a table affected by a foreign key enforcement action (such as ON DELETE CASCADE), they could fire more than once per outer SQL statement. This is contrary to the SQL standard, so change it.

Move sequences' metadata fields into a new pg_sequence system catalog (Peter Eisentraut)
A sequence relation now stores only the fields that can be modified by nextval(), that is last_value, log_cnt, and is_called. Other sequence properties, such as the starting value and increment, are kept in a corresponding row of the pg_sequence catalog. ALTER SEQUENCE updates are now fully transactional, implying that the sequence is locked until commit. The nextval() and setval() functions remain nontransactional.
The main incompatibility introduced by this change is that selecting from a sequence relation now returns only the three fields named above. To obtain the sequence's other properties, applications must look into pg_sequence. The new system view pg_sequences can also be used for this purpose; it provides column names that are more compatible with existing code.
Also, sequences created for SERIAL columns now generate positive 32-bit wide values, whereas previous versions generated 64-bit wide values. This has no visible effect if the values are only stored in a column.
The output of psql's \d command for a sequence has been redesigned, too.

Make pg_basebackup stream the WAL needed to restore the backup by default (Magnus Hagander)
This changes pg_basebackup's -X/--wal-method default to stream. An option value none has been added to reproduce the old behavior. The pg_basebackup option -x has been removed (instead, use -X fetch).

Change how logical replication uses pg_hba.conf (Peter Eisentraut)
In previous releases, a logical replication connection required the replication keyword in the database column. As of this release, logical replication matches a normal entry with a database name or keywords such as all. Physical replication continues to use the replication keyword. Since built-in logical replication is new in this release, this change only affects users of third-party logical replication plugins.

Make all pg_ctl actions wait for completion by default (Peter Eisentraut)
Previously some pg_ctl actions didn't wait for completion, and required the use of -w to do so.

Change the default value of the log_directory server parameter from pg_log to log (Andreas Karlsson)

Add configuration option ssl_dh_params_file to specify file name for custom OpenSSL DH parameters (Heikki Linnakangas)
This replaces the hardcoded, undocumented file name dh1024.pem. Note that dh1024.pem is no longer examined by default; you must set this option if you want to use custom DH parameters.

Increase the size of the default DH parameters used for OpenSSL ephemeral DH ciphers to 2048 bits (Heikki Linnakangas)
The size of the compiled-in DH parameters has been increased from 1024 to 2048 bits, making DH key exchange more resistant to brute-force attacks. However, some old SSL implementations, notably some revisions of Java Runtime Environment version 6, will not accept DH parameters longer than 1024 bits, and hence will not be able to connect over SSL. If it's necessary to support such old clients, you can use custom 1024-bit DH parameters instead of the compiled-in defaults. See ssl_dh_params_file.

Remove the ability to store unencrypted passwords on the server (Heikki Linnakangas)
The password_encryption server parameter no longer supports off or plain. The UNENCRYPTED option is no longer supported in CREATE/ALTER USER ... PASSWORD. Similarly, the --unencrypted option has been removed from createuser. Unencrypted passwords migrated from older versions will be stored encrypted in this release. The default setting for password_encryption is still md5.

Add min_parallel_table_scan_size and min_parallel_index_scan_size server parameters to control parallel queries (Amit Kapila, Robert Haas)
These replace min_parallel_relation_size, which was found to be too generic.

Don't downcase unquoted text within shared_preload_libraries and related server parameters (QL Zhuo)
These settings are really lists of file names, but they were previously treated as lists of SQL identifiers, which have different parsing rules.

Remove sql_inheritance server parameter (Robert Haas)
Changing this setting from the default value caused queries referencing parent tables to not include child tables. The SQL standard requires them to be included, however, and this has been the default since PostgreSQL 7.1.

Allow multi-dimensional arrays to be passed into PL/Python functions, and returned as nested Python lists (Alexey Grishchenko, Dave Cramer, Heikki Linnakangas)
This feature requires a backwards-incompatible change to the handling of arrays of composite types in PL/Python. Previously, you could return an array of composite values by writing, e.g., [[col1, col2], [col1, col2]]; but now that is interpreted as a two-dimensional array. Composite types in arrays must now be written as Python tuples, not lists, to resolve the ambiguity; that is, write [(col1, col2), (col1, col2)] instead.

Remove PL/Tcl's "module" auto-loading facility (Tom Lane)
This functionality has been replaced by new server parameters pltcl.start_proc and pltclu.start_proc, which are easier to use and more similar to features available in other PLs.

Remove pg_dump/pg_dumpall support for dumping from pre-8.0 servers (Tom Lane)
Users needing to dump from pre-8.0 servers will need to use dump programs from PostgreSQL 9.6 or earlier. The resulting output should still load successfully into newer servers.

Remove support for floating-point timestamps and intervals (Tom Lane)
This removes configure's --disable-integer-datetimes option. Floating-point timestamps have few advantages and have not been the default since PostgreSQL 8.3.

Remove server support for client/server protocol version 1.0 (Tom Lane)
This protocol hasn't had client support since PostgreSQL 6.3.

Remove contrib/tsearch2 module (Robert Haas)
This module provided compatibility with the version of full text search that shipped in pre-8.3 PostgreSQL releases.

Remove createlang and droplang command-line applications (Peter Eisentraut)
These had been deprecated since PostgreSQL 9.1. Instead, use CREATE EXTENSION and DROP EXTENSION directly.

Remove support for version-0 function calling conventions (Andres Freund)
Extensions providing C-coded functions must now conform to version 1 calling conventions. Version 0 has been deprecated since 2001.

## E.24.3. Changes
Below you will find a detailed account of the changes between PostgreSQL 10 and the previous major release.

## E.24.3.1. Server

E.24.3.1.1. Parallel Queries

Support parallel B-tree index scans (Rahila Syed, Amit Kapila, Robert Haas, Rafia Sabih)
This change allows B-tree index pages to be searched by separate parallel workers.

Support parallel bitmap heap scans (Dilip Kumar)
This allows a single index scan to dispatch parallel workers to process different areas of the heap.

Allow merge joins to be performed in parallel (Dilip Kumar)

Allow non-correlated subqueries to be run in parallel (Amit Kapila)

Improve ability of parallel workers to return pre-sorted data (Rushabh Lathia)

Increase parallel query usage in procedural language functions (Robert Haas, Rafia Sabih)

Add max_parallel_workers server parameter to limit the number of worker processes that can be used for query parallelism (Julien Rouhaud)
This parameter can be set lower than max_worker_processes to reserve worker processes for purposes other than parallel queries.

Enable parallelism by default by changing the default setting of max_parallel_workers_per_gather to 2.

E.24.3.1.2. Indexes

Add write-ahead logging support to hash indexes (Amit Kapila)
This makes hash indexes crash-safe and replicatable. The former warning message about their use is removed.

Improve hash index performance (Amit Kapila, Mithun Cy, Ashutosh Sharma)

Add SP-GiST index support for INET and CIDR data types (Emre Hasegeli)

Add option to allow BRIN index summarization to happen more aggressively (Alvaro Herrera)
A new CREATE INDEX option enables auto-summarization of the previous BRIN page range when a new page range is created.

Add functions to remove and re-add BRIN summarization for BRIN index ranges (Alvaro Herrera)
The new SQL function brin_summarize_range() updates BRIN index summarization for a specified range and brin_desummarize_range() removes it. This is helpful to update summarization of a range that is now smaller due to UPDATEs and DELETEs.

Improve accuracy in determining if a BRIN index scan is beneficial (David Rowley, Emre Hasegeli)

Allow faster GiST inserts and updates by reusing index space more efficiently (Andrey Borodin)

Reduce page locking during vacuuming of GIN indexes (Andrey Borodin)

E.24.3.1.3. Locking

Reduce locking required to change table parameters (Simon Riggs, Fabrizio Mello)
For example, changing a table's effective_io_concurrency setting can now be done with a more lightweight lock.

Allow tuning of predicate lock promotion thresholds (Dagfinn Ilmari Mannsaker)
Lock promotion can now be controlled through two new server parameters, max_pred_locks_per_relation and max_pred_locks_per_page.

E.24.3.1.4. Optimizer

Add multi-column optimizer statistics to compute the correlation ratio and number of distinct values (Tomas Vondra, David Rowley, Alvaro Herrera)
New commands are CREATE STATISTICS, ALTER STATISTICS, and DROP STATISTICS. This feature is helpful in estimating query memory usage and when combining the statistics from individual columns.

Improve performance of queries affected by row-level security restrictions (Tom Lane)
The optimizer now has more knowledge about where it can place RLS filter conditions, allowing better plans to be generated while still enforcing the RLS conditions safely.

E.24.3.1.5. General Performance

Speed up aggregate functions that calculate a running sum using numeric-type arithmetic, including some variants of SUM(), AVG(), and STDDEV() (Heikki Linnakangas)

Improve performance of character encoding conversions by using radix trees (Kyotaro Horiguchi, Heikki Linnakangas)

Reduce expression evaluation overhead during query execution, as well as plan node calling overhead (Andres Freund)
This is particularly helpful for queries that process many rows.

Allow hashed aggregation to be used with grouping sets (Andrew Gierth)

Use uniqueness guarantees to optimize certain join types (David Rowley)

Improve sort performance of the macaddr data type (Brandur Leach)

Reduce statistics tracking overhead in sessions that reference many thousands of relations (Aleksander Alekseev)

E.24.3.1.6. Monitoring

Allow explicit control over EXPLAIN's display of planning and execution time (Ashutosh Bapat)
By default planning and execution time are displayed by EXPLAIN ANALYZE and are not displayed in other cases. The new EXPLAIN option SUMMARY allows explicit control of this.

Add default monitoring roles (Dave Page)
New roles pg_monitor, pg_read_all_settings, pg_read_all_stats, and pg_stat_scan_tables allow simplified permission configuration.

Properly update the statistics collector during REFRESH MATERIALIZED VIEW (Jim Mlodgenski)

E.24.3.1.6.1. Logging

Change the default value of log_line_prefix to include current timestamp (with milliseconds) and the process ID in each line of postmaster log output (Christoph Berg)
The previous default was an empty prefix.

Add functions to return the log and WAL directory contents (Dave Page)
The new functions are pg_ls_logdir() and pg_ls_waldir() and can be executed by non-superusers with the proper permissions.

Add function pg_current_logfile() to read logging collector's current stderr and csvlog output file names (Gilles Darold)

Report the address and port number of each listening socket in the server log during postmaster startup (Tom Lane)
Also, when logging failure to bind a listening socket, include the specific address we attempted to bind to.

Reduce log chatter about the starting and stopping of launcher subprocesses (Tom Lane)
These are now DEBUG1-level messages.

Reduce message verbosity of lower-numbered debug levels controlled by log_min_messages (Robert Haas)
This also changes the verbosity of client_min_messages debug levels.

E.24.3.1.6.2. pg_stat_activity

Add pg_stat_activity reporting of low-level wait states (Michael Paquier, Robert Haas, Rushabh Lathia)
This change enables reporting of numerous low-level wait conditions, including latch waits, file reads/writes/fsyncs, client reads/writes, and synchronous replication.

Show auxiliary processes, background workers, and walsender processes in pg_stat_activity (Kuntal Ghosh, Michael Paquier)
This simplifies monitoring. A new column backend_type identifies the process type.

Allow pg_stat_activity to show the SQL query being executed by parallel workers (Rafia Sabih)

Rename pg_stat_activity.wait_event_type values LWLockTranche and LWLockNamed to LWLock (Robert Haas)
This makes the output more consistent.

E.24.3.1.7. Authentication

Add SCRAM-SHA-256 support for password negotiation and storage (Michael Paquier, Heikki Linnakangas)
This provides better security than the existing md5 negotiation and storage method.

Change the password_encryption server parameter from boolean to enum (Michael Paquier)
This was necessary to support additional password hashing options.

Add view pg_hba_file_rules to display the contents of pg_hba.conf (Haribabu Kommi)
This shows the file contents, not the currently active settings.

Support multiple RADIUS servers (Magnus Hagander)
All the RADIUS related parameters are now plural and support a comma-separated list of servers.

E.24.3.1.8. Server Configuration

Allow SSL configuration to be updated during configuration reload (Andreas Karlsson, Tom Lane)
This allows SSL to be reconfigured without a server restart, by using pg_ctl reload, SELECT pg_reload_conf(), or sending a SIGHUP signal. However, reloading the SSL configuration does not work if the server's SSL key requires a passphrase, as there is no way to re-prompt for the passphrase. The original configuration will apply for the life of the postmaster in that case.

Make the maximum value of bgwriter_lru_maxpages effectively unlimited (Jim Nasby)

E.24.3.1.9. Reliability

After creating or unlinking files, perform an fsync on their parent directory (Michael Paquier)
This reduces the risk of data loss after a power failure.

E.24.3.1.9.1. Write-Ahead Log (WAL)

Prevent unnecessary checkpoints and WAL archiving on otherwise-idle systems (Michael Paquier)

Add wal_consistency_checking server parameter to add details to WAL that can be sanity-checked on the standby (Kuntal Ghosh, Robert Haas)
Any sanity-check failure generates a fatal error on the standby.

Increase the maximum configurable WAL segment size to one gigabyte (Beena Emerson)
A larger WAL segment size allows for fewer archive_command invocations and fewer WAL files to manage.

## E.24.3.2. Replication and Recovery

Add the ability to logically replicate tables to standby servers (Petr Jelinek)
Logical replication allows more flexibility than physical replication does, including replication between different major versions of PostgreSQL and selective replication.

Allow waiting for commit acknowledgment from standby servers irrespective of the order they appear in synchronous_standby_names (Masahiko Sawada)
Previously the server always waited for the active standbys that appeared first in synchronous_standby_names. The new synchronous_standby_names keyword ANY allows waiting for any number of standbys irrespective of their ordering. This is known as quorum commit.

Reduce configuration changes necessary to perform streaming backup and replication (Magnus Hagander, Dang Minh Huong)
Specifically, the defaults were changed for wal_level, max_wal_senders, max_replication_slots, and hot_standby to make them suitable for these usages out-of-the-box.

Enable replication from localhost connections by default in pg_hba.conf (Michael Paquier)
Previously pg_hba.conf's replication connection lines were commented out by default. This is particularly useful for pg_basebackup.

Add columns to pg_stat_replication to report replication delay times (Thomas Munro)
The new columns are write_lag, flush_lag, and replay_lag.

Allow specification of the recovery stopping point by Log Sequence Number (LSN) in recovery.conf (Michael Paquier)
Previously the stopping point could only be selected by timestamp or XID.

Allow users to disable pg_stop_backup()'s waiting for all WAL to be archived (David Steele)
An optional second argument to pg_stop_backup() controls that behavior.

Allow creation of temporary replication slots (Petr Jelinek)
Temporary slots are automatically removed on session exit or error.

Improve performance of hot standby replay with better tracking of Access Exclusive locks (Simon Riggs, David Rowley)

Speed up two-phase commit recovery performance (Stas Kelvich, Nikhil Sontakke, Michael Paquier)

## E.24.3.3. Queries

Add XMLTABLE function that converts XML-formatted data into a row set (Pavel Stehule, Alvaro Herrera)

Fix regular expressions' character class handling for large character codes, particularly Unicode characters above U+7FF (Tom Lane)
Previously, such characters were never recognized as belonging to locale-dependent character classes such as [[:alpha:]].

## E.24.3.4. Utility Commands

Add table partitioning syntax that automatically creates partition constraints and handles routing of tuple insertions and updates (Amit Langote)
The syntax supports range and list partitioning.

Add AFTER trigger transition tables to record changed rows (Kevin Grittner, Thomas Munro)
Transition tables are accessible from triggers written in server-side languages.

Allow restrictive row-level security policies (Stephen Frost)
Previously all security policies were permissive, meaning that any matching policy allowed access. A restrictive policy must match for access to be granted. These policy types can be combined.

When creating a foreign-key constraint, check for REFERENCES permission on only the referenced table (Tom Lane)
Previously REFERENCES permission on the referencing table was also required. This appears to have stemmed from a misreading of the SQL standard. Since creating a foreign key (or any other type of) constraint requires ownership privilege on the constrained table, additionally requiring REFERENCES permission seems rather pointless.

Allow default permissions on schemas (Matheus Oliveira)
This is done using the ALTER DEFAULT PRIVILEGES command.

Add CREATE SEQUENCE AS command to create a sequence matching an integer data type (Peter Eisentraut)
This simplifies the creation of sequences matching the range of base columns.

Allow COPY view FROM source on views with INSTEAD INSERT triggers (Haribabu Kommi)
The triggers are fed the data rows read by COPY.

Allow the specification of a function name without arguments in DDL commands, if it is unique (Peter Eisentraut)
For example, allow DROP FUNCTION on a function name without arguments if there is only one function with that name. This behavior is required by the SQL standard.

Allow multiple functions, operators, and aggregates to be dropped with a single DROP command (Peter Eisentraut)

Support IF NOT EXISTS in CREATE SERVER, CREATE USER MAPPING, and CREATE COLLATION (Anastasia Lubennikova, Peter Eisentraut)

Make VACUUM VERBOSE report the number of skipped frozen pages and oldest xmin (Masahiko Sawada, Simon Riggs)
This information is also included in log_autovacuum_min_duration output.

Improve speed of VACUUM's removal of trailing empty heap pages (Claudio Freire, Alvaro Herrera)

## E.24.3.5. Data Types

Add full text search support for JSON and JSONB (Dmitry Dolgov)
The functions ts_headline() and to_tsvector() can now be used on these data types.

Add support for EUI-64MAC addresses, as a new data type macaddr8 (Haribabu Kommi)
This complements the existing support for EUI-48MAC addresses (type macaddr).

Add identity columns for assigning a numeric value to columns on insert (Peter Eisentraut)
These are similar to SERIAL columns, but are SQL standard compliant.

Allow ENUM values to be renamed (Dagfinn Ilmari Mannsaker)
This uses the syntax ALTER TYPE ... RENAME VALUE.

Properly treat array pseudotypes (anyarray) as arrays in to_json() and to_jsonb() (Andrew Dunstan)
Previously columns declared as anyarray (particularly those in the pg_stats view) were converted to JSON strings rather than arrays.

Add operators for multiplication and division of money values with int8 values (Peter Eisentraut)
Previously such cases would result in converting the int8 values to float8 and then using the money-and-float8 operators. The new behavior avoids possible precision loss. But note that division of money by int8 now truncates the quotient, like other integer-division cases, while the previous behavior would have rounded.

Check for overflow in the money type's input function (Peter Eisentraut)

## E.24.3.6. Functions

Add simplified regexp_match() function (Emre Hasegeli)
This is similar to regexp_matches(), but it only returns results from the first match so it does not need to return a set, making it easier to use for simple cases.

Add a version of jsonb's delete operator that takes an array of keys to delete (Magnus Hagander)

Make json_populate_record() and related functions process JSON arrays and objects recursively (Nikita Glukhov)
With this change, array-type fields in the destination SQL type are properly converted from JSON arrays, and composite-type fields are properly converted from JSON objects. Previously, such cases would fail because the text representation of the JSON value would be fed to array_in() or record_in(), and its syntax would not match what those input functions expect.

Add function txid_current_if_assigned() to return the current transaction ID or NULL if no transaction ID has been assigned (Craig Ringer)
This is different from txid_current(), which always returns a transaction ID, assigning one if necessary. Unlike that function, this function can be run on standby servers.

Add function txid_status() to check if a transaction was committed (Craig Ringer)
This is useful for checking after an abrupt disconnection whether your previous transaction committed and you just didn't receive the acknowledgment.

Allow make_date() to interpret negative years as BC years (Alvaro Herrera)

Make to_timestamp() and to_date() reject out-of-range input fields (Artur Zakirov)
For example, previously to_date('2009-06-40','YYYY-MM-DD') was accepted and returned 2009-07-10. It will now generate an error.

## E.24.3.7. Server-Side Languages

Allow PL/Python's cursor() and execute() functions to be called as methods of their plan-object arguments (Peter Eisentraut)
This allows a more object-oriented programming style.

Allow PL/pgSQL's GET DIAGNOSTICS statement to retrieve values into array elements (Tom Lane)
Previously, a syntactic restriction prevented the target variable from being an array element.

E.24.3.7.1. PL/Tcl

Allow PL/Tcl functions to return composite types and sets (Karl Lehenbauer)

Add a subtransaction command to PL/Tcl (Victor Wagner)
This allows PL/Tcl queries to fail without aborting the entire function.

Add server parameters pltcl.start_proc and pltclu.start_proc, to allow initialization functions to be called on PL/Tcl startup (Tom Lane)

## E.24.3.8. Client Interfaces

Allow specification of multiple host names or addresses in libpq connection strings and URIs (Robert Haas, Heikki Linnakangas)
libpq will connect to the first responsive server in the list.

Allow libpq connection strings and URIs to request a read/write host, that is a master server rather than a standby server (Victor Wagner, Mithun Cy)
This is useful when multiple host names are specified. It is controlled by libpq connection parameter target_session_attrs.

Allow the password file name to be specified as a libpq connection parameter (Julian Markwort)
Previously this could only be specified via an environment variable.

Add function PQencryptPasswordConn() to allow creation of more types of encrypted passwords on the client side (Michael Paquier, Heikki Linnakangas)
Previously only MD5-encrypted passwords could be created using PQencryptPassword(). This new function can also create SCRAM-SHA-256-encrypted passwords.

Change ecpg preprocessor version from 4.12 to 10 (Tom Lane)
Henceforth the ecpg version will match the PostgreSQL distribution version number.

## E.24.3.9. Client Applications

E.24.3.9.1. psql

Add conditional branch support to psql (Corey Huinker)
This feature adds psql meta-commands \if, \elif, \else, and \endif. This is primarily helpful for scripting.

Add psql\gx meta-command to execute (\g) a query in expanded mode (\x) (Christoph Berg)

Expand psql variable references in backtick-executed strings (Tom Lane)
This is particularly useful in the new psql conditional branch commands.

Prevent psql's special variables from being set to invalid values (Daniel Verite, Tom Lane)
Previously, setting one of psql's special variables to an invalid value silently resulted in the default behavior. \set on a special variable now fails if the proposed new value is invalid. As a special exception, \set with an empty or omitted new value, on a boolean-valued special variable, still has the effect of setting the variable to on; but now it actually acquires that value rather than an empty string. \unset on a special variable now explicitly sets the variable to its default value, which is also the value it acquires at startup. In sum, a control variable now always has a displayable value that reflects what psql is actually doing.

Add variables showing server version and psql version (Fabien Coelho)

Improve psql's \d (display relation) and \dD (display domain) commands to show collation, nullable, and default properties in separate columns (Peter Eisentraut)
Previously they were shown in a single "Modifiers" column.

Make the various \d commands handle no-matching-object cases more consistently (Daniel Gustafsson)
They now all print the message about that to stderr, not stdout, and the message wording is more consistent.

Improve psql's tab completion (Jeff Janes, Ian Barwick, Andreas Karlsson, Sehrope Sarkuni, Thomas Munro, Kevin Grittner, Dagfinn Ilmari Mannsaker)

E.24.3.9.2. pgbench

Add pgbench option --log-prefix to control the log file prefix (Masahiko Sawada)

Allow pgbench's meta-commands to span multiple lines (Fabien Coelho)
A meta-command can now be continued onto the next line by writing backslash-return.

Remove restriction on placement of -M option relative to other command line options (Tom Lane)

## E.24.3.10. Server Applications

Add pg_receivewal option -Z/--compress to specify compression (Michael Paquier)

Add pg_recvlogical option --endpos to specify the ending position (Craig Ringer)
This complements the existing --startpos option.

Rename initdb options --noclean and --nosync to be spelled --no-clean and --no-sync (Vik Fearing, Peter Eisentraut)
The old spellings are still supported.

E.24.3.10.1. pg_dump, pg_dumpall, pg_restore

Allow pg_restore to exclude schemas (Michael Banck)
This adds a new -N/--exclude-schema option.

Add --no-blobs option to pg_dump (Guillaume Lelarge)
This suppresses dumping of large objects.

Add pg_dumpall option --no-role-passwords to omit role passwords (Robins Tharakan, Simon Riggs)
This allows use of pg_dumpall by non-superusers; without this option, it fails due to inability to read passwords.

Support using synchronized snapshots when dumping from a standby server (Petr Jelinek)

Issue fsync() on the output files generated by pg_dump and pg_dumpall (Michael Paquier)
This provides more security that the output is safely stored on disk before the program exits. This can be disabled with the new --no-sync option.

E.24.3.10.2. pg_basebackup

Allow pg_basebackup to stream write-ahead log in tar mode (Magnus Hagander)
The WAL will be stored in a separate tar file from the base backup.

Make pg_basebackup use temporary replication slots (Magnus Hagander)
Temporary replication slots will be used by default when pg_basebackup uses WAL streaming with default options.

Be more careful about fsync'ing in all required places in pg_basebackup and pg_receivewal (Michael Paquier)

Add pg_basebackup option --no-sync to disable fsync (Michael Paquier)

Improve pg_basebackup's handling of which directories to skip (David Steele)

E.24.3.10.3. pg_ctl

Add wait option for pg_ctl's promote operation (Peter Eisentraut)

Add long options for pg_ctl wait (--wait) and no-wait (--no-wait) (Vik Fearing)

Add long option for pg_ctl server options (--options) (Peter Eisentraut)

Make pg_ctl start --wait detect server-ready by watching postmaster.pid, not by attempting connections (Tom Lane)
The postmaster has been changed to report its ready-for-connections status in postmaster.pid, and pg_ctl now examines that file to detect whether startup is complete. This is more efficient and reliable than the old method, and it eliminates postmaster log entries about rejected connection attempts during startup.

Reduce pg_ctl's reaction time when waiting for postmaster start/stop (Tom Lane)
pg_ctl now probes ten times per second when waiting for a postmaster state change, rather than once per second.

Ensure that pg_ctl exits with nonzero status if an operation being waited for does not complete within the timeout (Peter Eisentraut)
The start and promote operations now return exit status 1, not 0, in such cases. The stop operation has always done that.

## E.24.3.11. Source Code

Change to two-part release version numbering (Peter Eisentraut, Tom Lane)
Release numbers will now have two parts (e.g., 10.1) rather than three (e.g., 9.6.3). Major versions will now increase just the first number, and minor releases will increase just the second number. Release branches will be referred to by single numbers (e.g., 10 rather than 9.6). This change is intended to reduce user confusion about what is a major or minor release of PostgreSQL.

Improve behavior of pgindent (Piotr Stefaniak, Tom Lane)
We have switched to a new version of pg_bsd_indent based on recent improvements made by the FreeBSD project. This fixes numerous small bugs that led to odd C code formatting decisions. Most notably, lines within parentheses (such as in a multi-line function call) are now uniformly indented to match the opening paren, even if that would result in code extending past the right margin.

Allow the ICU library to optionally be used for collation support (Peter Eisentraut)
The ICU library has versioning that allows detection of collation changes between versions. It is enabled via configure option --with-icu. The default still uses the operating system's native collation library.

Automatically mark all PG_FUNCTION_INFO_V1 functions as DLLEXPORT-ed on Windows (Laurenz Albe)
If third-party code is using extern function declarations, they should also add DLLEXPORT markers to those declarations.

Remove SPI functions SPI_push(), SPI_pop(), SPI_push_conditional(), SPI_pop_conditional(), and SPI_restore_connection() as unnecessary (Tom Lane)
Their functionality now happens automatically. There are now no-op macros by these names so that external modules don't need to be updated immediately, but eventually such calls should be removed.
A side effect of this change is that SPI_palloc() and allied functions now require an active SPI connection; they do not degenerate to simple palloc() if there is none. That previous behavior was not very useful and posed risks of unexpected memory leaks.

Allow shared memory to be dynamically allocated (Thomas Munro, Robert Haas)

Add slab-like memory allocator for efficient fixed-size allocations (Tomas Vondra)

Use POSIX semaphores rather than SysV semaphores on Linux and FreeBSD (Tom Lane)
This avoids platform-specific limits on SysV semaphore usage.

Improve support for 64-bit atomics (Andres Freund)

Enable 64-bit atomic operations on ARM64 (Roman Shaposhnik)

Switch to using clock_gettime(), if available, for duration measurements (Tom Lane)
gettimeofday() is still used if clock_gettime() is not available.

Add more robust random number generators to be used for cryptographically secure uses (Magnus Hagander, Michael Paquier, Heikki Linnakangas)
If no strong random number generator can be found, configure will fail unless the --disable-strong-random option is used. However, with this option, pgcrypto functions requiring a strong random number generator will be disabled.

Allow WaitLatchOrSocket() to wait for socket connection on Windows (Andres Freund)

tupconvert.c functions no longer convert tuples just to embed a different composite-type OID in them (Ashutosh Bapat, Tom Lane)
The majority of callers don't care about the composite-type OID; but if the result tuple is to be used as a composite Datum, steps should be taken to make sure the correct OID is inserted in it.

Remove SCO and Unixware ports (Tom Lane)

Overhaul documentation build process (Alexander Lakhin)

Use XSLT to build the PostgreSQL documentation (Peter Eisentraut)
Previously Jade, DSSSL, and JadeTex were used.

Build HTML documentation using XSLT stylesheets by default (Peter Eisentraut)

## E.24.3.12. Additional Modules

Allow file_fdw to read from program output as well as files (Corey Huinker, Adam Gomaa)

In postgres_fdw, push aggregate functions to the remote server, when possible (Jeevan Chalke, Ashutosh Bapat)
This reduces the amount of data that must be passed from the remote server, and offloads aggregate computation from the requesting server.

In postgres_fdw, push joins to the remote server in more cases (David Rowley, Ashutosh Bapat, Etsuro Fujita)

Properly support OID columns in postgres_fdw tables (Etsuro Fujita)
Previously OID columns always returned zeros.

Allow btree_gist and btree_gin to index enum types (Andrew Dunstan)
This allows enums to be used in exclusion constraints.

Add indexing support to btree_gist for the UUID data type (Paul Jungwirth)

Add amcheck which can check the validity of B-tree indexes (Peter Geoghegan)

Show ignored constants as $N rather than ? in pg_stat_statements (Lukas Fittl)

Improve cube's handling of zero-dimensional cubes (Tom Lane)
This also improves handling of infinite and NaN values.

Allow pg_buffercache to run with fewer locks (Ivan Kartyshov)
This makes it less disruptive when run on production systems.

Add pgstattuple function pgstathashindex() to view hash index statistics (Ashutosh Sharma)

Use GRANT permissions to control pgstattuple function usage (Stephen Frost)
This allows DBAs to allow non-superusers to run these functions.

Reduce locking when pgstattuple examines hash indexes (Amit Kapila)

Add pageinspect function page_checksum() to show a page's checksum (Tomas Vondra)

Add pageinspect function bt_page_items() to print page items from a page image (Tomas Vondra)

Add hash index support to pageinspect (Jesper Pedersen, Ashutosh Sharma)

## E.24.4. Acknowledgments
The following individuals (in alphabetical order) have contributed to this release as patch authors, committers, reviewers, testers, or reporters of issues.

 | Adam Brightwell
 | Adam Brusselback
 | Adam Gomaa
 | Adam Sah
 | Adrian Klaver
 | Aidan Van Dyk
 | Aleksander Alekseev
 | Alexander Korotkov
 | Alexander Lakhin
 | Alexander Sosna
 | Alexey Bashtanov
 | Alexey Grishchenko
 | Alexey Isayko
 | Alvaro Hernandez Tortosa
 | Alvaro Herrera
 | Amit Kapila
 | Amit Khandekar
 | Amit Langote
 | Amul Sul
 | Anastasia Lubennikova
 | Andreas Joseph Krogh
 | Andreas Karlsson
 | Andreas Scherbaum
 | Andreas Seltenreich
 | Andres Freund
 | Andrew Dunstan
 | Andrew Gierth
 | Andrew Wheelwright
 | Andrey Borodin
 | Andrey Lizenko
 | Andy Abelisto
 | Antonin Houska
 | Ants Aasma
 | Arjen Nienhuis
 | Arseny Sher
 | Artur Zakirov
 | Ashutosh Bapat
 | Ashutosh Sharma
 | Ashwin Agrawal
 | Atsushi Torikoshi
 | Ayumi Ishii
 | Basil Bourque
 | Beena Emerson
 | Ben de Graaff
 | Benedikt Grundmann
 | Bernd Helmle
 | Brad DeJong
 | Brandur Leach
 | Breen Hagan
 | Bruce Momjian
 | Bruno Wolff III
 | Catalin Iacob
 | Chapman Flack
 | Chen Huajun
 | Choi Doo-Won
 | Chris Bandy
 | Chris Richards
 | Chris Ruprecht
 | Christian Ullrich
 | Christoph Berg
 | Chuanting Wang
 | Claudio Freire
 | Clinton Adams
 | Const Zhang
 | Constantin Pan
 | Corey Huinker
 | Craig Ringer
 | Cynthia Shang
 | Dagfinn Ilmari Mannsaker
 | Daisuke Higuchi
 | Damian Quiroga
 | Dan Wood
 | Dang Minh Huong
 | Daniel Gustafsson
 | Daniel Verite
 | Daniel Westermann
 | Daniele Varrazzo
 | Danylo Hlynskyi
 | Darko Prelec
 | Dave Cramer
 | Dave Page
 | David Christensen
 | David Fetter
 | David Johnston
 | David Rader
 | David Rowley
 | David Steele
 | Dean Rasheed
 | Denis Smirnov
 | Denish Patel
 | Dennis Bjorklund
 | Devrim Gunduz
 | Dilip Kumar
 | Dilyan Palauzov
 | Dima Pavlov
 | Dimitry Ivanov
 | Dmitriy Sarafannikov
 | Dmitry Dolgov
 | Dmitry Fedin
 | Don Morrison
 | Egor Rogov
 | Eiji Seki
 | Emil Iggland
 | Emre Hasegeli
 | Enrique Meneses
 | Erik Nordstrom
 | Erik Rijkers
 | Erwin Brandstetter
 | Etsuro Fujita
 | Eugen Konkov
 | Eugene Kazakov
 | Euler Taveira
 | Fabien Coelho
 | Fabrizio de Royes Mello
 | Feike Steenbergen
 | Felix Gerzaguet
 | Filip Jirsak
 | Fujii Masao
 | Gabriele Bartolini
 | Gabrielle Roth
 | Gao Zengqi
 | Gerdan Santos
 | Gianni Ciolli
 | Gilles Darold
 | Giuseppe Broccolo
 | Graham Dutton
 | Greg Atkins
 | Greg Burek
 | Grigory Smolkin
 | Guillaume Lelarge
 | Hans Buschmann
 | Haribabu Kommi
 | Heikki Linnakangas
 | Henry Boehlert
 | Huan Ruan
 | Ian Barwick
 | Igor Korot
 | Ildus Kurbangaliev
 | Ivan Kartyshov
 | Jaime Casanova
 | Jakob Egger
 | James Parks
 | Jarred Ward
 | Jason Li
 | Jason O'Donnell
 | Jason Petersen
 | Jeevan Chalke
 | Jeevan Ladhe
 | Jeff Dafoe
 | Jeff Davis
 | Jeff Janes
 | Jelte Fennema
 | Jeremy Finzel
 | Jeremy Schneider
 | Jeroen van der Ham
 | Jesper Pedersen
 | Jim Mlodgenski
 | Jim Nasby
 | Jinyu Zhang
 | Joe Conway
 | Joel Jacobson
 | John Harvey
 | Jon Nelson
 | Jordan Gigov
 | Josh Berkus
 | Josh Soref
 | Julian Markwort
 | Julien Rouhaud
 | Junseok Yang
 | Justin Muise
 | Justin Pryzby
 | Kacper Zuk
 | KaiGai Kohei
 | Karen Huddleston
 | Karl Lehenbauer
 | Karl O. Pinc
 | Keith Fiske
 | Kevin Grittner
 | Kim Rose Carlsen
 | Konstantin Evteev
 | Konstantin Knizhnik
 | Kuntal Ghosh
 | Kurt Kartaltepe
 | Kyle Conroy
 | Kyotaro Horiguchi
 | Laurenz Albe
 | Leonardo Cecchi
 | Ludovic Vaugeois-Pepin
 | Lukas Fittl
 | Magnus Hagander
 | Maksim Milyutin
 | Maksym Sobolyev
 | Marc Rassbach
 | Marc-Olaf Jaschke
 | Marcos Castedo
 | Marek Cvoren
 | Mark Dilger
 | Mark Kirkwood
 | Mark Pether
 | Marko Tiikkaja
 | Markus Winand
 | Marllius Ribeiro
 | Marti Raudsepp
 | Martin Marques
 | Masahiko Sawada
 | Matheus Oliveira
 | Mathieu Fenniak
 | Merlin Moncure
 | Michael Banck
 | Michael Day
 | Michael Meskes
 | Michael Overmeyer
 | Michael Paquier
 | Mike Palmiotto
 | Milos Urbanek
 | Mithun Cy
 | Moshe Jacobson
 | Murtuza Zabuawala
 | Naoki Okano
 | Nathan Bossart
 | Nathan Wagner
 | Neha Khatri
 | Neha Sharma
 | Neil Anderson
 | Nicolas Baccelli
 | Nicolas Guini
 | Nicolas Thauvin
 | Nikhil Sontakke
 | Nikita Glukhov
 | Nikolaus Thiel
 | Nikolay Nikitin
 | Nikolay Shaplov
 | Noah Misch
 | Noriyoshi Shinoda
 | Olaf Gawenda
 | Oleg Bartunov
 | Oskari Saarenmaa
 | Otar Shavadze
 | Paresh More
 | Paul Jungwirth
 | Paul Ramsey
 | Pavan Deolasee
 | Pavel Golub
 | Pavel Hanak
 | Pavel Raiskup
 | Pavel Stehule
 | Peng Sun
 | Peter Eisentraut
 | Peter Geoghegan
 | Petr Jelinek
 | Philippe Beaudoin
 | Pierre-Emmanuel Andre
 | Piotr Stefaniak
 | Prabhat Sahu
 | QL Zhuo
 | Radek Slupik
 | Rafa de la Torre
 | Rafia Sabih
 | Ragnar Ouchterlony
 | Rahila Syed
 | Rajkumar Raghuwanshi
 | Regina Obe
 | Richard Pistole
 | Robert Haas
 | Robins Tharakan
 | Rod Taylor
 | Roman Shaposhnik
 | Rushabh Lathia
 | Ryan Murphy
 | Sandeep Thakkar
 | Scott Milliken
 | Sean Farrell
 | Sebastian Luque
 | Sehrope Sarkuni
 | Sergey Burladyan
 | Sergey Koposov
 | Shay Rojansky
 | Shinichi Matsuda
 | Sho Kato
 | Simon Riggs
 | Simone Gotti
 | Spencer Thomason
 | Stas Kelvich
 | Stepan Pesternikov
 | Stephen Frost
 | Steve Randall
 | Steve Singer
 | Steven Fackler
 | Steven Winfield
 | Suraj Kharage
 | Sveinn Sveinsson
 | Sven R. Kunze
 | Tahir Fakhroutdinov
 | Taiki Kondo
 | Takayuki Tsunakawa
 | Takeshi Ideriha
 | Tatsuo Ishii
 | Tatsuro Yamada
 | Teodor Sigaev
 | Thom Brown
 | Thomas Kellerer
 | Thomas Munro
 | Tim Goodaire
 | Tobias Bussmann
 | Tom Dunstan
 | Tom Lane
 | Tom van Tilburg
 | Tomas Vondra
 | Tomonari Katsumata
 | Tushar Ahuja
 | Vaishnavi Prabakaran
 | Venkata Balaji Nagothi
 | Vicky Vergara
 | Victor Wagner
 | Vik Fearing
 | Vinayak Pokale
 | Viren Negi
 | Vitaly Burovoy
 | Vladimir Kunshchikov
 | Vladimir Rusinov
 | Yi Wen Wong
 | Yugo Nagata
 | Zhen Ming Yang
 | Zhou Digoal

 | Prev | Up | Next
 | E.23. Release 10.1  | Home |  E.25. Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 10.23 
 10.22 
 10.21 
 10.20 
 10.19 
 10.18 
 10.17 
 10.16 
 10.15 
 10.14 
 10.13 
 10.12 
 10.11 
 10.10 
 10.9 
 10.8 
 10.7 
 10.6 
 10.5 
 10.4 
 10.3 
 10.2 
 10.1 
 10.0

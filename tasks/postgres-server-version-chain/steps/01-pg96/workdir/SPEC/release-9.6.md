<!-- source: https://www.postgresql.org/docs/release/9.6.0/ -->

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

##  PostgreSQL 9.6.0 

 | PostgreSQL 9.6.24 Documentation
 | Prev | Up | Appendix E. Release Notes | Next

## E.25. Release 9.6

Release date: 2016-09-29

## E.25.1. Overview
Major enhancements in PostgreSQL 9.6 include:

Parallel execution of sequential scans, joins and aggregates

Avoid scanning pages unnecessarily during vacuum freeze operations

Synchronous replication now allows multiple standby servers for increased reliability

Full-text search can now search for phrases (multiple adjacent words)

postgres_fdw now supports remote joins, sorts, UPDATEs, and DELETEs

Substantial performance improvements, especially in the area of scalability on multi-CPU-socket servers
The above items are explained in more detail in the sections below.

## E.25.2. Migration to Version 9.6
A dump/restore using pg_dumpall, or use of pg_upgrade, is required for those wishing to migrate data from any previous release.
Version 9.6 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

Improve the pg_stat_activity view's information about what a process is waiting for (Amit Kapila, Ildus Kurbangaliev)
Historically a process has only been shown as waiting if it was waiting for a heavyweight lock. Now waits for lightweight locks and buffer pins are also shown in pg_stat_activity. Also, the type of lock being waited for is now visible. These changes replace the waiting column with wait_event_type and wait_event.

In to_char(), do not count a minus sign (when needed) as part of the field width for time-related fields (Bruce Momjian)
For example, to_char('-4 years'::interval, 'YY') now returns -04, rather than -4.

Make extract() behave more reasonably with infinite inputs (Vitaly Burovoy)
Historically the extract() function just returned zero given an infinite timestamp, regardless of the given field name. Make it return infinity or -infinity as appropriate when the requested field is one that is monotonically increasing (e.g, year, epoch), or NULL when it is not (e.g., day, hour). Also, throw the expected error for bad field names.

Remove PL/pgSQL's "feature" that suppressed the innermost line of CONTEXT for messages emitted by RAISE commands (Pavel Stehule)
This ancient backwards-compatibility hack was agreed to have outlived its usefulness.

Fix the default text search parser to allow leading digits in email and host tokens (Artur Zakirov)
In most cases this will result in few changes in the parsing of text. But if you have data where such addresses occur frequently, it may be worth rebuilding dependent tsvector columns and indexes so that addresses of this form will be found properly by text searches.

Extend contrib/unaccent's standard unaccent.rules file to handle all diacritics known to Unicode, and to expand ligatures correctly (Thomas Munro, Leonard Benedetti)
The previous version neglected to convert some less-common letters with diacritic marks. Also, ligatures are now expanded into separate letters. Installations that use this rules file may wish to rebuild tsvector columns and indexes that depend on the result.

Remove the long-deprecated CREATEUSER/NOCREATEUSER options from CREATE ROLE and allied commands (Tom Lane)
CREATEUSER actually meant SUPERUSER, for ancient backwards-compatibility reasons. This has been a constant source of confusion for people who (reasonably) expect it to mean CREATEROLE. It has been deprecated for ten years now, so fix the problem by removing it.

Treat role names beginning with pg_ as reserved (Stephen Frost)
User creation of such role names is now disallowed. This prevents conflicts with built-in roles created by initdb.

Change a column name in the information_schema.routines view from result_cast_character_set_name to result_cast_char_set_name (Clement Prevost)
The SQL:2011 standard specifies the longer name, but that appears to be a mistake, because adjacent column names use the shorter style, as do other information_schema views.

psql's -c option no longer implies --no-psqlrc (Pavel Stehule, Catalin Iacob)
Write --no-psqlrc (or its abbreviation -X) explicitly to obtain the old behavior. Scripts so modified will still work with old versions of psql.

Improve pg_restore's -t option to match all types of relations, not only plain tables (Craig Ringer)

Change the display format used for NextXID in pg_controldata and related places (Joe Conway, Bruce Momjian)
Display epoch-and-transaction-ID values in the format number:number. The previous format number/number was confusingly similar to that used for LSNs.

Update extension functions to be marked parallel-safe where appropriate (Andreas Karlsson)
Many of the standard extensions have been updated to allow their functions to be executed within parallel query worker processes. These changes will not take effect in databases pg_upgrade'd from prior versions unless you apply ALTER EXTENSION UPDATE to each such extension (in each database of a cluster).

## E.25.3. Changes
Below you will find a detailed account of the changes between PostgreSQL 9.6 and the previous major release.

## E.25.3.1. Server

## E.25.3.1.1. Parallel Queries

Parallel queries (Robert Haas, Amit Kapila, David Rowley, many others)
With 9.6, PostgreSQL introduces initial support for parallel execution of large queries. Only strictly read-only queries where the driving table is accessed via a sequential scan can be parallelized. Hash joins and nested loops can be performed in parallel, as can aggregation (for supported aggregates). Much remains to be done, but this is already a useful set of features.
Parallel query execution is not (yet) enabled by default. To allow it, set the new configuration parameter max_parallel_workers_per_gather to a value larger than zero. Additional control over use of parallelism is available through other new configuration parameters force_parallel_mode, parallel_setup_cost, parallel_tuple_cost, and min_parallel_relation_size.

Provide infrastructure for marking the parallel-safety status of functions (Robert Haas, Amit Kapila)

## E.25.3.1.2. Indexes

Allow GIN index builds to make effective use of maintenance_work_mem settings larger than 1 GB (Robert Abraham, Teodor Sigaev)

Add pages deleted from a GIN index's pending list to the free space map immediately (Jeff Janes, Teodor Sigaev)
This reduces bloat if the table is not vacuumed often.

Add gin_clean_pending_list() function to allow manual invocation of pending-list cleanup for a GIN index (Jeff Janes)
Formerly, such cleanup happened only as a byproduct of vacuuming or analyzing the parent table.

Improve handling of dead index tuples in GiST indexes (Anastasia Lubennikova)
Dead index tuples are now marked as such when an index scan notices that the corresponding heap tuple is dead. When inserting tuples, marked-dead tuples will be removed if needed to make space on the page.

Add an SP-GiST operator class for type box (Alexander Lebedev)

## E.25.3.1.3. Sorting

Improve sorting performance by using quicksort, not replacement selection sort, when performing external sort steps (Peter Geoghegan)
The new approach makes better use of the CPU cache for typical cache sizes and data volumes. Where necessary, the behavior can be adjusted via the new configuration parameter replacement_sort_tuples.

Speed up text sorts where the same string occurs multiple times (Peter Geoghegan)

Speed up sorting of uuid, bytea, and char(n) fields by using "abbreviated" keys (Peter Geoghegan)
Support for abbreviated keys has also been added to the non-default operator classes text_pattern_ops, varchar_pattern_ops, and bpchar_pattern_ops. Processing of ordered-set aggregates can also now exploit abbreviated keys.

Speed up CREATE INDEX CONCURRENTLY by treating TIDs as 64-bit integers during sorting (Peter Geoghegan)

## E.25.3.1.4. Locking

Reduce contention for the ProcArrayLock (Amit Kapila, Robert Haas)

Improve performance by moving buffer content locks into the buffer descriptors (Andres Freund, Simon Riggs)

Replace shared-buffer header spinlocks with atomic operations to improve scalability (Alexander Korotkov, Andres Freund)

Use atomic operations, rather than a spinlock, to protect an LWLock's wait queue (Andres Freund)

Partition the shared hash table freelist to reduce contention on multi-CPU-socket servers (Aleksander Alekseev)

Reduce interlocking on standby servers during the replay of btree index vacuuming operations (Simon Riggs)
This change avoids substantial replication delays that sometimes occurred while replaying such operations.

## E.25.3.1.5. Optimizer Statistics

Improve ANALYZE's estimates for columns with many nulls (Tomas Vondra, Alex Shulgin)
Previously ANALYZE tended to underestimate the number of non-NULL distinct values in a column with many NULLs, and was also inaccurate in computing the most-common values.

Improve planner's estimate of the number of distinct values in a query result (Tomas Vondra)

Use foreign key relationships to infer selectivity for join predicates (Tomas Vondra, David Rowley)
If a table t has a foreign key restriction, say (a,b) REFERENCES r (x,y), then a WHERE condition such as t.a = r.x AND t.b = r.y cannot select more than one r row per t row. The planner formerly considered these AND conditions to be independent and would often drastically misestimate selectivity as a result. Now it compares the WHERE conditions to applicable foreign key constraints and produces better estimates.

## E.25.3.1.6. VACUUM

Avoid re-vacuuming pages containing only frozen tuples (Masahiko Sawada, Robert Haas, Andres Freund)
Formerly, anti-wraparound vacuum had to visit every page of a table, even pages where there was nothing to do. Now, pages containing only already-frozen tuples are identified in the table's visibility map, and can be skipped by vacuum even when doing transaction wraparound prevention. This should greatly reduce the cost of maintaining large tables containing mostly-unchanging data.
If necessary, vacuum can be forced to process all-frozen pages using the new DISABLE_PAGE_SKIPPING option. Normally this should never be needed, but it might help in recovering from visibility-map corruption.

Avoid useless heap-truncation attempts during VACUUM (Jeff Janes, Tom Lane)
This change avoids taking an exclusive table lock in some cases where no truncation is possible. The main benefit comes from avoiding unnecessary query cancellations on standby servers.

## E.25.3.1.7. General Performance

Allow old MVCC snapshots to be invalidated after a configurable timeout (Kevin Grittner)
Normally, deleted tuples cannot be physically removed by vacuuming until the last transaction that could "see" them is gone. A transaction that stays open for a long time can thus cause considerable table bloat because space cannot be recycled. This feature allows setting a time-based limit, via the new configuration parameter old_snapshot_threshold, on how long an MVCC snapshot is guaranteed to be valid. After that, dead tuples are candidates for removal. A transaction using an outdated snapshot will get an error if it attempts to read a page that potentially could have contained such data.

Ignore GROUP BY columns that are functionally dependent on other columns (David Rowley)
If a GROUP BY clause includes all columns of a non-deferred primary key, as well as other columns of the same table, those other columns are redundant and can be dropped from the grouping. This saves computation in many common cases.

Allow use of an index-only scan on a partial index when the index's WHERE clause references columns that are not indexed (Tomas Vondra, Kyotaro Horiguchi)
For example, an index defined by CREATE INDEX tidx_partial ON t(b) WHERE a > 0 can now be used for an index-only scan by a query that specifies WHERE a > 0 and does not otherwise use a. Previously this was disallowed because a is not listed as an index column.

Perform checkpoint writes in sorted order (Fabien Coelho, Andres Freund)
Previously, checkpoints wrote out dirty pages in whatever order they happen to appear in shared buffers, which usually is nearly random. That performs poorly, especially on rotating media. This change causes checkpoint-driven writes to be done in order by file and block number, and to be balanced across tablespaces.

Where feasible, trigger kernel writeback after a configurable number of writes, to prevent accumulation of dirty data in kernel disk buffers (Fabien Coelho, Andres Freund)
PostgreSQL writes data to the kernel's disk cache, from where it will be flushed to physical storage in due time. Many operating systems are not smart about managing this and allow large amounts of dirty data to accumulate before deciding to flush it all at once, causing long delays for new I/O requests until the flushing finishes. This change attempts to alleviate this problem by explicitly requesting data flushes after a configurable interval.
On Linux, sync_file_range() is used for this purpose, and the feature is on by default on Linux because that function has few downsides. This flushing capability is also available on other platforms if they have msync() or posix_fadvise(), but those interfaces have some undesirable side-effects so the feature is disabled by default on non-Linux platforms.
The new configuration parameters backend_flush_after, bgwriter_flush_after, checkpoint_flush_after, and wal_writer_flush_after control this behavior.

Improve aggregate-function performance by sharing calculations across multiple aggregates if they have the same arguments and transition functions (David Rowley)
For example, SELECT AVG(x), VARIANCE(x) FROM tab can use a single per-row computation for both aggregates.

Speed up visibility tests for recently-created tuples by checking the current transaction's snapshot, not pg_clog, to decide if the source transaction should be considered committed (Jeff Janes, Tom Lane)

Allow tuple hint bits to be set sooner than before (Andres Freund)

Improve performance of short-lived prepared transactions (Stas Kelvich, Simon Riggs, Pavan Deolasee)
Two-phase commit information is now written only to WAL during PREPARE TRANSACTION, and will be read back from WAL during COMMIT PREPARED if that happens soon thereafter. A separate state file is created only if the pending transaction does not get committed or aborted by the time of the next checkpoint.

Improve performance of memory context destruction (Jan Wieck)

Improve performance of resource owners with many tracked objects (Aleksander Alekseev)

Improve speed of the output functions for timestamp, time, and date data types (David Rowley, Andres Freund)

Avoid some unnecessary cancellations of hot-standby queries during replay of actions that take AccessExclusive locks (Jeff Janes)

Extend relations multiple blocks at a time when there is contention for the relation's extension lock (Dilip Kumar)
This improves scalability by decreasing contention.

Increase the number of clog buffers for better scalability (Amit Kapila, Andres Freund)

Speed up expression evaluation in PL/pgSQL by keeping ParamListInfo entries for simple variables valid at all times (Tom Lane)

Avoid reducing the SO_SNDBUF setting below its default on recent Windows versions (Chen Huajun)

Disable update_process_title by default on Windows (Takayuki Tsunakawa)
The overhead of updating the process title is much larger on Windows than most other platforms, and it is also less useful to do it since most Windows users do not have tools that can display process titles.

## E.25.3.1.8. Monitoring

Add pg_stat_progress_vacuum system view to provide progress reporting for VACUUM operations (Amit Langote, Robert Haas, Vinayak Pokale, Rahila Syed)

Add pg_control_system(), pg_control_checkpoint(), pg_control_recovery(), and pg_control_init() functions to expose fields of pg_control to SQL (Joe Conway, Michael Paquier)

Add pg_config system view (Joe Conway)
This view exposes the same information available from the pg_config command-line utility, namely assorted compile-time configuration information for PostgreSQL.

Add a confirmed_flush_lsn column to the pg_replication_slots system view (Marko Tiikkaja)

Add pg_stat_wal_receiver system view to provide information about the state of a hot-standby server's WAL receiver process (Michael Paquier)

Add pg_blocking_pids() function to reliably identify which sessions block which others (Tom Lane)
This function returns an array of the process IDs of any sessions that are blocking the session with the given process ID. Historically users have obtained such information using a self-join on the pg_locks view. However, it is unreasonably tedious to do it that way with any modicum of correctness, and the addition of parallel queries has made the old approach entirely impractical, since locks might be held or awaited by child worker processes rather than the session's main process.

Add function pg_current_xlog_flush_location() to expose the current transaction log flush location (Tomas Vondra)

Add function pg_notification_queue_usage() to report how full the NOTIFY queue is (Brendan Jurd)

Limit the verbosity of memory context statistics dumps (Tom Lane)
The memory usage dump that is output to the postmaster log during an out-of-memory failure now summarizes statistics when there are a large number of memory contexts, rather than possibly generating a very large report. There is also a "grand total" summary line now.

## E.25.3.1.9. Authentication

Add a BSD authentication method to allow use of the BSD Authentication service for PostgreSQL client authentication (Marisa Emerson)
BSD Authentication is currently only available on OpenBSD.

When using PAM authentication, provide the client IP address or host name to PAM modules via the PAM_RHOST item (Grzegorz Sampolski)

Provide detail in the postmaster log for more types of password authentication failure (Tom Lane)
All ordinarily-reachable password authentication failure cases should now provide specific DETAIL fields in the log.

Support RADIUS passwords up to 128 characters long (Marko Tiikkaja)

Add new SSPI authentication parameters compat_realm and upn_username to control whether NetBIOS or Kerberos realm names and user names are used during SSPI authentication (Christian Ullrich)

## E.25.3.1.10. Server Configuration

Allow sessions to be terminated automatically if they are in idle-in-transaction state for too long (Vik Fearing)
This behavior is controlled by the new configuration parameter idle_in_transaction_session_timeout. It can be useful to prevent forgotten transactions from holding locks or preventing vacuum cleanup for too long.

Raise the maximum allowed value of checkpoint_timeout to 24 hours (Simon Riggs)

Allow effective_io_concurrency to be set per-tablespace to support cases where different tablespaces have different I/O characteristics (Julien Rouhaud)

Add log_line_prefix option %n to print the current time in Unix epoch form, with milliseconds (Tomas Vondra, Jeff Davis)

Add syslog_sequence_numbers and syslog_split_messages configuration parameters to provide more control over the message format when logging to syslog (Peter Eisentraut)

Merge the archive and hot_standby values of the wal_level configuration parameter into a single new value replica (Peter Eisentraut)
Making a distinction between these settings is no longer useful, and merging them is a step towards a planned future simplification of replication setup. The old names are still accepted but are converted to replica internally.

Add configure option --with-systemd to enable calling sd_notify() at server start and stop (Peter Eisentraut)
This allows the use of systemd service units of type notify, which greatly simplifies the management of PostgreSQL under systemd.

Allow the server's SSL key file to have group read access if it is owned by root (Christoph Berg)
Formerly, we insisted the key file be owned by the user running the PostgreSQL server, but that is inconvenient on some systems (such as Debian) that are configured to manage certificates centrally. Therefore, allow the case where the key file is owned by root and has group read access. It is up to the operating system administrator to ensure that the group does not include any untrusted users.

## E.25.3.1.11. Reliability

Force backends to exit if the postmaster dies (Rajeev Rastogi, Robert Haas)
Under normal circumstances the postmaster should always outlive its child processes. If for some reason the postmaster dies, force backend sessions to exit with an error. Formerly, existing backends would continue to run until their clients disconnect, but that is unsafe and inefficient. It also prevents a new postmaster from being started until the last old backend has exited. Backends will detect postmaster death when waiting for client I/O, so the exit will not be instantaneous, but it should happen no later than the end of the current query.

Check for serializability conflicts before reporting constraint-violation failures (Thomas Munro)
When using serializable transaction isolation, it is desirable that any error due to concurrent transactions should manifest as a serialization failure, thereby cueing the application that a retry might succeed. Unfortunately, this does not reliably happen for duplicate-key failures caused by concurrent insertions. This change ensures that such an error will be reported as a serialization error if the application explicitly checked for the presence of a conflicting key (and did not find it) earlier in the transaction.

Ensure that invalidation messages are recorded in WAL even when issued by a transaction that has no XID assigned (Andres Freund)
This fixes some corner cases in which transactions on standby servers failed to notice changes, such as new indexes.

Prevent multiple processes from trying to clean a GIN index's pending list concurrently (Teodor Sigaev, Jeff Janes)
This had been intentionally allowed, but it causes race conditions that can result in vacuum missing index entries it needs to delete.

## E.25.3.2. Replication and Recovery

Allow synchronous replication to support multiple simultaneous synchronous standby servers, not just one (Masahiko Sawada, Beena Emerson, Michael Paquier, Fujii Masao, Kyotaro Horiguchi)
The number of standby servers that must acknowledge a commit before it is considered complete is now configurable as part of the synchronous_standby_names parameter.

Add new setting remote_apply for configuration parameter synchronous_commit (Thomas Munro)
In this mode, the master waits for the transaction to be applied on the standby server, not just written to disk. That means that you can count on a transaction started on the standby to see all commits previously acknowledged by the master.

Add a feature to the replication protocol, and a corresponding option to pg_create_physical_replication_slot(), to allow reserving WAL immediately when creating a replication slot (Gurjeet Singh, Michael Paquier)
This allows the creation of a replication slot to guarantee that all the WAL needed for a base backup will be available.

Add a --slot option to pg_basebackup (Peter Eisentraut)
This lets pg_basebackup use a replication slot defined for WAL streaming. After the base backup completes, selecting the same slot for regular streaming replication allows seamless startup of the new standby server.

Extend pg_start_backup() and pg_stop_backup() to support non-exclusive backups (Magnus Hagander)

## E.25.3.3. Queries

Allow functions that return sets of tuples to return simple NULLs (Andrew Gierth, Tom Lane)
In the context of SELECT FROM function(...), a function that returned a set of composite values was previously not allowed to return a plain NULL value as part of the set. Now that is allowed and interpreted as a row of NULLs. This avoids corner-case errors with, for example, unnesting an array of composite values.

Fully support array subscripts and field selections in the target column list of an INSERT with multiple VALUES rows (Tom Lane)
Previously, such cases failed if the same target column was mentioned more than once, e.g., INSERT INTO tab (x[1], x[2]) VALUES (...).

When appropriate, postpone evaluation of SELECT output expressions until after an ORDER BY sort (Konstantin Knizhnik)
This change ensures that volatile or expensive functions in the output list are executed in the order suggested by ORDER BY, and that they are not evaluated more times than required when there is a LIMIT clause. Previously, these properties held if the ordering was performed by an index scan or pre-merge-join sort, but not if it was performed by a top-level sort.

Widen counters recording the number of tuples processed to 64 bits (Andreas Scherbaum)
This change allows command tags, e.g., SELECT, to correctly report tuple counts larger than 4 billion. This also applies to PL/pgSQL's GET DIAGNOSTICS ... ROW_COUNT command.

Avoid doing encoding conversions by converting through the MULE_INTERNAL encoding (Tom Lane)
Previously, many conversions for Cyrillic and Central European single-byte encodings were done by converting to a related MULE_INTERNAL coding scheme and then to the destination encoding. Aside from being inefficient, this meant that when the conversion encountered an untranslatable character, the error message would confusingly complain about failure to convert to or from MULE_INTERNAL, rather than the user-visible encoding.

Consider performing joins of foreign tables remotely only when the tables will be accessed under the same role ID (Shigeru Hanada, Ashutosh Bapat, Etsuro Fujita)
Previously, the foreign join pushdown infrastructure left the question of security entirely up to individual foreign data wrappers, but that made it too easy for an FDW to inadvertently create subtle security holes. So, make it the core code's job to determine which role ID will access each table, and do not attempt join pushdown unless the role is the same for all relevant relations.

## E.25.3.4. Utility Commands

Allow COPY to copy the output of an INSERT/UPDATE/DELETE ... RETURNING query (Marko Tiikkaja)
Previously, an intermediate CTE had to be written to get this result.

Introduce ALTER object DEPENDS ON EXTENSION (Abhijit Menon-Sen)
This command allows a database object to be marked as depending on an extension, so that it will be dropped automatically if the extension is dropped (without needing CASCADE). However, the object is not part of the extension, and thus will be dumped separately by pg_dump.

Make ALTER object SET SCHEMA do nothing when the object is already in the requested schema, rather than throwing an error as it historically has for most object types (Marti Raudsepp)

Add options to ALTER OPERATOR to allow changing the selectivity functions associated with an existing operator (Yury Zhuravlev)

Add an IF NOT EXISTS option to ALTER TABLE ADD COLUMN (Fabrizio de Royes Mello)

Reduce the lock strength needed by ALTER TABLE when setting fillfactor and autovacuum-related relation options (Fabrizio de Royes Mello, Simon Riggs)

Introduce CREATE ACCESS METHOD to allow extensions to create index access methods (Alexander Korotkov, Petr Jelinek)

Add a CASCADE option to CREATE EXTENSION to automatically create any extensions the requested one depends on (Petr Jelinek)

Make CREATE TABLE ... LIKE include an OID column if any source table has one (Bruce Momjian)

If a CHECK constraint is declared NOT VALID in a table creation command, automatically mark it as valid (Amit Langote, Amul Sul)
This is safe because the table has no existing rows. This matches the longstanding behavior of FOREIGN KEY constraints.

Fix DROP OPERATOR to clear pg_operator.oprcom and pg_operator.oprnegate links to the dropped operator (Roma Sokolov)
Formerly such links were left as-is, which could pose a problem in the somewhat unlikely event that the dropped operator's OID was reused for another operator.

Do not show the same subplan twice in EXPLAIN output (Tom Lane)
In certain cases, typically involving SubPlan nodes in index conditions, EXPLAIN would print data for the same subplan twice.

Disallow creation of indexes on system columns, except for OID columns (David Rowley)
Such indexes were never considered supported, and would very possibly misbehave since the system might change the system-column fields of a tuple without updating indexes. However, previously there were no error checks to prevent them from being created.

## E.25.3.5. Permissions Management

Use the privilege system to manage access to sensitive functions (Stephen Frost)
Formerly, many security-sensitive functions contained hard-wired checks that would throw an error if they were called by a non-superuser. This forced the use of superuser roles for some relatively pedestrian tasks. The hard-wired error checks are now gone in favor of making initdb revoke the default public EXECUTE privilege on these functions. This allows installations to choose to grant usage of such functions to trusted roles that do not need all superuser privileges.

Create some built-in roles that can be used to grant access to what were previously superuser-only functions (Stephen Frost)
Currently the only such role is pg_signal_backend, but more are expected to be added in future.

## E.25.3.6. Data Types

Improve full-text search to support searching for phrases, that is, lexemes appearing adjacent to each other in a specific order, or with a specified distance between them (Teodor Sigaev, Oleg Bartunov, Dmitry Ivanov)
A phrase-search query can be specified in tsquery input using the new operators <-> and <N>. The former means that the lexemes before and after it must appear adjacent to each other in that order. The latter means they must be exactly N lexemes apart.

Allow omitting one or both boundaries in an array slice specifier, e.g., array_col[3:] (Yury Zhuravlev)
Omitted boundaries are taken as the upper or lower limit of the corresponding array subscript. This allows simpler specification for many common use-cases.

Be more careful about out-of-range dates and timestamps (Vitaly Burovoy)
This change prevents unexpected out-of-range errors for timestamp with time zone values very close to the implementation limits. Previously, the "same" value might be accepted or not depending on the timezone setting, meaning that a dump and reload could fail on a value that had been accepted when presented. Now the limits are enforced according to the equivalent UTC time, not local time, so as to be independent of timezone.
Also, PostgreSQL is now more careful to detect overflow in operations that compute new date or timestamp values, such as date+integer.

For geometric data types, make sure infinity and NaN component values are treated consistently during input and output (Tom Lane)
Such values will now always print the same as they would in a simple float8 column, and be accepted the same way on input. Previously the behavior was platform-dependent.

Upgrade the ispell dictionary type to handle modern Hunspell files and support more languages (Artur Zakirov)

Implement look-behind constraints in regular expressions (Tom Lane)
A look-behind constraint is like a lookahead constraint in that it consumes no text; but it checks for existence (or nonexistence) of a match ending at the current point in the string, rather than one starting at the current point. Similar features exist in many other regular-expression engines.

In regular expressions, if an apparent three-digit octal escape \nnn would exceed 377 (255 decimal), assume it is a two-digit octal escape instead (Tom Lane)
This makes the behavior match current Tcl releases.

Add transaction ID operators xid<>xid and xid<>int4, for consistency with the corresponding equality operators (Michael Paquier)

## E.25.3.7. Functions

Add jsonb_insert() function to insert a new element into a jsonb array, or a not-previously-existing key into a jsonb object (Dmitry Dolgov)

Improve the accuracy of the ln(), log(), exp(), and pow() functions for type numeric (Dean Rasheed)

Add a scale(numeric) function to extract the display scale of a numeric value (Marko Tiikkaja)

Add trigonometric functions that work in degrees (Dean Rasheed)
For example, sind() measures its argument in degrees, whereas sin() measures in radians. These functions go to some lengths to deliver exact results for values where an exact result can be expected, for instance sind(30) = 0.5.

Ensure that trigonometric functions handle infinity and NaN inputs per the POSIX standard (Dean Rasheed)
The POSIX standard says that these functions should return NaN for NaN input, and should throw an error for out-of-range inputs including infinity. Previously our behavior varied across platforms.

Make to_timestamp(float8) convert float infinity to timestamp infinity (Vitaly Burovoy)
Formerly it just failed on an infinite input.

Add new functions for tsvector data (Stas Kelvich)
The new functions are ts_delete(), ts_filter(), unnest(), tsvector_to_array(), array_to_tsvector(), and a variant of setweight() that sets the weight only for specified lexeme(s).

Allow ts_stat() and tsvector_update_trigger() to operate on values that are of types binary-compatible with the expected argument type, not just exactly that type; for example allow citext where text is expected (Teodor Sigaev)

Add variadic functions num_nulls() and num_nonnulls() that count the number of their arguments that are null or non-null (Marko Tiikkaja)
An example usage is CHECK(num_nonnulls(a,b,c) = 1) which asserts that exactly one of a,b,c is not NULL. These functions can also be used to count the number of null or nonnull elements in an array.

Add function parse_ident() to split a qualified, possibly quoted SQL identifier into its parts (Pavel Stehule)

In to_number(), interpret a V format code as dividing by 10 to the power of the number of digits following V (Bruce Momjian)
This makes it operate in an inverse fashion to to_char().

Make the to_reg*() functions accept type text not cstring (Petr Korobeinikov)
This avoids the need to write an explicit cast in most cases where the argument is not a simple literal constant.

Add pg_size_bytes() function to convert human-readable size strings to numbers (Pavel Stehule, Vitaly Burovoy, Dean Rasheed)
This function converts strings like those produced by pg_size_pretty() into bytes. An example usage is SELECT oid::regclass FROM pg_class WHERE pg_total_relation_size(oid) > pg_size_bytes('10 GB').

In pg_size_pretty(), format negative numbers similarly to positive ones (Adrian Vondendriesch)
Previously, negative numbers were never abbreviated, just printed in bytes.

Add an optional missing_ok argument to the current_setting() function (David Christensen)
This allows avoiding an error for an unrecognized parameter name, instead returning a NULL.

Change various catalog-inspection functions to return NULL for invalid input (Michael Paquier)
pg_get_viewdef() now returns NULL if given an invalid view OID, and several similar functions likewise return NULL for bad input. Previously, such cases usually led to "cache lookup failed" errors, which are not meant to occur in user-facing cases.

Fix pg_replication_origin_xact_reset() to not have any arguments (Fujii Masao)
The documentation said that it has no arguments, and the C code did not expect any arguments, but the entry in pg_proc mistakenly specified two arguments.

## E.25.3.8. Server-Side Languages

In PL/pgSQL, detect mismatched CONTINUE and EXIT statements while compiling a function, rather than at execution time (Jim Nasby)

Extend PL/Python's error-reporting and message-reporting functions to allow specifying additional message fields besides the primary error message (Pavel Stehule)

Allow PL/Python functions to call themselves recursively via SPI, and fix the behavior when multiple set-returning PL/Python functions are called within one query (Alexey Grishchenko, Tom Lane)

Fix session-lifespan memory leaks in PL/Python (Heikki Linnakangas, Haribabu Kommi, Tom Lane)

Modernize PL/Tcl to use Tcl's "object"APIs instead of simple strings (Jim Nasby, Karl Lehenbauer)
This can improve performance substantially in some cases. Note that PL/Tcl now requires Tcl 8.4 or later.

In PL/Tcl, make database-reported errors return additional information in Tcl's errorCode global variable (Jim Nasby, Tom Lane)
This feature follows the Tcl convention for returning auxiliary data about an error.

Fix PL/Tcl to perform encoding conversion between the database encoding and UTF-8, which is what Tcl expects (Tom Lane)
Previously, strings were passed through without conversion, leading to misbehavior with non-ASCII characters when the database encoding was not UTF-8.

## E.25.3.9. Client Interfaces

Add a nonlocalized version of the severity field in error and notice messages (Tom Lane)
This change allows client code to determine severity of an error or notice without having to worry about localized variants of the severity strings.

Introduce a feature in libpq whereby the CONTEXT field of messages can be suppressed, either always or only for non-error messages (Pavel Stehule)
The default behavior of PQerrorMessage() is now to print CONTEXT only for errors. The new function PQsetErrorContextVisibility() can be used to adjust this.

Add support in libpq for regenerating an error message with a different verbosity level (Alex Shulgin)
This is done with the new function PQresultVerboseErrorMessage(). This supports psql's new \errverbose feature, and may be useful for other clients as well.

Improve libpq's PQhost() function to return useful data for default Unix-socket connections (Tom Lane)
Previously it would return NULL if no explicit host specification had been given; now it returns the default socket directory path.

Fix ecpg's lexer to handle line breaks within comments starting on preprocessor directive lines (Michael Meskes)

## E.25.3.10. Client Applications

Add a --strict-names option to pg_dump and pg_restore (Pavel Stehule)
This option causes the program to complain if there is no match for a -t or -n option, rather than silently doing nothing.

In pg_dump, dump locally-made changes of privilege assignments for system objects (Stephen Frost)
While it has always been possible for a superuser to change the privilege assignments for built-in or extension-created objects, such changes were formerly lost in a dump and reload. Now, pg_dump recognizes and dumps such changes. (This works only when dumping from a 9.6 or later server, however.)

Allow pg_dump to dump non-extension-owned objects that are within an extension-owned schema (Martin Marques)
Previously such objects were ignored because they were mistakenly assumed to belong to the extension owning their schema.

In pg_dump output, include the table name in object tags for object types that are only uniquely named per-table (for example, triggers) (Peter Eisentraut)

## E.25.3.10.1.psql

Support multiple -c and -f command-line options (Pavel Stehule, Catalin Iacob)
The specified operations are carried out in the order in which the options are given, and then psql terminates.

Add a \crosstabview command that prints the results of a query in a cross-tabulated display (Daniel Verite)
In the crosstab display, data values from one query result column are placed in a grid whose column and row headers come from other query result columns.

Add an \errverbose command that shows the last server error at full verbosity (Alex Shulgin)
This is useful after getting an unexpected error -- you no longer need to adjust the VERBOSITY variable and recreate the failure in order to see error fields that are not shown by default.

Add \ev and \sv commands for editing and showing view definitions (Petr Korobeinikov)
These are parallel to the existing \ef and \sf commands for functions.

Add a \gexec command that executes a query and re-submits the result(s) as new queries (Corey Huinker)

Allow \pset C string to set the table title, for consistency with \C string (Bruce Momjian)

In \pset expanded auto mode, do not use expanded format for query results with only one column (Andreas Karlsson, Robert Haas)

Improve the headers output by the \watch command (Michael Paquier, Tom Lane)
Include the \pset title string if one has been set, and shorten the prefabricated part of the header to be timestamp (every Ns). Also, the timestamp format now obeys psql's locale environment.

Improve tab-completion logic to consider the entire input query, not only the current line (Tom Lane)
Previously, breaking a command into multiple lines defeated any tab completion rules that needed to see words on earlier lines.

Numerous minor improvements in tab-completion behavior (Peter Eisentraut, Vik Fearing, Kevin Grittner, Kyotaro Horiguchi, Jeff Janes, Andreas Karlsson, Fujii Masao, Thomas Munro, Masahiko Sawada, Pavel Stehule)

Add a PROMPT option %p to insert the process ID of the connected backend (Julien Rouhaud)

Introduce a feature whereby the CONTEXT field of messages can be suppressed, either always or only for non-error messages (Pavel Stehule)
Printing CONTEXT only for errors is now the default behavior. This can be changed by setting the special variable SHOW_CONTEXT.

Make \df+ show function access privileges and parallel-safety attributes (Michael Paquier)

## E.25.3.10.2.pgbench

SQL commands in pgbench scripts are now ended by semicolons, not newlines (Kyotaro Horiguchi, Tom Lane)
This change allows SQL commands in scripts to span multiple lines. Existing custom scripts will need to be modified to add a semicolon at the end of each line that does not have one already. (Doing so does not break the script for use with older versions of pgbench.)

Support floating-point arithmetic, as well as some built-in functions, in expressions in backslash commands (Fabien Coelho)

Replace \setrandom with built-in functions (Fabien Coelho)
The new built-in functions include random(), random_exponential(), and random_gaussian(), which perform the same work as \setrandom, but are easier to use since they can be embedded in larger expressions. Since these additions have made \setrandom obsolete, remove it.

Allow invocation of multiple copies of the built-in scripts, not only custom scripts (Fabien Coelho)
This is done with the new -b switch, which works similarly to -f for custom scripts.

Allow changing the selection probabilities (weights) for scripts (Fabien Coelho)
When multiple scripts are specified, each pgbench transaction randomly chooses one to execute. Formerly this was always done with uniform probability, but now different selection probabilities can be specified for different scripts.

Collect statistics for each script in a multi-script run (Fabien Coelho)
This feature adds an intermediate level of detail to existing global and per-command statistics printouts.

Add a --progress-timestamp option to report progress with Unix epoch timestamps, instead of time since the run started (Fabien Coelho)

Allow the number of client connections (-c) to not be an exact multiple of the number of threads (-j) (Fabien Coelho)

When the -T option is used, stop promptly at the end of the specified time (Fabien Coelho)
Previously, specifying a low transaction rate could cause pgbench to wait significantly longer than specified.

## E.25.3.11. Server Applications

Improve error reporting during initdb's post-bootstrap phase (Tom Lane)
Previously, an error here led to reporting the entire input file as the "failing query"; now just the current query is reported. To get the desired behavior, queries in initdb's input files must be separated by blank lines.

Speed up initdb by using just one standalone-backend session for all the post-bootstrap steps (Tom Lane)

Improve pg_rewind so that it can work when the target timeline changes (Alexander Korotkov)
This allows, for example, rewinding a promoted standby back to some state of the old master's timeline.

## E.25.3.12. Source Code

Remove obsolete heap_formtuple/heap_modifytuple/heap_deformtuple functions (Peter Geoghegan)

Add macros to make AllocSetContextCreate() calls simpler and safer (Tom Lane)
Writing out the individual sizing parameters for a memory context is now deprecated in favor of using one of the new macros ALLOCSET_DEFAULT_SIZES, ALLOCSET_SMALL_SIZES, or ALLOCSET_START_SMALL_SIZES. Existing code continues to work, however.

Unconditionally use static inline functions in header files (Andres Freund)
This may result in warnings and/or wasted code space with very old compilers, but the notational improvement seems worth it.

Improve TAP testing infrastructure (Michael Paquier, Craig Ringer, Alvaro Herrera, Stephen Frost)
Notably, it is now possible to test recovery scenarios using this infrastructure.

Make trace_lwlocks identify individual locks by name (Robert Haas)

Improve psql's tab-completion code infrastructure (Thomas Munro, Michael Paquier)
Tab-completion rules are now considerably easier to write, and more compact.

Nail the pg_shseclabel system catalog into cache, so that it is available for access during connection authentication (Adam Brightwell)
The core code does not use this catalog for authentication, but extensions might wish to consult it.

Restructure index access method API to hide most of it at the C level (Alexander Korotkov, Andrew Gierth)
This change modernizes the index AM API to look more like the designs we have adopted for foreign data wrappers and tablesample handlers. This simplifies the C code and makes it much more practical to define index access methods in installable extensions. A consequence is that most of the columns of the pg_am system catalog have disappeared. New inspection functions have been added to allow SQL queries to determine index AM properties that used to be discoverable from pg_am.

Add pg_init_privs system catalog to hold original privileges of initdb-created and extension-created objects (Stephen Frost)
This infrastructure allows pg_dump to dump changes that an installation may have made in privileges attached to system objects. Formerly, such changes would be lost in a dump and reload, but now they are preserved.

Change the way that extensions allocate custom LWLocks (Amit Kapila, Robert Haas)
The RequestAddinLWLocks() function is removed, and replaced by RequestNamedLWLockTranche(). This allows better identification of custom LWLocks, and is less error-prone.

Improve the isolation tester to allow multiple sessions to wait concurrently, allowing testing of deadlock scenarios (Robert Haas)

Introduce extensible node types (KaiGai Kohei)
This change allows FDWs or custom scan providers to store data in a plan tree in a more convenient format than was previously possible.

Make the planner deal with post-scan/join query steps by generating and comparing Paths, replacing a lot of ad-hoc logic (Tom Lane)
This change provides only marginal user-visible improvements today, but it enables future work on a lot of upper-planner improvements that were impractical to tackle using the old code structure.

Support partial aggregation (David Rowley, Simon Riggs)
This change allows the computation of an aggregate function to be split into separate parts, for example so that parallel worker processes can cooperate on computing an aggregate. In future it might allow aggregation across local and remote data to occur partially on the remote end.

Add a generic command progress reporting facility (Vinayak Pokale, Rahila Syed, Amit Langote, Robert Haas)

Separate out psql's flex lexer to make it usable by other client programs (Tom Lane, Kyotaro Horiguchi)
This eliminates code duplication for programs that need to be able to parse SQL commands well enough to identify command boundaries. Doing that in full generality is more painful than one could wish, and up to now only psql has really gotten it right among our supported client programs.
A new source-code subdirectory src/fe_utils/ has been created to hold this and other code that is shared across our client programs. Formerly such sharing was accomplished by symbolic linking or copying source files at build time, which was ugly and required duplicate compilation.

Introduce WaitEventSetAPI to allow efficient waiting for event sets that usually do not change from one wait to the next (Andres Freund, Amit Kapila)

Add a generic interface for writing WAL records (Alexander Korotkov, Petr Jelinek, Markus Nullmeier)
This change allows extensions to write WAL records for changes to pages using a standard layout. The problem of needing to replay WAL without access to the extension is solved by having generic replay code. This allows extensions to implement, for example, index access methods and have WAL support for them.

Support generic WAL messages for logical decoding (Petr Jelinek, Andres Freund)
This feature allows extensions to insert data into the WAL stream that can be read by logical-decoding plugins, but is not connected to physical data restoration.

Allow SP-GiST operator classes to store an arbitrary "traversal value" while descending the index (Alexander Lebedev, Teodor Sigaev)
This is somewhat like the "reconstructed value", but it could be any arbitrary chunk of data, not necessarily of the same data type as the indexed column.

Introduce a LOG_SERVER_ONLY message level for ereport() (David Steele)
This level acts like LOG except that the message is never sent to the client. It is meant for use in auditing and similar applications.

Provide a Makefile target to build all generated headers (Michael Paquier, Tom Lane)
submake-generated-headers can now be invoked to ensure that generated backend header files are up-to-date. This is useful in subdirectories that might be built "standalone".

Support OpenSSL 1.1.0 (Andreas Karlsson, Heikki Linnakangas)

## E.25.3.13. Additional Modules

Add configuration parameter auto_explain.sample_rate to allow contrib/auto_explain to capture just a configurable fraction of all queries (Craig Ringer, Julien Rouhaud)
This allows reduction of overhead for heavy query traffic, while still getting useful information on average.

Add contrib/bloom module that implements an index access method based on Bloom filtering (Teodor Sigaev, Alexander Korotkov)
This is primarily a proof-of-concept for non-core index access methods, but it could be useful in its own right for queries that search many columns.

In contrib/cube, introduce distance operators for cubes, and support kNN-style searches in GiST indexes on cube columns (Stas Kelvich)

Make contrib/hstore's hstore_to_jsonb_loose() and hstore_to_json_loose() functions agree on what is a number (Tom Lane)
Previously, hstore_to_jsonb_loose() would convert numeric-looking strings to JSON numbers, rather than strings, even if they did not exactly match the JSON syntax specification for numbers. This was inconsistent with hstore_to_json_loose(), so tighten the test to match the JSON syntax.

Add selectivity estimation functions for contrib/intarray operators to improve plans for queries using those operators (Yury Zhuravlev, Alexander Korotkov)

Make contrib/pageinspect's heap_page_items() function show the raw data in each tuple, and add new functions tuple_data_split() and heap_page_item_attrs() for inspection of individual tuple fields (Nikolay Shaplov)

Add an optional S2K iteration count parameter to contrib/pgcrypto's pgp_sym_encrypt() function (Jeff Janes)

Add support for "word similarity" to contrib/pg_trgm (Alexander Korotkov, Artur Zakirov)
These functions and operators measure the similarity between one string and the most similar single word of another string.

Add configuration parameter pg_trgm.similarity_threshold for contrib/pg_trgm's similarity threshold (Artur Zakirov)
This threshold has always been configurable, but formerly it was controlled by special-purpose functions set_limit() and show_limit(). Those are now deprecated.

Improve contrib/pg_trgm's GIN operator class to speed up index searches in which both common and rare keys appear (Jeff Janes)

Improve performance of similarity searches in contrib/pg_trgm GIN indexes (Christophe Fornaroli)

Add contrib/pg_visibility module to allow examining table visibility maps (Robert Haas)

Add ssl_extension_info() function to contrib/sslinfo, to print information about SSL extensions present in the X509 certificate used for the current connection (Dmitry Voronin)

## E.25.3.13.1.postgres_fdw

Allow extension-provided operators and functions to be sent for remote execution, if the extension is whitelisted in the foreign server's options (Paul Ramsey)
Users can enable this feature when the extension is known to exist in a compatible version in the remote database. It allows more efficient execution of queries involving extension operators.

Consider performing sorts on the remote server (Ashutosh Bapat)

Consider performing joins on the remote server (Shigeru Hanada, Ashutosh Bapat)

When feasible, perform UPDATE or DELETE entirely on the remote server (Etsuro Fujita)
Formerly, remote updates involved sending a SELECT FOR UPDATE command and then updating or deleting the selected rows one-by-one. While that is still necessary if the operation requires any local processing, it can now be done remotely if all elements of the query are safe to send to the remote server.

Allow the fetch size to be set as a server or table option (Corey Huinker)
Formerly, postgres_fdw always fetched 100 rows at a time from remote queries; now that behavior is configurable.

Use a single foreign-server connection for local user IDs that all map to the same remote user (Ashutosh Bapat)

Transmit query cancellation requests to the remote server (Michael Paquier, Etsuro Fujita)
Previously, a local query cancellation request did not cause an already-sent remote query to terminate early.

 | Prev | Home | Next
 | Release 9.6.1 | Up | Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 9.6.24 
 9.6.23 
 9.6.22 
 9.6.21 
 9.6.20 
 9.6.19 
 9.6.18 
 9.6.17 
 9.6.16 
 9.6.15 
 9.6.14 
 9.6.13 
 9.6.12 
 9.6.11 
 9.6.10 
 9.6.9 
 9.6.8 
 9.6.7 
 9.6.6 
 9.6.5 
 9.6.4 
 9.6.3 
 9.6.2 
 9.6.1 
 9.6.0

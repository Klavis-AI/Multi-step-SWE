<!-- source: https://www.postgresql.org/docs/release/16.0/ -->

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

##  PostgreSQL 16.0 

 | E.16. Release 16
 | Prev | Up | Appendix E. Release Notes | Home | Next

## E.16. Release 16 #

E.16.1. Overview
E.16.2. Migration to Version 16
E.16.3. Changes
E.16.4. Acknowledgments
Release date: 2023-09-14

## E.16.1. Overview #
PostgreSQL 16 contains many new features and enhancements, including:

Allow parallelization of FULL and internal right OUTER hash joins

Allow logical replication from standby servers

Allow logical replication subscribers to apply large transactions in parallel

Allow monitoring of I/O statistics using the new pg_stat_io view

Add SQL/JSON constructors and identity functions

Improve performance of vacuum freezing

Add support for regular expression matching of user and database names in pg_hba.conf, and user names in pg_ident.conf
The above items and other new features of PostgreSQL 16 are explained in more detail in the sections below.

## E.16.2. Migration to Version 16 #
A dump/restore using pg_dumpall or use of pg_upgrade or logical replication is required for those wishing to migrate data from any previous release. See Section 19.6 for general information on migrating to new major releases.
Version 16 contains a number of changes that may affect compatibility with previous releases. Observe the following incompatibilities:

Change assignment rules for PL/pgSQL bound cursor variables (Tom Lane) Section 
Previously, the string value of such variables was set to match the variable name during cursor assignment; now it will be assigned during OPEN, and will not match the variable name. To restore the previous behavior, assign the desired portal name to the cursor variable before OPEN.

Disallow NULLS NOT DISTINCT indexes for primary keys (Daniel Gustafsson) Section 

Change REINDEX DATABASE and reindexdb to not process indexes on system catalogs (Simon Riggs) Section Section 
Processing such indexes is still possible using REINDEX SYSTEM and reindexdb --system.

Tighten GENERATED expression restrictions on inherited and partitioned tables (Amit Langote, Tom Lane) Section 
Columns of parent/partitioned and child/partition tables must all have the same generation status, though now the actual generation expressions can be different.

Remove pg_walinspect functions pg_get_wal_records_info_till_end_of_wal() and pg_get_wal_stats_till_end_of_wal() (Bharath Rupireddy) Section 

Rename server variable force_parallel_mode to debug_parallel_query (David Rowley) Section Section 

Remove the ability to create views manually with ON SELECT rules (Tom Lane) Section 

Remove the server variable vacuum_defer_cleanup_age (Andres Freund) Section 
This has been unnecessary since hot_standby_feedback and replication slots were added.

Remove server variable promote_trigger_file (Simon Riggs) Section 
This was used to promote a standby to primary, but is now more easily accomplished with pg_ctl promote or pg_promote().

Remove read-only server variables lc_collate and lc_ctype (Peter Eisentraut) Section 
Collations and locales can vary between databases so having them as read-only server variables was unhelpful.

Role inheritance now controls the default inheritance status of member roles added during GRANT (Robert Haas) Section 
The role's default inheritance behavior can be overridden with the new GRANT ... WITH INHERIT clause. This allows inheritance of some roles and not others because the members' inheritance status is set at GRANT time. Previously the inheritance status of member roles was controlled only by the role's inheritance status, and changes to a role's inheritance status affected all previous and future member roles.

Restrict the privileges of CREATEROLE and its ability to modify other roles (Robert Haas) Section Section 
Previously roles with CREATEROLE privileges could change many aspects of any non-superuser role. Such changes, including adding members, now require the role requesting the change to have ADMIN OPTION permission. For example, they can now change the CREATEDB, REPLICATION, and BYPASSRLS properties only if they also have those permissions.

Remove symbolic links for the postmaster binary (Peter Eisentraut) Section 

## E.16.3. Changes #
Below you will find a detailed account of the changes between PostgreSQL 16 and the previous major release.

## E.16.3.1. Server #

E.16.3.1.1. Optimizer #

Allow incremental sorts in more cases, including DISTINCT (David Rowley) Section Section 

Add the ability for aggregates having ORDER BY or DISTINCT to use pre-sorted data (David Rowley) Section Section Section 
The new server variable enable_presorted_aggregate can be used to disable this.

Allow memoize atop a UNION ALL (Richard Guo) Section 

Allow anti-joins to be performed with the non-nullable input as the inner relation (Richard Guo) Section 

Allow parallelization of FULL and internal right OUTER hash joins (Melanie Plageman, Thomas Munro) Section 

Improve the accuracy of GIN index access optimizer costs (Ronan Dunklau) Section 

E.16.3.1.2. General Performance #

Allow more efficient addition of heap and index pages (Andres Freund) Section Section 

During non-freeze operations, perform page freezing where appropriate (Peter Geoghegan) Section Section Section 
This makes full-table freeze vacuums less necessary.

Allow window functions to use the faster ROWS mode internally when RANGE mode is active but unnecessary (David Rowley) Section 

Allow optimization of always-increasing window functions ntile(), cume_dist() and percent_rank() (David Rowley) Section 

Allow aggregate functions string_agg() and array_agg() to be parallelized (David Rowley) Section 

Improve performance by caching RANGE and LIST partition lookups (Amit Langote, Hou Zhijie, David Rowley) Section 

Allow control of the shared buffer usage by vacuum and analyze (Melanie Plageman) Section Section Section 
The VACUUM/ANALYZE option is BUFFER_USAGE_LIMIT, and the vacuumdb option is --buffer-usage-limit. The default value is set by server variable vacuum_buffer_usage_limit, which also controls autovacuum.

Support wal_sync_method=fdatasync on Windows (Thomas Munro) Section 

Allow HOT updates if only BRIN-indexed columns are updated (Matthias van de Meent, Josef Simanek, Tomas Vondra) Section 

Improve the speed of updating the process title (David Rowley) Section 

Allow xid/subxid searches and ASCII string detection to use vector operations (Nathan Bossart, John Naylor) Section Section Section Section 
ASCII detection is particularly useful for COPY FROM. Vector operations are also used for some C array searches.

Reduce overhead of memory allocations (Andres Freund, David Rowley) Section 

E.16.3.1.3. Monitoring #

Add system view pg_stat_io view to track I/O statistics (Melanie Plageman) Section Section Section Section Section 

Record statistics on the last sequential and index scans on tables (Dave Page) Section 
This information appears in pg_stat_*_tables and pg_stat_*_indexes.

Record statistics on the occurrence of updated rows moving to new pages (Corey Huinker) Section 
The pg_stat_*_tables column is n_tup_newpage_upd.

Add speculative lock information to the pg_locks system view (Masahiko Sawada, Noriyoshi Shinoda) Section 
The transaction id is displayed in the transactionid column and the speculative insertion token is displayed in the objid column.

Add the display of prepared statement result types to the pg_prepared_statements view (Dagfinn Ilmari Mannsaker) Section Section 

Create subscription statistics entries at subscription creation time so stats_reset is accurate (Andres Freund) Section 
Previously entries were created only when the first statistics were reported.

Correct the I/O accounting for temp relation writes shown in pg_stat_database (Melanie Plageman) Section 

Add function pg_stat_get_backend_subxact() to report on a session's subtransaction cache (Dilip Kumar) Section 

Have pg_stat_get_backend_idset(), pg_stat_get_backend_activity(), and related functions use the unchanging backend id (Nathan Bossart) Section 
Previously the index values might change during the lifetime of the session.

Report stand-alone backends with a special backend type (Melanie Plageman) Section 

Add wait event SpinDelay to report spinlock sleep delays (Andres Freund) Section 

Create new wait event DSMAllocate to indicate waiting for dynamic shared memory allocation (Thomas Munro) Section 
Previously this type of wait was reported as DSMFillZeroWrite, which was also used by mmap() allocations.

Add the database name to the process title of logical WAL senders (Tatsuhiro Nakamori) Section 
Physical WAL senders do not display a database name.

Add checkpoint and REDO LSN information to log_checkpoints messages (Bharath Rupireddy, Kyotaro Horiguchi) Section 

Provide additional details during client certificate failures (Jacob Champion) Section 

E.16.3.1.4. Privileges #

Add predefined role pg_create_subscription with permission to create subscriptions (Robert Haas) Section 

Allow subscriptions to not require passwords (Robert Haas) Section Section Section 
This is accomplished with the option password_required=false.

Simplify permissions for LOCK TABLE (Jeff Davis) Section 
Previously a user's ability to perform LOCK TABLE at various lock levels was limited to the lock levels required by the commands they had permission to execute on the table. For example, someone with UPDATE permission could perform all lock levels except ACCESS SHARE, even though it was a lesser lock level. Now users can issue lesser lock levels if they already have permission for greater lock levels.

Allow ALTER GROUP group_name ADD USER user_name to be performed with ADMIN OPTION (Robert Haas) Section 
Previously CREATEROLE permission was required.

Allow GRANT to use WITH ADMIN TRUE/FALSE syntax (Robert Haas) Section 
Previously only the WITH ADMIN OPTION syntax was supported.

Allow roles that create other roles to automatically inherit the new role's rights or the ability to SET ROLE to the new role (Robert Haas, Shi Yu) Section Section 
This is controlled by server variable createrole_self_grant.

Prevent users from changing the default privileges of non-inherited roles (Robert Haas) Section 
This is now only allowed for inherited roles.

When granting role membership, require the granted-by role to be a role that has appropriate permissions (Robert Haas) Section 
This is a requirement even when a non-bootstrap superuser is granting role membership.

Allow non-superusers to grant permissions using a granted-by user that is not the current user (Robert Haas) Section 
The current user still must have sufficient permissions given by the specified granted-by user.

Add GRANT to control permission to use SET ROLE (Robert Haas) Section 
This is controlled by a new GRANT ... SET option.

Add dependency tracking to roles which have granted privileges (Robert Haas) Section 
For example, removing ADMIN OPTION will fail if there are privileges using that option; CASCADE must be used to revoke dependent permissions.

Add dependency tracking of grantors for GRANT records (Robert Haas) Section 
This guarantees that pg_auth_members.grantor values are always valid.

Allow multiple role membership records (Robert Haas) Section Section 
Previously a new membership grant would remove a previous matching membership grant, even if other aspects of the grant did not match.

Prevent removal of superuser privileges for the bootstrap user (Robert Haas) Section 
Restoring such users could lead to errors.

Allow makeaclitem() to accept multiple privilege names (Robins Tharakan) Section 
Previously only a single privilege name, like SELECT, was accepted.

E.16.3.1.5. Server Configuration #

Add support for Kerberos credential delegation (Stephen Frost) Section Section Section Section 
This is enabled with server variable gss_accept_delegation and libpq connection parameter gssdelegation.

Allow the SCRAM iteration count to be set with server variable scram_iterations (Daniel Gustafsson) Section 

Improve performance of server variable management (Tom Lane) Section Section 

Tighten restrictions on which server variables can be reset (Masahiko Sawada) Section 
Previously, while certain variables, like transaction_isolation, were not affected by RESET ALL, they could be individually reset in inappropriate situations.

Move various postgresql.conf items into new categories (Shinya Kato) Section 
This also affects the categories displayed in the pg_settings view.

Prevent configuration file recursion beyond 10 levels (Julien Rouhaud) Section 

Allow autovacuum to more frequently honor changes to delay settings (Melanie Plageman) Section Section 
Rather than honor changes only at the start of each relation, honor them at the start of each block.

Remove restrictions that archive files be durably renamed (Nathan Bossart) Section Section 
The archive_command command is now more likely to be called with already-archived files after a crash.

Prevent archive_library and archive_command from being set at the same time (Nathan Bossart) Section 
Previously archive_library would override archive_command.

Allow the postmaster to terminate children with an abort signal (Tom Lane) Section 
This allows collection of a core dump for a stuck child process. This is controlled by send_abort_for_crash and send_abort_for_kill. The postmaster's -T switch is now the same as setting send_abort_for_crash.

Remove the non-functional postmaster -n option (Tom Lane) Section 

Allow the server to reserve backend slots for roles with pg_use_reserved_connections membership (Nathan Bossart) Section 
The number of reserved slots is set by server variable reserved_connections.

Allow huge pages to work on newer versions of Windows 10 (Thomas Munro) Section 
This adds the special handling required to enable huge pages on newer versions of Windows 10.

Add debug_io_direct setting for developer usage (Thomas Munro, Andres Freund, Bharath Rupireddy) Section Section 
While primarily for developers, wal_sync_method=open_sync/open_datasync has been modified to not use direct I/O with wal_level=minimal; this is now enabled with debug_io_direct=wal.

Add function pg_split_walfile_name() to report the segment and timeline values of WAL file names (Bharath Rupireddy) Section Section 

E.16.3.1.6. pg_hba.conf#

Add support for regular expression matching on database and role entries in pg_hba.conf (Bertrand Drouvot) Section 
Regular expression patterns are prefixed with a slash. Database and role names that begin with slashes need to be double-quoted if referenced in pg_hba.conf.

Improve user-column handling of pg_ident.conf to match pg_hba.conf (Jelte Fennema) Section 
Specifically, add support for all, role membership with +, and regular expressions with a leading slash. Any user name that matches these patterns must be double-quoted.

Allow include files in pg_hba.conf and pg_ident.conf (Julien Rouhaud) Section 
These are controlled by include, include_if_exists, and include_dir. System views pg_hba_file_rules and pg_ident_file_mappings now display the file name.

Allow pg_hba.conf tokens to be of unlimited length (Tom Lane) Section 

Add rule and map numbers to the system view pg_hba_file_rules (Julien Rouhaud) Section 

E.16.3.1.7. Localization#

Determine the default encoding from the locale when using ICU (Jeff Davis) Section 
Previously the default was always UTF-8.

Have CREATE DATABASE and CREATE COLLATION's LOCALE options, and initdb and createdb--locale options, control non-libc collation providers (Jeff Davis)
Previously they only controlled libc providers.

Add predefined collations unicode and ucs_basic (Peter Eisentraut) Section 
This only works if ICU support is enabled.

Allow custom ICU collation rules to be created (Peter Eisentraut) Section 
This is done using CREATE COLLATION's new RULES clause, as well as new options for CREATE DATABASE, createdb, and initdb.

Allow Windows to import system locales automatically (Juan Jose Santamaria Flecha) Section 
Previously, only ICU locales could be imported on Windows.

## E.16.3.2. Logical Replication#

Allow logical decoding on standbys (Bertrand Drouvot, Andres Freund, Amit Khandekar) Section Section Section 
Snapshot WAL records are required for logical slot creation but cannot be created on standbys. To avoid delays, the new function pg_log_standby_snapshot() allows creation of such records.

Add server variable to control how logical decoding publishers transfer changes and how subscribers apply them (Shi Yu) Section Section Section 
The variable is debug_logical_replication_streaming.

Allow logical replication initial table synchronization to copy rows in binary format (Melih Mutlu) Section 
This is only possible for subscriptions marked as binary.

Allow parallel application of logical replication (Hou Zhijie, Wang Wei, Amit Kapila) Section Section Section 
The CREATE SUBSCRIPTIONSTREAMING option now supports parallel to enable application of large transactions by parallel workers. The number of parallel workers is controlled by the new server variable max_parallel_apply_workers_per_subscription. Wait events LogicalParallelApplyMain, LogicalParallelApplyStateChange, and LogicalApplySendData were also added. Column leader_pid was added to system view pg_stat_subscription to track parallel activity.

Improve performance for logical replication apply without a primary key (Onder Kalaci, Amit Kapila) Section 
Specifically, REPLICA IDENTITY FULL can now use btree indexes rather than sequentially scanning the table to find matches.

Allow logical replication subscribers to process only changes that have no origin (Vignesh C, Amit Kapila) Section Section 
This can be used to avoid replication loops. This is controlled by the new CREATE SUBSCRIPTION ... ORIGIN option.

Perform logical replication SELECT and DML actions as the table owner (Robert Haas) Section Section 
This improves security and now requires subscription owners to be either superusers or to have SET ROLE permission on all roles owning tables in the replication set. The previous behavior of performing all operations as the subscription owner can be enabled with the subscription run_as_owner option.

Have wal_retrieve_retry_interval operate on a per-subscription basis (Nathan Bossart) Section 
Previously the retry time was applied globally. This also adds wait events >LogicalRepLauncherDSA and LogicalRepLauncherHash.

## E.16.3.3. Utility Commands #

Add EXPLAIN option GENERIC_PLAN to display the generic plan for a parameterized query (Laurenz Albe) Section 

Allow a COPY FROM value to map to a column's DEFAULT (Israel Barth Rubio) Section 

Allow COPY into foreign tables to add rows in batches (Andrey Lepikhov, Etsuro Fujita) Section 
This is controlled by the postgres_fdw option batch_size.

Allow the STORAGE type to be specified by CREATE TABLE (Teodor Sigaev, Aleksander Alekseev) Section Section 
Previously only ALTER TABLE could control this.

Allow truncate triggers on foreign tables (Yugo Nagata) Section 

Allow VACUUM and vacuumdb to only process TOAST tables (Nathan Bossart) Section 
This is accomplished by having VACUUM turn off PROCESS_MAIN or by vacuumdb using the --no-process-main option.

Add VACUUM options to skip or update all frozen statistics (Tom Lane, Nathan Bossart) Section 
The options are SKIP_DATABASE_STATS and ONLY_DATABASE_STATS.

Change REINDEX DATABASE and REINDEX SYSTEM to no longer require an argument (Simon Riggs) Section Section 
Previously the database name had to be specified.

Allow CREATE STATISTICS to generate a statistics name if none is specified (Simon Riggs) Section 

## E.16.3.4. Data Types #

Allow non-decimal integer literals (Peter Eisentraut) Section 
For example, 0x42F, 0o273, and 0b100101.

Allow NUMERIC to process hexadecimal, octal, and binary integers of any size (Dean Rasheed) Section 
Previously only unquoted eight-byte integers were supported with these non-decimal bases.

Allow underscores in integer and numeric constants (Peter Eisentraut, Dean Rasheed) Section 
This can improve readability for long strings of digits.

Accept the spelling +infinity in datetime input (Vik Fearing) Section 

Prevent the specification of epoch and infinity together with other fields in datetime strings (Joseph Koshakow) Section 

Remove undocumented support for date input in the form YyearMmonthDday (Joseph Koshakow) Section 

Add functions pg_input_is_valid() and pg_input_error_info() to check for type conversion errors (Tom Lane) Section Section 

## E.16.3.5. General Queries #

Allow subqueries in the FROM clause to omit aliases (Dean Rasheed) Section 

Add support for enhanced numeric literals in SQL/JSON paths (Peter Eisentraut) Section 
For example, allow hexadecimal, octal, and binary integers and underscores between digits.

## E.16.3.6. Functions #

Add SQL/JSON constructors (Nikita Glukhov, Teodor Sigaev, Oleg Bartunov, Alexander Korotkov, Amit Langote) Section 
The new functions JSON_ARRAY(), JSON_ARRAYAGG(), JSON_OBJECT(), and JSON_OBJECTAGG() are part of the SQL standard.

Add SQL/JSON object checks (Nikita Glukhov, Teodor Sigaev, Oleg Bartunov, Alexander Korotkov, Amit Langote, Andrew Dunstan) Section 
The IS JSON checks include checks for values, arrays, objects, scalars, and unique keys.

Allow JSON string parsing to use vector operations (John Naylor) Section 

Improve the handling of full text highlighting function ts_headline() for OR and NOT expressions (Tom Lane) Section 

Add functions to add, subtract, and generate timestamptz values in a specified time zone (Przemyslaw Sztoch, Gurjeet Singh) Section 
The functions are date_add(), date_subtract(), and generate_series().

Change date_trunc(unit, timestamptz, time_zone) to be an immutable function (Przemyslaw Sztoch) Section 
This allows the creation of expression indexes using this function.

Add server variable SYSTEM_USER (Bertrand Drouvot) Section 
This reports the authentication method and its authenticated user.

Add functions array_sample() and array_shuffle() (Martin Kalcher) Section 

Add aggregate function ANY_VALUE() which returns any value from a set (Vik Fearing) Section 

Add function random_normal() to supply normally-distributed random numbers (Paul Ramsey) Section 

Add error function erf() and its complement erfc() (Dean Rasheed) Section 

Improve the accuracy of numeric power() for integer exponents (Dean Rasheed) Section 

Add XMLSERIALIZE() option INDENT to pretty-print its output (Jim Jones) Section 

Change pg_collation_actual_version() to return a reasonable value for the default collation (Jeff Davis) Section 
Previously it returned NULL.

Allow pg_read_file() and pg_read_binary_file() to ignore missing files (Kyotaro Horiguchi) Section 

Add byte specification (B) to pg_size_bytes() (Peter Eisentraut) Section 

Allow to_reg* functions to accept numeric OIDs as input (Tom Lane) Section 

## E.16.3.7. PL/pgSQL#

Add the ability to get the current function's OID in PL/pgSQL (Pavel Stehule) Section 
This is accomplished with GET DIAGNOSTICS variable = PG_ROUTINE_OID.

## E.16.3.8. libpq#

Add libpq connection option require_auth to specify a list of acceptable authentication methods (Jacob Champion) Section 
This can also be used to disallow certain authentication methods.

Allow multiple libpq-specified hosts to be randomly selected (Jelte Fennema) Section Section 
This is enabled with load_balance_hosts=random and can be used for load balancing.

Add libpq option sslcertmode to control transmission of the client certificate (Jacob Champion) Section 
The option values are disable, allow, and require.

Allow libpq to use the system certificate pool for certificate verification (Jacob Champion, Thomas Habets) Section 
This is enabled with sslrootcert=system, which also enables sslmode=verify-full.

## E.16.3.9. Client Applications #

Allow ECPG variable declarations to use typedef names that match unreserved SQL keywords (Tom Lane) Section 
This change does prevent keywords which match C typedef names from being processed as keywords in later EXEC SQL blocks.

E.16.3.9.1. psql#

Allow psql to control the maximum width of header lines in expanded format (Platon Pronko) Section 
This is controlled by xheader_width.

Add psql command \drg to show role membership details (Pavel Luzanov) Section Section 
The Member of output column has been removed from \du and \dg because this new command displays this information in more detail.

Allow psql's access privilege commands to show system objects (Nathan Bossart) Section Section 
The options are \dpS and \zS.

Add FOREIGN designation to psql\d+ for foreign table children and partitions (Ian Lawrence Barwick) Section 

Prevent \df+ from showing function source code (Isaac Morland) Section 
Function bodies are more easily viewed with \sf.

Allow psql to submit queries using the extended query protocol (Peter Eisentraut) Section 
Passing arguments to such queries is done using the new psql\bind command.

Allow psql\watch to limit the number of executions (Andrey Borodin) Section 
The \watch options can now be named when specified.

Detect invalid values for psql\watch, and allow zero to specify no delay (Andrey Borodin) Section 

Allow psql scripts to obtain the exit status of shell commands and queries (Corey Huinker, Tom Lane) Section Section 
The new psql control variables are SHELL_ERROR and SHELL_EXIT_CODE.

Various psql tab completion improvements (Vignesh C, Aleksander Alekseev, Dagfinn Ilmari Mannsaker, Shi Yu, Michael Paquier, Ken Kato, Peter Smith) Section Section Section Section Section Section Section Section Section Section Section Section 

E.16.3.9.2. pg_dump#

Add pg_dump control of dumping child tables and partitions (Gilles Darold) Section 
The new options are --table-and-children, --exclude-table-and-children, and --exclude-table-data-and-children.

Add LZ4 and Zstandard compression to pg_dump (Georgios Kokolatos, Justin Pryzby)

Allow pg_dump and pg_basebackup to use long mode for compression (Justin Pryzby) Section Section Section Section 

Improve pg_dump to accept a more consistent compression syntax (Georgios Kokolatos) Section 
Options like --compress=gzip:5.

## E.16.3.10. Server Applications #

Add initdb option to set server variables for the duration of initdb and all future server starts (Tom Lane) Section 
The option is -c name=value.

Add options to createuser to control more user options (Shinya Kato) Section Section 
Specifically, the new options control the valid-until date, bypassing of row-level security, and role membership.

Deprecate createuser option --role (Nathan Bossart) Section Section 
This option could be easily confused with new createuser role membership options, so option --member-of has been added with the same functionality. The --role option can still be used.

Allow control of vacuumdb schema processing (Gilles Darold) Section 
These are controlled by options --schema and --exclude-schema.

Use new VACUUM options to improve the performance of vacuumdb (Tom Lane, Nathan Bossart) Section 

Have pg_upgrade set the new cluster's locale and encoding (Jeff Davis) Section 
This removes the requirement that the new cluster be created with the same locale and encoding settings.

Add pg_upgrade option to specify the default transfer mode (Peter Eisentraut) Section 
The option is --copy.

Improve pg_basebackup to accept numeric compression options (Georgios Kokolatos, Michael Paquier) Section 
Options like --compress=server-5 are now supported.

Fix pg_basebackup to handle tablespaces stored in the PGDATA directory (Robert Haas) Section 

Add pg_waldump option --save-fullpage to dump full page images (David Christensen) Section 

Allow pg_waldump options -t/--timeline to accept hexadecimal values (Peter Eisentraut) Section 

Add support for progress reporting to pg_verifybackup (Masahiko Sawada) Section 

Allow pg_rewind to properly track timeline changes (Heikki Linnakangas) Section Section 
Previously if pg_rewind was run after a timeline switch but before a checkpoint was issued, it might incorrectly determine that a rewind was unnecessary.

Have pg_receivewal and pg_recvlogical cleanly exit on SIGTERM (Christoph Berg) Section 
This signal is often used by systemd.

## E.16.3.11. Source Code #

Build ICU support by default (Jeff Davis) Section 
This removes build flag--with-icu and adds flag --without-icu.

Add support for SSE2 (Streaming SIMD Extensions 2) vector operations on x86-64 architectures (John Naylor) Section 

Add support for Advanced SIMD (Single Instruction Multiple Data) (NEON) instructions on ARM architectures (Nathan Bossart) Section 

Have Windows binaries built with MSVC use RandomizedBaseAddress (ASLR) (Michael Paquier) Section 
This was already enabled on MinGW builds.

Prevent extension libraries from exporting their symbols by default (Andres Freund, Tom Lane) Section Section 
Functions that need to be called from the core backend or other extensions must now be explicitly marked PGDLLEXPORT.

Require Windows 10 or newer versions (Michael Paquier, Juan Jose Santamaria Flecha) Section 
Previously Windows Vista and Windows XP were supported.

Require Perl version 5.14 or later (John Naylor) Section 

Require Bison version 2.3 or later (John Naylor) Section 

Require Flex version 2.5.35 or later (John Naylor) Section 

Require MIT Kerberos for GSSAPI support (Stephen Frost) Section 

Remove support for Visual Studio 2013 (Michael Paquier) Section 

Remove support for HP-UX (Thomas Munro) Section 

Remove support for HP/Intel Itanium (Thomas Munro) Section 

Remove support for M68K, M88K, M32R, and SuperHCPU architectures (Thomas Munro) Section Section 

Remove libpq support for SCM credential authentication (Michael Paquier) Section 
Backend support for this authentication method was removed in PostgresSQL 9.1.

Add meson build system (Andres Freund, Nazir Bilal Yavuz, Peter Eisentraut) Section 
This eventually will replace the Autoconf and Windows-based MSVC build systems.

Allow control of the location of the openssl binary used by the build system (Peter Eisentraut) Section 
Make finding openssl program a configure or meson option

Add build option to allow testing of small table segment sizes (Andres Freund) Section 
The build options are --with-segsize-blocks and -Dsegsize_blocks.

Add pgindent options (Andrew Dunstan) Section Section Section Section Section Section Section 
The new options are --show-diff, --silent-diff, --commit, and --help, and allow multiple --exclude options. Also require the typedef file to be explicitly specified. Options --code-base and --build were also removed.

Add pg_bsd_indent source code to the main tree (Tom Lane) Section 

Improve make_ctags and make_etags (Yugo Nagata) Section 

Adjust pg_attribute columns for efficiency (Peter Eisentraut) Section 

## E.16.3.12. Additional Modules #

Improve use of extension-based indexes on boolean columns (Zongliang Quan, Tom Lane) Section 

Add support for Daitch-Mokotoff Soundex to fuzzystrmatch (Dag Lem) Section 

Allow auto_explain to log values passed to parameterized statements (Dagfinn Ilmari Mannsaker) Section 
This affects queries using server-side PREPARE/EXECUTE and client-side parse/bind. Logging is controlled by auto_explain.log_parameter_max_length; by default query parameters will be logged with no length restriction.

Have auto_explain's log_verbose mode honor the value of compute_query_id (Atsushi Torikoshi) Section 
Previously even if compute_query_id was enabled, log_verbose was not showing the query identifier.

Change the maximum length of ltree labels from 256 to 1000 and allow hyphens (Garen Torikian) Section 

Have pg_stat_statements normalize constants used in utility commands (Michael Paquier) Section 
Previously constants appeared instead of placeholders, e.g., $1.

Add pg_walinspect function pg_get_wal_block_info() to report WAL block information (Michael Paquier, Melanie Plageman, Bharath Rupireddy) Section Section Section Section 

Change how pg_walinspect functions pg_get_wal_records_info() and pg_get_wal_stats() interpret ending LSNs (Bharath Rupireddy) Section 
Previously ending LSNs which represent nonexistent WAL locations would generate an error, while they will now be interpreted as the end of the WAL.

Add detailed descriptions of WAL records in pg_walinspect and pg_waldump (Melanie Plageman, Peter Geoghegan) Section Section Section Section 

Add pageinspect function bt_multi_page_stats() to report statistics on multiple pages (Hamid Akhtar) Section 
This is similar to bt_page_stats() except it can report on a range of pages.

Add empty range output column to pageinspect function brin_page_items() (Tomas Vondra) Section 

Redesign archive modules to be more flexible (Nathan Bossart) Section 
Initialization changes will require modules written for older versions of Postgres to be updated.

Correct inaccurate pg_stat_statements row tracking extended query protocol statements (Sami Imseih) Section 

Add pg_buffercache function pg_buffercache_usage_counts() to report usage totals (Nathan Bossart) Section 

Add pg_buffercache function pg_buffercache_summary() to report summarized buffer statistics (Melih Mutlu) Section 

Allow the schemas of required extensions to be referenced in extension scripts using the new syntax @extschema:referenced_extension_name@ (Regina Obe) Section 

Allow required extensions to be marked as non-relocatable using no_relocate (Regina Obe) Section 
This allows @extschema:referenced_extension_name@ to be treated as a constant for the lifetime of the extension.

E.16.3.12.1. postgres_fdw#

Allow postgres_fdw to do aborts in parallel (Etsuro Fujita) Section 
This is enabled with postgres_fdw option parallel_abort.

Make ANALYZE on foreign postgres_fdw tables more efficient (Tomas Vondra) Section 
The postgres_fdw option analyze_sampling controls the sampling method.

Restrict shipment of reg* type constants in postgres_fdw to those referencing built-in objects or extensions marked as shippable (Tom Lane) Section 

Have postgres_fdw and dblink handle interrupts during connection establishment (Andres Freund) Section 

## E.16.4. Acknowledgments #
The following individuals (in alphabetical order) have contributed to this release as patch authors, committers, reviewers, testers, or reporters of issues.

 | Abhijit Menon-Sen
 | Adam Mackler
 | Adrian Klaver
 | Ahsan Hadi
 | Ajin Cherian
 | Ajit Awekar
 | Alan Hodgson
 | Aleksander Alekseev
 | Alex Denman
 | Alex Kozhemyakin
 | Alexander Korolev
 | Alexander Korotkov
 | Alexander Lakhin
 | Alexander Pyhalov
 | Alexey Borzov
 | Alexey Ermakov
 | Alexey Makhmutov
 | Alvaro Herrera
 | Amit Kapila
 | Amit Khandekar
 | Amit Langote
 | Amul Sul
 | Anastasia Lubennikova
 | Anban Company
 | Andreas Dijkman
 | Andreas Karlsson
 | Andreas Scherbaum
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
 | Ankit Kumar Pandey
 | Ante Kresic
 | Anton Melnikov
 | Anton Sidyakin
 | Anton Voloshin
 | Antonin Houska
 | Arne Roland
 | Artem Anisimov
 | Arthur Zakirov
 | Ashutosh Bapat
 | Ashutosh Sharma
 | Asim Praveen
 | Atsushi Torikoshi
 | Ayaki Tachikake
 | Balazs Szilfai
 | Benoit Lobreau
 | Bernd Helmle
 | Bertrand Drouvot
 | Bharath Rupireddy
 | Bilva Sanaba
 | Bob Krier
 | Boris Zentner
 | Brad Nicholson
 | Brar Piening
 | Bruce Momjian
 | Bruno da Silva
 | Carl Sopchak
 | Cary Huang
 | Changhong Fei
 | Chris Travers
 | Christoph Berg
 | Christophe Pettus
 | Corey Huinker
 | Craig Ringer
 | Curt Kolovson
 | Dag Lem
 | Dagfinn Ilmari Mannsaker
 | Daniel Gustafsson
 | Daniel Verite
 | Daniel Watzinger
 | Daniel Westermann
 | Daniele Varrazzo
 | Daniil Anisimov
 | Danny Shemesh
 | Dave Page
 | David Christensen
 | David G. Johnston
 | David Geier
 | David Gilman
 | David Kimura
 | David Rowley
 | David Steele
 | David Turon
 | David Zhang
 | Davinder Singh
 | Dean Rasheed
 | Denis Laxalde
 | Dilip Kumar
 | Dimos Stamatakis
 | Dmitriy Kuzmin
 | Dmitry Astapov
 | Dmitry Dolgov
 | Dmitry Koval
 | Dong Wook Lee
 | Dongming Liu
 | Drew DeVault
 | Duncan Sands
 | Ed Maste
 | Egor Chindyaskin
 | Ekaterina Kiryanova
 | Elena Indrupskaya
 | Emmanuel Quincerot
 | Eric Mutta
 | Erik Rijkers
 | Erki Eessaar
 | Erwin Brandstetter
 | Etsuro Fujita
 | Eugeny Zhuzhnev
 | Euler Taveira
 | Evan Jones
 | Evgeny Morozov
 | Fabrizio de Royes Mello
 | Farias de Oliveira
 | Florin Irion
 | Franz-Josef Farber
 | Garen Torikian
 | Georgios Kokolatos
 | Gilles Darold
 | Greg Stark
 | Guillaume Lelarge
 | Gunnar Bluth
 | Gunnar Morling
 | Gurjeet Singh
 | Haiyang Wang
 | Haiying Tang
 | Hamid Akhtar
 | Hans Buschmann
 | Hao Wu
 | Hayato Kuroda
 | Heath Lord
 | Heikki Linnakangas
 | Himanshu Upadhyaya
 | Hisahiro Kauchi
 | Hongyu Song
 | Hubert Lubaczewski
 | Hung Nguyen
 | Ian Barwick
 | Ibrar Ahmed
 | Ilya Gladyshev
 | Ilya Nenashev
 | Isaac Morland
 | Israel Barth Rubio
 | Jacob Champion
 | Jacob Speidel
 | Jaime Casanova
 | Jakub Wartak
 | James Coleman
 | James Inform
 | James Vanns
 | Jan Wieck
 | Japin Li
 | Jeevan Ladhe
 | Jeff Davis
 | Jeff Janes
 | Jehan-Guillaume de Rorthais
 | Jelte Fennema
 | Jian He
 | Jim Jones
 | Jinbao Chen
 | Joe Conway
 | Joel Jacobson
 | John Naylor
 | Jonathan Katz
 | Josef Simanek
 | Joseph Koshakow
 | Juan Jose Santamaria Flecha
 | Julien Rouhaud
 | Julien Roze
 | Junwang Zhao
 | Justin Pryzby
 | Justin Zhang
 | Karina Litskevich
 | Karl O. Pinc
 | Keisuke Kuroda
 | Ken Kato
 | Kevin McKibbin
 | Kieran McCusker
 | Kirk Wolak
 | Konstantin Knizhnik
 | Koshi Shibagaki
 | Kotaro Kawamoto
 | Kui Liu
 | Kyotaro Horiguchi
 | Lakshmi Narayanan Sreethar
 | Laurence Parry
 | Laurenz Albe
 | Luca Ferrari
 | Lukas Fittl
 | Maciek Sakrejda
 | Magnus Hagander
 | Maja Zaloznik
 | Marcel Hofstetter
 | Marina Polyakova
 | Mark Dilger
 | Marko Tiikkaja
 | Markus Winand
 | Martijn van Oosterhout
 | Martin Jurca
 | Martin Kalcher
 | Mary Xu
 | Masahiko Sawada
 | Masahiro Ikeda
 | Masao Fujii
 | Mason Sharp
 | Matheus Alcantara
 | Mats Kindahl
 | Matthias van de Meent
 | Matthijs van der Vleuten
 | Maxim Orlov
 | Maxim Yablokov
 | Mehmet Emin Karakas
 | Melanie Plageman
 | Melih Mutlu
 | Micah Gates
 | Michael Banck
 | Michael Paquier
 | Michail Nikolaev
 | Michel Pelletier
 | Mike Oh
 | Mikhail Gribkov
 | Mingli Zhang
 | Miroslav Bendik
 | Mitsuru Hinata
 | Myo Wai Thant
 | Naeem Akhter
 | Naoki Okano
 | Nathan Bossart
 | Nazir Bilal Yavuz
 | Neha Sharma
 | Nick Babadzhanian
 | Nicola Contu
 | Nikhil Shetty
 | Nikita Glukhov
 | Nikolay Samokhvalov
 | Nikolay Shaplov
 | Nishant Sharma
 | Nitin Jadhav
 | Noah Misch
 | Noboru Saito
 | Noriyoshi Shinoda
 | Nuko Yokohama
 | Oleg Bartunov
 | Oleg Tselebrovskiy
 | Olly Betts
 | Onder Kalaci
 | Onur Tirtir
 | Pablo Federico
 | Palle Girgensohn
 | Paul Guo
 | Paul Jungwirth
 | Paul Ramsey
 | Pavel Borisov
 | Pavel Kulakov
 | Pavel Luzanov
 | Pavel Stehule
 | Peifeng Qiu
 | Peter Eisentraut
 | Peter Geoghegan
 | Peter Smith
 | Phil Florent
 | Philippe Godfrin
 | Platon Pronko
 | Przemyslaw Sztoch
 | Rachel Heaton
 | Ranier Vilela
 | Regina Obe
 | Reid Thompson
 | Reiner Peterke
 | Richard Guo
 | Riivo Kolka
 | Rishu Bagga
 | Robert Haas
 | Robert Sjoblom
 | Robert Treat
 | Roberto Mello
 | Robins Tharakan
 | Roman Zharkov
 | Ronan Dunklau
 | Rushabh Lathia
 | Ryo Matsumura
 | Samay Sharma
 | Sami Imseih
 | Sandeep Thakkar
 | Sandro Santilli
 | Sebastien Flaesch
 | Sebastien Lardiere
 | Sehrope Sarkuni
 | Sergey Belyashov
 | Sergey Pankov
 | Sergey Shinderuk
 | Shi Yu
 | Shinya Kato
 | Sho Kato
 | Shruthi Gowda
 | Shveta Mallik
 | Simon Riggs
 | Sindy Senorita
 | Sirisha Chamarthi
 | Sravan Kumar
 | Stephane Tachoires
 | Stephen Frost
 | Steve Chavez
 | Stone Tickle
 | Sven Klemm
 | Takamichi Osumi
 | Takeshi Ideriha
 | Tatsuhiro Nakamori
 | Tatsuo Ishii
 | Teja Mupparti
 | Tender Wang
 | Teodor Sigaev
 | Thiago Nunes
 | Thom Brown
 | Thomas Habets
 | Thomas Mc Kay
 | Thomas Munro
 | Tim Carey-Smith
 | Tim Field
 | Timo Stolz
 | Tom Lane
 | Tomas Vondra
 | Tor Erik Linnerud
 | Torsten Fortsch
 | Tristan Partin
 | Troy Frericks
 | Tushar Ahuja
 | Valerie Woolard
 | Vibhor Kumar
 | Victor Spirin
 | Victoria Shepard
 | Vignesh C
 | Vik Fearing
 | Vitaly Burovoy
 | Vitaly Davydov
 | Wang Wei
 | Wenjing Zeng
 | Whale Song
 | Will Mortensen
 | Wolfgang Walther
 | Xin Wen
 | Xing Guo
 | Xingwang Xu
 | XueJing Zhao
 | Yanliang Lei
 | Youmiu Mo
 | Yugo Nagata
 | Yura Sokolov
 | Yuta Katsuragi
 | Zhen Mingyang
 | Zheng Li
 | Zhihong Yu
 | Zhijie Hou
 | Zongliang Quan
 | Zuming Jiang

 | Prev | Up | Next
 | E.15. Release 16.1  | Home |  E.17. Prior Releases

 Return to the Release Notes Archive 
 Next 

## Versions
 16.15 
 16.14 
 16.13 
 16.12 
 16.11 
 16.10 
 16.9 
 16.8 
 16.7 
 16.6 
 16.5 
 16.4 
 16.3 
 16.2 
 16.1 
 16.0

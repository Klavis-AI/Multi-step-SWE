SELECT pg_current_wal_lsn() IS NOT NULL AS have_current_wal_lsn;
SELECT pg_current_wal_insert_lsn() IS NOT NULL AS have_insert_lsn;
SELECT pg_is_in_recovery();
SELECT pg_walfile_name('0/16B2180');

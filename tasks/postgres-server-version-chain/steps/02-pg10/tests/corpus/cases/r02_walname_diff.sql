SELECT pg_wal_lsn_diff('0/10'::pg_lsn, '0/1'::pg_lsn);
SELECT pg_wal_lsn_diff('0/1'::pg_lsn, '0/10'::pg_lsn);
SELECT '0/10'::pg_lsn - '0/1'::pg_lsn;
SELECT pg_walfile_name_offset('0/16B2180');
SELECT pg_xlog_location_diff('0/10'::pg_lsn, '0/1'::pg_lsn);

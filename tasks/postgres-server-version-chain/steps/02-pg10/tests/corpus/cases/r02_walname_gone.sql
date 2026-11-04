SELECT pg_current_xlog_location() IS NOT NULL AS have_xlog_location;
SELECT pg_current_xlog_insert_location() IS NOT NULL AS have_xlog_insert_location;
SELECT pg_xlogfile_name('0/16B2180');
SELECT pg_xlog_location_diff('0/20'::pg_lsn, '0/2'::pg_lsn);
SELECT pg_last_xlog_receive_location() IS NULL AS receive_location_null;

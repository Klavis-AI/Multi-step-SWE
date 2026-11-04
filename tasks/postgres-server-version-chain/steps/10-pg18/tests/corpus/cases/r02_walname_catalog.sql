SELECT proname FROM pg_proc WHERE proname IN
  ('pg_current_wal_lsn','pg_current_wal_insert_lsn','pg_current_wal_flush_lsn',
   'pg_switch_wal','pg_walfile_name','pg_wal_lsn_diff','pg_last_wal_receive_lsn',
   'pg_last_wal_replay_lsn','pg_is_wal_replay_paused') ORDER BY proname;
SELECT proname FROM pg_proc WHERE proname IN
  ('pg_current_xlog_location','pg_current_xlog_insert_location','pg_switch_xlog',
   'pg_xlogfile_name','pg_xlog_location_diff','pg_last_xlog_receive_location',
   'pg_last_xlog_replay_location','pg_is_xlog_replay_paused') ORDER BY proname;
SELECT count(*) FROM pg_proc WHERE proname LIKE 'pg%xlog%';

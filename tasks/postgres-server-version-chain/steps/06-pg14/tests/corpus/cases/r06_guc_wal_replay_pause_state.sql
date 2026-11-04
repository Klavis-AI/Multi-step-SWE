SELECT pg_get_wal_replay_pause_state();
SELECT pg_is_in_recovery();
SELECT count(*) FROM pg_proc WHERE proname='pg_get_wal_replay_pause_state';
SELECT proname, prorettype::regtype::text FROM pg_proc
  WHERE proname IN ('pg_get_wal_replay_pause_state','pg_is_wal_replay_paused') ORDER BY proname;

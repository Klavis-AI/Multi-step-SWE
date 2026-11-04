SELECT count(*) FROM pg_attribute
  WHERE attrelid='pg_stat_activity'::regclass AND attname='leader_pid';
SELECT count(*) FROM pg_attribute
  WHERE attrelid='pg_stat_progress_analyze'::regclass AND attname='phase';
SELECT attname FROM pg_attribute
  WHERE attrelid='pg_stat_progress_basebackup'::regclass AND attnum>0 ORDER BY attnum;

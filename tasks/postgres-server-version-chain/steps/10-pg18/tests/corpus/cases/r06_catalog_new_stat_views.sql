SELECT to_regclass('pg_stat_wal') IS NOT NULL AS wal,
       to_regclass('pg_stat_progress_copy') IS NOT NULL AS copy_prog,
       to_regclass('pg_backend_memory_contexts') IS NOT NULL AS memctx,
       to_regclass('pg_stat_replication_slots') IS NOT NULL AS replslots;
SELECT count(*) FROM pg_stat_progress_copy;
SELECT count(*) FROM pg_stat_replication_slots;
SELECT attname FROM pg_attribute WHERE attrelid='pg_stat_wal'::regclass AND attnum>0
  ORDER BY attnum;

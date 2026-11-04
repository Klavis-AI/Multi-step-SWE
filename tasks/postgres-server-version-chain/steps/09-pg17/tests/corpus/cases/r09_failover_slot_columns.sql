SELECT attname FROM pg_attribute WHERE attrelid = 'pg_replication_slots'::regclass AND attnum > 0 ORDER BY attname;
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_replication_slots'::regclass AND attname IN ('failover','synced');
SELECT column_name FROM information_schema.columns WHERE table_name = 'pg_replication_slots'
  AND column_name IN ('failover','synced','conflicting','invalidation_reason','inactive_since') ORDER BY column_name;

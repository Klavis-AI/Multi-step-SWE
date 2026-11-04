SELECT attname FROM pg_attribute
  WHERE attrelid = 'pg_replication_slots'::regclass AND attnum > 0 ORDER BY attnum;
SELECT count(*) FROM pg_proc WHERE proname IN
  ('pg_create_logical_replication_slot','pg_create_physical_replication_slot',
   'pg_drop_replication_slot','pg_replication_slot_advance') ORDER BY 1;
SELECT proname FROM pg_proc WHERE proname = 'pg_logical_slot_get_changes';

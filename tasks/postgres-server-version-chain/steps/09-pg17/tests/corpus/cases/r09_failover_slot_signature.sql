SELECT proname, pg_get_function_arguments(oid) FROM pg_proc
  WHERE proname = 'pg_create_logical_replication_slot' ORDER BY 1;
SELECT count(*) FROM pg_proc WHERE proname = 'pg_sync_replication_slots';
SELECT proname FROM pg_proc WHERE proname LIKE 'pg_%replication_slot%' ORDER BY proname;

SELECT proname, pronargs, proretset, prorettype::regtype::text FROM pg_proc
  WHERE proname LIKE 'pg\_backup\_%' ORDER BY proname, pronargs;
SELECT proname, proargtypes::regtype[]::text[] AS argtypes FROM pg_proc
  WHERE proname LIKE 'pg\_backup\_%' ORDER BY proname, pronargs;
SELECT count(*) FROM pg_proc WHERE proname IN ('pg_is_in_backup','pg_backup_start_time');

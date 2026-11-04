SELECT proname FROM pg_proc
  WHERE proname IN ('pg_start_backup','pg_stop_backup','pg_backup_start','pg_backup_stop')
  ORDER BY proname, pronargs;
SELECT count(*) FROM pg_proc WHERE proname IN ('pg_start_backup','pg_stop_backup');
SELECT count(*) FROM pg_proc WHERE proname IN ('pg_backup_start','pg_backup_stop');

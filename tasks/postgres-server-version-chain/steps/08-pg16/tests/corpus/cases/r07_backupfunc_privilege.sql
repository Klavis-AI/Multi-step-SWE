SELECT has_function_privilege('pg_backup_start(text,boolean)','EXECUTE');
SELECT has_function_privilege('pg_backup_stop(boolean)','EXECUTE');
SELECT has_function_privilege('pg_start_backup(text,boolean,boolean)','EXECUTE');
SELECT count(*) FROM pg_proc WHERE proname LIKE 'pg\_back%';

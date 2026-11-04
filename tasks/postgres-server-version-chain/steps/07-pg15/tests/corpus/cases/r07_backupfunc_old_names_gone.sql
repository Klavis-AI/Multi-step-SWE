SELECT pg_is_in_backup();
SELECT pg_start_backup('x07_53label', true, false, 'extra');
SELECT pg_stop_backup('x07_53label');
SELECT pg_backup_stop('x07_53label');
SELECT to_regprocedure('pg_start_backup(text,boolean,boolean)') IS NULL AS old_start_gone,
       to_regprocedure('pg_backup_start(text,boolean)') IS NULL AS new_start_missing;

SELECT rolname FROM pg_roles WHERE rolname LIKE 'pg\_%' ORDER BY rolname;
SELECT count(*) FROM pg_roles WHERE rolname IN ('pg_read_all_data','pg_write_all_data');
SELECT rolname, rolcanlogin, rolsuper FROM pg_roles
  WHERE rolname IN ('pg_read_all_data','pg_monitor') ORDER BY rolname;

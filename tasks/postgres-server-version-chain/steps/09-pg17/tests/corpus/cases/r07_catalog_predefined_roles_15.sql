SELECT rolname FROM pg_roles WHERE rolname LIKE 'pg\_%' ORDER BY rolname;
SELECT count(*) FROM pg_roles WHERE rolname = 'pg_checkpoint';
SELECT rolname, rolsuper, rolcanlogin FROM pg_roles
  WHERE rolname IN ('pg_checkpoint','pg_database_owner') ORDER BY rolname;

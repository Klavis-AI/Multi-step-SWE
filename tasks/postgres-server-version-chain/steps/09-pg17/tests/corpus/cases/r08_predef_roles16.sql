SELECT rolname FROM pg_roles
  WHERE rolname IN ('pg_create_subscription','pg_use_reserved_connections','pg_maintain','pg_monitor')
  ORDER BY rolname;
SELECT rolname, rolcanlogin, rolsuper FROM pg_roles WHERE rolname = 'pg_create_subscription';
SELECT count(*) FROM pg_roles WHERE rolname LIKE 'pg\_%';

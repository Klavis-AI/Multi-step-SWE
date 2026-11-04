SELECT a.privilege_type FROM pg_namespace n, aclexplode(n.nspacl) a
  WHERE n.nspname = 'public' AND a.grantee = 0 ORDER BY a.privilege_type;
SELECT count(*) FROM information_schema.schemata WHERE schema_name='public';
SELECT nspname, pg_get_userbyid(nspowner) = 'pg_database_owner' AS owned_by_dbowner
  FROM pg_namespace WHERE nspname='public';

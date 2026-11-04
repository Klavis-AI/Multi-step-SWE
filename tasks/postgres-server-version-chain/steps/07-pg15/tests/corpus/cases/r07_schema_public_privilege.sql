SELECT has_schema_privilege('public','public','CREATE') AS public_create,
       has_schema_privilege('public','public','USAGE') AS public_usage;
SELECT has_schema_privilege(current_user,'public','CREATE') AS me_create;
SELECT nspname, nspacl IS NOT NULL AS has_acl FROM pg_namespace WHERE nspname='public';

SELECT attname FROM pg_attribute WHERE attrelid = 'pg_auth_members'::regclass AND attnum > 0 ORDER BY attname;
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_auth_members'::regclass AND attname IN ('inherit_option','set_option');
SELECT relkind, relpersistence FROM pg_class WHERE relname = 'pg_auth_members';

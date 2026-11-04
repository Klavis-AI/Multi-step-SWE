SELECT attname FROM pg_attribute WHERE attrelid = 'pg_subscription'::regclass AND attnum > 0 ORDER BY attname;
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_subscription'::regclass AND attname = 'suborigin';
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_subscription'::regclass AND attname IN ('subpasswordrequired','subrunasowner');
SELECT relname FROM pg_class WHERE relname IN ('pg_subscription','pg_subscription_rel') ORDER BY relname;

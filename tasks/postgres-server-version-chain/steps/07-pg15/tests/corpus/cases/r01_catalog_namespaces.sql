SELECT nspname FROM pg_namespace WHERE nspname IN ('public','pg_catalog','information_schema') ORDER BY 1;
SELECT count(*) > 0 FROM pg_class WHERE relname = 'pg_class';
SELECT current_database() = current_database(), current_schema();
SELECT count(*) FROM information_schema.schemata WHERE schema_name IN ('public','information_schema');

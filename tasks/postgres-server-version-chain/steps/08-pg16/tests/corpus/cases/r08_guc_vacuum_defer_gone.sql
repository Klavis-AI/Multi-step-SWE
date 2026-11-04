SHOW vacuum_defer_cleanup_age;
SELECT count(*) FROM pg_settings WHERE name = 'vacuum_defer_cleanup_age';
SELECT current_setting('vacuum_defer_cleanup_age', true) IS NULL;
SET vacuum_defer_cleanup_age = 5;

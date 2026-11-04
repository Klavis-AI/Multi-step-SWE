SELECT count(*) FROM pg_attribute WHERE attrelid='pg_database'::regclass
  AND attname IN ('datlocprovider','daticulocale','datcollversion');
SELECT attname FROM pg_attribute WHERE attrelid='pg_database'::regclass
  AND attnum > 0 AND attname LIKE 'dat%' ORDER BY attname;
SELECT to_regclass('pg_stat_subscription_stats') IS NOT NULL AS substats;

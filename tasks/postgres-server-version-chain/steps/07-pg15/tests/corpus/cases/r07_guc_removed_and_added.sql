SELECT count(*) FROM pg_settings WHERE name IN
  ('stats_temp_directory','operator_precedence_warning','wal_keep_segments');
SELECT name FROM pg_settings WHERE name LIKE 'shared\_memory%' ORDER BY name;
SELECT count(*) FROM pg_settings WHERE name LIKE 'recovery\_%';
SELECT name, context FROM pg_settings WHERE name IN
  ('shared_memory_size_in_huge_pages','shared_memory_size') ORDER BY name;

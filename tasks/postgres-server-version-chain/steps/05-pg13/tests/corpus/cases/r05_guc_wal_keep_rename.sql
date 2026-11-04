SHOW wal_keep_size;
SHOW wal_keep_segments;
SELECT current_setting('wal_keep_size');
SELECT current_setting('wal_keep_segments');
SELECT current_setting('wal_keep_segments', true) IS NULL;

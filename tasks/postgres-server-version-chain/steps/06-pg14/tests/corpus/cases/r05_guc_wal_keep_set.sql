SET wal_keep_size = '16MB';
SET wal_keep_segments = 32;
SET LOCAL wal_keep_size = '16MB';
SELECT set_config('wal_keep_size','16MB',false);
SELECT set_config('wal_keep_segments','32',false);

SHOW old_snapshot_threshold;
SELECT count(*) FROM pg_settings WHERE name = 'old_snapshot_threshold';
SELECT current_setting('old_snapshot_threshold', true) IS NULL;
SET old_snapshot_threshold = 5;

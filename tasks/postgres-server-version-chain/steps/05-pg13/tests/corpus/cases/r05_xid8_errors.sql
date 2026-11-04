SELECT 'bad'::pg_snapshot;
SELECT '20:10:'::pg_snapshot;
SELECT '1'::xid8 + 1;
SELECT pg_snapshot_xmin('10:20:14,15');
SELECT pg_visible_in_snapshot('12','10:20:14,15'::pg_snapshot);

SELECT '10:20:14,15'::pg_snapshot;
SELECT pg_snapshot_xmin('10:20:14,15'::pg_snapshot), pg_snapshot_xmax('10:20:14,15'::pg_snapshot);
SELECT x FROM pg_snapshot_xip('10:20:14,15'::pg_snapshot) x ORDER BY x;
SELECT pg_visible_in_snapshot('12'::xid8,'10:20:14,15'::pg_snapshot),
       pg_visible_in_snapshot('14'::xid8,'10:20:14,15'::pg_snapshot),
       pg_visible_in_snapshot('25'::xid8,'10:20:14,15'::pg_snapshot);
SELECT '10:20:'::pg_snapshot, pg_snapshot_xmin('10:20:'::pg_snapshot);

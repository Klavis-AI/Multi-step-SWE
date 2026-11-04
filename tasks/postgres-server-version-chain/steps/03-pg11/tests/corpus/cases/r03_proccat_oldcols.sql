SELECT proisagg FROM pg_proc WHERE proname = 'count';
SELECT proiswindow FROM pg_proc WHERE proname = 'rank';
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_proc'::regclass
  AND attname IN ('proisagg','proiswindow','prokind');
SELECT attname FROM pg_attribute WHERE attrelid = 'pg_proc'::regclass
  AND attname IN ('proisagg','proiswindow','prokind') ORDER BY attname;

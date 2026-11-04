SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_constraint'::regclass AND attname IN ('conenforced','conperiod');
SELECT attname FROM pg_attribute WHERE attrelid = 'pg_constraint'::regclass AND attnum > 0 ORDER BY attname;
SELECT column_name FROM information_schema.columns WHERE table_name = 'pg_constraint'
  AND column_name IN ('conenforced','conperiod','convalidated','connoinherit') ORDER BY column_name;

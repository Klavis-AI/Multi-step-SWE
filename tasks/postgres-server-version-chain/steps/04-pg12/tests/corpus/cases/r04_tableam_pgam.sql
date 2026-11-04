SELECT amname, amtype FROM pg_am ORDER BY amname;
SELECT amname FROM pg_am WHERE amtype = 't' ORDER BY amname;
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_am'::regclass AND attname = 'amtype';

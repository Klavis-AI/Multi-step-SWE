CREATE SEQUENCE q2_sqc START 3;
SELECT attname, atttypid::regtype::text FROM pg_attribute
  WHERE attrelid = 'q2_sqc'::regclass AND attnum > 0 ORDER BY attnum;
SELECT last_value, is_called FROM q2_sqc;
SELECT nextval('q2_sqc'), nextval('q2_sqc');
SELECT last_value, is_called FROM q2_sqc;
SELECT start_value FROM q2_sqc;
SELECT data_type, numeric_precision FROM information_schema.sequences
  WHERE sequence_name = 'q2_sqc';
DROP SEQUENCE q2_sqc;

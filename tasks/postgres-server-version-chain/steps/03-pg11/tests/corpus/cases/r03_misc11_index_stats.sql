CREATE TABLE q3_mis(a int, b int);
CREATE INDEX q3_mis_e ON q3_mis ((a + b));
SELECT attname, attstattarget FROM pg_attribute WHERE attrelid = 'q3_mis_e'::regclass
  AND attnum > 0 ORDER BY attnum;
ALTER INDEX q3_mis_e ALTER COLUMN 1 SET STATISTICS 42;
SELECT attname, attstattarget FROM pg_attribute WHERE attrelid = 'q3_mis_e'::regclass
  AND attnum > 0 ORDER BY attnum;
ALTER INDEX q3_mis_e ALTER COLUMN 9 SET STATISTICS 42;
DROP TABLE q3_mis;

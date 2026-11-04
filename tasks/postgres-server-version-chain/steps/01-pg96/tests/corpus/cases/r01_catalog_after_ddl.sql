CREATE TABLE cd1(a int);
SELECT count(*) FROM information_schema.columns WHERE table_name='cd1';
ALTER TABLE cd1 ADD COLUMN b text;
SELECT column_name FROM information_schema.columns WHERE table_name='cd1' ORDER BY ordinal_position;
ALTER TABLE cd1 DROP COLUMN a;
SELECT column_name, ordinal_position FROM information_schema.columns WHERE table_name='cd1' ORDER BY ordinal_position;
DROP TABLE cd1;
SELECT count(*) FROM information_schema.columns WHERE table_name='cd1';

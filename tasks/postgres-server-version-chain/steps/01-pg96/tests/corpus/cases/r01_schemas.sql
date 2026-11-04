CREATE SCHEMA sc1;
CREATE TABLE sc1.t1(i int);
INSERT INTO sc1.t1 VALUES (1);
SELECT * FROM sc1.t1;
SELECT table_schema, table_name FROM information_schema.tables WHERE table_schema='sc1';
SELECT * FROM t1;
DROP SCHEMA sc1 CASCADE;
SELECT count(*) FROM information_schema.schemata WHERE schema_name='sc1';

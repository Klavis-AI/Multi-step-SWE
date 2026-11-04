CREATE TABLE q3_pid(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q3_pid_1 PARTITION OF q3_pid FOR VALUES FROM (1) TO (10);
CREATE INDEX q3_pid_b ON q3_pid (b);
DROP INDEX q3_pid_1_b_idx;
ALTER TABLE q3_pid_1 DROP COLUMN b;
DROP INDEX q3_pid_b;
SELECT count(*) FROM pg_class WHERE relname IN ('q3_pid_b','q3_pid_1_b_idx');
DROP TABLE q3_pid;

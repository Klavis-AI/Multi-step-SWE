CREATE TABLE r09tc1(a int, b text);
INSERT INTO r09tc1 VALUES (1,'short');
SELECT a, pg_column_toast_chunk_id(b) IS NULL AS untoasted FROM r09tc1 ORDER BY a;
SELECT count(*) FROM pg_proc WHERE proname = 'pg_column_toast_chunk_id';
DROP TABLE r09tc1;

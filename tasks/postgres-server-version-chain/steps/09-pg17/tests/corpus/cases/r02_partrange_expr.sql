CREATE TABLE q2_pre(a int, b text) PARTITION BY RANGE ((a + 1));
CREATE TABLE q2_pre_1 PARTITION OF q2_pre FOR VALUES FROM (1) TO (10);
INSERT INTO q2_pre VALUES (0,'zero'),(8,'eight');
INSERT INTO q2_pre VALUES (9,'nine');
SELECT a, b FROM q2_pre ORDER BY a;
SELECT pg_get_partkeydef('q2_pre'::regclass);
DROP TABLE q2_pre;

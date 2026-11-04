CREATE TABLE q4_rct(a int primary key, b text);
INSERT INTO q4_rct SELECT g, 'v' || g FROM generate_series(1,30) g;
CREATE INDEX q4_rct_b ON q4_rct (b);
REINDEX TABLE CONCURRENTLY q4_rct;
SELECT indexname FROM pg_indexes WHERE tablename = 'q4_rct' ORDER BY indexname;
SELECT count(*) FROM q4_rct;
INSERT INTO q4_rct VALUES (1,'dup');
DROP TABLE q4_rct;

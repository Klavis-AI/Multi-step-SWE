CREATE TABLE q4_rcp(a int);
INSERT INTO q4_rcp SELECT generate_series(1,20);
CREATE INDEX q4_rcp_a ON q4_rcp (a);
REINDEX INDEX q4_rcp_a;
REINDEX TABLE q4_rcp;
REINDEX INDEX CONCURRENTLY q4_rcp_a;
SELECT count(*) FROM q4_rcp WHERE a = 7;
SELECT indexname FROM pg_indexes WHERE tablename = 'q4_rcp' ORDER BY indexname;
DROP TABLE q4_rcp;

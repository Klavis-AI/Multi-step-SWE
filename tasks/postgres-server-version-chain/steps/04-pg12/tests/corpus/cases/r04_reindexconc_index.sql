CREATE TABLE q4_rci(a int, b text);
INSERT INTO q4_rci SELECT g, 'v' || g FROM generate_series(1,50) g;
CREATE INDEX q4_rci_a ON q4_rci (a);
REINDEX INDEX CONCURRENTLY q4_rci_a;
SELECT indexname FROM pg_indexes WHERE tablename = 'q4_rci' ORDER BY indexname;
SELECT count(*) FROM q4_rci WHERE a > 25;
SELECT indisvalid, indisready FROM pg_index WHERE indexrelid = 'q4_rci_a'::regclass;
DROP TABLE q4_rci;

CREATE TABLE q2_hia(a int, b text);
CREATE INDEX q2_hia_h ON q2_hia USING hash (a);
INSERT INTO q2_hia SELECT g, 'v' || g FROM generate_series(1,50) g;
SELECT a, b FROM q2_hia WHERE a = 17;
SELECT count(*) FROM q2_hia WHERE a > 40;
SELECT indexname FROM pg_indexes WHERE tablename = 'q2_hia' ORDER BY indexname;
DROP TABLE q2_hia;

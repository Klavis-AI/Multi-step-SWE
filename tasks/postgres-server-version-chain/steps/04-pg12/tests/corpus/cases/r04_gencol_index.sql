CREATE TABLE q4_gcn(a int, b text GENERATED ALWAYS AS (upper(a::text)) STORED);
CREATE INDEX q4_gcn_i ON q4_gcn (b);
INSERT INTO q4_gcn(a) SELECT generate_series(1,20);
SELECT a, b FROM q4_gcn WHERE b = '13';
SELECT count(*) FROM q4_gcn WHERE b > '5';
SELECT indexdef FROM pg_indexes WHERE indexname = 'q4_gcn_i';
DROP TABLE q4_gcn;

CREATE TABLE q4_mgv(a int, b int GENERATED ALWAYS AS (a * 2) STORED);
INSERT INTO q4_mgv(a) VALUES (1),(2);
CREATE VIEW q4_mgv_v AS SELECT a, b FROM q4_mgv;
SELECT a, b FROM q4_mgv_v ORDER BY a;
INSERT INTO q4_mgv_v(a) VALUES (3);
SELECT a, b FROM q4_mgv_v ORDER BY a;
UPDATE q4_mgv_v SET b = 1 WHERE a = 1;
DROP VIEW q4_mgv_v;
DROP TABLE q4_mgv;

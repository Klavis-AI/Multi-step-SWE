CREATE TABLE q3_cvb(a int, b int, c text);
CREATE INDEX q3_cvb_i ON q3_cvb (a) INCLUDE (b, c);
INSERT INTO q3_cvb SELECT g, g * 2, 'v' || g FROM generate_series(1,10) g;
SELECT a, b, c FROM q3_cvb WHERE a = 4;
SELECT indexdef FROM pg_indexes WHERE indexname = 'q3_cvb_i';
SELECT count(*) FROM q3_cvb WHERE a > 5;
DROP TABLE q3_cvb;

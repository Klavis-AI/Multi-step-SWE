CREATE TABLE q3_cvp(a int, b int, c text) PARTITION BY RANGE (a);
CREATE TABLE q3_cvp_1 PARTITION OF q3_cvp FOR VALUES FROM (1) TO (10);
CREATE UNIQUE INDEX q3_cvp_u ON q3_cvp (a) INCLUDE (b);
INSERT INTO q3_cvp VALUES (1,1,'x');
INSERT INTO q3_cvp VALUES (1,2,'y');
SELECT indnkeyatts, indnatts FROM pg_index WHERE indexrelid = 'q3_cvp_u'::regclass;
SELECT indexdef FROM pg_indexes WHERE indexname = 'q3_cvp_1_a_b_idx';
DROP TABLE q3_cvp;

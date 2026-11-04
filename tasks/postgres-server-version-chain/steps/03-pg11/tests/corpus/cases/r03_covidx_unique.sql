CREATE TABLE q3_cvu(a int, b int, c text);
CREATE UNIQUE INDEX q3_cvu_i ON q3_cvu (a) INCLUDE (b);
INSERT INTO q3_cvu VALUES (1,10,'x');
INSERT INTO q3_cvu VALUES (1,20,'y');
INSERT INTO q3_cvu VALUES (2,20,'y');
SELECT a, b, c FROM q3_cvu ORDER BY a;
SELECT indisunique, indnkeyatts FROM pg_index WHERE indexrelid = 'q3_cvu_i'::regclass;
DROP TABLE q3_cvu;

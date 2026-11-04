CREATE TABLE q4_gcc(a int, b int GENERATED ALWAYS AS (a * 2) STORED CHECK (b < 100),
                    c int GENERATED ALWAYS AS (a + 1) STORED UNIQUE);
INSERT INTO q4_gcc(a) VALUES (1);
INSERT INTO q4_gcc(a) VALUES (1);
INSERT INTO q4_gcc(a) VALUES (60);
INSERT INTO q4_gcc(a) VALUES (2);
SELECT a, b, c FROM q4_gcc ORDER BY a;
DROP TABLE q4_gcc;

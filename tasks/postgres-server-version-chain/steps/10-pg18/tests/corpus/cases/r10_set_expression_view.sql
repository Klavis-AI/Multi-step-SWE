CREATE TABLE r10se5(a int, b int GENERATED ALWAYS AS (a + 1) VIRTUAL);
INSERT INTO r10se5(a) VALUES (1),(2);
CREATE VIEW r10se5v AS SELECT a, b FROM r10se5;
SELECT a, b FROM r10se5v ORDER BY a;
ALTER TABLE r10se5 ALTER COLUMN b SET EXPRESSION AS (a + 100);
SELECT a, b FROM r10se5v ORDER BY a;
SELECT column_name, generation_expression FROM information_schema.columns WHERE table_name = 'r10se5' ORDER BY ordinal_position;
DROP VIEW r10se5v;
DROP TABLE r10se5;

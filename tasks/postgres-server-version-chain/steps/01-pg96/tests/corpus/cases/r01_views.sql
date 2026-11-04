CREATE TABLE vb(i int, t text);
INSERT INTO vb VALUES (1,'a'),(2,'b');
CREATE VIEW vv AS SELECT i, upper(t) AS ut FROM vb WHERE i>0;
SELECT * FROM vv ORDER BY i;
CREATE OR REPLACE VIEW vv AS SELECT i, lower(t) AS ut FROM vb;
SELECT * FROM vv ORDER BY i;
SELECT table_name, view_definition IS NOT NULL FROM information_schema.views WHERE table_name='vv';
DROP VIEW vv;
DROP TABLE vb;

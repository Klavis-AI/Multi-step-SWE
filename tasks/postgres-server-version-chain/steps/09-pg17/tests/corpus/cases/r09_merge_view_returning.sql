CREATE TABLE r09mv2_b(id int primary key, v int);
INSERT INTO r09mv2_b VALUES (1,1),(2,2);
CREATE VIEW r09mv2_v AS SELECT id, v FROM r09mv2_b;
MERGE INTO r09mv2_v t USING (VALUES (2)) s(id) ON t.id = s.id
  WHEN MATCHED THEN DELETE RETURNING merge_action(), t.id;
SELECT id, v FROM r09mv2_b ORDER BY id;
DROP VIEW r09mv2_v;
DROP TABLE r09mv2_b;

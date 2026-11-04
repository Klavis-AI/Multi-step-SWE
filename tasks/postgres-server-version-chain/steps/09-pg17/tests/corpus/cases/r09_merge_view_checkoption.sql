CREATE TABLE r09mv3_b(id int primary key, v int);
INSERT INTO r09mv3_b VALUES (1,5);
CREATE VIEW r09mv3_v AS SELECT id, v FROM r09mv3_b WHERE v > 3 WITH CHECK OPTION;
MERGE INTO r09mv3_v t USING (VALUES (1,1)) s(id,v) ON t.id = s.id
  WHEN MATCHED THEN UPDATE SET v = s.v;
MERGE INTO r09mv3_v t USING (VALUES (2,2)) s(id,v) ON t.id = s.id
  WHEN NOT MATCHED THEN INSERT VALUES (s.id, s.v);
SELECT id, v FROM r09mv3_b ORDER BY id;
DROP VIEW r09mv3_v;
DROP TABLE r09mv3_b;

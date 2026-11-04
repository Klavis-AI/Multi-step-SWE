CREATE TABLE r09ms4_t(id int primary key, v int);
INSERT INTO r09ms4_t VALUES (1,10);
MERGE INTO r09ms4_t t USING (VALUES (2,22)) s(id,v) ON t.id = s.id
  WHEN NOT MATCHED BY TARGET THEN INSERT VALUES (s.id, s.v);
SELECT id, v FROM r09ms4_t ORDER BY id;
MERGE INTO r09ms4_t t USING (VALUES (3,33)) s(id,v) ON t.id = s.id
  WHEN NOT MATCHED THEN INSERT VALUES (s.id, s.v);
SELECT id, v FROM r09ms4_t ORDER BY id;
DROP TABLE r09ms4_t;

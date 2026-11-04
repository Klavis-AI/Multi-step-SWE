CREATE TABLE r09mr3_t(id int primary key, v int);
INSERT INTO r09mr3_t VALUES (1,10);
MERGE INTO r09mr3_t t USING (VALUES (1)) s(id) ON t.id = s.id
  WHEN MATCHED THEN DO NOTHING RETURNING merge_action(), t.id;
MERGE INTO r09mr3_t t USING (VALUES (9)) s(id) ON t.id = s.id
  WHEN NOT MATCHED THEN DO NOTHING RETURNING merge_action();
SELECT id, v FROM r09mr3_t ORDER BY id;
DROP TABLE r09mr3_t;

CREATE TABLE r09ms2_t(id int primary key, v int);
INSERT INTO r09ms2_t VALUES (1,10),(2,20),(3,30);
WITH m AS (
  MERGE INTO r09ms2_t t USING (VALUES (2)) s(id) ON t.id = s.id
    WHEN MATCHED THEN UPDATE SET v = -1
    WHEN NOT MATCHED BY SOURCE THEN UPDATE SET v = 0
    RETURNING merge_action() AS act, t.id AS id, t.v AS v
) SELECT act, id, v FROM m ORDER BY id;
SELECT id, v FROM r09ms2_t ORDER BY id;
DROP TABLE r09ms2_t;

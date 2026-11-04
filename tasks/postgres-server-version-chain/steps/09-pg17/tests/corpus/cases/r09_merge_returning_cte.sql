CREATE TABLE r09mr6_t(id int primary key, v int);
INSERT INTO r09mr6_t VALUES (1,10),(2,20);
WITH m AS (
  MERGE INTO r09mr6_t t USING (VALUES (1,100),(3,300)) s(id,v) ON t.id = s.id
    WHEN MATCHED THEN UPDATE SET v = s.v
    WHEN NOT MATCHED THEN INSERT VALUES (s.id, s.v)
    RETURNING merge_action() AS act, t.id AS id
) SELECT act, id FROM m ORDER BY id;
SELECT id, v FROM r09mr6_t ORDER BY id;
DROP TABLE r09mr6_t;

CREATE TABLE r10ro5(id int primary key, v int);
INSERT INTO r10ro5 VALUES (1,10),(2,20);
WITH m AS (
  MERGE INTO r10ro5 t USING (VALUES (1,100),(3,300)) s(id,v) ON t.id = s.id
    WHEN MATCHED THEN UPDATE SET v = s.v
    WHEN NOT MATCHED THEN INSERT VALUES (s.id, s.v)
    RETURNING merge_action() AS act, t.id AS id, old.v AS ov, new.v AS nv
) SELECT act, id, ov, nv FROM m ORDER BY id;
MERGE INTO r10ro5 t USING (VALUES (2)) s(id) ON t.id = s.id
  WHEN MATCHED THEN DELETE RETURNING merge_action(), old.v, new.v;
SELECT id, v FROM r10ro5 ORDER BY id;
DROP TABLE r10ro5;

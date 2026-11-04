CREATE TABLE r10ra5(id int primary key, v int);
INSERT INTO r10ra5 VALUES (1,10),(2,20);
WITH m AS (
  MERGE INTO r10ra5 t USING (VALUES (1,111),(3,333)) s(id,v) ON t.id = s.id
    WHEN MATCHED THEN UPDATE SET v = s.v
    WHEN NOT MATCHED THEN INSERT VALUES (s.id, s.v)
    RETURNING WITH (OLD AS o, NEW AS n) merge_action() AS act, t.id AS id, o.v AS before, n.v AS after
) SELECT act, id, before, after FROM m ORDER BY id;
SELECT id, v FROM r10ra5 ORDER BY id;
DROP TABLE r10ra5;

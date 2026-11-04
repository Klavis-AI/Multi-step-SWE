CREATE TABLE r09mr2_t(id int primary key, v int);
CREATE TABLE r09mr2_s(id int);
INSERT INTO r09mr2_t VALUES (1,10),(2,20);
INSERT INTO r09mr2_s VALUES (1),(3);
WITH m AS (
  MERGE INTO r09mr2_t t USING r09mr2_s s ON t.id = s.id
    WHEN MATCHED THEN DELETE
    WHEN NOT MATCHED THEN INSERT VALUES (s.id, -1)
    RETURNING merge_action() AS act, t.id AS tid, s.id AS sid
) SELECT act, tid, sid FROM m ORDER BY sid;
SELECT id, v FROM r09mr2_t ORDER BY id;
DROP TABLE r09mr2_t, r09mr2_s;

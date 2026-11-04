SELECT merge_action();
CREATE TABLE r09mr5_t(id int primary key, v int);
INSERT INTO r09mr5_t VALUES (1,1);
SELECT merge_action() FROM r09mr5_t;
MERGE INTO r09mr5_t t USING (VALUES (1)) s(id) ON t.id = s.id
  WHEN MATCHED AND merge_action() = 'UPDATE' THEN UPDATE SET v = 2;
UPDATE r09mr5_t SET v = 3 RETURNING merge_action();
DROP TABLE r09mr5_t;

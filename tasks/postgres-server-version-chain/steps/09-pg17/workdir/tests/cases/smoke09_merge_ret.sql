CREATE TABLE smr(i int primary key, v int);
MERGE INTO smr t USING (VALUES (1,1)) s(i,v) ON t.i=s.i
  WHEN NOT MATCHED THEN INSERT VALUES (s.i,s.v)
  RETURNING merge_action(), t.i;
DROP TABLE smr;

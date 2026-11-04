CREATE TABLE q2_pln(a int) PARTITION BY LIST (a);
CREATE TABLE q2_pln_12 PARTITION OF q2_pln FOR VALUES IN (1,2);
CREATE TABLE q2_pln_nul PARTITION OF q2_pln FOR VALUES IN (NULL);
INSERT INTO q2_pln VALUES (1),(2),(NULL);
SELECT tableoid::regclass::text AS part, a FROM q2_pln ORDER BY a NULLS LAST;
SELECT count(*) FROM q2_pln WHERE a IS NULL;
SELECT attname, attnotnull FROM pg_attribute
  WHERE attrelid = 'q2_pln'::regclass AND attnum > 0 ORDER BY attnum;
DROP TABLE q2_pln;

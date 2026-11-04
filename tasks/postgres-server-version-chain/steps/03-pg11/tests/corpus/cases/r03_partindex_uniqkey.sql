CREATE TABLE q3_pik(a int, b int) PARTITION BY RANGE (a);
CREATE UNIQUE INDEX q3_pik_u ON q3_pik (b);
ALTER TABLE q3_pik ADD UNIQUE (b);
ALTER TABLE q3_pik ADD UNIQUE (a, b);
SELECT conname, contype FROM pg_constraint WHERE conrelid = 'q3_pik'::regclass ORDER BY conname;
DROP TABLE q3_pik;

CREATE SEQUENCE q2_sqa START 5 INCREMENT 2 MINVALUE 1 MAXVALUE 99 CACHE 1;
SELECT seqstart, seqincrement, seqmax, seqmin, seqcache, seqcycle
  FROM pg_sequence WHERE seqrelid = 'q2_sqa'::regclass;
SELECT seqtypid::regtype::text FROM pg_sequence WHERE seqrelid = 'q2_sqa'::regclass;
ALTER SEQUENCE q2_sqa INCREMENT 3 CYCLE;
SELECT seqincrement, seqcycle FROM pg_sequence WHERE seqrelid = 'q2_sqa'::regclass;
DROP SEQUENCE q2_sqa;

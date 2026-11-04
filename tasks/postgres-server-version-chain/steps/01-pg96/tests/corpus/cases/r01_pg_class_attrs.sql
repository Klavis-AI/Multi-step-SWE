CREATE TABLE ct1(a int primary key, b text not null default 'x', c numeric(5,2), d timestamptz);
CREATE INDEX ct1_b_idx ON ct1(b);
SELECT relname, relkind FROM pg_class WHERE relname LIKE 'ct1%' ORDER BY relname;
SELECT attname, atttypid::regtype::text, attnotnull, atthasdef, attnum
  FROM pg_attribute WHERE attrelid='ct1'::regclass AND attnum>0 ORDER BY attnum;
SELECT conname, contype FROM pg_constraint WHERE conrelid='ct1'::regclass AND contype IN ('p','u','f','c') ORDER BY conname;
SELECT c.relname, n.nspname FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace
  WHERE c.relname='ct1';
DROP TABLE ct1;

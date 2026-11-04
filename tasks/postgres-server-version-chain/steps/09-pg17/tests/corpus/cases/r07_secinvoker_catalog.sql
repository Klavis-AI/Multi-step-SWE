CREATE TABLE x07_35a(a int);
CREATE VIEW x07_35v WITH (security_invoker = true, security_barrier = true)
  AS SELECT * FROM x07_35a;
SELECT unnest(reloptions) AS opt FROM pg_class WHERE relname='x07_35v' ORDER BY opt;
SELECT relname, relkind FROM pg_class WHERE relname='x07_35v';
SELECT pg_get_viewdef('x07_35v'::regclass, true);
DROP VIEW x07_35v;
DROP TABLE x07_35a;

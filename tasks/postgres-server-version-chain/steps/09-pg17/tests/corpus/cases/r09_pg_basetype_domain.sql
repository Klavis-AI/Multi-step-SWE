CREATE DOMAIN r09d1 AS int CHECK (VALUE > 0);
CREATE DOMAIN r09d2 AS r09d1;
SELECT pg_basetype('r09d1'::regtype), pg_basetype('r09d2'::regtype), pg_basetype('int'::regtype);
SELECT pg_basetype('text'::regtype);
DROP DOMAIN r09d2;
DROP DOMAIN r09d1;

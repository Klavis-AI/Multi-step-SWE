SELECT sum(i), avg(i) FROM (VALUES (interval '1 day'),(interval '2 hours')) v(i);
SELECT sum(i), avg(i) FROM (VALUES (interval '1 day'),(NULL::interval)) v(i);
SELECT avg(i) FROM (VALUES (interval '1 day')) v(i) WHERE false;
SELECT sum(i) FROM (VALUES (interval '2147483647 days'),(interval '1 day')) v(i);

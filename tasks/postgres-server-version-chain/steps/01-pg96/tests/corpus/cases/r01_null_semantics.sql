SELECT NULL = NULL, NULL <> NULL, NULL IS NULL;
SELECT 1 + NULL, 'a' || NULL, NULL::int + NULL::int;
SELECT count(*) FROM (VALUES (NULL),(1)) v(x) WHERE x = 1;
SELECT sum(x), count(x), count(*) FROM (VALUES (NULL::int),(1)) v(x);
SELECT x IS NULL, x IS NOT NULL FROM (VALUES (NULL::int)) v(x);
SELECT NULL::text = '', NULL::text IS NULL;
SELECT array_agg(x) FROM (VALUES (NULL::int),(1)) v(x);

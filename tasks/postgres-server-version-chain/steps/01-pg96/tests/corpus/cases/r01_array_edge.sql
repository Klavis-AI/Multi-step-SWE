SELECT '{}'::int[], array_length('{}'::int[],1), cardinality('{}'::int[]);
SELECT '{a,"b c",NULL}'::text[], '{"x,y"}'::text[];
SELECT ARRAY[NULL,1], ARRAY[1,NULL] @> ARRAY[1];
SELECT ARRAY[1,2] @> ARRAY[2], ARRAY[2] <@ ARRAY[1,2], ARRAY[1,2] && ARRAY[2,3];
SELECT 2 = ANY(ARRAY[1,2]), 5 = ALL(ARRAY[5,5]), 2 <> ALL(ARRAY[1,3]);
SELECT array_agg(x ORDER BY x DESC) FROM (VALUES (3),(1),(2)) v(x);

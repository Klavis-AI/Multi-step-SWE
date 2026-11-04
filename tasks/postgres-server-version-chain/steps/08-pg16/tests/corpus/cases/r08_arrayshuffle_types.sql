SELECT cardinality(array_shuffle(ARRAY['a','b','c']));
SELECT cardinality(array_shuffle(ARRAY[1.5,2.5]::numeric[]));
SELECT cardinality(array_shuffle(ARRAY[NULL,1,NULL]::int[]));
SELECT pg_typeof(array_shuffle(ARRAY[1,2])), pg_typeof(array_sample(ARRAY['a'],1));

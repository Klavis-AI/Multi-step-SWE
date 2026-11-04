SELECT cardinality(array_sample(ARRAY[1,2,3,4,5],3));
SELECT cardinality(array_sample(ARRAY[1,2,3,4,5],5));
SELECT cardinality(array_sample(ARRAY['a','b','c'],2));
SELECT array_sample(NULL::int[],1) IS NULL;

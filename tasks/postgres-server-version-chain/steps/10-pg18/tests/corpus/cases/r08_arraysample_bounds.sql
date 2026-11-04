SELECT array_sample(ARRAY[1,2,3],0), cardinality(array_sample(ARRAY[1,2,3],0));
SELECT cardinality(array_sample(ARRAY[]::int[],0));
SELECT cardinality(array_sample(ARRAY[9],1));
SELECT array_sample(ARRAY[9],1);

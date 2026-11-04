SELECT cardinality(array_shuffle(ARRAY[1,2,3,4,5]));
SELECT array_length(array_shuffle(ARRAY[1,2,3]),1);
SELECT array_shuffle(ARRAY[]::int[]);
SELECT array_shuffle(NULL::int[]) IS NULL;
SELECT cardinality(array_shuffle(ARRAY[1,2,3] || ARRAY[4,5]));

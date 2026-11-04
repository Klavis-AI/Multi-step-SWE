SELECT array_sort('{}'::int[]), array_sort(ARRAY[1]);
SELECT array_sort(NULL::int[]) IS NULL;
SELECT array_sort(ARRAY[[3,4],[1,2]]);
SELECT array_sort(ARRAY[1.5,0.5]::numeric[], false);
SELECT pg_typeof(array_sort(ARRAY[1,2])), pg_typeof(array_reverse(ARRAY['a']));

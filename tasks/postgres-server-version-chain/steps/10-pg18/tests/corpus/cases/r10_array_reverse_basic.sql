SELECT array_reverse(ARRAY[1,2,3]);
SELECT array_reverse(ARRAY['a','b']);
SELECT array_reverse('{}'::int[]), array_reverse(ARRAY[1]);
SELECT array_reverse(NULL::int[]) IS NULL;
SELECT array_reverse(ARRAY[[1,2],[3,4],[5,6]]);

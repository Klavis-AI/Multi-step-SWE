SELECT array_sort(ARRAY[3,1,2]);
SELECT array_sort(ARRAY[3,1,2], false);
SELECT array_sort(ARRAY['b','a','c'], true, true);
SELECT array_sort(ARRAY[2,NULL,1]);
SELECT array_sort(ARRAY[2,NULL,1], true, false);

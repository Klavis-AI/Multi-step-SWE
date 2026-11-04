SELECT trim_array(ARRAY[1,2,3,4], 2), trim_array(ARRAY[1,2,3], 0);
SELECT trim_array(ARRAY[1,2,3], 3);
SELECT trim_array(ARRAY['a','b','c'], 1);
SELECT trim_array(ARRAY[1,2,3], 5);
SELECT trim_array(ARRAY[1,2,3], -1);

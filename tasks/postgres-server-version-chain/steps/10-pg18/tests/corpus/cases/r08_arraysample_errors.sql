SELECT array_sample(ARRAY[1,2,3],-1);
SELECT array_sample(ARRAY[1,2,3],4);
SELECT array_sample(ARRAY[]::int[],1);
SELECT array_sample(ARRAY[1,2,3],NULL) IS NULL;

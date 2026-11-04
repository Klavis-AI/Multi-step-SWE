SELECT unnest(ARRAY['a','b','c']) AS u ORDER BY u;
SELECT x FROM unnest(ARRAY[3,1,2]) AS x ORDER BY x;
SELECT g FROM generate_series(1,5) g WHERE g % 2 = 1 ORDER BY g;
SELECT count(*) FROM generate_series(1,100);

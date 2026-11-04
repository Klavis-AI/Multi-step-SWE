SELECT coalesce(NULL, NULL, 3, 4), coalesce(NULL, NULL) IS NULL;
SELECT ifnull(NULL, 'x'), ifnull('y', 'x'), ifnull(NULL, NULL) IS NULL;
SELECT nullif(1, 1) IS NULL, nullif(1, 2), nullif('a', 'A');
SELECT typeof(NULL), typeof(1), typeof(1.5), typeof('a'), typeof(x'00');

SELECT 2+3, 7-9, 6*7, 17/5, 17%5, -17/5, -17%5;
SELECT 2::int2+3::int2, 2::int8*3::int8, 9::int8/4::int8;
SELECT abs(-5), abs(-5::int8), sign(-7), sign(0);
SELECT greatest(1,5,3), least(1,5,3), greatest(NULL,2), least(2,NULL);

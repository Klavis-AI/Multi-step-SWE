SELECT '2020-01-02 03:04:05'::timestamp, '2020-01-02 03:04:05.5'::timestamp;
SELECT '2020-01-02 03:04:05+02'::timestamptz, '2020-01-02 03:04:05Z'::timestamptz;
SELECT timestamp '2020-03-01 00:00' - timestamp '2020-01-01 12:00';
SELECT '2020-01-31'::date + interval '1 month', '2020-01-31'::date + interval '1 day';
SELECT age(timestamp '2020-03-01', timestamp '2019-01-15');
SELECT date_trunc('hour', timestamptz '2020-05-17 13:45:12+00');

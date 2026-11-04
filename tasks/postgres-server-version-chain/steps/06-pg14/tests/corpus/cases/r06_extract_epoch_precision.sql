SELECT EXTRACT(epoch FROM timestamptz '2020-01-01 00:00:00.123456+00');
SELECT EXTRACT(epoch FROM interval '1 day');
SELECT EXTRACT(second FROM timestamp '2020-01-02 03:04:05.5');
SELECT EXTRACT(milliseconds FROM timestamp '2020-01-02 03:04:05.5');
SELECT EXTRACT(microseconds FROM timestamp '2020-01-02 03:04:05.5');

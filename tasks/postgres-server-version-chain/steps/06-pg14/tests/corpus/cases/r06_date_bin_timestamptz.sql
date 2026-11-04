SET TIME ZONE 'UTC';
SELECT date_bin('1 day', timestamptz '2020-02-11 15:44:17+00', timestamptz '2001-01-01+00');
SELECT date_bin('12 hours', timestamptz '2020-02-11 15:44:17+00', timestamptz '2001-01-01+00');
SELECT pg_typeof(date_bin('1 day', timestamptz '2020-02-11 15:44:17+00', timestamptz '2001-01-01+00'));
SELECT pg_typeof(date_bin('1 day', timestamp '2020-02-11 15:44:17', timestamp '2001-01-01'));

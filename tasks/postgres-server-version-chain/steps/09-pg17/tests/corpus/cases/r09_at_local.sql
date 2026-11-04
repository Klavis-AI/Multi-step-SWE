SELECT timestamptz '2020-06-01 12:00:00+00' AT LOCAL;
SELECT timestamp '2020-06-01 12:00:00' AT LOCAL;
SELECT pg_typeof(timestamptz '2020-06-01 12:00:00+00' AT LOCAL);
SELECT pg_typeof(timestamp '2020-06-01 12:00:00' AT LOCAL);
SELECT (timestamptz '2020-06-01 12:00:00+00' AT LOCAL) = (timestamptz '2020-06-01 12:00:00+00' AT TIME ZONE current_setting('TimeZone'));

SELECT date_trunc('day', timestamptz '2020-05-17 13:45:12+00', 'Australia/Sydney');
SELECT date_trunc('hour', timestamptz '2020-05-17 13:45:12+00', 'UTC');
SELECT date_trunc('day', timestamptz '2020-05-17 13:45:12+00');
SELECT date_trunc('day', timestamptz '2020-05-17 13:45:12+00', 'nosuchzone');

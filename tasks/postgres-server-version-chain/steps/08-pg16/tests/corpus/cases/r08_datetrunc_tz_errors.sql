SELECT date_trunc('day', timestamptz '2020-05-17 13:45:12+00', 'No/Such_Zone');
SELECT date_trunc('bogus', timestamptz '2020-05-17 13:45:12+00', 'UTC');
SELECT date_trunc('day', timestamp '2020-05-17 13:45:12', 'UTC');
SELECT date_trunc('day', timestamptz '2020-05-17 13:45:12+00', NULL) IS NULL;

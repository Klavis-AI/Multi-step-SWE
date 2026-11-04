SELECT date_add(timestamptz '2020-03-28 22:00:00+00', interval '1 day', 'Europe/Berlin');
SELECT date_add(timestamptz '2020-03-28 22:00:00+00', interval '1 day', 'UTC');
SELECT date_add(timestamptz '2020-01-31 00:00:00+00', interval '1 month', 'UTC');
SELECT date_add(timestamptz '2020-05-17 13:45:12+00', interval '0', 'No/Such_Zone');

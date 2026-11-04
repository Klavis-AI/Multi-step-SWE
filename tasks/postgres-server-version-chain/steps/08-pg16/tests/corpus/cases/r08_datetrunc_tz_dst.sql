SELECT date_trunc('day', timestamptz '2020-03-08 12:00:00+00', 'America/New_York');
SELECT date_trunc('day', timestamptz '2020-11-01 12:00:00+00', 'America/New_York');
SELECT date_trunc('week', timestamptz '2020-03-11 12:00:00+00', 'Europe/Berlin');
SELECT date_trunc('day', timestamptz '2020-06-01 00:00:00+00', '+05:30');

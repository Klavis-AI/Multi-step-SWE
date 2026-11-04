SET TimeZone = 'Asia/Tokyo';
SELECT timestamptz '2020-06-01 12:00:00+00' AT LOCAL;
SET TimeZone = 'America/New_York';
SELECT timestamptz '2020-06-01 12:00:00+00' AT LOCAL;
RESET TimeZone;
SELECT timestamptz '2020-06-01 12:00:00+00' AT LOCAL;

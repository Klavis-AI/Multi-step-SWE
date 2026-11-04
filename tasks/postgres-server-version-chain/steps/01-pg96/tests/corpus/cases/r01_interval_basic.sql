SELECT '1 year 2 months 3 days 04:05:06'::interval;
SELECT interval '1 day' + interval '2 hours', interval '1 day' * 3, interval '1 day' / 2;
SELECT '-1 day -2 hours'::interval, '3 4:05:06'::interval, 'P1Y2M3DT4H5M6S'::interval;
SELECT justify_hours(interval '30 hours'), justify_days(interval '35 days');
SELECT interval '2 hours 30 minutes', interval '90 minutes';

SELECT date_subtract(timestamptz '2020-03-30 22:00:00+00', interval '1 day', 'Europe/Berlin');
SELECT date_subtract(timestamptz '2020-03-30 22:00:00+00', interval '1 day', 'UTC');
SELECT date_subtract(timestamptz '2020-03-01 00:00:00+00', interval '1 month', 'UTC');
SELECT proname, pg_get_function_arguments(oid) FROM pg_proc WHERE proname IN ('date_add','date_subtract') ORDER BY proname, pg_get_function_arguments(oid);

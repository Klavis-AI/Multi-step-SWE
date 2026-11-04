SELECT system_user IS NULL;
SELECT pg_typeof(system_user);
SELECT current_user = session_user;
SELECT count(*) FROM pg_proc WHERE proname = 'system_user';

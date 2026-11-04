SET idle_session_timeout = '10s';
SHOW idle_session_timeout;
RESET idle_session_timeout;
SET compute_query_id = off;
SHOW compute_query_id;
SET compute_query_id = 'regress';
SHOW compute_query_id;
RESET ALL;

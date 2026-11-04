SELECT current_setting('server_version_num')::int / 10000 AS major;
SELECT substring(version() from 'PostgreSQL [0-9.]+');
SHOW server_version_num;

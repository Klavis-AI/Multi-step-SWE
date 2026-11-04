CREATE ROLE rl_a LOGIN;
CREATE ROLE rl_b NOLOGIN CREATEDB;
SELECT rolname, rolcanlogin, rolcreatedb, rolsuper FROM pg_roles WHERE rolname LIKE 'rl\_%' ORDER BY rolname;
SELECT rolname FROM pg_authid WHERE rolname LIKE 'rl\_%' ORDER BY rolname;
CREATE ROLE rl_a;
DROP ROLE rl_a, rl_b;
DROP ROLE rl_a;
DROP ROLE IF EXISTS rl_a;

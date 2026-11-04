CREATE ROLE x06_46r LOGIN PASSWORD 'pw_x06_46';
SELECT rolname, substring(rolpassword from 1 for 3) = 'md5' AS stored_md5,
       rolpassword LIKE 'SCRAM-SHA-256$%' AS stored_scram
  FROM pg_authid WHERE rolname='x06_46r';
DROP OWNED BY x06_46r;
DROP ROLE x06_46r;
SELECT count(*) FROM pg_authid WHERE rolname='x06_46r';

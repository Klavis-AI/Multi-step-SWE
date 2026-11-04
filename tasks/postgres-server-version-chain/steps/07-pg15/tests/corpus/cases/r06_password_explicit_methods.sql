SET password_encryption = 'md5';
CREATE ROLE x06_47r LOGIN PASSWORD 'pw_x06_47';
SELECT rolname, substring(rolpassword from 1 for 3) = 'md5' AS stored_md5,
       length(rolpassword) = 35 AS md5_len
  FROM pg_authid WHERE rolname='x06_47r';
DROP OWNED BY x06_47r;
DROP ROLE x06_47r;
SET password_encryption = 'scram-sha-256';
CREATE ROLE x06_47s LOGIN PASSWORD 'pw_x06_47';
SELECT rolname, split_part(rolpassword,'$',1) AS method
  FROM pg_authid WHERE rolname='x06_47s';
DROP OWNED BY x06_47s;
DROP ROLE x06_47s;
RESET password_encryption;
SHOW password_encryption;

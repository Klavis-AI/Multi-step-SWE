SET password_encryption = 'scram-sha-256';
SHOW password_encryption;
CREATE ROLE q2_scr_role LOGIN PASSWORD 'q2_scr_secret';
SELECT rolname, substring(rolpassword from 1 for 14) AS verifier_prefix,
       length(rolpassword) > 40 AS looks_long
  FROM pg_authid WHERE rolname = 'q2_scr_role';
DROP ROLE q2_scr_role;
RESET password_encryption;
SHOW password_encryption;

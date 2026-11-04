SET password_encryption = 'md5';
SHOW password_encryption;
CREATE ROLE q2_smd_role LOGIN PASSWORD 'q2_smd_secret';
SELECT rolname, substring(rolpassword from 1 for 3) AS verifier_prefix,
       length(rolpassword) AS verifier_len FROM pg_authid WHERE rolname = 'q2_smd_role';
DROP ROLE q2_smd_role;
RESET password_encryption;

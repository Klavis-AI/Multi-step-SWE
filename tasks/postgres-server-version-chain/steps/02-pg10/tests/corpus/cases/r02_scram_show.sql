SHOW password_encryption;
SELECT setting FROM pg_settings WHERE name = 'password_encryption';
SELECT vartype, context FROM pg_settings WHERE name = 'password_encryption';
SELECT boot_val, reset_val FROM pg_settings WHERE name = 'password_encryption';

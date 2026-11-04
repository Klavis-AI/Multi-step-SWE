SHOW password_encryption;
SELECT current_setting('password_encryption');
SELECT name, setting, boot_val, vartype FROM pg_settings WHERE name='password_encryption';
SELECT enumvals FROM pg_settings WHERE name='password_encryption';

SHOW default_toast_compression;
SET default_toast_compression = 'lz4';
SHOW default_toast_compression;
RESET default_toast_compression;
SHOW default_toast_compression;
SELECT name, boot_val, vartype FROM pg_settings WHERE name='default_toast_compression';

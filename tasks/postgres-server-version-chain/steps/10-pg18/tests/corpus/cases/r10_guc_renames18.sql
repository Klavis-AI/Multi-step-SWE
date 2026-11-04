SHOW ssl_ecdh_curve;
SELECT name FROM pg_settings WHERE name IN ('ssl_ecdh_curve','ssl_groups','extension_destdir','extension_control_path') ORDER BY name;
SELECT name FROM pg_settings WHERE name IN ('io_method','io_workers','file_copy_method','vacuum_truncate','md5_password_warnings') ORDER BY name;
SHOW io_method;
SHOW vacuum_truncate;

SELECT * FROM pg_split_walfile_name('bogus');
SELECT * FROM pg_split_walfile_name('');
SELECT * FROM pg_split_walfile_name('00000001000000010000000');
SELECT * FROM pg_split_walfile_name('ZZ0000010000000100000002');
SELECT * FROM pg_split_walfile_name(NULL);

SELECT * FROM pg_split_walfile_name('000000010000000000000000');
SELECT * FROM pg_split_walfile_name('FFFFFFFF00000001000000FF');
SELECT segment_number FROM pg_split_walfile_name('0000000100000FFF000000FF');
SELECT pg_typeof(segment_number), pg_typeof(timeline_id) FROM pg_split_walfile_name('000000010000000100000002');

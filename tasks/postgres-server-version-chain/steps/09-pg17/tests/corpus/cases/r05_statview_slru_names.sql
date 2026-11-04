SELECT name FROM pg_stat_slru ORDER BY name;
SELECT count(*) > 0 FROM pg_stat_slru;
SELECT attname FROM pg_attribute WHERE attrelid='pg_stat_slru'::regclass AND attnum>0
  ORDER BY attnum;

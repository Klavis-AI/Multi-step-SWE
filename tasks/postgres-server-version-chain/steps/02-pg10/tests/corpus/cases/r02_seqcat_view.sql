CREATE SEQUENCE q2_sqb START 10 INCREMENT 5 MAXVALUE 1000;
SELECT sequencename, start_value, min_value, max_value, increment_by, cycle, cache_size
  FROM pg_sequences WHERE sequencename = 'q2_sqb';
SELECT last_value IS NULL AS never_called FROM pg_sequences WHERE sequencename = 'q2_sqb';
SELECT nextval('q2_sqb');
SELECT last_value FROM pg_sequences WHERE sequencename = 'q2_sqb';
DROP SEQUENCE q2_sqb;

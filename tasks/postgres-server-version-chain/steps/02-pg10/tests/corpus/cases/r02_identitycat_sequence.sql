CREATE TABLE q2_icc(a int GENERATED ALWAYS AS IDENTITY, b text);
SELECT pg_get_serial_sequence('q2_icc','a');
SELECT pg_get_serial_sequence('q2_icc','b');
SELECT relname, relkind FROM pg_class WHERE relname = 'q2_icc_a_seq';
INSERT INTO q2_icc(b) VALUES ('x');
SELECT last_value, is_called FROM q2_icc_a_seq;
DROP TABLE q2_icc;
SELECT count(*) FROM pg_class WHERE relname = 'q2_icc_a_seq';

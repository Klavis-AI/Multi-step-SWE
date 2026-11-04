CREATE TABLE q2_esc(a int, b int);
CREATE STATISTICS q2_esc_one ON a FROM q2_esc;
CREATE STATISTICS q2_esc_bad (nosuchkind) ON a, b FROM q2_esc;
CREATE STATISTICS q2_esc_col ON a, nosuchcol FROM q2_esc;
CREATE STATISTICS q2_esc_ok ON a, b FROM q2_esc;
CREATE STATISTICS q2_esc_ok ON a, b FROM q2_esc;
DROP STATISTICS q2_esc_ok;
DROP STATISTICS q2_esc_ok;
DROP TABLE q2_esc;

SHOW operator_precedence_warning;
SET operator_precedence_warning = on;
SELECT count(*) FROM pg_settings WHERE name='operator_precedence_warning';
SELECT current_setting('operator_precedence_warning', true) IS NULL;

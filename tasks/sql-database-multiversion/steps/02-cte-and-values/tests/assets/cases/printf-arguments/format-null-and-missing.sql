-- lang_corefunc.html: "If the FORMAT argument is missing or NULL then the
-- result is NULL."
SELECT typeof(printf()), typeof(printf(NULL)), typeof(printf(NULL, 1, 2));
-- "If there are too few arguments in the argument list, missing arguments are
-- assumed to have a NULL value, which is translated into 0 or 0.0 for numeric
-- formats or an empty string for %s."
SELECT printf('[%s][%d][%f]');
SELECT printf('[%d %d %d]', 1);
SELECT printf('[%s][%d][%f]', NULL, NULL, NULL);

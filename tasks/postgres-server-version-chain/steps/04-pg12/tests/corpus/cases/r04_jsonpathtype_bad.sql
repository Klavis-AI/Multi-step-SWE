SELECT '$.a['::jsonpath;
SELECT 'nonsense'::jsonpath;
SELECT ''::jsonpath;
SELECT '$ ? ()'::jsonpath;
SELECT '$.a ? (@ >)'::jsonpath;

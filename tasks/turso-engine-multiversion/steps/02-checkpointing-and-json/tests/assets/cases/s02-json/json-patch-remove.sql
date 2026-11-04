-- R02.7: json_patch merges one object into another and json_remove deletes
-- the member a path names, each returning the same text stock sqlite3 returns.
SELECT json_patch('{"a":1,"b":2}','{"b":3,"c":4}');
SELECT json_remove('{"a":1,"b":2}','$.a');

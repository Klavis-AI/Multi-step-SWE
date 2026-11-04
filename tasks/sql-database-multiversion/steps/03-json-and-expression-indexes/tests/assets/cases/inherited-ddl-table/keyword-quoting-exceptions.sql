-- lang_keywords.html, "For resilience when confronted with historical SQL
-- statements, SQLite will sometimes bend the quoting rules above": "If a
-- keyword in single quotes (ex: 'key' or 'glob') is used in a context where an
-- identifier is allowed but where a string literal is not allowed, then the
-- token is understood to be an identifier instead of a string literal." and
-- "If a keyword in double quotes (ex: "key" or "glob") is used in a context
-- where it cannot be resolved to an identifier but where a string literal is
-- allowed, then the token is understood to be a string literal instead of an
-- identifier."  PROFILE.md section 1 puts identifier quoting and "the keyword
-- rules" in scope as documented on lang_keywords.html.
CREATE TABLE t(a INTEGER, "key" TEXT);
INSERT INTO t VALUES(1, 'kv');
-- a double-quoted keyword that does resolve to a column is that column
SELECT "key" FROM t;
-- a double-quoted keyword that cannot be resolved to an identifier, in a
-- context where a string literal is allowed, is that string literal
SELECT "glob" FROM t;
SELECT count(*) FROM t WHERE "glob" = 'glob';
SELECT "order" || "by";
-- a single-quoted keyword where an identifier is allowed but a string literal
-- is not is an identifier: a table name and a column name here
CREATE TABLE 'key2'('glob' INTEGER);
INSERT INTO key2(glob) VALUES(3);
SELECT glob FROM key2;
SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name;

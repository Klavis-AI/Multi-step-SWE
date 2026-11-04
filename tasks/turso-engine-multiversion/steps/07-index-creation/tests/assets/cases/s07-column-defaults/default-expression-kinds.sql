-- R07.15: a DEFAULT clause accepts an unsigned integer, a signed integer, a
-- real, a string literal and a parenthesised constant, and the stored value
-- keeps the storage class the literal names.
--
-- typeof() names the storage class so the check does not depend on how the
-- value is formatted.
CREATE TABLE k(id INTEGER, i INTEGER DEFAULT 5, r REAL DEFAULT 2.5,
               t TEXT DEFAULT 'abc', neg INTEGER DEFAULT -3,
               par INTEGER DEFAULT (7));
INSERT INTO k(id) VALUES(1);
SELECT 'i='||i||' '||typeof(i) FROM k;
SELECT 'r='||r||' '||typeof(r) FROM k;
SELECT 't='||t||' '||typeof(t) FROM k;
SELECT 'neg='||neg||' '||typeof(neg) FROM k;
SELECT 'par='||par||' '||typeof(par) FROM k;
SELECT 'sum='||(i+neg+par) FROM k;

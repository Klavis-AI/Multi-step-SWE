-- The three worked examples of lang_UPSERT.html, run as the page writes them.
-- PROFILE.md 4.4 says the results those examples describe are compared.

-- "The upsert above inserts the new vocabulary word 'jovial' if that word is
-- not already in the dictionary, or if it is already in the dictionary, it
-- increments the counter."
CREATE TABLE vocabulary(word TEXT PRIMARY KEY, count INT DEFAULT 1);
INSERT INTO vocabulary(word) VALUES('jovial')
  ON CONFLICT(word) DO UPDATE SET count=count+1;
SELECT word,count FROM vocabulary ORDER BY word;
INSERT INTO vocabulary(word) VALUES('jovial')
  ON CONFLICT(word) DO UPDATE SET count=count+1;
SELECT word,count FROM vocabulary ORDER BY word;
-- "The 'count+1' expression could also be written as 'vocabulary.count'.
-- PostgreSQL requires the second form, but SQLite accepts either."
INSERT INTO vocabulary(word) VALUES('jovial')
  ON CONFLICT(word) DO UPDATE SET count=vocabulary.count+1;
SELECT word,count FROM vocabulary ORDER BY word;

-- "the effect of the upsert is to insert a phonenumber of Alice if none
-- exists, or to overwrite any prior phonenumber for Alice with the new one."
CREATE TABLE phonebook(name TEXT PRIMARY KEY, phonenumber TEXT);
INSERT INTO phonebook(name,phonenumber) VALUES('Alice','704-555-1212')
  ON CONFLICT(name) DO UPDATE SET phonenumber=excluded.phonenumber;
SELECT name,phonenumber FROM phonebook ORDER BY name;
INSERT INTO phonebook(name,phonenumber) VALUES('Alice','704-555-9999')
  ON CONFLICT(name) DO UPDATE SET phonenumber=excluded.phonenumber;
SELECT name,phonenumber FROM phonebook ORDER BY name;

-- "the phonebook2 entry is only updated if the validDate for the newly
-- inserted value is newer than the entry already in the table."
CREATE TABLE phonebook2(
  name TEXT PRIMARY KEY,
  phonenumber TEXT,
  validDate DATE
);
INSERT INTO phonebook2(name,phonenumber,validDate)
  VALUES('Alice','704-555-1212','2018-05-08')
  ON CONFLICT(name) DO UPDATE SET
    phonenumber=excluded.phonenumber,
    validDate=excluded.validDate
  WHERE excluded.validDate>phonebook2.validDate;
SELECT name,phonenumber,validDate FROM phonebook2 ORDER BY name;
-- an older validDate leaves the row alone
INSERT INTO phonebook2(name,phonenumber,validDate)
  VALUES('Alice','704-555-0000','2018-01-01')
  ON CONFLICT(name) DO UPDATE SET
    phonenumber=excluded.phonenumber,
    validDate=excluded.validDate
  WHERE excluded.validDate>phonebook2.validDate;
SELECT name,phonenumber,validDate FROM phonebook2 ORDER BY name;
-- a newer one replaces it
INSERT INTO phonebook2(name,phonenumber,validDate)
  VALUES('Alice','704-555-7777','2019-02-03')
  ON CONFLICT(name) DO UPDATE SET
    phonenumber=excluded.phonenumber,
    validDate=excluded.validDate
  WHERE excluded.validDate>phonebook2.validDate;
SELECT name,phonenumber,validDate FROM phonebook2 ORDER BY name;
PRAGMA integrity_check;

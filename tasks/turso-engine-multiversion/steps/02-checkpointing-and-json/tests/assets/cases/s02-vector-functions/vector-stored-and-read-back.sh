# R02.12: a vector value is stored in an ordinary column, survives the process
# that
# wrote it, and prints back as the text it was built from. Because it is an
# opaque blob on disk, stock sqlite3 -- which has never heard of the vector
# functions -- still opens the file, reports the column as a blob and agrees
# with the engine about how many bytes it holds.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>/dev/null <<'SQL'
CREATE TABLE v(id INTEGER PRIMARY KEY, e);
INSERT INTO v VALUES(1, vector('[1,0]'));
INSERT INTO v VALUES(2, vector('[0,1]'));
INSERT INTO v VALUES(3, vector('[1,2,3]'));
-- The writing invocation ends by reading back the table it wrote.
-- PROFILE.md's durability rule already requires a writer to leave its
-- committed data recoverable from the files on disk, so a conforming engine
-- does not need this; it is here so that what this case grades is the vector
-- value, and not the commit path that step 01 already grades.
SELECT count(*) FROM v;
SQL
echo "reopened:"
"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed -e '/^[[:space:]]*$/d'
SELECT id, vector_extract(e) FROM v ORDER BY id;
SELECT id, typeof(e), length(e) FROM v ORDER BY id;
SELECT count(*) FROM v;
SQL
echo "as stock sqlite3 sees it:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id, typeof(e), length(e) FROM v ORDER BY id;' 2>/dev/null

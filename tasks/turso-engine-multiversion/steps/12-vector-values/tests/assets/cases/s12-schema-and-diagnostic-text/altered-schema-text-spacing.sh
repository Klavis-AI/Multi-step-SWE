
# R12.6: ALTER TABLE ADD COLUMN, DROP COLUMN and RENAME COLUMN each rebuild the
# stored CREATE TABLE text, and this case observes the rebuilt text beside the
# text stored for a table created outright with the same columns.
# ALTER TABLE rebuilds the stored CREATE TABLE text. Earlier checkpoints
# rebuilt it with a space inside each parenthesis -- `CREATE TABLE t ( a, b )`
# -- which parses but does not match the text the same engine writes for a
# table it created outright. From this checkpoint the rebuilt text is spaced
# the same way as freshly created text, so a schema's rendering no longer
# depends on how it was reached.
#
# The stored schema text is a value the engine reports, not a diagnostic, so
# this case is allowed to pin it: it is the same text `sqlite_schema.sql`
# hands back for a table created directly.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE created(a INTEGER, b TEXT);
CREATE TABLE added(a INTEGER);
ALTER TABLE added ADD COLUMN b TEXT;
CREATE TABLE dropped(a INTEGER, gone TEXT, b TEXT);
ALTER TABLE dropped DROP COLUMN gone;
CREATE TABLE renamed(a INTEGER, b TEXT);
ALTER TABLE renamed RENAME COLUMN b TO label;
SELECT name, sql FROM sqlite_schema ORDER BY name;
SELECT count(*) FROM created;
SELECT count(*) FROM added;
SELECT count(*) FROM dropped;
SELECT count(*) FROM renamed;
SQL

-- R01.1: the `sql` column of a `sqlite_schema` row, read back through the
-- engine's own shell.
--
-- R01.1 says a CREATE TABLE statement is recorded in `sqlite_schema.sql` in
-- the profile's normalised rendering. This case is the first of the two halves
-- the row names: the schema row is read back through the engine itself, which
-- is the only way to see a rendering that is the engine's own and not the file
-- format's. `type`, `name` and `tbl_name` are selected alongside it so that a
-- build which stores the text correctly but files it under the wrong row is
-- caught too. The second half -- stock sqlite3 re-parsing the same file -- is
-- `s01-file-interop/sqlite-reads-our-database.sh`.
--
-- This case and its companion `schema-sql-text-normalisation.sql` divide
-- `spec/PROFILE.md` rule 2 between them and do not overlap. The companion
-- grades the punctuation clauses: the space before a parenthesised list, the
-- space after a comma, a parameterised type, a DEFAULT, a table constraint
-- re-printed after the column list, and a binary operator inside a CHECK.
-- Every statement in the companion is already single-line and already
-- upper-case, so three of rule 2's clauses are unreachable there and are
-- graded here instead:
--
--   * runs of whitespace are collapsed, so the irregular spacing inside table
--     `b` and the three source lines of table `d` both read back on one line
--     with single spaces;
--   * keywords are emitted upper-case, so the lower-case `create table` that
--     declares `b` reads back as `CREATE TABLE`;
--   * identifiers, type names and literals keep the source's spelling, so
--     `int` stays `int` and `VarChar` stays `VarChar` rather than being
--     upper-cased along with the keywords.
--
-- The last of those three is the one a build is most likely to get wrong in
-- the direction that still looks tidy -- upper-casing the whole statement --
-- and it is the reason the two tables are declared in deliberately different
-- letter-cases. The two statements are the second and fourth rows of
-- `spec/PROFILE.md` rule 2's worked table, with two deliberate changes: `b`'s
-- `create table` is written lower-case here so that the keyword-casing clause
-- is graded, and `d` is spread over three source lines so that the
-- whitespace-collapsing clause is. Both stored renderings are the table's,
-- unchanged.
--
-- Stock sqlite3 stores the source bytes verbatim and so disagrees with every
-- line of the expected `sql` output here. That is the documented divergence of
-- rule 2 and the case is listed in `corpus/DIFFERENTIAL_EXCEPTIONS.tsv` for
-- it; the expectation is read off rule 2, not off the reference.
create table b(  y  TEXT  ,   z   REAL );
CREATE TABLE
    d(m int,
      n VarChar(20));
SELECT type, name, tbl_name FROM sqlite_schema ORDER BY name;
SELECT sql FROM sqlite_schema ORDER BY name;

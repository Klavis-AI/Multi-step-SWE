/* TEST_INTERFACE.md 7: "a SELECT carrying window functions is a query like any
   other."  sqlite3_prepare_v2() compiles one statement at a time and leaves
   *pzTail after its semicolon; sqlite3_step() reports SQLITE_ROW once per
   result row and then SQLITE_DONE; sqlite3_column_count() counts the result
   columns including the window-function ones; and sqlite3_column_type()
   reports SQLITE_INTEGER for row_number(), rank(), dense_rank() and ntile()
   and SQLITE_FLOAT for percent_rank() and cume_dist() (PROFILE.md 5.4).
   Every window-function column read by name carries an AS alias
   (PROFILE.md 5.8). */
#include <stdio.h>
#include "sqlite3.h"

static sqlite3 *db;

int main(void) {
  sqlite3_stmt *st = 0;
  const char *tail = 0;
  int i, rc, rows = 0;

  if (sqlite3_open("w.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db,
        "CREATE TABLE t(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);"
        "INSERT INTO t VALUES(1,'a',10),(2,'a',20),(3,'b',30),"
        "                    (4,'b',40),(5,'b',50);",
        0, 0, 0) != SQLITE_OK) return 2;

  /* One windowed SELECT, with a second statement after its semicolon. */
  if (sqlite3_prepare_v2(db,
        "SELECT k AS k, row_number() OVER w AS rn, rank() OVER w AS rk,"
        " percent_rank() OVER w AS pr, sum(v) OVER w AS s"
        " FROM t WINDOW w AS (ORDER BY k) ORDER BY k; SELECT 1;",
        -1, &st, &tail) != SQLITE_OK) return 3;
  printf("cols|%d\n", sqlite3_column_count(st));
  printf("tail|%s\n", tail);
  for (i = 0; i < sqlite3_column_count(st); i++)
    printf("name|%d|%s\n", i, sqlite3_column_name(st, i));
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    rows++;
    printf("row|%d|%d|%d|%.2f|%d\n",
           sqlite3_column_int(st, 0), sqlite3_column_int(st, 1),
           sqlite3_column_int(st, 2), sqlite3_column_double(st, 3),
           sqlite3_column_int(st, 4));
    if (rows == 1)
      printf("types|%d|%d|%d|%d|%d\n",
             sqlite3_column_type(st, 0), sqlite3_column_type(st, 1),
             sqlite3_column_type(st, 2), sqlite3_column_type(st, 3),
             sqlite3_column_type(st, 4));
  }
  printf("done|%d|rows|%d\n", rc == SQLITE_DONE, rows);
  sqlite3_finalize(st);

  /* The remaining built-ins whose storage class PROFILE.md 5.4 fixes.
     PROFILE.md 5.13 keeps LIMIT and OFFSET out of every windowed query, so the
     first row is stepped to and the statement is then finalized. */
  if (sqlite3_prepare_v2(db,
        "SELECT dense_rank() OVER (ORDER BY k) AS dr,"
        " ntile(2) OVER (ORDER BY k) AS nt,"
        " cume_dist() OVER (ORDER BY k) AS cd"
        " FROM t ORDER BY k;",
        -1, &st, 0) != SQLITE_OK) return 4;
  if (sqlite3_step(st) != SQLITE_ROW) return 5;
  printf("types2|%d|%d|%d\n", sqlite3_column_type(st, 0),
         sqlite3_column_type(st, 1), sqlite3_column_type(st, 2));
  sqlite3_finalize(st);

  /* A bound parameter in the WHERE clause of a windowed query
     (TEST_INTERFACE.md 7). */
  if (sqlite3_prepare_v2(db,
        "SELECT k AS k, sum(v) OVER (ORDER BY k) AS s FROM t WHERE v >= ?1"
        " ORDER BY k;", -1, &st, 0) != SQLITE_OK) return 6;
  if (sqlite3_bind_int(st, 1, 30) != SQLITE_OK) return 7;
  while (sqlite3_step(st) == SQLITE_ROW)
    printf("bound|%d|%d\n", sqlite3_column_int(st, 0),
           sqlite3_column_int(st, 1));
  sqlite3_finalize(st);

  /* TEST_INTERFACE.md 7: "ALTER TABLE ... RENAME COLUMN is DDL.  It returns no
     rows, so sqlite3_step() reports SQLITE_DONE without ever reporting
     SQLITE_ROW."  Nothing is asserted about sqlite3_column_count() here,
     because no shipped page states a value for it on a DDL statement. */
  {
    int saw_row = 0;
    if (sqlite3_prepare_v2(db, "ALTER TABLE t RENAME COLUMN g TO grp;",
                           -1, &st, 0) != SQLITE_OK) return 8;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) saw_row = 1;
    printf("ddl_saw_row|%d\n", saw_row);
    printf("ddl_done|%d\n", rc == SQLITE_DONE);
    sqlite3_finalize(st);
  }

  /* The new name works through the library, the old one does not. */
  printf("new_name|%d\n",
         sqlite3_exec(db, "SELECT grp FROM t;", 0, 0, 0) == SQLITE_OK);
  printf("old_name|%d\n",
         sqlite3_exec(db, "SELECT g FROM t;", 0, 0, 0) != SQLITE_OK);

  sqlite3_close(db);
  return 0;
}

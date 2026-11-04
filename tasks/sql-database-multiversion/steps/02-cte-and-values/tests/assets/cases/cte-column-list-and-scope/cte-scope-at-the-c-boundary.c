/* TEST_INTERFACE.md section 7: a WITH statement is an ordinary statement at the
   library boundary - sqlite3_prepare_v2() compiles one at a time and leaves
   *pzTail after its semicolon - and a statement the profile says must be
   rejected returns something other than SQLITE_OK there, exactly as it gives a
   non-zero exit status through the shell. The two rules this group scores are
   the ones observed here: the column-name list's arity (PROFILE.md section 2.1,
   "The column-name list") and a common table expression's lifetime
   ("Lifetime and scope"). Section 3.3: a failing call is compared only against
   "not SQLITE_OK", never a particular code and never the message. */
#include <stdio.h>
#include "sqlite3.h"

/* 1 when the statement did not compile, 0 when it did. */
static int rejected(sqlite3 *db, const char *sql) {
  sqlite3_stmt *st = 0;
  int rc = sqlite3_prepare_v2(db, sql, -1, &st, 0);
  if (st) sqlite3_finalize(st);
  return rc != SQLITE_OK;
}

static int count_rows(sqlite3 *db) {
  sqlite3_stmt *st = 0;
  int n = -1;
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM t", -1, &st, 0) != SQLITE_OK) return -1;
  if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return n;
}

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  const char *tail = 0;
  int rc, n;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE t(a INTEGER, b TEXT);"
                       "INSERT INTO t VALUES(1,'p'),(2,'q');", 0, 0, 0) != SQLITE_OK) return 2;

  /* One WITH statement out of two, and the tail left after its semicolon. */
  if (sqlite3_prepare_v2(db,
        "WITH c(k, v) AS (SELECT a, b FROM t) SELECT k, v FROM c ORDER BY k; SELECT 1;",
        -1, &st, &tail) != SQLITE_OK) return 3;
  printf("columns=%d\n", sqlite3_column_count(st));
  printf("tail=%s\n", tail);
  n = 0;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    n++;
    printf("row %d %s\n", sqlite3_column_int(st, 0), sqlite3_column_text(st, 1));
  }
  printf("rows=%d done=%d\n", n, rc == SQLITE_DONE);
  printf("finalize=%d\n", sqlite3_finalize(st) == SQLITE_OK);

  /* Column-name list arity, both directions. */
  printf("reject_too_many=%d\n",
         rejected(db, "WITH c(x, y) AS (SELECT a FROM t) SELECT x FROM c"));
  printf("reject_too_few=%d\n",
         rejected(db, "WITH c(x) AS (SELECT a, b FROM t) SELECT x FROM c"));

  /* Lifetime: the name is unknown to a statement that does not define it. */
  printf("reject_out_of_scope=%d\n", rejected(db, "SELECT x FROM c"));

  /* A rejected CTE-fed INSERT leaves the table exactly as it was. */
  printf("rows_before=%d\n", count_rows(db));
  printf("reject_insert=%d\n",
         sqlite3_exec(db, "WITH s(y, z) AS (SELECT 9) INSERT INTO t SELECT y, z FROM s;",
                      0, 0, 0) != SQLITE_OK);
  printf("rows_after=%d\n", count_rows(db));

  sqlite3_close(db);
  return 0;
}

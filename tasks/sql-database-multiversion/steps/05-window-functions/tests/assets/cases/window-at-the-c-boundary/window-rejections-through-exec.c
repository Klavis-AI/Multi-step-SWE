/* TEST_INTERFACE.md 7: a statement PROFILE.md 5.11 says must be rejected fails
   at the library boundary too -- "a non-SQLITE_OK return from the library, and
   message wording that is not scored".  Only "did not return SQLITE_OK" is
   compared, never the result code beyond that nor which call in a
   prepare/step sequence surfaces the failure (TEST_INTERFACE.md 3.3, 6, 7). */
#include <stdio.h>
#include "sqlite3.h"

static sqlite3 *db;

/* Runs one statement and prints whether it was rejected. sqlite3_exec() drives
   prepare and step for us, so a failure at either shows up here. */
static void rejected(const char *label, const char *sql) {
  char *msg = 0;
  int rc = sqlite3_exec(db, sql, 0, 0, &msg);
  printf("%s|%d\n", label, rc != SQLITE_OK);
  sqlite3_free(msg);
}

static int scalar(const char *sql) {
  sqlite3_stmt *st = 0;
  int n = -1;
  if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return -1;
  if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return n;
}

int main(void) {
  if (sqlite3_open("r.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db,
        "CREATE TABLE t(k INTEGER PRIMARY KEY, alpha TEXT, v INTEGER);"
        "INSERT INTO t VALUES(1,'x',10),(2,'y',20),(3,'z',30);",
        0, 0, 0) != SQLITE_OK) return 2;

  /* Well-formed windowed queries first, to show the connection works. */
  rejected("ok-select",
    "SELECT k, row_number() OVER (ORDER BY k) AS rn FROM t ORDER BY k;");
  rejected("ok-order-by",
    "SELECT k FROM t ORDER BY rank() OVER (ORDER BY v);");

  /* "Window functions may only appear in the result set and in the ORDER BY
     clause of a SELECT statement." */
  rejected("in-where",
    "SELECT k FROM t WHERE row_number() OVER (ORDER BY k) = 1;");
  rejected("in-group-by",
    "SELECT k FROM t GROUP BY row_number() OVER (ORDER BY k);");
  rejected("in-having",
    "SELECT k FROM t GROUP BY k HAVING count(*) OVER () > 0;");
  rejected("in-update-set",
    "UPDATE t SET v = row_number() OVER (ORDER BY k);");
  rejected("in-delete-where",
    "DELETE FROM t WHERE row_number() OVER (ORDER BY k) = 1;");
  rejected("in-do-update",
    "INSERT INTO t VALUES(1,'q',9) ON CONFLICT(k)"
    " DO UPDATE SET v = rank() OVER (ORDER BY k);");

  /* "window functions cannot use the DISTINCT keyword" */
  rejected("distinct", "SELECT count(DISTINCT v) OVER (ORDER BY k) FROM t;");
  /* "It is a syntax error to specify a FILTER clause as part of a built-in
     window function invocation." */
  rejected("filter-on-builtin",
    "SELECT row_number() FILTER (WHERE v > 0) OVER (ORDER BY k) FROM t;");
  /* "This frame boundary type may only be used with ROWS frames." */
  rejected("range-offset",
    "SELECT sum(v) OVER (ORDER BY k RANGE BETWEEN 1 PRECEDING AND CURRENT ROW)"
    " FROM t;");
  /* Ending boundary higher in the list than the starting one. */
  rejected("bad-bound-order",
    "SELECT sum(v) OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND 1 PRECEDING)"
    " FROM t;");
  /* "<expr> is a constant expression that evaluates to a non-negative integer
     value" */
  rejected("negative-offset",
    "SELECT sum(v) OVER (ORDER BY k ROWS BETWEEN -1 PRECEDING AND CURRENT ROW)"
    " FROM t;");

  /* The three RENAME COLUMN rejections of PROFILE.md 5.11. */
  rejected("rename-missing-table",
    "ALTER TABLE nosuch RENAME COLUMN alpha TO beta;");
  rejected("rename-missing-column",
    "ALTER TABLE t RENAME COLUMN nosuch TO beta;");
  rejected("rename-to-existing",
    "ALTER TABLE t RENAME COLUMN alpha TO v;");

  /* And a well-formed rename afterwards, to show the connection still works. */
  rejected("ok-rename", "ALTER TABLE t RENAME COLUMN alpha TO beta;");

  /* Nothing above changed the data, and the schema is what the one accepted
     rename left. */
  printf("rows|%d\n", scalar("SELECT count(*) FROM t"));
  printf("sum_v|%d\n", scalar("SELECT sum(v) FROM t"));
  printf("row1_is_x|%d\n", scalar("SELECT count(*) FROM t WHERE k=1 AND beta='x'"));
  printf("beta_reads|%d\n", scalar("SELECT count(beta) FROM t"));
  /* PROFILE.md 5.10: the one accepted rename rewrote the stored CREATE TABLE
     text -- "the text it held before, with each occurrence of the column
     identifier old replaced by new, and nothing else changed" -- and step 1 6
     puts sqlite_master and its sql column in scope.  The statement here is an
     ordinary SELECT, so it needs nothing of the C interface that
     TEST_INTERFACE.md 3.2 does not already list. */
  printf("schema_text|%d\n",
         scalar("SELECT count(*) FROM sqlite_master WHERE type='table'"
                " AND name='t' AND sql='CREATE TABLE t(k INTEGER PRIMARY KEY,"
                " beta TEXT, v INTEGER)'"));
  sqlite3_close(db);
  return 0;
}

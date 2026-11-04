/* UPSERT is SQL syntax, so the C library reaches it through the same
   sqlite3_exec() / sqlite3_prepare_v2() / sqlite3_step() surface as any other
   INSERT (TEST_INTERFACE.md 3.2, 7).  An INSERT carrying an upsert clause
   returns no rows, so sqlite3_column_count() is 0 (c3ref/column_count.html),
   sqlite3_step() reports SQLITE_DONE without ever reporting SQLITE_ROW, and
   *pzTail is left after the statement's semicolon.

   Every row inserted below is written so that at most one uniqueness
   constraint can be violated (PROFILE.md 4.9).

   Every sqlite3_column_type() read is hoisted into a local BEFORE any other
   accessor touches that column: c3ref/column_blob.html leaves the type
   undefined after a conversion, and C leaves argument evaluation order
   unspecified. */
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

static sqlite3 *db;

static int dump(const char *label) {
  sqlite3_stmt *st = 0;
  int rc;
  if (sqlite3_prepare_v2(db, "SELECT k,u,n FROM t ORDER BY k", -1, &st, 0)
      != SQLITE_OK) return 1;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    int t0 = sqlite3_column_type(st, 0);
    int k = sqlite3_column_int(st, 0);
    const char *u = (const char *)sqlite3_column_text(st, 1);
    printf("%s|%d|%d|%s|%d\n", label, t0, k, u, sqlite3_column_int(st, 2));
  }
  sqlite3_finalize(st);
  return rc == SQLITE_DONE ? 0 : 1;
}

int main(void) {
  sqlite3_stmt *st = 0;
  const char *tail = 0;
  int rc, sawrow;

  if (sqlite3_open("u.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db,
        "CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, n INTEGER);"
        "INSERT INTO t VALUES(1,'x',10),(2,'y',20);",
        0, 0, 0) != SQLITE_OK) return 2;

  /* DO UPDATE through prepare_v2/step: no result columns, DONE, tail after
     the semicolon.  The first row collides on the targeted PRIMARY KEY and is
     updated; the second collides with nothing and is inserted. */
  if (sqlite3_prepare_v2(db,
        "INSERT INTO t(k,u,n) VALUES(1,'p',5),(3,'z',30)"
        " ON CONFLICT(k) DO UPDATE SET n=t.n+excluded.n; SELECT 1;",
        -1, &st, &tail) != SQLITE_OK) return 3;
  printf("cols|%d\n", sqlite3_column_count(st));
  sawrow = 0;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) sawrow++;
  printf("step|%d|%d\n", rc == SQLITE_DONE, sawrow);
  printf("tail|%s\n", tail);
  if (sqlite3_finalize(st) != SQLITE_OK) return 4;
  if (dump("after-update")) return 5;

  /* DO NOTHING through sqlite3_exec() */
  if (sqlite3_exec(db,
        "INSERT INTO t VALUES(2,'q',99) ON CONFLICT DO NOTHING;",
        0, 0, 0) != SQLITE_OK) return 6;
  if (dump("after-nothing")) return 7;

  /* Bound parameters work in an upsert exactly as in any other INSERT
     (TEST_INTERFACE.md 3.2, step 1 section 3).  c3ref/reset.html: reset does
     not clear the bindings, so only parameter 3 is rebound for the second
     run, whose trailing WHERE clause is then false. */
  st = 0;
  if (sqlite3_prepare_v2(db,
        "INSERT INTO t(k,u,n) VALUES(?1,?2,?3)"
        " ON CONFLICT(k) DO UPDATE SET n=excluded.n WHERE excluded.n > t.n",
        -1, &st, 0) != SQLITE_OK) return 8;
  if (sqlite3_bind_int(st, 1, 1) != SQLITE_OK) return 9;
  if (sqlite3_bind_text(st, 2, "r", -1, SQLITE_TRANSIENT) != SQLITE_OK) return 10;
  if (sqlite3_bind_int(st, 3, 99) != SQLITE_OK) return 11;
  printf("bound|%d\n", sqlite3_step(st) == SQLITE_DONE);
  if (sqlite3_reset(st) != SQLITE_OK) return 12;
  if (sqlite3_bind_int(st, 3, 1) != SQLITE_OK) return 13;
  printf("bound|%d\n", sqlite3_step(st) == SQLITE_DONE);
  if (sqlite3_finalize(st) != SQLITE_OK) return 14;
  if (dump("after-bound")) return 15;

  sqlite3_close(db);
  return 0;
}

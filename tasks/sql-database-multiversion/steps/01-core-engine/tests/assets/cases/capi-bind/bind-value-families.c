#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *ins = 0, *sel = 0;
  char buf[8];
  int rc;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db, "CREATE TABLE t(k INTEGER PRIMARY KEY, v);", 0, 0, 0);

  if (sqlite3_prepare_v2(db, "INSERT INTO t(k, v) VALUES(?, ?)", -1, &ins, 0) != SQLITE_OK) return 2;

  sqlite3_bind_int(ins, 1, 1);
  sqlite3_bind_int(ins, 2, -7);
  printf("step1=%d\n", sqlite3_step(ins) == SQLITE_DONE);
  sqlite3_reset(ins);

  sqlite3_bind_int(ins, 1, 2);
  sqlite3_bind_int64(ins, 2, (sqlite3_int64)4294967296LL);
  printf("step2=%d\n", sqlite3_step(ins) == SQLITE_DONE);
  sqlite3_reset(ins);

  sqlite3_bind_int(ins, 1, 3);
  sqlite3_bind_double(ins, 2, 2.5);
  printf("step3=%d\n", sqlite3_step(ins) == SQLITE_DONE);
  sqlite3_reset(ins);

  sqlite3_bind_int(ins, 1, 4);
  strcpy(buf, "hello");
  sqlite3_bind_text(ins, 2, buf, -1, SQLITE_TRANSIENT);
  memset(buf, 'z', sizeof buf);          /* TRANSIENT copies, so this is safe */
  printf("step4=%d\n", sqlite3_step(ins) == SQLITE_DONE);
  sqlite3_reset(ins);

  sqlite3_bind_int(ins, 1, 5);
  sqlite3_bind_text(ins, 2, "static-text", 6, SQLITE_STATIC);
  printf("step5=%d\n", sqlite3_step(ins) == SQLITE_DONE);
  sqlite3_reset(ins);

  sqlite3_bind_int(ins, 1, 6);
  sqlite3_bind_blob(ins, 2, "\x01\x02\x03", 3, SQLITE_TRANSIENT);
  printf("step6=%d\n", sqlite3_step(ins) == SQLITE_DONE);
  sqlite3_reset(ins);

  sqlite3_bind_int(ins, 1, 7);
  sqlite3_bind_null(ins, 2);
  printf("step7=%d\n", sqlite3_step(ins) == SQLITE_DONE);
  sqlite3_finalize(ins);

  /* a parameter that is never bound reads as NULL */
  if (sqlite3_prepare_v2(db, "INSERT INTO t(k, v) VALUES(8, ?)", -1, &ins, 0) != SQLITE_OK) return 3;
  printf("step8=%d\n", sqlite3_step(ins) == SQLITE_DONE);
  sqlite3_finalize(ins);

  if (sqlite3_prepare_v2(db,
        "SELECT k, typeof(v), CASE typeof(v) WHEN 'blob' THEN hex(v) ELSE v END"
        " FROM t ORDER BY k", -1, &sel, 0) != SQLITE_OK) return 4;
  while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
    printf("%d %s %s\n", sqlite3_column_int(sel, 0),
           sqlite3_column_text(sel, 1),
           sqlite3_column_type(sel, 2) == SQLITE_NULL
             ? (const unsigned char *)"(null)" : sqlite3_column_text(sel, 2));
  }
  sqlite3_finalize(sel);
  sqlite3_close(db);
  return 0;
}

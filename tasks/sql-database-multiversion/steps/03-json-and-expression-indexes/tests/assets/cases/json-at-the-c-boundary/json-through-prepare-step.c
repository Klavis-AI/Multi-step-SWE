/* The JSON functions are SQL functions, so the C library reaches them through
   the same sqlite3_prepare_v2()/sqlite3_step()/sqlite3_column_*() surface as
   any other expression (TEST_INTERFACE.md 3.2, 7).  json1.html 3.4 fixes the
   SQL datatype of a single-path json_extract(), which is what the type codes
   below observe.

   Every sqlite3_column_type() read is hoisted into a local BEFORE any other
   accessor touches that column: c3ref/column_blob.html leaves the type
   undefined after a conversion, and C leaves argument evaluation order
   unspecified. */
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

static sqlite3 *db;

static int one_text(const char *sql, const char *label) {
  sqlite3_stmt *st = 0;
  int rc, ty;
  if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return 1;
  if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return 1; }
  ty = sqlite3_column_type(st, 0);
  printf("%s|%d|%s\n", label, ty, (const char *)sqlite3_column_text(st, 0));
  sqlite3_finalize(st);
  return 0;
}

static int type_and_int(const char *sql, const char *label) {
  sqlite3_stmt *st = 0;
  int ty;
  if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return 1;
  if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return 1; }
  ty = sqlite3_column_type(st, 0);
  printf("%s|%d|%d\n", label, ty, sqlite3_column_int(st, 0));
  sqlite3_finalize(st);
  return 0;
}

int main(void) {
  sqlite3_stmt *st = 0;
  int rc, n, ty;
  const char *tail = 0;

  if (sqlite3_open("j.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db,
        "CREATE TABLE d(id INTEGER PRIMARY KEY, body TEXT);"
        "INSERT INTO d VALUES(1,'{\"name\":\"ann\",\"age\":31}'),"
                            "(2,'{\"name\":\"bob\",\"age\":25}');",
        0, 0, 0) != SQLITE_OK) return 2;

  /* json1.html 3.4: INTEGER for a JSON numeric value, dequoted text for a
     JSON string value, a text representation for an object. */
  if (type_and_int("SELECT json_extract('{\"a\":7}','$.a')", "int")) return 3;
  if (one_text("SELECT json_extract('{\"a\":\"xy\"}','$.a')", "text")) return 4;
  if (one_text("SELECT json_extract('{\"a\":{\"b\":1}}','$.a')", "obj")) return 5;
  if (one_text("SELECT json_object('k',json_array(1,2))", "built")) return 6;
  if (one_text("SELECT json(' [ 1 , 2 ] ')", "minified")) return 7;

  /* json1.html 3.4 again: a JSON null extracts as an SQL NULL. */
  if (sqlite3_prepare_v2(db, "SELECT json_extract('{\"a\":null}','$.a')",
                         -1, &st, 0) != SQLITE_OK) return 8;
  if (sqlite3_step(st) != SQLITE_ROW) return 9;
  ty = sqlite3_column_type(st, 0);
  printf("null|%d|%d\n", ty, ty == SQLITE_NULL);
  sqlite3_finalize(st);

  /* Two result columns from a query over stored JSON, and the column count
     and names the statement reports. */
  st = 0;
  if (sqlite3_prepare_v2(db,
        "SELECT json_extract(body,'$.name') AS nm,"
        " json_extract(body,'$.age') AS age FROM d ORDER BY id; SELECT 1;",
        -1, &st, &tail) != SQLITE_OK) return 10;
  n = sqlite3_column_count(st);
  printf("cols|%d|%s|%s\n", n, sqlite3_column_name(st, 0), sqlite3_column_name(st, 1));
  printf("tail|%s\n", tail);
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    int t0 = sqlite3_column_type(st, 0);
    int t1 = sqlite3_column_type(st, 1);
    printf("row|%d|%d|%s|%d\n", t0, t1,
           (const char *)sqlite3_column_text(st, 0), sqlite3_column_int(st, 1));
  }
  printf("done|%d\n", rc == SQLITE_DONE);
  sqlite3_finalize(st);

  /* A JSON document written through the library reads back through it. */
  if (sqlite3_exec(db,
        "UPDATE d SET body = json_set(body,'$.age',32) WHERE id=1;",
        0, 0, 0) != SQLITE_OK) return 11;
  if (type_and_int("SELECT json_extract(body,'$.age') FROM d WHERE id=1", "upd")) return 12;
  if (one_text("SELECT body FROM d WHERE id=1", "stored")) return 13;

  sqlite3_close(db);
  return 0;
}

#define _POSIX_C_SOURCE 200809L
#include "mxdb.h"
#include "mxutil.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *g_path;
static pthread_key_t g_key;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static const char *SCHEMA =
"PRAGMA journal_mode=WAL;"
"PRAGMA synchronous=NORMAL;"
"PRAGMA foreign_keys=ON;"
"CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY, v TEXT);"
"CREATE TABLE IF NOT EXISTS seq(name TEXT PRIMARY KEY, value INTEGER NOT NULL);"
"INSERT OR IGNORE INTO seq(name,value) VALUES('stream',0);"
"CREATE TABLE IF NOT EXISTS users("
"  user_id TEXT PRIMARY KEY, password_hash TEXT, created_ts INTEGER,"
"  deactivated INTEGER DEFAULT 0, displayname TEXT, avatar_url TEXT,"
"  is_guest INTEGER DEFAULT 0);"
"CREATE TABLE IF NOT EXISTS devices("
"  user_id TEXT, device_id TEXT, display_name TEXT, created_ts INTEGER,"
"  PRIMARY KEY(user_id, device_id));"
"CREATE TABLE IF NOT EXISTS access_tokens("
"  token TEXT PRIMARY KEY, user_id TEXT, device_id TEXT, created_ts INTEGER);"
"CREATE TABLE IF NOT EXISTS uia("
"  session TEXT PRIMARY KEY, user_id TEXT, kind TEXT, completed TEXT,"
"  params TEXT, created_ts INTEGER);"
"CREATE TABLE IF NOT EXISTS rooms("
"  room_id TEXT PRIMARY KEY, version TEXT, creator TEXT, created_ts INTEGER,"
"  published INTEGER DEFAULT 0);"
"CREATE TABLE IF NOT EXISTS events("
"  stream_id INTEGER PRIMARY KEY, event_id TEXT UNIQUE, room_id TEXT,"
"  type TEXT, state_key TEXT, sender TEXT, depth INTEGER,"
"  origin_server_ts INTEGER, json TEXT, is_state INTEGER,"
"  redacted_by TEXT, topological INTEGER);"
"CREATE INDEX IF NOT EXISTS ev_room ON events(room_id, stream_id);"
"CREATE INDEX IF NOT EXISTS ev_topo ON events(room_id, topological, stream_id);"
"CREATE TABLE IF NOT EXISTS room_state("
"  room_id TEXT, type TEXT, state_key TEXT, event_id TEXT,"
"  PRIMARY KEY(room_id, type, state_key));"
"CREATE TABLE IF NOT EXISTS state_before("
"  event_id TEXT PRIMARY KEY, state_json TEXT);"
"CREATE TABLE IF NOT EXISTS room_members("
"  room_id TEXT, user_id TEXT, membership TEXT, event_id TEXT,"
"  stream_id INTEGER, PRIMARY KEY(room_id, user_id));"
"CREATE INDEX IF NOT EXISTS mem_user ON room_members(user_id, membership);"
"CREATE TABLE IF NOT EXISTS account_data("
"  user_id TEXT, room_id TEXT, type TEXT, content TEXT, stream_id INTEGER,"
"  PRIMARY KEY(user_id, room_id, type));"
"CREATE TABLE IF NOT EXISTS room_tags("
"  user_id TEXT, room_id TEXT, tag TEXT, content TEXT, stream_id INTEGER,"
"  PRIMARY KEY(user_id, room_id, tag));"
"CREATE TABLE IF NOT EXISTS receipts("
"  room_id TEXT, user_id TEXT, receipt_type TEXT, thread_id TEXT,"
"  event_id TEXT, ts INTEGER, stream_id INTEGER,"
"  PRIMARY KEY(room_id, user_id, receipt_type, thread_id));"
"CREATE TABLE IF NOT EXISTS typing("
"  room_id TEXT, user_id TEXT, expires_ms INTEGER, stream_id INTEGER,"
"  PRIMARY KEY(room_id, user_id));"
"CREATE TABLE IF NOT EXISTS presence("
"  user_id TEXT PRIMARY KEY, presence TEXT, status_msg TEXT,"
"  last_active_ts INTEGER, stream_id INTEGER);"
"CREATE TABLE IF NOT EXISTS filters("
"  user_id TEXT, filter_id TEXT, content TEXT,"
"  PRIMARY KEY(user_id, filter_id));"
"CREATE TABLE IF NOT EXISTS aliases("
"  alias TEXT PRIMARY KEY, room_id TEXT, creator TEXT);"
"CREATE TABLE IF NOT EXISTS erased_users("
"  user_id TEXT PRIMARY KEY, stream_id INTEGER);"
"CREATE TABLE IF NOT EXISTS media("
"  media_id TEXT PRIMARY KEY, user_id TEXT, content_type TEXT, filename TEXT,"
"  size INTEGER, path TEXT, created_ts INTEGER, unused_expires_at INTEGER);"
"CREATE TABLE IF NOT EXISTS transactions("
"  user_id TEXT, device_id TEXT, txn_id TEXT, path TEXT, response TEXT,"
"  PRIMARY KEY(user_id, device_id, txn_id, path));"
"CREATE TABLE IF NOT EXISTS to_device("
"  stream_id INTEGER PRIMARY KEY, user_id TEXT, device_id TEXT, sender TEXT,"
"  type TEXT, content TEXT);"
"CREATE INDEX IF NOT EXISTS td_user ON to_device(user_id, device_id, stream_id);"
;

static void make_key(void) { pthread_key_create(&g_key, NULL); }

sqlite3 *db(void) {
    pthread_once(&g_once, make_key);
    sqlite3 *h = pthread_getspecific(g_key);
    if (h) return h;
    if (!g_path) return NULL;
    if (sqlite3_open(g_path, &h) != SQLITE_OK) {
        fprintf(stderr, "sqlite open %s failed\n", g_path);
        return NULL;
    }
    sqlite3_busy_timeout(h, 30000);
    sqlite3_exec(h, "PRAGMA journal_mode=WAL;PRAGMA synchronous=NORMAL;"
                    "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    pthread_setspecific(g_key, h);
    return h;
}

bool db_open(const char *path) {
    const char *p = path;
    if (str_prefix(p, "file:")) p += 5;
    g_path = xstrdup_(p);
    sqlite3 *h = db();
    if (!h) return false;
    char *err = NULL;
    if (sqlite3_exec(h, SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "schema: %s\n", err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    return true;
}

void db_close(void) { free(g_path); g_path = NULL; }

bool db_exec(const char *sql) {
    char *err = NULL;
    if (sqlite3_exec(db(), sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "sql error: %s (%s)\n", err ? err : "?", sql);
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool db_execf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *sql = sqlite3_vmprintf(fmt, ap);
    va_end(ap);
    bool ok = db_exec(sql);
    sqlite3_free(sql);
    return ok;
}

static sqlite3_stmt *prep_bind(const char *sql, const char *types, va_list ap) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), sql, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "prepare failed: %s (%s)\n", sqlite3_errmsg(db()), sql);
        return NULL;
    }
    int idx = 1;
    for (const char *t = types ? types : ""; *t; t++, idx++) {
        switch (*t) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s) sqlite3_bind_text(st, idx, s, -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(st, idx);
            break;
        }
        case 'i': sqlite3_bind_int64(st, idx, va_arg(ap, int64_t)); break;
        case 'b': {
            const void *p = va_arg(ap, const void *);
            size_t n = va_arg(ap, size_t);
            sqlite3_bind_blob(st, idx, p, (int)n, SQLITE_TRANSIENT);
            break;
        }
        case 'n': sqlite3_bind_null(st, idx); break;
        default: break;
        }
    }
    return st;
}

bool db_run(const char *sql, const char *types, ...) {
    va_list ap;
    va_start(ap, types);
    sqlite3_stmt *st = prep_bind(sql, types, ap);
    va_end(ap);
    if (!st) return false;
    int rc = sqlite3_step(st);
    bool ok = (rc == SQLITE_DONE || rc == SQLITE_ROW);
    if (!ok) fprintf(stderr, "step failed: %s (%s)\n", sqlite3_errmsg(db()), sql);
    sqlite3_finalize(st);
    return ok;
}

char *db_str(const char *sql, const char *types, ...) {
    va_list ap;
    va_start(ap, types);
    sqlite3_stmt *st = prep_bind(sql, types, ap);
    va_end(ap);
    if (!st) return NULL;
    char *out = NULL;
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL)
        out = xstrdup_((const char *)sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    return out;
}

bool db_int(const char *sql, int64_t *out, const char *types, ...) {
    va_list ap;
    va_start(ap, types);
    sqlite3_stmt *st = prep_bind(sql, types, ap);
    va_end(ap);
    if (!st) return false;
    bool got = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        if (out) *out = sqlite3_column_int64(st, 0);
        got = true;
    }
    sqlite3_finalize(st);
    return got;
}

int64_t db_last_insert_rowid(void) { return sqlite3_last_insert_rowid(db()); }

bool db_begin(void) { return db_exec("BEGIN IMMEDIATE"); }
bool db_commit(void) { return db_exec("COMMIT"); }
bool db_rollback(void) { return db_exec("ROLLBACK"); }

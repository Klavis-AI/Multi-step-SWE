#define _POSIX_C_SOURCE 200809L
/* Push rules, notifications, event reports, presence refinements and the
 * login-token / refresh-token surfaces of releases v1.7 to v1.9. */
#include "mxpush.h"

#include "mxapi.h"
#include "mxdb.h"
#include "mxevent.h"
#include "mxroom.h"
#include "mxserver.h"
#include "mxutil.h"

#include <openssl/evp.h>

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS  "/_matrix/client/v3"
#define CSR "/_matrix/client/r0"
#define CS1 "/_matrix/client/v1"

/* A login token is single-use and short lived; two minutes is the value the
 * specification recommends. */
#define LOGIN_TOKEN_MS 120000
/* What an access token issued alongside a refresh token is advertised to last. */
#define ACCESS_TOKEN_MS 3600000
/* How many events one lazy evaluation pass walks per room. */
#define SCAN_MAX 4000
/* The largest `content.body` a glob condition is matched against, the longest
 * pattern that is matched at all, and the work one match may cost. */
#define BODY_MAX 8192
#define PATTERN_MAX 512
#define GLOB_BUDGET 200000
#define NOTIF_DEFAULT_LIMIT 50
#define NOTIF_MAX_LIMIT 500

/* ------------------------------------------------------------- schema ----- */
static void push_init(void) {
    db_exec(
        "CREATE TABLE IF NOT EXISTS push_rules("
        "  user_id TEXT NOT NULL, scope TEXT NOT NULL, kind TEXT NOT NULL,"
        "  rule_id TEXT NOT NULL, priority INTEGER NOT NULL DEFAULT 0,"
        "  enabled INTEGER, conditions TEXT, actions TEXT, pattern TEXT,"
        "  is_default INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(user_id, scope, kind, rule_id));"
        "CREATE TABLE IF NOT EXISTS push_notifications("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, user_id TEXT NOT NULL,"
        "  room_id TEXT NOT NULL, event_id TEXT NOT NULL, stream_id INTEGER,"
        "  actions TEXT, profile_tag TEXT, highlight INTEGER NOT NULL DEFAULT 0,"
        "  ts INTEGER);"
        "CREATE UNIQUE INDEX IF NOT EXISTS pn_unique"
        "  ON push_notifications(user_id, event_id);"
        "CREATE INDEX IF NOT EXISTS pn_room"
        "  ON push_notifications(user_id, room_id, stream_id);"
        "CREATE TABLE IF NOT EXISTS push_scan_state("
        "  user_id TEXT PRIMARY KEY, last_stream INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS event_reports("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, room_id TEXT, event_id TEXT,"
        "  user_id TEXT, score INTEGER, reason TEXT, ts INTEGER);"
        "CREATE TABLE IF NOT EXISTS login_tokens("
        "  token TEXT PRIMARY KEY, user_id TEXT NOT NULL, expires_ts INTEGER,"
        "  used INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS refresh_tokens("
        "  token TEXT PRIMARY KEY, user_id TEXT NOT NULL, device_id TEXT,"
        "  access_token TEXT, next_token TEXT, created_ts INTEGER,"
"  spent_predecessor INTEGER DEFAULT 0);");
}

/* --------------------------------------------------------- small helpers -- */
static json *parse_text(const char *text) {
    return text ? j_parse(text, strlen(text), NULL) : NULL;
}

static json *parse_owned(char *text) {
    json *v = parse_text(text);
    free(text);
    return v;
}

static char *dump_text(const json *v) {
    size_t n = 0;
    char *s = j_dump(v, &n);
    return s;
}

static const char *col_str(sqlite3_stmt *st, int col) {
    const char *s = (const char *)sqlite3_column_text(st, col);
    return s ? s : "";
}

/* ------------------------------------------------- glob-style matching ---- */
/* "Glob-style matching": `*` matches zero or more characters, `?` exactly one.
 * Matching is case-insensitive, as the push module requires. */
static bool is_word_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* A word boundary is the start or end of the value, or any character outside
 * [A-Z], [a-z], [0-9] and `_`. */
static bool boundary_before(const char *s, size_t i) {
    return i == 0 || !is_word_char((unsigned char)s[i - 1]);
}

static bool boundary_at(const char *s, size_t len, size_t i) {
    return i == len || !is_word_char((unsigned char)s[i]);
}

/* Matches `pat` against t[0..len).  When `wb_end` the match may stop at any
 * word boundary of the whole value `base` rather than only at its end.  A rule
 * comes from a client, so the work a pattern full of wildcards can ask for is
 * bounded rather than trusted; running out of budget is not a match. */
static bool glob_at(const char *pat, const char *base, size_t len, size_t pos,
                    bool wb_end, int *budget) {
    if (--*budget < 0) return false;
    if (*pat == 0)
        return wb_end ? boundary_at(base, len, pos) : pos == len;
    if (*pat == '*') {
        for (size_t q = pos;; q++) {
            if (glob_at(pat + 1, base, len, q, wb_end, budget)) return true;
            if (q == len || *budget < 0) break;
        }
        return false;
    }
    if (pos == len) return false;
    if (*pat != '?' &&
        tolower((unsigned char)*pat) != tolower((unsigned char)base[pos]))
        return false;
    return glob_at(pat + 1, base, len, pos + 1, wb_end, budget);
}

static bool glob_usable(const char *pat) {
    return pat && strlen(pat) <= PATTERN_MAX;
}

/* The whole value must match the pattern. */
static bool glob_whole(const char *pat, const char *value) {
    if (!glob_usable(pat) || !value) return false;
    int budget = GLOB_BUDGET;
    return glob_at(pat, value, strlen(value), 0, false, &budget);
}

/* As a special case for `content.body` and for content rules, the pattern must
 * match any substring which starts and ends at a word boundary. */
static bool glob_word(const char *pat, const char *value) {
    if (!glob_usable(pat) || !value) return false;
    size_t len = strlen(value);
    if (len > BODY_MAX) len = BODY_MAX;
    int budget = GLOB_BUDGET;
    for (size_t i = 0; i <= len && budget > 0; i++) {
        if (!boundary_before(value, i)) continue;
        if (glob_at(pat, value, len, i, true, &budget)) return true;
    }
    return false;
}

/* ------------------------------------------ dot-separated property paths -- */
/* Copies the next segment of a dot-separated property path into `out` and
 * returns the position just past it.  A literal dot or backslash in a property
 * name is escaped with a backslash; every other escape sequence is left as-is. */
static const char *path_segment(const char *p, char *out, size_t n) {
    size_t o = 0;
    while (*p && *p != '.') {
        char c = *p;
        if (c == '\\' && (p[1] == '.' || p[1] == '\\')) {
            c = p[1];
            p += 2;
        } else {
            p++;
        }
        if (o + 1 < n) out[o++] = c;
    }
    out[o] = 0;
    return p;
}

/* Resolves a dot-separated property path against an event.  Returns a borrowed
 * value, or NULL when any step is missing or not an object. */
static const json *path_get(const json *ev, const char *key) {
    if (!ev || !key) return NULL;
    const json *cur = ev;
    const char *p = key;
    char seg[256];
    for (;;) {
        p = path_segment(p, seg, sizeof seg);
        if (!cur || cur->type != J_OBJECT) return NULL;
        cur = j_get(cur, seg);
        if (!cur) return NULL;
        if (*p != '.') break;
        p++;
    }
    return cur;
}

/* Exact equality over the non-compound canonical JSON values: strings,
 * integers, booleans and null. */
static bool json_scalar_eq(const json *a, const json *b) {
    if (!a || !b || a->type != b->type) return false;
    switch (a->type) {
    case J_STRING: return a->slen == b->slen && memcmp(a->s, b->s, a->slen) == 0;
    case J_INT:    return a->i == b->i;
    case J_BOOL:   return a->b == b->b;
    case J_NULL:   return true;
    default:       return false;
    }
}

/* ---------------------------------------------------- the default rules --- */
/* The server-default rules, exactly as the Push Notifications module lists
 * them, in the order it lists them (which is their priority order within each
 * kind).  `%U` stands for the user's Matrix ID and `%L` for its local part. */
typedef struct {
    const char *kind;
    const char *rule_id;
    bool enabled;
    const char *conditions;     /* JSON array; NULL for a content rule */
    const char *actions;        /* JSON array */
    const char *pattern;        /* content rules only */
} defrule;

static const defrule DEFAULTS[] = {
    /* ------------------------------------------------ default override --- */
    {"override", ".m.rule.master", false, "[]", "[]", NULL},
    {"override", ".m.rule.suppress_notices", true,
     "[{\"kind\":\"event_match\",\"key\":\"content.msgtype\","
      "\"pattern\":\"m.notice\"}]", "[]", NULL},
    {"override", ".m.rule.invite_for_me", true,
     "[{\"key\":\"type\",\"kind\":\"event_match\",\"pattern\":\"m.room.member\"},"
      "{\"key\":\"content.membership\",\"kind\":\"event_match\","
      "\"pattern\":\"invite\"},"
      "{\"key\":\"state_key\",\"kind\":\"event_match\",\"pattern\":\"%U\"}]",
     "[\"notify\",{\"set_tweak\":\"sound\",\"value\":\"default\"}]", NULL},
    {"override", ".m.rule.member_event", true,
     "[{\"key\":\"type\",\"kind\":\"event_match\",\"pattern\":\"m.room.member\"}]",
     "[]", NULL},
    {"override", ".m.rule.is_user_mention", true,
     "[{\"kind\":\"event_property_contains\","
      "\"key\":\"content.m\\\\.mentions.user_ids\",\"value\":\"%U\"}]",
     "[\"notify\",{\"set_tweak\":\"sound\",\"value\":\"default\"},"
      "{\"set_tweak\":\"highlight\"}]", NULL},
    {"override", ".m.rule.contains_display_name", true,
     "[{\"kind\":\"contains_display_name\"}]",
     "[\"notify\",{\"set_tweak\":\"sound\",\"value\":\"default\"},"
      "{\"set_tweak\":\"highlight\"}]", NULL},
    {"override", ".m.rule.is_room_mention", true,
     "[{\"kind\":\"event_property_is\",\"key\":\"content.m\\\\.mentions.room\","
      "\"value\":true},"
      "{\"kind\":\"sender_notification_permission\",\"key\":\"room\"}]",
     "[\"notify\",{\"set_tweak\":\"highlight\"}]", NULL},
    {"override", ".m.rule.roomnotif", true,
     "[{\"kind\":\"event_match\",\"key\":\"content.body\",\"pattern\":\"@room\"},"
      "{\"kind\":\"sender_notification_permission\",\"key\":\"room\"}]",
     "[\"notify\",{\"set_tweak\":\"highlight\"}]", NULL},
    {"override", ".m.rule.tombstone", true,
     "[{\"kind\":\"event_match\",\"key\":\"type\","
      "\"pattern\":\"m.room.tombstone\"},"
      "{\"kind\":\"event_match\",\"key\":\"state_key\",\"pattern\":\"\"}]",
     "[\"notify\",{\"set_tweak\":\"highlight\"}]", NULL},
    {"override", ".m.rule.reaction", true,
     "[{\"kind\":\"event_match\",\"key\":\"type\",\"pattern\":\"m.reaction\"}]",
     "[]", NULL},
    {"override", ".m.rule.room.server_acl", true,
     "[{\"kind\":\"event_match\",\"key\":\"type\","
      "\"pattern\":\"m.room.server_acl\"},"
      "{\"kind\":\"event_match\",\"key\":\"state_key\",\"pattern\":\"\"}]",
     "[]", NULL},
    {"override", ".m.rule.suppress_edits", true,
     "[{\"kind\":\"event_property_is\","
      "\"key\":\"content.m\\\\.relates_to.rel_type\",\"value\":\"m.replace\"}]",
     "[]", NULL},

    /* ------------------------------------------------- default content --- */
    {"content", ".m.rule.contains_user_name", true, NULL,
     "[\"notify\",{\"set_tweak\":\"sound\",\"value\":\"default\"},"
      "{\"set_tweak\":\"highlight\"}]", "%L"},

    /* ----------------------------------------------- default underride --- */
    {"underride", ".m.rule.call", true,
     "[{\"key\":\"type\",\"kind\":\"event_match\",\"pattern\":\"m.call.invite\"}]",
     "[\"notify\",{\"set_tweak\":\"sound\",\"value\":\"ring\"}]", NULL},
    {"underride", ".m.rule.encrypted_room_one_to_one", true,
     "[{\"kind\":\"room_member_count\",\"is\":\"2\"},"
      "{\"kind\":\"event_match\",\"key\":\"type\","
      "\"pattern\":\"m.room.encrypted\"}]",
     "[\"notify\",{\"set_tweak\":\"sound\",\"value\":\"default\"}]", NULL},
    {"underride", ".m.rule.room_one_to_one", true,
     "[{\"kind\":\"room_member_count\",\"is\":\"2\"},"
      "{\"kind\":\"event_match\",\"key\":\"type\","
      "\"pattern\":\"m.room.message\"}]",
     "[\"notify\",{\"set_tweak\":\"sound\",\"value\":\"default\"}]", NULL},
    {"underride", ".m.rule.message", true,
     "[{\"kind\":\"event_match\",\"key\":\"type\","
      "\"pattern\":\"m.room.message\"}]", "[\"notify\"]", NULL},
    {"underride", ".m.rule.encrypted", true,
     "[{\"kind\":\"event_match\",\"key\":\"type\","
      "\"pattern\":\"m.room.encrypted\"}]", "[\"notify\"]", NULL},
};
#define NDEFAULTS ((int)(sizeof DEFAULTS / sizeof DEFAULTS[0]))

/* The rules the specification deprecated in v1.7: they match on `content.body`
 * and are only to be applied to events that carry no `m.mentions` property. */
static bool rule_superseded_by_mentions(const char *rule_id) {
    return str_eq(rule_id, ".m.rule.contains_display_name") ||
           str_eq(rule_id, ".m.rule.roomnotif") ||
           str_eq(rule_id, ".m.rule.contains_user_name");
}

static const char *user_localpart(const char *user_id) {
    return user_id && user_id[0] == '@' ? user_id + 1 : user_id;
}

/* Expands `%U` and `%L` in a default rule's JSON template. */
static char *expand(const char *tmpl, const char *user_id) {
    if (!tmpl) return NULL;
    const char *server = user_id_server(user_id);
    size_t lp_len = server && server > user_id ? (size_t)(server - user_id - 2) : 0;
    buf out;
    memset(&out, 0, sizeof out);
    for (const char *p = tmpl; *p; p++) {
        if (p[0] != '%') { buf_add(&out, p, 1); continue; }
        if (p[1] == 'U') { buf_puts(&out, user_id); p++; }
        else if (p[1] == 'L') { buf_add(&out, user_localpart(user_id), lp_len); p++; }
        else buf_add(&out, p, 1);
    }
    size_t n = 0;
    return buf_take(&out, &n);
}

static json *expand_json(const char *tmpl, const char *user_id) {
    char *text = expand(tmpl, user_id);
    json *v = parse_owned(text);
    return v ? v : j_arr();
}

/* One default rule as the API serves it, with the user's own `enabled` and
 * `actions` applied when they have changed them. */
static json *default_rule_json(const defrule *d, const char *user_id) {
    json *r = j_obj();
    char *ov_actions = db_str("SELECT actions FROM push_rules WHERE user_id=?1 "
                              "AND scope='global' AND kind=?2 AND rule_id=?3",
                              "sss", user_id, d->kind, d->rule_id);
    int64_t enabled = d->enabled ? 1 : 0;
    db_int("SELECT enabled FROM push_rules WHERE user_id=?1 AND scope='global' "
           "AND kind=?2 AND rule_id=?3 AND enabled IS NOT NULL", &enabled,
           "sss", user_id, d->kind, d->rule_id);
    j_set(r, "rule_id", j_str(d->rule_id));
    j_set(r, "default", j_bool(true));
    j_set(r, "enabled", j_bool(enabled != 0));
    if (d->conditions) j_set(r, "conditions", expand_json(d->conditions, user_id));
    if (d->pattern) {
        char *pat = expand(d->pattern, user_id);
        j_set(r, "pattern", j_str(pat));
        free(pat);
    }
    json *actions = ov_actions ? parse_owned(ov_actions)
                               : expand_json(d->actions, user_id);
    j_set(r, "actions", actions ? actions : j_arr());
    return r;
}

/* One user-defined rule, read from the row the statement is sitting on:
 * rule_id, enabled, conditions, actions, pattern. */
static json *user_rule_json(sqlite3_stmt *st) {
    json *r = j_obj();
    j_set(r, "rule_id", j_str(col_str(st, 0)));
    j_set(r, "default", j_bool(false));
    j_set(r, "enabled", j_bool(sqlite3_column_int64(st, 1) != 0));
    json *cond = parse_text((const char *)sqlite3_column_text(st, 2));
    if (cond) j_set(r, "conditions", cond);
    json *act = parse_text((const char *)sqlite3_column_text(st, 3));
    j_set(r, "actions", act ? act : j_arr());
    if (sqlite3_column_type(st, 4) != SQLITE_NULL)
        j_set(r, "pattern", j_str(col_str(st, 4)));
    return r;
}

static json *user_rules_of_kind(const char *user_id, const char *kind) {
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT rule_id, COALESCE(enabled,1), conditions, actions, pattern "
            "FROM push_rules WHERE user_id=?1 AND scope='global' AND kind=?2 "
            "AND is_default=0 ORDER BY priority ASC, rowid ASC",
            -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, kind, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) j_push(out, user_rule_json(st));
    sqlite3_finalize(st);
    return out;
}

static const char *const KINDS[] = {"override", "content", "room", "sender",
                                    "underride"};
#define NKINDS 5

static bool valid_kind(const char *kind) {
    for (int i = 0; i < NKINDS; i++) if (str_eq(kind, KINDS[i])) return true;
    return false;
}

json *push_ruleset(const char *user_id) {
    json *set = j_obj();
    for (int k = 0; k < NKINDS; k++) {
        const char *kind = KINDS[k];
        json *arr = j_arr();
        /* `.m.rule.master` always outranks every other rule, user rules
         * included; every other server default sits below them. */
        if (str_eq(kind, "override"))
            j_push(arr, default_rule_json(&DEFAULTS[0], user_id));
        json *mine = user_rules_of_kind(user_id, kind);
        for (size_t i = 0; i < j_len(mine); i++)
            j_push(arr, j_clone(j_at(mine, i)));
        j_free(mine);
        for (int i = 0; i < NDEFAULTS; i++) {
            if (!str_eq(DEFAULTS[i].kind, kind)) continue;
            if (str_eq(DEFAULTS[i].rule_id, ".m.rule.master")) continue;
            j_push(arr, default_rule_json(&DEFAULTS[i], user_id));
        }
        j_set(set, kind, arr);
    }
    return set;
}

/* ------------------------------------------------------------ conditions -- */
/* The display name `user_id` is currently using in `room_id`, falling back to
 * their profile.  Caller frees. */
static char *display_name_in(const char *room_id, const char *user_id) {
    json *member = room_state_event(room_id, "m.room.member", user_id);
    char *name = NULL;
    if (member) {
        const char *dn = j_gets(j_geto(member, "content"), "displayname", NULL);
        if (dn && *dn) name = xstrdup_(dn);
        j_free(member);
    }
    if (!name)
        name = db_str("SELECT displayname FROM users WHERE user_id=?1", "s",
                      user_id);
    return name;
}

static int64_t joined_member_count(const char *room_id) {
    int64_t n = 0;
    db_int("SELECT COUNT(*) FROM room_members WHERE room_id=?1 AND "
           "membership='join'", &n, "s", room_id);
    return n;
}

/* `is`: a decimal integer optionally prefixed by ==, <, >, >= or <=. */
static bool member_count_matches(const char *is, int64_t count) {
    if (!is) return false;
    while (*is == ' ') is++;
    const char *op = "==";
    if (!strncmp(is, ">=", 2) || !strncmp(is, "<=", 2) || !strncmp(is, "==", 2)) {
        op = is;
        is += 2;
    } else if (*is == '<' || *is == '>') {
        op = is;
        is += 1;
    }
    char *end = NULL;
    long long want = strtoll(is, &end, 10);
    if (end == is) return false;
    if (!strncmp(op, ">=", 2)) return count >= want;
    if (!strncmp(op, "<=", 2)) return count <= want;
    if (*op == '<') return count < want;
    if (*op == '>') return count > want;
    return count == want;
}

/* The power level the sender needs to trigger a notification of `key`, from
 * the `notifications` object of the room's m.room.power_levels. */
static bool sender_may_notify(const char *room_id, const char *sender,
                              const char *key) {
    if (!key) return false;
    int64_t need = 50;
    json *pl = room_state_event(room_id, "m.room.power_levels", "");
    if (pl) {
        json *content = j_geto(pl, "content");
        json *notifs = content ? j_geto(content, "notifications") : NULL;
        if (notifs) {
            json *v = j_get(notifs, key);
            if (v && v->type == J_INT) need = v->i;
        }
        j_free(pl);
    }
    return room_user_level(room_id, sender) >= need;
}

static bool condition_holds(const json *cond, const char *user_id,
                            const char *room_id, const json *ev) {
    const char *kind = j_gets(cond, "kind", "");
    if (str_eq(kind, "event_match")) {
        const char *key = j_gets(cond, "key", NULL);
        const char *pattern = j_gets(cond, "pattern", NULL);
        if (!key || !pattern) return false;
        const json *v = path_get(ev, key);
        if (!v || !j_is_str(v)) return false;
        /* `content.body` matches any substring at word boundaries; every other
         * property must match in full. */
        return str_eq(key, "content.body") ? glob_word(pattern, v->s)
                                           : glob_whole(pattern, v->s);
    }
    if (str_eq(kind, "event_property_is")) {
        const char *key = j_gets(cond, "key", NULL);
        const json *want = j_get(cond, "value");
        if (!key || !want) return false;
        return json_scalar_eq(path_get(ev, key), want);
    }
    if (str_eq(kind, "event_property_contains")) {
        const char *key = j_gets(cond, "key", NULL);
        const json *want = j_get(cond, "value");
        if (!key || !want) return false;
        const json *arr = path_get(ev, key);
        if (!arr || arr->type != J_ARRAY) return false;
        for (size_t i = 0; i < j_len(arr); i++)
            if (json_scalar_eq(j_at(arr, i), want)) return true;
        return false;
    }
    if (str_eq(kind, "contains_display_name")) {
        const json *body = path_get(ev, "content.body");
        if (!body || !j_is_str(body)) return false;
        char *name = display_name_in(room_id, user_id);
        bool ok = name && *name && glob_word(name, body->s);
        free(name);
        return ok;
    }
    if (str_eq(kind, "room_member_count"))
        return member_count_matches(j_gets(cond, "is", NULL),
                                    joined_member_count(room_id));
    if (str_eq(kind, "sender_notification_permission"))
        return sender_may_notify(room_id, j_gets(ev, "sender", ""),
                                 j_gets(cond, "key", NULL));
    /* "Unrecognised conditions MUST NOT match any events." */
    return false;
}

/* --------------------------------------------------------- evaluation ----- */
static void actions_effect(const json *actions, bool *notify, bool *highlight) {
    *notify = false;
    *highlight = false;
    for (size_t i = 0; actions && i < j_len(actions); i++) {
        const json *a = j_at(actions, i);
        if (j_is_str(a)) {
            if (str_eq(a->s, "notify")) *notify = true;
            continue;
        }
        if (!a || a->type != J_OBJECT) continue;
        const char *tweak = j_gets(a, "set_tweak", NULL);
        if (!tweak || !str_eq(tweak, "highlight")) continue;
        const json *v = j_get(a, "value");
        /* "If a highlight tweak is given with no value, its value is defined
         * to be true." */
        *highlight = !v || (v->type == J_BOOL && v->b);
    }
}

static bool rule_matches(const json *rule, const char *kind, const char *user_id,
                         const char *room_id, const json *ev, bool has_mentions) {
    if (!j_getb(rule, "enabled", true)) return false;
    const char *rule_id = j_gets(rule, "rule_id", "");
    if (has_mentions && rule_superseded_by_mentions(rule_id)) return false;

    if (str_eq(kind, "content")) {
        const char *pattern = j_gets(rule, "pattern", NULL);
        const json *body = path_get(ev, "content.body");
        if (!pattern || !*pattern || !body || !j_is_str(body)) return false;
        return glob_word(pattern, body->s);
    }
    if (str_eq(kind, "room")) return str_eq(rule_id, room_id);
    if (str_eq(kind, "sender")) return str_eq(rule_id, j_gets(ev, "sender", ""));

    /* override and underride: every condition must hold, and a rule with no
     * conditions always matches. */
    const json *conds = j_geta((json *)rule, "conditions");
    for (size_t i = 0; conds && i < j_len(conds); i++)
        if (!condition_holds(j_at(conds, i), user_id, room_id, ev)) return false;
    return true;
}

static json *evaluate(const json *ruleset, const char *user_id,
                      const char *room_id, const json *ev) {
    const json *content = j_geto((json *)ev, "content");
    bool has_mentions = content && j_get(content, "m.mentions") != NULL;
    for (int k = 0; k < NKINDS; k++) {
        const json *arr = j_geta((json *)ruleset, KINDS[k]);
        for (size_t i = 0; arr && i < j_len(arr); i++) {
            const json *rule = j_at(arr, i);
            if (!rule_matches(rule, KINDS[k], user_id, room_id, ev, has_mentions))
                continue;
            const json *actions = j_geta((json *)rule, "actions");
            return actions ? j_clone(actions) : j_arr();
        }
    }
    return NULL;
}

json *push_actions_for(const char *user_id, const char *room_id,
                       const json *event, bool *notify, bool *highlight) {
    bool n = false, h = false;
    json *ruleset = push_ruleset(user_id);
    json *actions = evaluate(ruleset, user_id, room_id, event);
    j_free(ruleset);
    if (actions) actions_effect(actions, &n, &h);
    if (notify) *notify = n;
    if (highlight) *highlight = h;
    return actions;
}

/* ------------------------------------------------- lazy rule evaluation --- */
/* How far the user has read in a room.  Both m.read and m.read.private clear
 * notifications, and the receipt which is further ahead wins. */
static int64_t read_upto_stream(const char *user_id, const char *room_id) {
    int64_t s = 0;
    db_int("SELECT COALESCE(MAX(e.stream_id),0) FROM receipts r "
           "JOIN events e ON e.event_id = r.event_id "
           "WHERE r.room_id=?1 AND r.user_id=?2 AND "
           "r.receipt_type IN ('m.read','m.read.private')", &s, "ss",
           room_id, user_id);
    return s;
}

static bool record_notification(const char *user_id, const char *room_id,
                                const json *ev, int64_t stream_id,
                                const json *actions, bool highlight) {
    char *text = dump_text(actions);
    bool ok = db_run(
        "INSERT OR IGNORE INTO push_notifications(user_id,room_id,event_id,"
        "stream_id,actions,profile_tag,highlight,ts) "
        "VALUES(?1,?2,?3,?4,?5,NULL,?6,?7)", "sssisii", user_id, room_id,
        j_gets(ev, "event_id", ""), stream_id, text,
        (int64_t)(highlight ? 1 : 0), j_geti(ev, "origin_server_ts", 0));
    free(text);
    return ok;
}

static bool scan_room(const json *ruleset, const char *user_id,
                      const char *room_id, int64_t from, int64_t upto) {
    bool wrote = false;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT stream_id, json FROM events WHERE room_id=?1 AND "
            "stream_id>?2 AND stream_id<=?3 AND sender<>?4 "
            "ORDER BY stream_id ASC LIMIT ?5", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, from);
    sqlite3_bind_int64(st, 3, upto);
    sqlite3_bind_text(st, 4, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, SCAN_MAX);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int64_t sid = sqlite3_column_int64(st, 0);
        json *ev = parse_text((const char *)sqlite3_column_text(st, 1));
        if (!ev) continue;
        json *actions = evaluate(ruleset, user_id, room_id, ev);
        bool notify = false, highlight = false;
        if (actions) actions_effect(actions, &notify, &highlight);
        if (notify && record_notification(user_id, room_id, ev, sid, actions,
                                          highlight))
            wrote = true;
        j_free(actions);
        j_free(ev);
    }
    sqlite3_finalize(st);
    return wrote;
}

/* An invited user is only delivered the invite itself, not the room's
 * timeline, so that is the only event their rules run over. */
static bool scan_invite(const json *ruleset, const char *user_id,
                        const char *room_id, const char *event_id,
                        int64_t stream_id) {
    json *ev = event_by_id(event_id);
    if (!ev) return false;
    bool wrote = false;
    if (!str_eq(j_gets(ev, "sender", ""), user_id)) {
        json *actions = evaluate(ruleset, user_id, room_id, ev);
        bool notify = false, highlight = false;
        if (actions) actions_effect(actions, &notify, &highlight);
        if (notify && record_notification(user_id, room_id, ev, stream_id,
                                          actions, highlight))
            wrote = true;
        j_free(actions);
    }
    j_free(ev);
    return wrote;
}

/* Runs every event delivered to `user_id` since the last pass through their
 * rules and records the ones a rule asked to notify about. */
static void push_scan(const char *user_id) {
    int64_t last = 0, now = stream_current();
    db_int("SELECT last_stream FROM push_scan_state WHERE user_id=?1", &last,
           "s", user_id);
    if (now <= last) return;

    hs_lock();
    last = 0;
    db_int("SELECT last_stream FROM push_scan_state WHERE user_id=?1", &last,
           "s", user_id);
    now = stream_current();
    if (now <= last) { hs_unlock(); return; }

    json *rooms = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT room_id, membership, stream_id, event_id FROM room_members "
            "WHERE user_id=?1 AND membership IN ('join','invite')",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            json *m = j_obj();
            j_set(m, "room_id", j_str(col_str(st, 0)));
            j_set(m, "membership", j_str(col_str(st, 1)));
            j_set(m, "stream_id", j_int(sqlite3_column_int64(st, 2)));
            j_set(m, "event_id", j_str(col_str(st, 3)));
            j_push(rooms, m);
        }
        sqlite3_finalize(st);
    }

    json *ruleset = push_ruleset(user_id);
    bool wrote = false;
    for (size_t i = 0; i < j_len(rooms); i++) {
        json *m = j_at(rooms, i);
        const char *room_id = j_gets(m, "room_id", "");
        int64_t mem = j_geti(m, "stream_id", 0);
        if (str_eq(j_gets(m, "membership", ""), "invite")) {
            if (mem > last && mem <= now)
                wrote |= scan_invite(ruleset, user_id, room_id,
                                     j_gets(m, "event_id", ""), mem);
            continue;
        }
        /* Nothing from before the user joined was ever delivered to them. */
        int64_t from = last > mem - 1 ? last : mem - 1;
        wrote |= scan_room(ruleset, user_id, room_id, from, now);
    }
    j_free(ruleset);
    j_free(rooms);

    db_run("INSERT OR REPLACE INTO push_scan_state(user_id,last_stream) "
           "VALUES(?1,?2)", "si", user_id, now);
    hs_unlock();
    if (wrote) stream_notify();
}

/* What the rules decided about one event when it was delivered.  Answers
 * false when the event has not been through them yet, which leaves the caller
 * to judge it itself.  The recorded decision is what makes a count stable:
 * editing the rules, or the room's power levels, after an event has arrived
 * does not re-open the question of whether it notified. */
bool push_decision(const char *user_id, const char *room_id, int64_t stream_id,
                   const char *event_id, bool *notify, bool *highlight) {
    if (!user_id || !event_id) return false;
    push_scan(user_id);
    int64_t last = 0;
    db_int("SELECT last_stream FROM push_scan_state WHERE user_id=?1", &last,
           "s", user_id);
    if (stream_id > last) return false;
    int64_t hit = -1;
    db_int("SELECT highlight FROM push_notifications WHERE user_id=?1 AND "
           "event_id=?2", &hit, "ss", user_id, event_id);
    (void)room_id;
    if (notify) *notify = hit >= 0;
    if (highlight) *highlight = hit > 0;
    return true;
}

void push_unread_counts(const char *user_id, const char *room_id, int64_t upto,
                        int64_t *notif, int64_t *highlight) {
    if (notif) *notif = 0;
    if (highlight) *highlight = 0;
    if (!user_id || !room_id) return;
    push_scan(user_id);
    int64_t read = read_upto_stream(user_id, room_id);
    int64_t bound = upto > 0 ? upto : INT64_MAX;
    int64_t n = 0, h = 0;
    db_int("SELECT COUNT(*) FROM push_notifications WHERE user_id=?1 AND "
           "room_id=?2 AND stream_id>?3 AND stream_id<=?4", &n, "ssii",
           user_id, room_id, read, bound);
    db_int("SELECT COUNT(*) FROM push_notifications WHERE user_id=?1 AND "
           "room_id=?2 AND stream_id>?3 AND stream_id<=?4 AND highlight=1", &h,
           "ssii", user_id, room_id, read, bound);
    if (notif) *notif = n;
    if (highlight) *highlight = h;
}

/* ---------------------------------------------------- push rules: API ----- */
/* "When a user changes their push rules a m.push_rules event is sent to all
 * clients in the account_data section of their next /sync request."  The
 * caller holds the write lock. */
static void pushrules_changed(const char *user_id) {
    /* The rules are run over an event once, when it is delivered, and the
     * outcome is kept: editing the rules afterwards changes what happens to
     * events that arrive from now on and leaves the already-counted ones
     * alone.  (Dropping the recorded notifications here instead would make
     * switching `.m.rule.master` on and off again resurrect every message
     * that was silenced while it was on.) */
    json *content = j_obj();
    j_set(content, "global", push_ruleset(user_id));
    char *text = dump_text(content);
    j_free(content);
    int64_t sid = stream_next();
    db_run("INSERT OR REPLACE INTO account_data(user_id,room_id,type,content,"
           "stream_id) VALUES(?1,'','m.push_rules',?2,?3)", "ssi", user_id,
           text, sid);
    free(text);
}

static json *find_rule(const json *ruleset, const char *kind,
                       const char *rule_id) {
    const json *arr = j_geta((json *)ruleset, kind);
    for (size_t i = 0; arr && i < j_len(arr); i++) {
        json *rule = j_at(arr, i);
        if (str_eq(j_gets(rule, "rule_id", ""), rule_id)) return rule;
    }
    return NULL;
}

static bool is_server_default(const char *rule_id) {
    return rule_id && rule_id[0] == '.';
}

/* Rejects a scope or kind the API does not define.  Returns false having
 * written the error. */
static bool check_scope_kind(http_resp *w, const char *scope, const char *kind,
                             bool for_write) {
    if (!str_eq(scope, "global")) {
        if (for_write)
            resp_error(w, 400, "M_INVALID_PARAM", "Unknown push rule scope");
        else
            resp_error(w, 404, "M_NOT_FOUND", "The push rule was not found.");
        return false;
    }
    if (!valid_kind(kind)) {
        if (for_write)
            resp_error(w, 400, "M_INVALID_PARAM", "Unknown push rule kind");
        else
            resp_error(w, 404, "M_NOT_FOUND", "The push rule was not found.");
        return false;
    }
    return true;
}

static json *user_rule_ids(const char *user_id, const char *kind) {
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT rule_id FROM push_rules WHERE user_id=?1 AND scope='global' "
            "AND kind=?2 AND is_default=0 ORDER BY priority ASC, rowid ASC",
            -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, kind, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) j_push(out, j_str(col_str(st, 0)));
    sqlite3_finalize(st);
    return out;
}

static int index_of(const json *arr, const char *s) {
    for (size_t i = 0; i < j_len(arr); i++)
        if (str_eq(j_at(arr, i)->s, s)) return (int)i;
    return -1;
}

/* Rewrites the priorities of one kind so they follow `order`. */
static void apply_order(const char *user_id, const char *kind, const json *order) {
    for (size_t i = 0; i < j_len(order); i++)
        db_run("UPDATE push_rules SET priority=?4 WHERE user_id=?1 AND "
               "scope='global' AND kind=?2 AND rule_id=?3", "sssi", user_id,
               kind, j_at(order, i)->s, (int64_t)i);
}

static void h_pushrules_all(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *b = j_obj();
    j_set(b, "global", push_ruleset(p.user_id));
    resp_json(w, 200, b);
    principal_free(&p);
}

/* "Clients can drill-down on the rulesets by suffixing a scope to this path." */
static void h_pushrules_scope(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!str_eq(r->part[0], "global")) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown push rule scope");
        principal_free(&p);
        return;
    }
    resp_json(w, 200, push_ruleset(p.user_id));
    principal_free(&p);
}

/* ... and further, on to one kind of rule. */
static void h_pushrules_kind(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!check_scope_kind(w, r->part[0], r->part[1], false)) {
        principal_free(&p);
        return;
    }
    json *set = push_ruleset(p.user_id);
    json *arr = j_geta(set, r->part[1]);
    resp_json(w, 200, arr ? j_clone(arr) : j_arr());
    j_free(set);
    principal_free(&p);
}

static void pushrule_get(http_req *r, http_resp *w, const principal *p) {
    const char *scope = r->part[0], *kind = r->part[1], *rule_id = r->part[2];
    if (!check_scope_kind(w, scope, kind, false)) return;
    json *set = push_ruleset(p->user_id);
    json *rule = find_rule(set, kind, rule_id);
    if (!rule) resp_error(w, 404, "M_NOT_FOUND", "The push rule was not found.");
    else resp_json(w, 200, j_clone(rule));
    j_free(set);
}

static void pushrule_delete(http_req *r, http_resp *w, const principal *p) {
    const char *scope = r->part[0], *kind = r->part[1], *rule_id = r->part[2];
    if (!check_scope_kind(w, scope, kind, false)) return;
    if (is_server_default(rule_id)) {
        resp_error(w, 404, "M_NOT_FOUND",
                   "Server-default push rules cannot be deleted.");
        return;
    }
    char *found = db_str("SELECT rule_id FROM push_rules WHERE user_id=?1 AND "
                         "scope='global' AND kind=?2 AND rule_id=?3 AND "
                         "is_default=0", "sss", p->user_id, kind, rule_id);
    if (!found) {
        resp_error(w, 404, "M_NOT_FOUND", "The push rule was not found.");
        return;
    }
    free(found);
    hs_lock();
    db_run("DELETE FROM push_rules WHERE user_id=?1 AND scope='global' AND "
           "kind=?2 AND rule_id=?3", "sss", p->user_id, kind, rule_id);
    json *order = user_rule_ids(p->user_id, kind);
    apply_order(p->user_id, kind, order);
    j_free(order);
    pushrules_changed(p->user_id);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
}

static void pushrule_put(http_req *r, http_resp *w, const principal *p) {
    const char *scope = r->part[0], *kind = r->part[1], *rule_id = r->part[2];
    if (!check_scope_kind(w, scope, kind, true)) return;
    if (!*rule_id || is_server_default(rule_id) || strchr(rule_id, '/') ||
        strchr(rule_id, '\\')) {
        resp_error(w, 400, "M_INVALID_PARAM",
                   "Rule IDs may not be empty, start with a dot, or contain "
                   "a slash or a backslash");
        return;
    }
    json *body = require_body(r, w);
    if (!body) return;
    json *actions = j_geta(body, "actions");
    if (!actions) {
        resp_error(w, 400, "M_BAD_JSON", "actions is required and must be a list");
        return;
    }
    json *conditions = j_geta(body, "conditions");
    const char *pattern = j_gets(body, "pattern", NULL);
    if (str_eq(kind, "content") && (!pattern || !*pattern)) {
        resp_error(w, 400, "M_BAD_JSON", "content rules require a pattern");
        return;
    }
    const char *before = http_query(r, "before");
    const char *after = http_query(r, "after");

    hs_lock();
    json *order = user_rule_ids(p->user_id, kind);
    /* "It is not possible to add a rule relative to a predefined server
     * rule." */
    const char *rel = before ? before : after;
    if (rel && (is_server_default(rel) || index_of(order, rel) < 0)) {
        j_free(order);
        hs_unlock();
        char *msg = str_fmt("before/after rule not found: %s", rel);
        resp_error(w, 400, "M_UNKNOWN", msg);
        free(msg);
        return;
    }

    int old = index_of(order, rule_id);
    json *without = j_arr();
    for (size_t i = 0; i < j_len(order); i++)
        if (!str_eq(j_at(order, i)->s, rule_id))
            j_push(without, j_str(j_at(order, i)->s));
    j_free(order);

    int at;
    if (before) at = index_of(without, before);
    else if (after) at = index_of(without, after) + 1;
    else if (old >= 0) at = old;                 /* an update keeps its place */
    else at = 0;                                 /* the most important so far */
    if (at < 0) at = 0;
    if (at > (int)j_len(without)) at = (int)j_len(without);

    json *placed = j_arr();
    for (int i = 0; i <= (int)j_len(without); i++) {
        if (i == at) j_push(placed, j_str(rule_id));
        if (i < (int)j_len(without)) j_push(placed, j_str(j_at(without, i)->s));
    }
    j_free(without);

    char *cond_text = conditions ? dump_text(conditions) : NULL;
    char *act_text = dump_text(actions);
    /* "When creating push rules, they MUST be enabled by default." */
    db_run("INSERT INTO push_rules(user_id,scope,kind,rule_id,priority,enabled,"
           "conditions,actions,pattern,is_default) "
           "VALUES(?1,'global',?2,?3,0,1,?4,?5,?6,0) "
           "ON CONFLICT(user_id,scope,kind,rule_id) DO UPDATE SET "
           "conditions=?4, actions=?5, pattern=?6, is_default=0, "
           "enabled=COALESCE(enabled,1)", "ssssss", p->user_id, kind, rule_id,
           cond_text, act_text, pattern);
    free(cond_text);
    free(act_text);
    apply_order(p->user_id, kind, placed);
    j_free(placed);
    pushrules_changed(p->user_id);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
}

static void h_pushrule(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (str_eq(r->method, "GET")) pushrule_get(r, w, &p);
    else if (str_eq(r->method, "DELETE")) pushrule_delete(r, w, &p);
    else pushrule_put(r, w, &p);
    principal_free(&p);
}

/* Stores an `enabled` or `actions` override.  A server-default rule gets a row
 * of its own the first time one of its two mutable properties is changed. */
static void store_override(const char *user_id, const char *kind,
                           const char *rule_id, const char *enabled_or_null,
                           const char *actions_or_null) {
    db_run("INSERT OR IGNORE INTO push_rules(user_id,scope,kind,rule_id,"
           "priority,enabled,conditions,actions,pattern,is_default) "
           "VALUES(?1,'global',?2,?3,0,NULL,NULL,NULL,NULL,1)", "sss",
           user_id, kind, rule_id);
    if (enabled_or_null)
        db_run("UPDATE push_rules SET enabled=?4 WHERE user_id=?1 AND "
               "scope='global' AND kind=?2 AND rule_id=?3", "sssi", user_id,
               kind, rule_id, (int64_t)(str_eq(enabled_or_null, "1") ? 1 : 0));
    if (actions_or_null)
        db_run("UPDATE push_rules SET actions=?4 WHERE user_id=?1 AND "
               "scope='global' AND kind=?2 AND rule_id=?3", "ssss", user_id,
               kind, rule_id, actions_or_null);
}

static void h_pushrule_enabled(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *scope = r->part[0], *kind = r->part[1], *rule_id = r->part[2];
    bool writing = !str_eq(r->method, "GET");
    if (!check_scope_kind(w, scope, kind, false)) { principal_free(&p); return; }
    json *set = push_ruleset(p.user_id);
    json *rule = find_rule(set, kind, rule_id);
    if (!rule) {
        resp_error(w, 404, "M_NOT_FOUND", "The push rule was not found.");
        j_free(set);
        principal_free(&p);
        return;
    }
    if (!writing) {
        json *b = j_obj();
        j_set(b, "enabled", j_bool(j_getb(rule, "enabled", true)));
        resp_json(w, 200, b);
        j_free(set);
        principal_free(&p);
        return;
    }
    j_free(set);
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    json *v = j_get(body, "enabled");
    if (!v || v->type != J_BOOL) {
        resp_error(w, 400, "M_BAD_JSON", "enabled is required and must be a boolean");
        principal_free(&p);
        return;
    }
    hs_lock();
    if (is_server_default(rule_id))
        store_override(p.user_id, kind, rule_id, v->b ? "1" : "0", NULL);
    else
        db_run("UPDATE push_rules SET enabled=?4 WHERE user_id=?1 AND "
               "scope='global' AND kind=?2 AND rule_id=?3", "sssi", p.user_id,
               kind, rule_id, (int64_t)(v->b ? 1 : 0));
    pushrules_changed(p.user_id);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_pushrule_actions(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *scope = r->part[0], *kind = r->part[1], *rule_id = r->part[2];
    bool writing = !str_eq(r->method, "GET");
    if (!check_scope_kind(w, scope, kind, false)) { principal_free(&p); return; }
    json *set = push_ruleset(p.user_id);
    json *rule = find_rule(set, kind, rule_id);
    if (!rule) {
        resp_error(w, 404, "M_NOT_FOUND", "The push rule was not found.");
        j_free(set);
        principal_free(&p);
        return;
    }
    if (!writing) {
        json *b = j_obj();
        json *a = j_geta(rule, "actions");
        j_set(b, "actions", a ? j_clone(a) : j_arr());
        resp_json(w, 200, b);
        j_free(set);
        principal_free(&p);
        return;
    }
    j_free(set);
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    json *actions = j_geta(body, "actions");
    if (!actions) {
        resp_error(w, 400, "M_BAD_JSON", "actions is required and must be a list");
        principal_free(&p);
        return;
    }
    char *text = dump_text(actions);
    hs_lock();
    /* "This can be used to change the actions of builtin rules." */
    if (is_server_default(rule_id))
        store_override(p.user_id, kind, rule_id, NULL, text);
    else
        db_run("UPDATE push_rules SET actions=?4 WHERE user_id=?1 AND "
               "scope='global' AND kind=?2 AND rule_id=?3", "ssss", p.user_id,
               kind, rule_id, text);
    pushrules_changed(p.user_id);
    hs_unlock();
    stream_notify();
    free(text);
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* ------------------------------------------------------- /notifications --- */
static void h_notifications(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    push_scan(p.user_id);

    const char *only = http_query(r, "only");
    bool highlight_only = only && str_eq(only, "highlight");
    const char *from = http_query(r, "from");
    int64_t start = INT64_MAX;
    if (from && *from) {
        const char *digits = strncmp(from, "nt_", 3) == 0 ? from + 3 : from;
        char *end = NULL;
        long long v = strtoll(digits, &end, 10);
        if (!end || *end || v < 0) {
            resp_error(w, 400, "M_INVALID_PARAM", "Invalid pagination token");
            principal_free(&p);
            return;
        }
        start = (int64_t)v;
    }
    int64_t limit = NOTIF_DEFAULT_LIMIT;
    const char *ql = http_query(r, "limit");
    if (ql && *ql) limit = strtoll(ql, NULL, 10);
    if (limit <= 0) limit = NOTIF_DEFAULT_LIMIT;
    if (limit > NOTIF_MAX_LIMIT) limit = NOTIF_MAX_LIMIT;

    json *out = j_arr();
    json *read_cache = j_obj();
    int64_t next = 0;
    bool more = false;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT id, room_id, event_id, stream_id, actions, profile_tag, ts "
            "FROM push_notifications WHERE user_id=?1 AND id<?2 AND "
            "(?3=0 OR highlight=1) ORDER BY id DESC LIMIT ?4",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, p.user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, start);
        sqlite3_bind_int64(st, 3, highlight_only ? 1 : 0);
        sqlite3_bind_int64(st, 4, limit + 1);
        while (sqlite3_step(st) == SQLITE_ROW) {
            if ((int64_t)j_len(out) >= limit) { more = true; break; }
            int64_t id = sqlite3_column_int64(st, 0);
            const char *room_id = col_str(st, 1);
            const char *event_id = col_str(st, 2);
            int64_t sid = sqlite3_column_int64(st, 3);
            json *stored = event_by_id(event_id);
            if (!stored) continue;
            json *n = j_obj();
            j_set(n, "room_id", j_str(room_id));
            j_set(n, "event", event_to_client(stored, p.user_id, p.device_id,
                                              false));
            j_free(stored);
            json *actions = parse_text((const char *)sqlite3_column_text(st, 4));
            j_set(n, "actions", actions ? actions : j_arr());
            if (sqlite3_column_type(st, 5) != SQLITE_NULL)
                j_set(n, "profile_tag", j_str(col_str(st, 5)));
            json *cached = j_get(read_cache, room_id);
            if (!cached) {
                cached = j_int(read_upto_stream(p.user_id, room_id));
                j_set(read_cache, room_id, cached);
            }
            j_set(n, "read", j_bool(sid <= cached->i));
            j_set(n, "ts", j_int(sqlite3_column_int64(st, 6)));
            j_push(out, n);
            next = id;
        }
        sqlite3_finalize(st);
    }
    j_free(read_cache);

    json *b = j_obj();
    j_set(b, "notifications", out);
    if (more && next > 0) {
        /* Opaque, and long enough that a client -- or a test harness -- cannot
         * mistake it for an ordinary number. */
        char *tok = str_fmt("nt_%012" PRId64, next);
        j_set(b, "next_token", j_str(tok));
        free(tok);
    }
    resp_json(w, 200, b);
    principal_free(&p);
}

/* ------------------------------------------------------- event reports --- */
static void h_report(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *room_id = r->part[0], *event_id = r->part[1];

    json *score = j_get(body, "score");
    /* The release describes the -100..0 range in prose and documents no
     * refusal for a value outside it, so a score out of range is stored as
     * given; only a `score` that is not an integer at all is refused. */
    if (score && score->type != J_INT) {
        resp_error(w, 400, "M_INVALID_PARAM", "score must be an integer");
        principal_free(&p);
        return;
    }
    json *reason = j_get(body, "reason");
    if (reason && !j_is_str(reason)) {
        resp_error(w, 400, "M_INVALID_PARAM", "reason must be a string");
        principal_free(&p);
        return;
    }

    /* The event must exist in that room, and {{< changed-in v="1.8" >}} the
     * reporter must currently be joined to it.  Both failures look the same
     * from the outside. */
    char *where = event_room(event_id);
    bool known = where && str_eq(where, room_id);
    free(where);
    char *membership = known ? room_membership(room_id, p.user_id) : NULL;
    bool joined = membership && str_eq(membership, "join");
    free(membership);
    if (!known || !joined) {
        resp_error(w, 404, "M_NOT_FOUND",
                   "The event was not found or you are not joined to the room.");
        principal_free(&p);
        return;
    }

    hs_lock();
    db_run("INSERT INTO event_reports(room_id,event_id,user_id,score,reason,ts) "
           "VALUES(?1,?2,?3,?4,?5,?6)", "sssisi", room_id, event_id, p.user_id,
           score ? score->i : 0, reason ? reason->s : NULL, now_ms());
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* ---------------------------------------------------------- presence ----- */
/* GET /presence/{userId}/status, with the last_active_ago and currently_active
 * the Presence module requires.  PUT stays with mxapi_misc.c. */
static void h_presence_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *uid = r->part[0];
    if (!str_eq(p.user_id, uid)) {
        int64_t shared = 0;
        db_int("SELECT COUNT(*) FROM room_members a JOIN room_members b "
               "ON a.room_id=b.room_id WHERE a.user_id=?1 AND b.user_id=?2 "
               "AND a.membership='join' AND b.membership='join'", &shared,
               "ss", p.user_id, uid);
        if (shared == 0) {
            resp_error(w, 403, "M_FORBIDDEN",
                       "You are not allowed to see their presence");
            principal_free(&p);
            return;
        }
    }
    char *found = db_str("SELECT user_id FROM users WHERE user_id=?1", "s", uid);
    if (!found) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown user");
        principal_free(&p);
        return;
    }
    free(found);
    char *pr = db_str("SELECT presence FROM presence WHERE user_id=?1", "s", uid);
    char *sm = db_str("SELECT status_msg FROM presence WHERE user_id=?1", "s", uid);
    int64_t last = 0;
    db_int("SELECT COALESCE(last_active_ts,0) FROM presence WHERE user_id=?1",
           &last, "s", uid);
    json *b = j_obj();
    j_set(b, "presence", j_str(pr ? pr : "offline"));
    if (sm) j_set(b, "status_msg", j_str(sm));
    /* "The last active time must be up to date whenever the server gives a
     * presence event to the client." */
    if (last > 0) {
        int64_t ago = now_ms() - last;
        j_set(b, "last_active_ago", j_int(ago < 0 ? 0 : ago));
    }
    j_set(b, "currently_active", j_bool(pr && str_eq(pr, "online")));
    free(pr); free(sm);
    resp_json(w, 200, b);
    principal_free(&p);
}

/* ------------------------------------------------ login and refresh ------ */
/* mxapi_account.c stores "pbkdf2$<iterations>$<salt>$<hash>"; its checker is
 * static, so this module verifies the same encoding itself. */
static bool password_ok(const char *stored, const char *pw) {
    if (!stored || !pw) return false;
    char copy[512];
    snprintf(copy, sizeof copy, "%s", stored);
    char *save = NULL;
    char *algo = strtok_r(copy, "$", &save);
    char *iter = strtok_r(NULL, "$", &save);
    char *salt_b64 = strtok_r(NULL, "$", &save);
    char *hash_b64 = strtok_r(NULL, "$", &save);
    if (!algo || !iter || !salt_b64 || !hash_b64 || strcmp(algo, "pbkdf2"))
        return false;
    size_t sn = 0;
    unsigned char *salt = b64_decode(salt_b64, &sn);
    if (!salt) return false;
    unsigned char out[32];
    PKCS5_PBKDF2_HMAC(pw, (int)strlen(pw), salt, (int)sn, atoi(iter),
                      EVP_sha256(), sizeof out, out);
    free(salt);
    char *h = b64_encode(out, sizeof out, false, true);
    bool ok = strcmp(h, hash_b64) == 0;
    free(h);
    return ok;
}

static char *new_access_token(const char *user_id, const char *device_id) {
    char *rnd = random_string(32);
    /* A distinct prefix so that using one of these can be noticed cheaply:
     * "the old refresh token remains valid until the new access token or
     * refresh token is used". */
    char *tok = str_fmt("syr_%s", rnd);
    free(rnd);
    db_run("INSERT INTO access_tokens(token,user_id,device_id,created_ts) "
           "VALUES(?1,?2,?3,?4)", "sssi", tok, user_id, device_id, now_ms());
    return tok;
}

static char *new_refresh_token(const char *user_id, const char *device_id,
                               const char *access_token) {
    char *rnd = random_string(32);
    char *tok = str_fmt("syr_%s", rnd);
    free(rnd);
    db_run("INSERT INTO refresh_tokens(token,user_id,device_id,access_token,"
           "next_token,created_ts) VALUES(?1,?2,?3,?4,NULL,?5)", "ssssi", tok,
           user_id, device_id, access_token, now_ms());
    return tok;
}

/* Issues the device, access token and (when the client asked for one) refresh
 * token for a completed login.  Mirrors mxapi_account.c's password login so
 * that standing in front of it changes nothing else. */
static void finish_login(http_resp *w, const char *uid, const json *body) {
    hs_lock();
    const char *did = j_gets(body, "device_id", NULL);
    char *device = did ? xstrdup_(did) : random_string(10);
    if (!did) for (char *q = device; *q; q++) *q = (char)(*q & ~0x20);
    if (did) {
        /* reusing a device id invalidates that device's previous token */
        db_run("DELETE FROM access_tokens WHERE user_id=?1 AND device_id=?2",
               "ss", uid, device);
    }
    db_run("INSERT OR REPLACE INTO devices(user_id,device_id,display_name,"
           "created_ts) VALUES(?1,?2,COALESCE((SELECT display_name FROM devices "
           "WHERE user_id=?1 AND device_id=?2),?3),?4)", "sssi", uid, device,
           j_gets(body, "initial_device_display_name", NULL), now_ms());
    char *tok = new_access_token(uid, device);
    char *refresh = j_getb(body, "refresh_token", false)
        ? new_refresh_token(uid, device, tok) : NULL;
    hs_unlock();

    json *b = j_obj();
    j_set(b, "user_id", j_str(uid));
    j_set(b, "access_token", j_str(tok));
    j_set(b, "device_id", j_str(device));
    if (refresh) {
        j_set(b, "refresh_token", j_str(refresh));
        j_set(b, "expires_in_ms", j_int(ACCESS_TOKEN_MS));
    }
    resp_json(w, 200, b);
    free(refresh);
    free(tok);
    free(device);
}

static void h_login_get(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    json *flows = j_arr();
    json *pw = j_obj();
    j_set(pw, "type", j_str("m.login.password"));
    j_push(flows, pw);
    /* The `m.login.token` flow is on offer because this server mints them:
     * `POST /login/get_token` is optional in the release and this server
     * offers it, so `get_login_token` is true wherever a client can look. */
    json *tk = j_obj();
    j_set(tk, "type", j_str("m.login.token"));
    j_set(tk, "get_login_token", j_bool(true));
    j_push(flows, tk);
    json *b = j_obj();
    j_set(b, "flows", flows);
    resp_json(w, 200, b);
}

/* Consumes a single-use login token.  Returns the user it was minted for. */
static char *consume_login_token(const char *token) {
    if (!token || !*token) return NULL;
    hs_lock();
    char *uid = db_str("SELECT user_id FROM login_tokens WHERE token=?1 AND "
                       "used=0 AND expires_ts>?2", "si", token, now_ms());
    if (uid) db_run("UPDATE login_tokens SET used=1 WHERE token=?1", "s", token);
    hs_unlock();
    return uid;
}

static void h_login_post(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    json *body = require_body(r, w);
    if (!body) return;
    const char *type = j_gets(body, "type", "");

    if (str_eq(type, "m.login.token")) {
        char *uid = consume_login_token(j_gets(body, "token", NULL));
        if (!uid) {
            resp_error(w, 403, "M_FORBIDDEN", "Invalid login token");
            return;
        }
        char *live = db_str("SELECT user_id FROM users WHERE user_id=?1 AND "
                            "deactivated=0", "s", uid);
        if (!live) {
            free(uid);
            resp_error(w, 403, "M_FORBIDDEN", "Invalid login token");
            return;
        }
        free(live);
        finish_login(w, uid, body);
        free(uid);
        return;
    }
    if (!str_eq(type, "m.login.password")) {
        resp_error(w, 400, "M_UNKNOWN", "Bad login type.");
        return;
    }

    json *ident = j_geto(body, "identifier");
    const char *who = NULL;
    if (ident) {
        const char *itype = j_gets(ident, "type", "");
        if (!str_eq(itype, "m.id.user")) {
            resp_error(w, 400, "M_UNKNOWN", "Unsupported identifier type");
            return;
        }
        who = j_gets(ident, "user", NULL);
    }
    if (!who) who = j_gets(body, "user", NULL);
    if (!who) {
        resp_error(w, 400, "M_UNKNOWN", "No identifier supplied");
        return;
    }
    char *uid = who[0] == '@' ? xstrdup_(who)
                              : str_fmt("@%s:%s", who, CFG.server_name);
    char *stored = db_str("SELECT password_hash FROM users WHERE user_id=?1 AND "
                          "deactivated=0", "s", uid);
    bool ok = password_ok(stored, j_gets(body, "password", ""));
    free(stored);
    if (!ok) {
        free(uid);
        resp_error(w, 403, "M_FORBIDDEN", "Invalid username or password");
        return;
    }
    finish_login(w, uid, body);
    free(uid);
}

/* POST /_matrix/client/v1/login/get_token */
static void h_login_get_token(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = http_json(r);
    json *empty = body ? NULL : j_obj();
    /* "Servers should always re-prompt the user for verification to ensure
     * explicit consent is gained for each additional client." */
    if (!uia_check(r, w, "login_token", p.user_id, body ? body : empty)) {
        j_free(empty);
        principal_free(&p);
        return;
    }
    j_free(empty);
    char *rnd = random_string(32);
    char *token = str_fmt("syl_%s", rnd);
    free(rnd);
    hs_lock();
    db_run("INSERT INTO login_tokens(token,user_id,expires_ts,used) "
           "VALUES(?1,?2,?3,0)", "ssi", token, p.user_id,
           now_ms() + LOGIN_TOKEN_MS);
    hs_unlock();
    json *b = j_obj();
    j_set(b, "login_token", j_str(token));
    j_set(b, "expires_in_ms", j_int(LOGIN_TOKEN_MS));
    resp_json(w, 200, b);
    free(token);
    principal_free(&p);
}

static void refresh_failed(http_resp *w) {
    json *b = j_obj();
    j_set(b, "errcode", j_str("M_UNKNOWN_TOKEN"));
    j_set(b, "error", j_str("Soft logged out"));
    j_set(b, "soft_logout", j_bool(true));
    resp_json(w, 401, b);
}

/* POST /refresh: authentication is the refresh token itself. */
static void h_refresh(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    json *body = require_body(r, w);
    if (!body) return;
    const char *rt = j_gets(body, "refresh_token", NULL);
    if (!rt || !*rt) {
        resp_error(w, 400, "M_MISSING_PARAM", "refresh_token is required");
        return;
    }
    hs_lock();
    char *uid = db_str("SELECT user_id FROM refresh_tokens WHERE token=?1",
                       "s", rt);
    if (!uid) {
        hs_unlock();
        refresh_failed(w);
        return;
    }
    char *device = db_str("SELECT device_id FROM refresh_tokens WHERE token=?1",
                          "s", rt);
    char *old_access = db_str("SELECT access_token FROM refresh_tokens WHERE "
                              "token=?1", "s", rt);
    char *next = db_str("SELECT next_token FROM refresh_tokens WHERE token=?1",
                        "s", rt);
    char *access = NULL, *refresh = NULL;
    if (next) {
        /* "The old refresh token remains valid until the new access token or
         * refresh token is used": repeating a refresh hands back the same
         * pair, so a client that lost the response can try again. */
        refresh = next;
        access = db_str("SELECT access_token FROM refresh_tokens WHERE token=?1",
                        "s", refresh);
    } else {
        access = new_access_token(uid, device);
        refresh = new_refresh_token(uid, device, access);
        db_run("UPDATE refresh_tokens SET next_token=?2 WHERE token=?1", "ss",
               rt, refresh);
        if (old_access)
            db_run("DELETE FROM access_tokens WHERE token=?1", "s", old_access);
        /* Using this refresh token revokes the one it replaced. */
        db_run("DELETE FROM refresh_tokens WHERE next_token=?1", "s", rt);
    }
    hs_unlock();
    free(old_access);
    free(device);
    free(uid);
    if (!access) {
        free(refresh);
        refresh_failed(w);
        return;
    }
    json *b = j_obj();
    j_set(b, "access_token", j_str(access));
    j_set(b, "refresh_token", j_str(refresh));
    j_set(b, "expires_in_ms", j_int(ACCESS_TOKEN_MS));
    resp_json(w, 200, b);
    free(access);
    free(refresh);
}

/* GET /capabilities, with m.get_login_token alongside what mxapi_account.c
 * already advertises. */
static void h_capabilities(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *caps = j_obj();
    json *cp = j_obj(); j_set(cp, "enabled", j_bool(true));
    j_set(caps, "m.change_password", cp);
    json *rv = j_obj();
    j_set(rv, "default", j_str(rv_default()));
    json *avail = j_obj();
    for (int i = 0; i < rv_count(); i++)
        j_set(avail, rv_index(i)->id, j_str("stable"));
    j_set(rv, "available", avail);
    j_set(caps, "m.room_versions", rv);
    json *sd = j_obj(); j_set(sd, "enabled", j_bool(true));
    j_set(caps, "m.set_displayname", sd);
    json *sa = j_obj(); j_set(sa, "enabled", j_bool(true));
    j_set(caps, "m.set_avatar_url", sa);
    json *tp = j_obj(); j_set(tp, "enabled", j_bool(false));
    j_set(caps, "m.3pid_changes", tp);
    json *lt = j_obj();
    j_set(lt, "enabled", j_bool(true));
    j_set(caps, "m.get_login_token", lt);
    json *b = j_obj();
    j_set(b, "capabilities", caps);
    resp_json(w, 200, b);
    principal_free(&p);
}

/* --------------------------------------------------------------- routing -- */
static void route_both(http_server *s, const char *method, const char *suffix,
                       http_handler fn) {
    char *a = str_fmt("%s%s", CS, suffix);
    char *b = str_fmt("%s%s", CSR, suffix);
    http_route(s, method, a, fn);
    http_route(s, method, b, fn);
    free(a); free(b);
}

void api_register_push(http_server *s) {
    spec_advertise("v1.7");
    spec_advertise("v1.8");
    spec_advertise("v1.9");
    push_init();

    /* push rules */
    route_both(s, "GET", "/pushrules", h_pushrules_all);
    route_both(s, "GET", "/pushrules/", h_pushrules_all);
    route_both(s, "GET", "/pushrules/:", h_pushrules_scope);
    route_both(s, "GET", "/pushrules/:/", h_pushrules_scope);
    route_both(s, "GET", "/pushrules/:/:", h_pushrules_kind);
    route_both(s, "GET", "/pushrules/:/:/", h_pushrules_kind);
    route_both(s, "GET", "/pushrules/:/:/:", h_pushrule);
    route_both(s, "PUT", "/pushrules/:/:/:", h_pushrule);
    route_both(s, "DELETE", "/pushrules/:/:/:", h_pushrule);
    route_both(s, "GET", "/pushrules/:/:/:/enabled", h_pushrule_enabled);
    route_both(s, "PUT", "/pushrules/:/:/:/enabled", h_pushrule_enabled);
    route_both(s, "GET", "/pushrules/:/:/:/actions", h_pushrule_actions);
    route_both(s, "PUT", "/pushrules/:/:/:/actions", h_pushrule_actions);

    /* notifications and reports */
    route_both(s, "GET", "/notifications", h_notifications);
    route_both(s, "POST", "/rooms/:/report/:", h_report);

    /* refinements of routes mxapi_account.c and mxapi_misc.c also register:
     * see the note in mxpush.h about registration order. */
    route_both(s, "GET", "/presence/:/status", h_presence_get);
    route_both(s, "GET", "/capabilities", h_capabilities);
    route_both(s, "GET", "/login", h_login_get);
    route_both(s, "POST", "/login", h_login_post);
    route_both(s, "POST", "/refresh", h_refresh);
    http_route(s, "POST", CS1 "/login/get_token", h_login_get_token);
}

/* Present only when this module is linked in; /sync tests it weakly. */
bool push_rules_enabled(void) { return true; }

/* A client reads its push rules out of global account data, so the event is
 * there from the first sync rather than only after the rules are first
 * changed. */
void push_ensure_account_data(const char *user_id) {
    if (!user_id || !*user_id) return;
    char *have = db_str("SELECT type FROM account_data WHERE user_id=?1 AND "
                        "room_id='' AND type='m.push_rules'", "s", user_id);
    if (have) { free(have); return; }
    hs_lock();
    pushrules_changed(user_id);
    hs_unlock();
    stream_notify();
}

/* An access token minted by a refresh has been used, so whatever refresh token
 * it replaced is now spent.  Called on the first authenticated request that
 * carries such a token; ordinary tokens never reach here. */
void push_access_token_used(const char *token) {
    if (!token || strncmp(token, "syr_", 4) != 0) return;
    char *mine = db_str("SELECT token FROM refresh_tokens WHERE access_token=?1 "
                        "AND spent_predecessor=0", "s", token);
    if (!mine) return;
    hs_lock();
    db_run("UPDATE refresh_tokens SET spent_predecessor=1 WHERE token=?1",
           "s", mine);
    db_run("DELETE FROM refresh_tokens WHERE next_token=?1", "s", mine);
    hs_unlock();
    free(mine);
}

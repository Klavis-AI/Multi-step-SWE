#define _POSIX_C_SOURCE 200809L
#include "mxevent.h"
#include "mxutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The room-version table.  Each row is one published room version; the flags
 * are the differences those versions' specifications call out. */
static const roomver RVS[] = {
  /* id  fmt url  keyv alias strictJ aliasAuth sres knock restr kres intpl joinauth r11 */
  {"1",  1, false,false,true, false, true,  1, false,false,false,false,false,false},
  {"2",  1, false,false,true, false, true,  2, false,false,false,false,false,false},
  {"3",  3, false,false,true, false, true,  2, false,false,false,false,false,false},
  {"4",  3, true, false,true, false, true,  2, false,false,false,false,false,false},
  {"5",  3, true, true, true, false, true,  2, false,false,false,false,false,false},
  {"6",  3, true, true, false,true,  false, 2, false,false,false,false,false,false},
  {"7",  3, true, true, false,true,  false, 2, true, false,false,false,false,false},
  {"8",  3, true, true, false,true,  false, 2, true, true, false,false,false,false},
  {"9",  3, true, true, false,true,  false, 2, true, true, false,false,true, false},
  {"10", 3, true, true, false,true,  false, 2, true, true, true, true, true, false},
  {"11", 3, true, true, false,true,  false, 2, true, true, true, true, true, true},
};

const roomver *rv_get(const char *v) {
    if (!v) return NULL;
    for (size_t i = 0; i < sizeof RVS / sizeof RVS[0]; i++)
        if (strcmp(RVS[i].id, v) == 0) return &RVS[i];
    return NULL;
}
const char *rv_default(void) { return "6"; }
int rv_count(void) { return (int)(sizeof RVS / sizeof RVS[0]); }
const roomver *rv_index(int i) {
    return (i >= 0 && i < rv_count()) ? &RVS[i] : NULL;
}

/* ------------------------------------------------------------ hashing --- */
char *event_content_hash_b64(const json *pdu) {
    json *c = j_clone(pdu);
    j_del(c, "signatures");
    j_del(c, "unsigned");
    j_del(c, "hashes");
    size_t n = 0;
    char *canon = j_canonical(c, &n);
    j_free(c);
    if (!canon) return NULL;
    unsigned char h[32];
    sha256(canon, n, h);
    free(canon);
    return b64_encode(h, 32, false, false);
}

bool event_reference_hash(const json *pdu, const roomver *rv, unsigned char out[32]) {
    json *r = event_redact(pdu, rv);
    if (!r) return false;
    j_del(r, "signatures");
    j_del(r, "age_ts");
    j_del(r, "unsigned");
    size_t n = 0;
    char *canon = j_canonical(r, &n);
    j_free(r);
    if (!canon) return false;
    sha256(canon, n, out);
    free(canon);
    return true;
}

char *event_compute_id(const json *pdu, const roomver *rv, const char *server_name) {
    if (rv->event_format == 1) {
        const char *existing = j_gets(pdu, "event_id", NULL);
        if (existing) return xstrdup_(existing);
        char *r = random_string(18);
        char *id = str_fmt("$%s:%s", r, server_name ? server_name : "localhost");
        free(r);
        return id;
    }
    unsigned char h[32];
    if (!event_reference_hash(pdu, rv, h)) return NULL;
    char *b = b64_encode(h, 32, rv->url_safe_event_ids, false);
    char *id = str_fmt("$%s", b);
    free(b);
    return id;
}

/* ------------------------------------------------------------ signing --- */
bool json_sign(json *obj, const char *server_name, const char *key_id,
               const unsigned char seed[32]) {
    json *sigs = j_get(obj, "signatures");
    json *unsig = j_get(obj, "unsigned");
    json *saved_sigs = sigs ? j_clone(sigs) : NULL;
    json *saved_unsig = unsig ? j_clone(unsig) : NULL;
    j_del(obj, "signatures");
    j_del(obj, "unsigned");

    size_t n = 0;
    char *canon = j_canonical(obj, &n);
    bool ok = false;
    unsigned char sig[64];
    if (canon && ed25519_sign(seed, canon, n, sig)) {
        char *b = b64_encode(sig, 64, false, false);
        json *all = saved_sigs ? saved_sigs : j_obj();
        saved_sigs = NULL;
        json *mine = j_get(all, server_name);
        if (!mine) { mine = j_obj(); j_set(all, server_name, mine); }
        j_set(mine, key_id, j_str(b));
        free(b);
        j_set(obj, "signatures", all);
        ok = true;
    }
    free(canon);
    if (saved_sigs) j_set(obj, "signatures", saved_sigs);
    if (saved_unsig) j_set(obj, "unsigned", saved_unsig);
    return ok;
}

bool json_verify(const json *obj, const char *server_name, const char *key_id,
                 const unsigned char pub[32]) {
    json *sigs = j_get((json *)obj, "signatures");
    if (!sigs) return false;
    json *mine = j_get(sigs, server_name);
    const char *b64 = mine ? j_gets(mine, key_id, NULL) : NULL;
    if (!b64) return false;
    size_t siglen = 0;
    unsigned char *sig = b64_decode(b64, &siglen);
    if (!sig || siglen != 64) { free(sig); return false; }

    json *c = j_clone(obj);
    j_del(c, "signatures");
    j_del(c, "unsigned");
    size_t n = 0;
    char *canon = j_canonical(c, &n);
    j_free(c);
    bool ok = canon && ed25519_verify(pub, canon, n, sig);
    free(canon);
    free(sig);
    return ok;
}

/* ----------------------------------------------------------- redaction --- */
static const char *TOP_KEEP[] = {
    "event_id", "type", "room_id", "sender", "state_key", "content", "hashes",
    "signatures", "depth", "prev_events", "prev_state", "auth_events", "origin",
    "origin_server_ts", "membership", NULL
};
/* Room version 11: "The top-level `origin`, `membership`, and `prev_state`
 * properties are no longer protected from redaction." */
static const char *TOP_DROP_V11[] = { "origin", "membership", "prev_state", NULL };

static bool in_list(const char *const *list, const char *s) {
    for (int i = 0; list[i]; i++) if (strcmp(list[i], s) == 0) return true;
    return false;
}

json *event_redact(const json *pdu, const roomver *rv) {
    if (!pdu || pdu->type != J_OBJECT) return NULL;
    bool v11 = rv->updated_redaction_rules;
    json *out = j_obj();
    for (size_t i = 0; i < j_len(pdu); i++) {
        const char *k = j_key(pdu, i);
        if (v11 && in_list(TOP_DROP_V11, k)) continue;
        if (in_list(TOP_KEEP, k) && strcmp(k, "content") != 0)
            j_set(out, k, j_clone(j_at(pdu, i)));
    }
    const char *type = j_gets(pdu, "type", "");
    json *content = j_get((json *)pdu, "content");
    json *nc = j_obj();
    if (v11 && !strcmp(type, "m.room.create") && content &&
        content->type == J_OBJECT) {
        /* "m.room.create allows all keys": from this version the create event
         * survives a redaction whole. */
        j_free(nc);
        nc = j_clone(content);
    } else if (content && content->type == J_OBJECT) {
        static const char *PL[] = {"ban", "events", "events_default", "kick",
                                   "redact", "state_default", "users",
                                   "users_default", NULL};
        /* Room version 11 adds `invite` to the power levels that survive. */
        static const char *PL11[] = {"ban", "events", "events_default", "invite",
                                     "kick", "redact", "state_default", "users",
                                     "users_default", NULL};
        for (size_t i = 0; i < j_len(content); i++) {
            const char *k = j_key(content, i);
            bool keep = false;
            if (!strcmp(type, "m.room.member")) keep = !strcmp(k, "membership");
            else if (!strcmp(type, "m.room.create")) keep = !strcmp(k, "creator");
            else if (!strcmp(type, "m.room.join_rules"))
                /* `allow` survives a redaction from the room version that
                 * introduces the restricted join rule, not from version 11:
                 * the condition a restricted room is gated on has to outlive
                 * a redaction of the event that states it. */
                keep = !strcmp(k, "join_rule") ||
                       (rv->restricted_join && !strcmp(k, "allow"));
            else if (!strcmp(type, "m.room.power_levels"))
                keep = in_list(v11 ? PL11 : PL, k);
            else if (!strcmp(type, "m.room.history_visibility"))
                keep = !strcmp(k, "history_visibility");
            else if (!strcmp(type, "m.room.aliases"))
                keep = rv->redaction_keeps_aliases && !strcmp(k, "aliases");
            else if (v11 && !strcmp(type, "m.room.redaction"))
                keep = !strcmp(k, "redacts");
            if (!keep && !strcmp(type, "m.room.member") &&
                rv->redaction_keeps_join_authorised &&
                !strcmp(k, "join_authorised_via_users_server"))
                keep = true;
            /* "The `third_party_invite` key ... is no longer redacted, *but*
             * will only contain the `signed` key after redaction." */
            if (!keep && v11 && !strcmp(type, "m.room.member") &&
                !strcmp(k, "third_party_invite")) {
                json *signed_ = j_geto(j_at(content, i), "signed");
                if (signed_) {
                    json *tpi = j_obj();
                    j_set(tpi, "signed", j_clone(signed_));
                    j_set(nc, k, tpi);
                }
                continue;
            }
            if (keep) j_set(nc, k, j_clone(j_at(content, i)));
        }
    }
    j_set(out, "content", nc);
    return out;
}

/* -------------------------------------------------------- power levels --- */
static int64_t as_int(const json *v, int64_t dflt) {
    if (!v) return dflt;
    if (v->type == J_INT) return v->i;
    if (v->type == J_DOUBLE) return (int64_t)v->d;
    if (v->type == J_STRING) {
        char *end = NULL;
        long long x = strtoll(v->s, &end, 10);
        if (end && *end == 0 && end != v->s) return (int64_t)x;
    }
    return dflt;
}

int64_t pl_user_level(const json *pl, const char *user_id) {
    if (!pl) return 0;
    json *users = j_geto((json *)pl, "users");
    json *v = users ? j_get(users, user_id) : NULL;
    if (v) return as_int(v, 0);
    return as_int(j_get((json *)pl, "users_default"), 0);
}

int64_t pl_event_level(const json *pl, const char *type, bool is_state) {
    if (!pl) return is_state ? 50 : 0;
    json *events = j_geto((json *)pl, "events");
    json *v = events ? j_get(events, type) : NULL;
    if (v) return as_int(v, 0);
    return as_int(j_get((json *)pl, is_state ? "state_default" : "events_default"),
                  is_state ? 50 : 0);
}

int64_t pl_action_level(const json *pl, const char *action, int64_t dflt) {
    if (!pl) return dflt;
    return as_int(j_get((json *)pl, action), dflt);
}

bool pl_content_valid(const json *content, const roomver *rv) {
    if (!content || content->type != J_OBJECT) return false;
    static const char *SCALARS[] = {"ban", "kick", "redact", "invite",
                                    "events_default", "state_default",
                                    "users_default", NULL};
    for (int i = 0; SCALARS[i]; i++) {
        json *v = j_get((json *)content, SCALARS[i]);
        if (!v) continue;
        if (v->type == J_INT) continue;
        if (!rv->integer_power_levels && v->type == J_STRING) {
            char *end = NULL;
            strtoll(v->s, &end, 10);
            if (end && *end == 0 && end != v->s) continue;
        }
        return false;
    }
    /* Room version 10: "If either of the properties `events` or
     * `notifications` in `content` are present and not an object with values
     * that are integers, reject." */
    const char *MAPS[4] = {"users", "events", NULL, NULL};
    if (rv->integer_power_levels) MAPS[2] = "notifications";
    for (int i = 0; MAPS[i]; i++) {
        json *m = j_get((json *)content, MAPS[i]);
        if (!m) continue;
        if (m->type != J_OBJECT) return false;
        for (size_t k = 0; k < j_len(m); k++) {
            json *v = j_at(m, k);
            if (v->type == J_INT) continue;
            if (!rv->integer_power_levels && v->type == J_STRING) {
                char *end = NULL;
                strtoll(v->s, &end, 10);
                if (end && *end == 0 && end != v->s) continue;
            }
            return false;
        }
    }
    json *notif = j_get((json *)content, "notifications");
    if (notif && notif->type != J_OBJECT) return false;
    return true;
}

/* ---------------------------------------------------------------- state --- */
char *state_key_of(const char *type, const char *state_key) {
    return str_fmt("%s%s%s", type, STATE_KEY_SEP, state_key ? state_key : "");
}

json *state_lookup(const json *state, const char *type, const char *sk) {
    char *k = state_key_of(type, sk);
    json *v = j_get((json *)state, k);
    free(k);
    return v;
}

/* ---------------------------------------------------- auth event selection */
/* The "Auth events selection" list is a *set*: a receiving server rejects an
 * event that names the same state key twice, so a self-membership event (where
 * the sender is the target) must not list its own member event twice. */
static void push_key(json *out, const char *key) {
    for (size_t i = 0; i < j_len(out); i++) {
        json *v = j_at(out, i);
        if (v && v->type == J_STRING && strcmp(v->s, key) == 0) return;
    }
    j_push(out, j_str(key));
}

json *event_auth_keys(const json *ev) {
    const char *type = j_gets(ev, "type", "");
    const char *sender = j_gets(ev, "sender", "");
    json *out = j_arr();
    if (!strcmp(type, "m.room.create")) return out;
    push_key(out, "m.room.create" STATE_KEY_SEP);
    char *k = state_key_of("m.room.power_levels", "");
    push_key(out, k); free(k);
    k = state_key_of("m.room.member", sender);
    push_key(out, k); free(k);
    if (!strcmp(type, "m.room.member")) {
        const char *target = j_gets(ev, "state_key", "");
        k = state_key_of("m.room.member", target);
        push_key(out, k); free(k);
        json *c = j_geto((json *)ev, "content");
        const char *ms = c ? j_gets(c, "membership", "") : "";
        /* "If `membership` is `join` or `invite`, the current
         * `m.room.join_rules` event, if any" - and `knock` from the room
         * version that introduces it.  A leave or a ban must *not* name it. */
        if (!strcmp(ms, "join") || !strcmp(ms, "invite") || !strcmp(ms, "knock"))
            push_key(out, "m.room.join_rules" STATE_KEY_SEP);
        /* Room version 8's restricted join rule authorises a join through a
         * user who is already in the room, so that user's membership is one of
         * the events that authorise it. */
        const char *via = c ? j_gets(c, "join_authorised_via_users_server", NULL)
                            : NULL;
        if (via && !strcmp(ms, "join")) {
            k = state_key_of("m.room.member", via);
            push_key(out, k); free(k);
        }
        if (!strcmp(ms, "invite") && c && j_get(c, "third_party_invite")) {
            json *tpi = j_geto(c, "third_party_invite");
            const char *token = tpi ? j_gets(tpi, "token", NULL) : NULL;
            if (token) {
                k = state_key_of("m.room.third_party_invite", token);
                push_key(out, k); free(k);
            }
        }
    }
    return out;
}

/* ---------------------------------------------------------- auth rules --- */
/* "If the room contains no m.room.power_levels event, the sender's power level
 * is 100 if they are the room creator, and 0 otherwise." */
static int64_t level_of(const json *plc, const json *create, const char *user) {
    if (plc) return pl_user_level(plc, user);
    json *cc = create ? j_geto((json *)create, "content") : NULL;
    const char *creator = cc ? j_gets(cc, "creator", NULL) : NULL;
    if (!creator && create) creator = j_gets(create, "sender", NULL);
    return (creator && user && strcmp(creator, user) == 0) ? 100 : 0;
}

static const char *membership_of(const json *state, const char *user) {
    json *m = state_lookup(state, "m.room.member", user);
    if (!m) return "leave";
    json *c = j_geto(m, "content");
    return c ? j_gets(c, "membership", "leave") : "leave";
}

bool event_auth_allowed(const json *ev, const json *state, const roomver *rv,
                        const char **why) {
    const char *type = j_gets(ev, "type", "");
    const char *sender = j_gets(ev, "sender", "");
    json *content = j_geto((json *)ev, "content");
    const char *reason = "not allowed";

    /* 1. m.room.create */
    if (!strcmp(type, "m.room.create")) {
        if (j_len(j_geta((json *)ev, "prev_events")) > 0) { reason = "create is not first"; goto deny; }
        if (!content || !j_gets(content, "room_version", NULL)) {
            /* room_version may be absent for v1 */
        }
        /* Room version 11 restated this rule without the `creator` check but
         * with "If content.room_version is present and is not a recognised
         * version, reject." */
        if (rv->updated_redaction_rules && content) {
            const char *want = j_gets(content, "room_version", NULL);
            if (want && !rv_get(want)) { reason = "unrecognised room version"; goto deny; }
        }
        return true;
    }

    json *create = state_lookup(state, "m.room.create", "");
    if (!create) { reason = "no m.room.create in state"; goto deny; }

    json *pl = state_lookup(state, "m.room.power_levels", "");
    json *plc = pl ? j_geto(pl, "content") : NULL;
    json *creator_pl = NULL;
    (void)creator_pl;
    const char *sender_ms = membership_of(state, sender);

    /* 4. m.room.aliases, for the versions that special-case it */
    if (rv->special_case_aliases_auth && !strcmp(type, "m.room.aliases")) {
        const char *sk = j_gets(ev, "state_key", "");
        const char *srv = user_id_server(sender);
        if (!sk || !*sk) { reason = "aliases needs a state key"; goto deny; }
        if (!srv || strcmp(sk, srv) != 0) { reason = "aliases state key must be the sender's server"; goto deny; }
        return true;
    }

    /* 5. m.room.member */
    if (!strcmp(type, "m.room.member")) {
        const char *target = j_gets(ev, "state_key", NULL);
        const char *ms = content ? j_gets(content, "membership", NULL) : NULL;
        if (!target || !ms) { reason = "member needs a state key and a membership"; goto deny; }
        const char *target_ms = membership_of(state, target);
        json *jr = state_lookup(state, "m.room.join_rules", "");
        const char *join_rule = jr && j_geto(jr, "content")
            ? j_gets(j_geto(jr, "content"), "join_rule", "invite") : "invite";

        if (!strcmp(ms, "join")) {
            if (strcmp(sender, target)) { reason = "join must target the sender"; goto deny; }
            if (!strcmp(target_ms, "ban")) { reason = "user is banned"; goto deny; }
            /* the first join after create is allowed when the sender created the room */
            if (j_len(j_geta((json *)ev, "prev_events")) == 1) {
                json *cc = j_geto(create, "content");
                const char *creator = cc ? j_gets(cc, "creator", NULL) : NULL;
                /* Room version 11 removed `creator` from the create event:
                 * "If the only previous event is an m.room.create and the
                 * state_key is the sender, allow."  The create event's own
                 * sender is that user in every room version. */
                if (!creator || rv->updated_redaction_rules)
                    creator = j_gets(create, "sender", NULL);
                const char *prev0 = NULL;
                json *pe = j_geta((json *)ev, "prev_events");
                json *p0 = j_at(pe, 0);
                if (p0 && p0->type == J_STRING) prev0 = p0->s;
                else if (p0 && p0->type == J_ARRAY && j_at(p0, 0)) prev0 = j_at(p0, 0)->s;
                const char *cid = j_gets(create, "event_id", NULL);
                if (creator && prev0 && cid && !strcmp(prev0, cid) && !strcmp(creator, sender))
                    return true;
            }
            if (!strcmp(join_rule, "public")) return true;
            if (!strcmp(target_ms, "invite") || !strcmp(target_ms, "join")) return true;
            /* Room version 7 lets someone who knocked join once invited; from
             * version 8 a restricted room lets a member of an allowed room join
             * when a resident server authorises the membership. */
            if (rv->knock && !strcmp(join_rule, "knock") &&
                !strcmp(target_ms, "knock") && !strcmp(sender, target))
                return true;
            if (rv->restricted_join &&
                (!strcmp(join_rule, "restricted") ||
                 (rv->knock_restricted && !strcmp(join_rule, "knock_restricted")))) {
                const char *auth_user = content
                    ? j_gets(content, "join_authorised_via_users_server", NULL) : NULL;
                if (auth_user) {
                    if (strcmp(membership_of(state, auth_user), "join")) {
                        reason = "the authorising user is not joined"; goto deny;
                    }
                    if (pl_user_level(plc, auth_user) <
                        pl_action_level(plc, "invite", 0)) {
                        reason = "the authorising user cannot invite"; goto deny;
                    }
                    return true;
                }
                reason = "a restricted join must name an authorising user";
                goto deny;
            }
            if (rv->knock_restricted && !strcmp(join_rule, "knock_restricted") &&
                !strcmp(target_ms, "knock") && !strcmp(sender, target))
                return true;
            reason = "join rule does not allow this join";
            goto deny;
        }
        if (!strcmp(ms, "invite")) {
            if (strcmp(sender_ms, "join")) { reason = "inviter is not joined"; goto deny; }
            /* A third-party invitation must be backed by the
             * m.room.third_party_invite it names. */
            json *tpi = content ? j_geto((json *)content, "third_party_invite") : NULL;
            if (tpi) {
                json *signed_ = j_geto(tpi, "signed");
                const char *token = signed_ ? j_gets(signed_, "token", NULL)
                                            : j_gets(tpi, "token", NULL);
                if (!token || !state_lookup(state, "m.room.third_party_invite",
                                            token)) {
                    reason = "no matching third-party invite";
                    goto deny;
                }
            }
            if (!strcmp(target_ms, "join") || !strcmp(target_ms, "ban")) {
                reason = "target is already joined or banned"; goto deny;
            }
            if (level_of(plc, create, sender) < pl_action_level(plc, "invite", 0)) {
                reason = "insufficient power level to invite"; goto deny;
            }
            return true;
        }
        if (!strcmp(ms, "leave")) {
            if (!strcmp(sender, target)) {
                if (!strcmp(sender_ms, "join") || !strcmp(sender_ms, "invite")) return true;
                reason = "cannot leave a room you are not in";
                goto deny;
            }
            if (strcmp(sender_ms, "join")) { reason = "kicker is not joined"; goto deny; }
            if (level_of(plc, create, sender) < pl_action_level(plc, "kick", 50)) {
                reason = "insufficient power level to kick"; goto deny;
            }
            if (level_of(plc, create, target) >= level_of(plc, create, sender)) {
                reason = "cannot kick a user at or above your power level"; goto deny;
            }
            if (!strcmp(target_ms, "ban") &&
                level_of(plc, create, sender) < pl_action_level(plc, "ban", 50)) {
                reason = "unbanning needs the ban level"; goto deny;
            }
            return true;
        }
        if (rv->knock && !strcmp(ms, "knock")) {
            if (strcmp(join_rule, "knock") &&
                !(rv->knock_restricted && !strcmp(join_rule, "knock_restricted"))) {
                reason = "the room's join rule does not allow knocking"; goto deny;
            }
            if (strcmp(sender, target)) { reason = "knock must target the sender"; goto deny; }
            if (!strcmp(target_ms, "ban")) { reason = "user is banned"; goto deny; }
            if (!strcmp(target_ms, "join")) { reason = "user is already joined"; goto deny; }
            if (!strcmp(target_ms, "invite")) { reason = "user is already invited"; goto deny; }
            return true;
        }
        if (!strcmp(ms, "ban")) {
            if (strcmp(sender_ms, "join")) { reason = "banner is not joined"; goto deny; }
            if (level_of(plc, create, sender) < pl_action_level(plc, "ban", 50)) {
                reason = "insufficient power level to ban"; goto deny;
            }
            if (level_of(plc, create, target) >= level_of(plc, create, sender)) {
                reason = "cannot ban a user at or above your power level"; goto deny;
            }
            return true;
        }
        /* Room version 7 adds `knock` to rule 5: the join rule has to permit
         * knocking, the sender has to be knocking for themselves, and they
         * must not already be in the room or banned from it. */
        if (!strcmp(ms, "knock")) {
            if (!rv->knock) {
                reason = "this room version does not support knocking"; goto deny;
            }
            if (strcmp(sender, target)) {
                reason = "a knock must target the sender"; goto deny;
            }
            if (strcmp(join_rule, "knock") &&
                !(rv->knock_restricted && !strcmp(join_rule, "knock_restricted"))) {
                reason = "the join rule does not allow knocking"; goto deny;
            }
            if (!strcmp(sender_ms, "ban")) { reason = "user is banned"; goto deny; }
            if (!strcmp(sender_ms, "invite") || !strcmp(sender_ms, "join")) {
                reason = "you are already in the room"; goto deny;
            }
            return true;
        }
        reason = "unknown membership";
        goto deny;
    }

    /* 6. the sender must be joined */
    if (strcmp(sender_ms, "join")) { reason = "sender is not in the room"; goto deny; }

    /* 7/8. power levels for the event type */
    bool is_state = j_gets(ev, "state_key", NULL) != NULL;
    int64_t sender_level = level_of(plc, create, sender);
    if (sender_level < pl_event_level(plc, type, is_state)) {
        reason = "insufficient power level for this event type"; goto deny;
    }
    if (is_state) {
        const char *sk = j_gets(ev, "state_key", "");
        if (sk[0] == '@' && strcmp(sk, sender)) {
            reason = "a state key beginning with @ must equal the sender"; goto deny;
        }
    }

    /* 10. m.room.power_levels */
    if (!strcmp(type, "m.room.power_levels")) {
        if (!pl_content_valid(content, rv)) { reason = "invalid power level content"; goto deny; }
        if (plc) {
            static const char *KEYS[] = {"users_default", "events_default",
                                         "state_default", "ban", "redact",
                                         "kick", "invite", NULL};
            for (int i = 0; KEYS[i]; i++) {
                int64_t dflt = (!strcmp(KEYS[i], "invite") ||
                                !strcmp(KEYS[i], "users_default") ||
                                !strcmp(KEYS[i], "events_default")) ? 0 : 50;
                int64_t old = pl_action_level(plc, KEYS[i], dflt);
                int64_t neu = pl_action_level(content, KEYS[i], dflt);
                if (old != neu) {
                    if (old > sender_level || neu > sender_level) {
                        reason = "cannot change a power level above your own"; goto deny;
                    }
                }
            }
            json *oldu = j_geto(plc, "users");
            json *newu = j_geto((json *)content, "users");
            /* every user level that changes must be <= the sender's level */
            for (size_t i = 0; oldu && i < j_len(oldu); i++) {
                const char *u = j_key(oldu, i);
                int64_t o = level_of(plc, create, u);
                int64_t n2 = pl_user_level(content, u);
                if (o == n2) continue;
                if (strcmp(u, sender) == 0 && n2 < o) continue;   /* self-demotion */
                if (o >= sender_level || n2 > sender_level) {
                    reason = "cannot change another user's level above your own"; goto deny;
                }
            }
            for (size_t i = 0; newu && i < j_len(newu); i++) {
                const char *u = j_key(newu, i);
                if (oldu && j_get(oldu, u)) continue;
                int64_t n2 = pl_user_level(content, u);
                if (n2 > sender_level) {
                    reason = "cannot grant a level above your own"; goto deny;
                }
            }
            /* From room version 6 the notifications keys are checked the same
             * way as the events map. */
            if (rv->strict_canonical_json) {
                json *oldn = j_geto(plc, "notifications");
                json *newn = j_geto((json *)content, "notifications");
                for (size_t i = 0; oldn && i < j_len(oldn); i++) {
                    const char *t = j_key(oldn, i);
                    int64_t o = as_int(j_at(oldn, i), 50);
                    json *nv = newn ? j_get(newn, t) : NULL;
                    int64_t n2 = nv ? as_int(nv, 50) : 50;
                    if (o != n2 && (o > sender_level || n2 > sender_level)) {
                        reason = "cannot change a notification level above your own";
                        goto deny;
                    }
                }
                for (size_t i = 0; newn && i < j_len(newn); i++) {
                    const char *t = j_key(newn, i);
                    if (oldn && j_get(oldn, t)) continue;
                    if (as_int(j_at(newn, i), 50) > sender_level) {
                        reason = "cannot set a notification level above your own";
                        goto deny;
                    }
                }
            }

            /* events map */
            json *olde = j_geto(plc, "events");
            json *newe = j_geto((json *)content, "events");
            for (size_t i = 0; olde && i < j_len(olde); i++) {
                const char *t = j_key(olde, i);
                int64_t o = as_int(j_at(olde, i), 0);
                json *nv = newe ? j_get(newe, t) : NULL;
                int64_t n2 = nv ? as_int(nv, 0) : pl_event_level(content, t, true);
                if (o != n2 && (o > sender_level || n2 > sender_level)) {
                    reason = "cannot change an event level above your own"; goto deny;
                }
            }
            for (size_t i = 0; newe && i < j_len(newe); i++) {
                const char *t = j_key(newe, i);
                if (olde && j_get(olde, t)) continue;
                if (as_int(j_at(newe, i), 0) > sender_level) {
                    reason = "cannot set an event level above your own"; goto deny;
                }
            }
        } else if (!pl_content_valid(content, rv)) {
            reason = "invalid power level content"; goto deny;
        }
        return true;
    }

    /* 11. m.room.redaction is authorised at send time and at apply time */
    return true;

deny:
    if (why) *why = reason;
    return false;
}

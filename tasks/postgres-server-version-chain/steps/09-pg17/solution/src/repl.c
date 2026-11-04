/* repl.c - the replication command surface and its catalog view.
 * Commands and their result shapes come from the "Streaming Replication
 * Protocol" section of the PostgreSQL protocol chapter. */
#include "engine.h"
#include "stmt.h"

typedef struct Slot { char *name, *plugin, *db; bool temporary, logical, failover, active;
                      int64 inactive_us; } Slot;
static Slot slots[64];
static int nslots;

/* PostgreSQL stamps timestamptz from an epoch of 2000-01-01, which is
 * 946684800 seconds after the Unix epoch. */
static int64 slot_clock_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((int64)ts.tv_sec - 946684800LL) * 1000000LL + ts.tv_nsec / 1000;
}

/* Column order follows pg_replication_slots as the release defines it.  The
 * view grew steadily: 13 added the WAL-retention pair, 14 two_phase, and 16
 * the flag that says a logical slot was invalidated by a recovery conflict.
 * 17 brought slot synchronisation, whose failover and synced columns arrived
 * together with inactive_since and invalidation_reason -- and those two were
 * spliced in around conflicting rather than appended after it.  18 added the
 * LSN two-phase decoding starts from, right behind two_phase itself. */
int repl_slots_scan(Session *s, QueryResult *r)
{
    for (int i = 0; i < nslots; i++) {
        Datum *row = xmalloc((size_t)r->ncols * sizeof(Datum));
        int k = 0;
        row[k++] = d_text(NAMEOID, slots[i].name);
        row[k++] = slots[i].plugin ? d_text(NAMEOID, slots[i].plugin) : d_null(NAMEOID);
        row[k++] = d_text(TEXTOID, slots[i].logical ? "logical" : "physical");
        row[k++] = d_null(OIDOID);                              /* datoid */
        row[k++] = slots[i].db ? d_text(NAMEOID, slots[i].db) : d_null(NAMEOID);
        row[k++] = d_bool(slots[i].temporary);
        row[k++] = d_bool(slots[i].active);
        row[k++] = d_null(INT4OID);                             /* active_pid */
        row[k++] = d_null(INT8OID);                             /* xmin */
        row[k++] = d_null(INT8OID);                             /* catalog_xmin */
        row[k++] = d_null(PGLSNOID);                            /* restart_lsn */
        row[k++] = d_null(PGLSNOID);                            /* confirmed_flush_lsn */
#if PGVC_VN >= 130
        row[k++] = d_text(TEXTOID, "reserved");                 /* wal_status */
        row[k++] = d_null(INT8OID);                             /* safe_wal_size */
#endif
#if PGVC_VN >= 140
        row[k++] = d_bool(false);                               /* two_phase */
#endif
#if PGVC_VN >= 180
        row[k++] = d_null(PGLSNOID);                            /* two_phase_at */
#endif
#if PGVC_VN >= 170
        /* set while the slot has no walsender attached, null while one is */
        row[k++] = slots[i].active ? d_null(TIMESTAMPTZOID)
                                   : d_int(TIMESTAMPTZOID, slots[i].inactive_us);
#endif
#if PGVC_VN >= 160
        /* only a logical slot can conflict with recovery; a physical slot
         * reports null rather than false */
        row[k++] = slots[i].logical ? d_bool(false) : d_null(BOOLOID);
#endif
#if PGVC_VN >= 170
        row[k++] = d_null(TEXTOID);                             /* invalidation_reason */
        row[k++] = d_bool(slots[i].failover);
        row[k++] = d_bool(false);                               /* synced */
#endif
        qr_addrow(r, row);
    }
    (void)s;
    return nslots;
}

/* The SQL-callable slot functions share the in-memory slot table with the
 * replication protocol commands. */
void repl_slot_create(Session *s, const char *name, const char *plugin,
                      bool logical, bool temporary)
{
    for (int i = 0; i < nslots; i++)
        if (!strcmp(slots[i].name, name))
            ereport(s, "42710", "replication slot \"%s\" already exists", name);
    if (logical) {
        /* 17 quotes the GUC name and the value it has to take; before that
         * the same complaint was spelled bare. */
        const char *wl = guc_get(s, "wal_level");
        if (!wl || strcmp(wl, "logical"))
#if PGVC_VN >= 170
            /* PostgreSQL 17 quotes parameter names and values in messages */
            ereport(s, "55000", "logical decoding requires \"wal_level\" >= \"logical\"");
#else
            ereport(s, "55000", "logical decoding requires wal_level >= logical");
#endif
    }
    if (nslots >= 64) ereport(s, "53400", "all replication slots are in use");
    slots[nslots].name = xstrdup(name);
    slots[nslots].plugin = plugin ? xstrdup(plugin) : NULL;
    slots[nslots].db = (logical && s->dbname) ? xstrdup(s->dbname) : NULL;
    slots[nslots].temporary = temporary;
    slots[nslots].logical = logical;
    slots[nslots].failover = false;
    slots[nslots].active = false;
    slots[nslots].inactive_us = slot_clock_us();
    nslots++;
}

void repl_slot_drop(Session *s, const char *name)
{
    for (int i = 0; i < nslots; i++)
        if (!strcmp(slots[i].name, name)) {
            memmove(&slots[i], &slots[i+1], (size_t)(nslots-i-1)*sizeof(Slot));
            nslots--;
            return;
        }
    ereport(s, "42704", "replication slot \"%s\" does not exist", name);
}

QueryResult *exec_repl(Session *s, Node *n)
{
    ReplStmt *st = n->x;
    QueryResult *q = qr_new();
    switch (n->tag) {
    case N_IDENT_SYS:
        qr_addcol(q, "systemid", TEXTOID, -1);
        qr_addcol(q, "timeline", INT4OID, -1);
        qr_addcol(q, "xlogpos", TEXTOID, -1);
        qr_addcol(q, "dbname", TEXTOID, -1);
        {
            Datum *row = xmalloc(4 * sizeof(Datum));
            row[0] = d_text(TEXTOID, "7000000000000000001");
            row[1] = d_int(INT4OID, 1);
            row[2] = d_text(TEXTOID, "0/1000000");
            row[3] = s->dbname ? d_text(TEXTOID, s->dbname) : d_null(TEXTOID);
            qr_addrow(q, row);
        }
        snprintf(q->tag, sizeof q->tag, "IDENTIFY_SYSTEM");
        return q;
    case N_CREATE_REPL_SLOT: {
        if (!st || !st->slot) ereport(s, "42601", "syntax error");
#if PGVC_VN < 170
        if (st->failover) ereport(s, "42601", "syntax error at or near \"FAILOVER\"");
#endif
        for (int i = 0; i < nslots; i++)
            if (!strcmp(slots[i].name, st->slot))
                ereport(s, "42710", "replication slot \"%s\" already exists", st->slot);
        if (nslots < 64) {
            slots[nslots].name = xstrdup(st->slot);
            slots[nslots].plugin = st->plugin ? xstrdup(st->plugin) : NULL;
            slots[nslots].db = st->logical && s->dbname ? xstrdup(s->dbname) : NULL;
            slots[nslots].temporary = st->temporary;
            slots[nslots].logical = st->logical;
            slots[nslots].failover = st->failover;
            slots[nslots].active = false;
            slots[nslots].inactive_us = slot_clock_us();
            nslots++;
        }
        (void)0;
        qr_addcol(q, "slot_name", TEXTOID, -1);
        qr_addcol(q, "consistent_point", TEXTOID, -1);
        qr_addcol(q, "snapshot_name", TEXTOID, -1);
        qr_addcol(q, "output_plugin", TEXTOID, -1);
        Datum *row = xmalloc(4 * sizeof(Datum));
        row[0] = d_text(TEXTOID, st->slot);
        row[1] = d_text(TEXTOID, "0/1000000");
        row[2] = d_null(TEXTOID);
        row[3] = st->plugin ? d_text(TEXTOID, st->plugin) : d_null(TEXTOID);
        qr_addrow(q, row);
        snprintf(q->tag, sizeof q->tag, "CREATE_REPLICATION_SLOT");
        return q;
    }
    case N_DROP_REPL_SLOT: {
        bool found = false;
        for (int i = 0; i < nslots; i++)
            if (st && st->slot && !strcmp(slots[i].name, st->slot)) {
                memmove(&slots[i], &slots[i+1], (size_t)(nslots-i-1)*sizeof(Slot));
                nslots--; found = true; break;
            }
        if (!found) ereport(s, "42704", "replication slot \"%s\" does not exist",
                            st && st->slot ? st->slot : "");
        snprintf(q->tag, sizeof q->tag, "DROP_REPLICATION_SLOT");
        return q;
    }
    case N_SHOW_REPL:
        qr_addcol(q, "name", TEXTOID, -1);
        snprintf(q->tag, sizeof q->tag, "SHOW");
        return q;
    case N_TIMELINE_HISTORY:
        ereport(s, "58P01", "could not open file \"pg_wal/00000001.history\"");
    case N_BASE_BACKUP:
        ereport(s, "0A000", "BASE_BACKUP is not supported by this server");
    case N_START_REPL:
        ereport(s, "0A000", "START_REPLICATION is not supported by this server");
    default:
        snprintf(q->tag, sizeof q->tag, "OK");
        return q;
    }
}

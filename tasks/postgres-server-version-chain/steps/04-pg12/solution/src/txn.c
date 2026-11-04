/* txn.c - transactions, MVCC snapshots and row-level locking.
 * One global mutex serialises statement execution; a condition variable lets a
 * writer wait for the transaction that holds a row, which is what makes the
 * isolation behaviour observable to a concurrent session. */
#include "engine.h"

#define MAXXID 262144

static pthread_mutex_t bigmux = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  bigcv  = PTHREAD_COND_INITIALIZER;
static unsigned char xstate[MAXXID];      /* 0 unused, 1 in progress, 2 committed, 3 aborted */
static uint64 next_xid = 3;

static __thread int lock_depth;
void engine_lock(void)   { if (!lock_depth) pthread_mutex_lock(&bigmux); lock_depth++; }
void engine_unlock(void) { if (lock_depth > 0 && --lock_depth == 0) pthread_mutex_unlock(&bigmux); }
void engine_unlock_all(void) { if (lock_depth) { lock_depth = 0; pthread_mutex_unlock(&bigmux); } }
void engine_wake(void)   { pthread_cond_broadcast(&bigcv); }

static bool xid_committed(uint64 x)
{
    if (x == 0) return false;
    if (x == 1) return true;                 /* bootstrap data */
    if (x >= MAXXID) return false;
    return xstate[x] == 2;
}
static bool xid_inprogress(uint64 x)
{
    if (x == 0 || x == 1 || x >= MAXXID) return false;
    return xstate[x] == 1;
}

typedef struct XactState {
    uint64 xids[64]; int nxids;              /* this transaction and its savepoint levels */
    char  *spnames[64]; int nsp;
    uint64 snapshot_max;                     /* xids >= this are invisible */
    bool   have_snapshot;
} XactState;

static __thread XactState *cur;

static XactState *xs(Session *s)
{
    if (!s->cur) {
        XactState *x = xmalloc(sizeof *x);
        memset(x, 0, sizeof *x);
        s->cur = x;
    }
    cur = s->cur;
    return s->cur;
}

uint64 xact_xid(Session *s)
{
    XactState *x = xs(s);
    if (x->nxids == 0) {
        uint64 id = next_xid++;
        if (id >= MAXXID) id = MAXXID - 1;
        xstate[id] = 1;
        x->xids[x->nxids++] = id;
        s->xid = id;
        if (!x->have_snapshot) { x->snapshot_max = next_xid; x->have_snapshot = true; }
    }
    return x->xids[x->nxids - 1];
}

static bool mine(Session *s, uint64 x)
{
    XactState *st = xs(s);
    for (int i = 0; i < st->nxids; i++) if (st->xids[i] == x) return xstate[x] != 3;
    return false;
}

void xact_snapshot_stmt(Session *s)
{
    XactState *x = xs(s);
    /* READ COMMITTED takes a fresh snapshot for every statement */
    if (s->isolation == 0 || !x->have_snapshot) { x->snapshot_max = next_xid; x->have_snapshot = true; }
}

uint64 snapshot_xid(Session *s) { return xs(s)->snapshot_max; }

bool row_visible(Session *s, Row *r)
{
    XactState *x = xs(s);
    uint64 lo = x->have_snapshot ? x->snapshot_max : next_xid;
    bool created;
    if (mine(s, r->xmin)) created = true;
    else created = xid_committed(r->xmin) && r->xmin < lo;
    if (!created) return false;
    if (r->xmax == 0) return true;
    bool deleted;
    if (mine(s, r->xmax)) deleted = true;
    else deleted = xid_committed(r->xmax) && r->xmax < lo;
    return !deleted;
}

/* A writer waits for whoever holds the row, then re-checks it.  This is what
 * makes one session's UPDATE visibly block another's, which is the whole point
 * of the concurrent-session tests. */
void row_lock_wait(Session *s, Row *r)
{
    int spins = 0;
    for (;;) {
        uint64 holder = 0;
        if (r->xmax != 0 && xid_inprogress(r->xmax) && !mine(s, r->xmax)) holder = r->xmax;
        else if (r->locker != 0 && xid_inprogress(r->locker) && !mine(s, r->locker)) holder = r->locker;
        if (!holder) break;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50 * 1000 * 1000;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&bigcv, &bigmux, &ts);
        if (++spins > 1200) break;         /* 60 s, then give up rather than hang */
    }
    /* REPEATABLE READ and SERIALIZABLE cannot update a row another transaction
     * changed after this transaction's snapshot. */
    if (s->isolation >= 1 && r->xmax != 0 && xid_committed(r->xmax) &&
        r->xmax >= xs(s)->snapshot_max && !mine(s, r->xmax))
        ereport(s, "40001", "could not serialize access due to concurrent update");
}

void row_mark_locked(Session *s, Row *r) { r->locker = xact_xid(s); }

/* A row updated in place by my own transaction is still the current version. */
bool row_is_mine(Session *s, uint64 x) { return mine(s, x); }

void lock_table_read(Session *s, Table *t)  { (void)s; (void)t; }
void lock_table_write(Session *s, Table *t) { (void)s; (void)t; }
void xact_touch(Session *s, Table *t)       { (void)s; (void)t; }

void xact_begin(Session *s, int isolation, int readonly)
{
    XactState *x = xs(s);
    memset(x, 0, sizeof *x);
    s->xact_state = 1;
    s->isolation = isolation;
    s->readonly = readonly;
    x->have_snapshot = false;
}

static void mark(Session *s, unsigned char state)
{
    XactState *x = xs(s);
    for (int i = 0; i < x->nxids; i++)
        if (xstate[x->xids[i]] == 1) xstate[x->xids[i]] = state;
    x->nxids = 0; x->nsp = 0; x->have_snapshot = false;
    s->xid = 0;
    engine_wake();
}

void xact_commit(Session *s)   { mark(s, 2); s->xact_state = 0; guc_end_xact(s); }
void xact_rollback(Session *s) { mark(s, 3); s->xact_state = 0; guc_end_xact(s); }

void xact_savepoint(Session *s, const char *name)
{
    XactState *x = xs(s);
    xact_xid(s);
    uint64 id = next_xid++;
    if (id >= MAXXID) id = MAXXID - 1;
    xstate[id] = 1;
    if (x->nxids < 64) x->xids[x->nxids++] = id;
    if (x->nsp < 64) x->spnames[x->nsp++] = xstrdup(name);
}

void xact_rollback_to(Session *s, const char *name)
{
    XactState *x = xs(s);
    int level = -1;
    for (int i = x->nsp - 1; i >= 0; i--)
        if (!strcmp(x->spnames[i], name)) { level = i; break; }
    if (level < 0) ereport(s, "3B001", "savepoint \"%s\" does not exist", name);
    int base = x->nxids - x->nsp;
    for (int i = base + level; i < x->nxids; i++)
        if (xstate[x->xids[i]] == 1) xstate[x->xids[i]] = 3;
    x->nxids = base + level;
    x->nsp = level;
    /* the savepoint is re-established, as PostgreSQL does */
    uint64 id = next_xid++;
    if (id >= MAXXID) id = MAXXID - 1;
    xstate[id] = 1;
    if (x->nxids < 64) x->xids[x->nxids++] = id;
    if (x->nsp < 64) x->spnames[x->nsp++] = xstrdup(name);
    s->xact_state = 1;
    engine_wake();
}

void xact_release(Session *s, const char *name)
{
    XactState *x = xs(s);
    int level = -1;
    for (int i = x->nsp - 1; i >= 0; i--)
        if (!strcmp(x->spnames[i], name)) { level = i; break; }
    if (level < 0) ereport(s, "3B001", "savepoint \"%s\" does not exist", name);
    x->nsp = level;
}

/* pg_stat_activity for the isolation runner's blocked-session probe.
 *
 * A session publishes "active" before it queues for the engine lock, so this
 * table cannot be protected by that lock: doing so would hide exactly the
 * blocked sessions the probe is looking for.  It gets a mutex of its own,
 * which is always taken innermost, and without it act_unregister()'s memmove
 * races a concurrent act_set() and the two free the same string twice. */
typedef struct { uint32 pid; char *user; char *db; char *state; char *query; char *wait; } ActRow;
static pthread_mutex_t actmux = PTHREAD_MUTEX_INITIALIZER;
static ActRow acts[512];
static int nacts;

void act_register(Session *s)
{
    pthread_mutex_lock(&actmux);
    if (nacts < 512) {
        acts[nacts].pid = s->pid;
        acts[nacts].user = s->user ? xstrdup(s->user) : xstrdup("postgres");
        acts[nacts].db = s->dbname ? xstrdup(s->dbname) : xstrdup("postgres");
        acts[nacts].state = xstrdup("idle");
        acts[nacts].query = xstrdup("");
        acts[nacts].wait = NULL;
        nacts++;
    }
    pthread_mutex_unlock(&actmux);
}
void act_set(Session *s, const char *state, const char *query, const char *wait)
{
    pthread_mutex_lock(&actmux);
    for (int i = 0; i < nacts; i++)
        if (acts[i].pid == s->pid) {
            free(acts[i].state); acts[i].state = xstrdup(state);
            if (query) { free(acts[i].query); acts[i].query = xstrdup(query); }
            free(acts[i].wait); acts[i].wait = wait ? xstrdup(wait) : NULL;
            break;
        }
    pthread_mutex_unlock(&actmux);
}
void act_unregister(Session *s)
{
    pthread_mutex_lock(&actmux);
    for (int i = 0; i < nacts; i++)
        if (acts[i].pid == s->pid) {
            free(acts[i].user); free(acts[i].db);
            free(acts[i].state); free(acts[i].query); free(acts[i].wait);
            memmove(&acts[i], &acts[i+1], (size_t)(nacts - i - 1) * sizeof(ActRow));
            nacts--; break;
        }
    pthread_mutex_unlock(&actmux);
}
void stat_activity_scan(Session *s, QueryResult *r)
{
    pthread_mutex_lock(&actmux);
    for (int i = 0; i < nacts; i++) {
        Datum *row = xmalloc(10 * sizeof(Datum));
        int k = 0;
        row[k++] = d_int(OIDOID, 16384);
        row[k++] = d_text(NAMEOID, acts[i].db);
        row[k++] = d_int(INT4OID, acts[i].pid);
#if PGVC_VN >= 130
        row[k++] = d_null(INT4OID);            /* leader_pid: never a worker here */
#endif
        row[k++] = d_text(NAMEOID, acts[i].user);
        row[k++] = d_text(TEXTOID, "psql");
        row[k++] = d_text(TEXTOID, acts[i].state);
        row[k++] = acts[i].wait ? d_text(TEXTOID, "Lock") : d_null(TEXTOID);
        row[k++] = acts[i].wait ? d_text(TEXTOID, acts[i].wait) : d_null(TEXTOID);
        row[k++] = d_text(TEXTOID, acts[i].query);
        qr_addrow(r, row);
    }
    pthread_mutex_unlock(&actmux);
    (void)s;
}

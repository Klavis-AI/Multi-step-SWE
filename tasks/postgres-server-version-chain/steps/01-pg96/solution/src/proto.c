/* proto.c - the PostgreSQL frontend/backend protocol, versions 3.0 and 3.2.
 * Message formats are those of the "Message Formats" section of the protocol
 * chapter of the PostgreSQL documentation. */
#include "engine.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <signal.h>
#include <poll.h>

#define PG_PROTOCOL_3_0 196608        /* 0x00030000 */
#define PG_PROTOCOL_3_2 196610        /* 0x00030002 */
#define SSL_REQUEST_CODE   80877103
#define CANCEL_REQUEST_CODE 80877102
#define GSSENC_REQUEST_CODE 80877104

void engine_lock(void);
void engine_unlock(void);
void engine_unlock_all(void);
void engine_wake(void);
void xact_snapshot_stmt(Session *s);
void act_register(Session *s);
void act_set(Session *s, const char *state, const char *query, const char *wait);
void act_unregister(Session *s);
void guc_set_global(const char *name, const char *val);
void catalog_undo_begin(void);
void catalog_undo_apply(void);
void catalog_undo_clear(void);

/* ------------------------------------------------------------- raw I/O */
typedef struct { int fd; Buf in; size_t inpos; Buf out; bool eof; } Wire;
static __thread Wire *W;

static void wflush(void)
{
    size_t off = 0;
    while (off < W->out.len) {
        ssize_t k = write(W->fd, W->out.data + off, W->out.len - off);
        if (k <= 0) { if (errno == EINTR) continue; W->eof = true; break; }
        off += (size_t)k;
    }
    buf_reset(&W->out);
}

static bool wfill(size_t need)
{
    while (W->in.len - W->inpos < need) {
        char tmp[16384];
        ssize_t k = read(W->fd, tmp, sizeof tmp);
        if (k == 0) { W->eof = true; return false; }
        if (k < 0) { if (errno == EINTR) continue; W->eof = true; return false; }
        buf_add(&W->in, tmp, (size_t)k);
        if (W->inpos > 65536) {
            memmove(W->in.data, W->in.data + W->inpos, W->in.len - W->inpos);
            W->in.len -= W->inpos; W->inpos = 0;
        }
    }
    return true;
}

static uint32 rd32(const unsigned char *p) { return ((uint32)p[0]<<24)|((uint32)p[1]<<16)|((uint32)p[2]<<8)|p[3]; }
static uint16 rd16(const unsigned char *p) { return (uint16)(((uint16)p[0]<<8)|p[1]); }

static void put8(Buf *b, unsigned char v)  { buf_addc(b, (char)v); }
static void put16(Buf *b, uint16 v) { put8(b,(unsigned char)(v>>8)); put8(b,(unsigned char)v); }
static void put32(Buf *b, uint32 v) { put8(b,(unsigned char)(v>>24)); put8(b,(unsigned char)(v>>16));
                                      put8(b,(unsigned char)(v>>8)); put8(b,(unsigned char)v); }
static void putstr(Buf *b, const char *s) { buf_adds(b, s ? s : ""); buf_addc(b, '\0'); }

/* message framing: start a typed message, patch its length when done */
typedef struct { char type; size_t at; } Msg;
static Msg msg_start(char type)
{
    Msg m; m.type = type; 
    buf_addc(&W->out, type);
    m.at = W->out.len;
    put32(&W->out, 0);
    return m;
}
static void msg_end(Msg m)
{
    uint32 len = (uint32)(W->out.len - m.at);
    unsigned char *p = (unsigned char *)W->out.data + m.at;
    p[0] = (unsigned char)(len >> 24); p[1] = (unsigned char)(len >> 16);
    p[2] = (unsigned char)(len >> 8);  p[3] = (unsigned char)len;
}

/* ------------------------------------------------------------- messages */
static void send_ready(Session *s)
{
    Msg m = msg_start('Z');
    put8(&W->out, s->xact_state == 2 ? 'E' : s->xact_state == 1 ? 'T' : 'I');
    msg_end(m);
    wflush();
}

static void send_param(const char *k, const char *v)
{
    Msg m = msg_start('S');
    putstr(&W->out, k); putstr(&W->out, v);
    msg_end(m);
}

static void send_error_like(Session *s, char type, const char *sev, const char *state,
                            const char *msg, const char *detail, const char *hint, int pos)
{
    const char *ctx = (type == 'E' && s && s->err.context[0]) ? s->err.context : NULL;
    Msg m = msg_start(type);
    put8(&W->out, 'S'); putstr(&W->out, sev);
    put8(&W->out, 'V'); putstr(&W->out, sev);
    put8(&W->out, 'C'); putstr(&W->out, state);
    put8(&W->out, 'M'); putstr(&W->out, msg);
    if (detail && *detail) { put8(&W->out, 'D'); putstr(&W->out, detail); }
    if (hint && *hint)     { put8(&W->out, 'H'); putstr(&W->out, hint); }
    if (ctx)               { put8(&W->out, 'W'); putstr(&W->out, ctx); }
    if (pos > 0) { char b[16]; snprintf(b, sizeof b, "%d", pos); put8(&W->out, 'P'); putstr(&W->out, b); }
    put8(&W->out, 'F'); putstr(&W->out, "pgvc.c");
    put8(&W->out, 'L'); putstr(&W->out, "1");
    put8(&W->out, 'R'); putstr(&W->out, "pgvc");
    put8(&W->out, 0);
    msg_end(m);
    (void)s;
}

static void send_error(Session *s)
{
    send_error_like(s, 'E', "ERROR", s->err.sqlstate, s->err.message,
                    s->err.detail, s->err.hint, s->err.position);
}

void notice(Session *s, const char *sqlstate, const char *fmt, ...)
{
    char msg[ERRBUF];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    send_error_like(s, 'N', "NOTICE", sqlstate, msg, NULL, NULL, 0);
    wflush();
}

void warning(Session *s, const char *sqlstate, const char *fmt, ...)
{
    char msg[ERRBUF];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    send_error_like(s, 'N', "WARNING", sqlstate, msg, NULL, NULL, 0);
    wflush();
}

/* A warning that carries a HINT line, the way CREATE SUBSCRIPTION emits one. */
void warning_hint(Session *s, const char *sqlstate, const char *hint, const char *fmt, ...)
{
    char msg[ERRBUF];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    send_error_like(s, 'N', "WARNING", sqlstate, msg, NULL, hint, 0);
    wflush();
}

/* A warning carrying both DETAIL and HINT, the way the PostgreSQL 18
 * MD5-password deprecation notice is raised. */
void warning_detail_hint(Session *s, const char *sqlstate, const char *detail,
                         const char *hint, const char *fmt, ...)
{
    char msg[ERRBUF];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    send_error_like(s, 'N', "WARNING", sqlstate, msg, detail, hint, 0);
    wflush();
}

static void send_row_desc(Session *s, QueryResult *q, int16 *fmts, int nfmts)
{
    Msg m = msg_start('T');
    put16(&W->out, (uint16)q->ncols);
    for (int i = 0; i < q->ncols; i++) {
        putstr(&W->out, q->colnames[i]);
        put32(&W->out, 0);                       /* table oid */
        put16(&W->out, 0);                       /* column attnum */
        put32(&W->out, q->coltypes[i]);
        int16 len = -1;
        switch (q->coltypes[i]) {
        case BOOLOID: case CHAROID: len = 1; break;
        case INT2OID: len = 2; break;
        case INT4OID: case FLOAT4OID: case OIDOID: case DATEOID:
        case REGCLASSOID: case REGTYPEOID: case REGPROCOID: len = 4; break;
        case INT8OID: case FLOAT8OID: case TIMEOID: case TIMESTAMPOID:
        case TIMESTAMPTZOID: len = 8; break;
        case INTERVALOID: len = 16; break;
        case UUIDOID: len = 16; break;
        case NAMEOID: len = 64; break;
        default: len = -1; break;
        }
        put16(&W->out, (uint16)len);
        put32(&W->out, (uint32)q->coltypmods[i]);
        int16 f = 0;
        if (fmts && nfmts == 1) f = fmts[0];
        else if (fmts && i < nfmts) f = fmts[i];
        put16(&W->out, (uint16)f);
    }
    msg_end(m);
    (void)s;
}

void datum_to_binary(Session *s, Datum d, Buf *b);

static void send_data_row(Session *s, QueryResult *q, Datum *row, int16 *fmts, int nfmts)
{
    Msg m = msg_start('D');
    put16(&W->out, (uint16)q->ncols);
    for (int i = 0; i < q->ncols; i++) {
        int16 f = 0;
        if (fmts && nfmts == 1) f = fmts[0];
        else if (fmts && i < nfmts) f = fmts[i];
        if (row[i].isnull) { put32(&W->out, 0xFFFFFFFFu); continue; }
        if (f == 1) {
            Buf bb; buf_init(&bb);
            datum_to_binary(s, row[i], &bb);
            put32(&W->out, (uint32)bb.len);
            buf_add(&W->out, bb.data, bb.len);
            buf_free(&bb);
        } else {
            char *t = out_func(s, row[i]);
            size_t n = strlen(t);
            put32(&W->out, (uint32)n);
            buf_add(&W->out, t, n);
            free(t);
        }
    }
    msg_end(m);
}

static void send_complete(const char *tag)
{
    Msg m = msg_start('C');
    putstr(&W->out, tag);
    msg_end(m);
}

/* ------------------------------------------------------------ error path */
void ereport(Session *s, const char *sqlstate, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(s->err.message, sizeof s->err.message, fmt, ap);
    va_end(ap);
    snprintf(s->err.sqlstate, sizeof s->err.sqlstate, "%s", sqlstate);
    longjmp(s->jmp, 1);
}
void ereport_pos(Session *s, const char *sqlstate, int pos, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(s->err.message, sizeof s->err.message, fmt, ap);
    va_end(ap);
    snprintf(s->err.sqlstate, sizeof s->err.sqlstate, "%s", sqlstate);
    s->err.position = pos;
    longjmp(s->jmp, 1);
}
void err_detail(Session *s, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(s->err.detail, sizeof s->err.detail, fmt, ap);
    va_end(ap);
}
void err_hint(Session *s, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(s->err.hint, sizeof s->err.hint, fmt, ap);
    va_end(ap);
}

void err_context(Session *s, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(s->err.context, sizeof s->err.context, fmt, ap);
    va_end(ap);
}

/* -------------------------------------------------------------- COPY glue */
static bool g_copy_in_active;
static Buf  g_copy_in_buf;
static size_t g_copy_in_pos;
static bool g_copy_in_done;

void proto_copy_out_start(Session *s, int ncols)
{
    (void)s;
    Msg m = msg_start('H');
    put8(&W->out, 0);
    put16(&W->out, (uint16)ncols);
    for (int i = 0; i < ncols; i++) put16(&W->out, 0);
    msg_end(m);
}
void proto_copy_out_row(Session *s, const char *line, size_t n)
{
    (void)s;
    Msg m = msg_start('d');
    buf_add(&W->out, line, n);
    msg_end(m);
    if (W->out.len > 32768) wflush();
}
void proto_copy_out_end(Session *s)
{
    (void)s;
    Msg m = msg_start('c');
    msg_end(m);
}

static bool read_message(unsigned char *type, const unsigned char **body, uint32 *blen);

void proto_copy_in_start(Session *s, int ncols)
{
    (void)s;
    Msg m = msg_start('G');
    put8(&W->out, 0);
    put16(&W->out, (uint16)ncols);
    for (int i = 0; i < ncols; i++) put16(&W->out, 0);
    msg_end(m);
    wflush();
    g_copy_in_active = true;
    g_copy_in_done = false;
    buf_reset(&g_copy_in_buf);
    g_copy_in_pos = 0;
}

bool proto_copy_in_line(Session *s, char **line, size_t *n)
{
    for (;;) {
        for (size_t i = g_copy_in_pos; i < g_copy_in_buf.len; i++)
            if (g_copy_in_buf.data[i] == '\n') {
                *line = g_copy_in_buf.data + g_copy_in_pos;
                *n = i - g_copy_in_pos;
                g_copy_in_pos = i + 1;
                return true;
            }
        if (g_copy_in_done) {
            if (g_copy_in_pos < g_copy_in_buf.len) {
                *line = g_copy_in_buf.data + g_copy_in_pos;
                *n = g_copy_in_buf.len - g_copy_in_pos;
                g_copy_in_pos = g_copy_in_buf.len;
                return true;
            }
            return false;
        }
        unsigned char type; const unsigned char *body; uint32 blen;
        if (!read_message(&type, &body, &blen)) { g_copy_in_done = true; continue; }
        if (type == 'd') { buf_add(&g_copy_in_buf, body, blen); continue; }
        if (type == 'c') { g_copy_in_done = true; continue; }
        if (type == 'f') {
            g_copy_in_done = true;
            g_copy_in_active = false;
            ereport(s, "57014", "COPY from stdin failed: %s", (const char *)body);
        }
        if (type == 'H') continue;
        g_copy_in_done = true;
    }
}
void proto_copy_in_end(void) { g_copy_in_active = false; }

/* --------------------------------------------------------------- session */
static Session *g_sessions[512];
static int g_nsessions;
static pthread_mutex_t g_slock = PTHREAD_MUTEX_INITIALIZER;

static bool read_message(unsigned char *type, const unsigned char **body, uint32 *blen)
{
    if (!wfill(5)) return false;
    const unsigned char *p = (const unsigned char *)W->in.data + W->inpos;
    *type = p[0];
    uint32 len = rd32(p + 1);
    if (len < 4) return false;
    if (!wfill(1 + len)) return false;
    p = (const unsigned char *)W->in.data + W->inpos;
    *body = p + 5;
    *blen = len - 4;
    W->inpos += 1 + len;
    return true;
}

static char *guc_of_startup(Session *s, const char *k) { return (char *)guc_get(s, k); }

static void send_startup_params(Session *s)
{
    send_param("application_name", s->appname ? s->appname : "");
    send_param("client_encoding", "UTF8");
    send_param("DateStyle", guc_of_startup(s, "DateStyle"));
    send_param("integer_datetimes", "on");
    send_param("IntervalStyle", guc_of_startup(s, "IntervalStyle"));
    send_param("is_superuser", "on");
    send_param("server_encoding", "UTF8");
    send_param("server_version", PGVC_VERSION_TEXT);
    send_param("session_authorization", s->user);
    send_param("standard_conforming_strings", "on");
    send_param("TimeZone", guc_of_startup(s, "TimeZone"));
#if PGVC_VN >= 140
    send_param("in_hot_standby", "off");
#endif
#if PGVC_VN >= 160
    send_param("scram_iterations", "4096");
#endif
    send_param("default_transaction_read_only", "off");
}

int hba_method_for(const char *user, const char *db, bool local);

static bool do_auth(Session *s)
{
    int method = hba_method_for(s->user, s->dbname, false);   /* 0 trust 1 md5 2 scram 3 reject */
    if (method == 3) {
        send_error_like(s, 'E', "FATAL", "28000",
                        "pg_hba.conf rejects connection for host, user, database", NULL, NULL, 0);
        wflush();
        return false;
    }
    RoleEntry *role = role_find(s->user);
    if (method == 0) {
        /* AuthenticationOk is not flushed on its own: PostgreSQL sends it in
         * the same write as the ParameterStatus set, BackendKeyData and
         * ReadyForQuery that follow. */
        Msg m = msg_start('R'); put32(&W->out, 0); msg_end(m);
        return true;
    }
    if (!role || !role->password) {
        /* PostgreSQL still challenges, then fails */
        method = 1;
    }
    unsigned char type; const unsigned char *body; uint32 blen;
    if (method == 1) {
        unsigned char salt[4] = {0x11, 0x22, 0x33, 0x44};
        Msg m = msg_start('R'); put32(&W->out, 5); buf_add(&W->out, salt, 4); msg_end(m);
        wflush();
        if (!read_message(&type, &body, &blen) || type != 'p') return false;
        char *stored = role ? role->password : NULL;
        bool ok = false;
        if (stored && !strncmp(stored, "md5", 3)) {
            char inner[64]; snprintf(inner, sizeof inner, "%s", stored + 3);
            char both[128];
            snprintf(both, sizeof both, "%s", inner);
            unsigned char cat[64];
            memcpy(cat, both, strlen(both));
            memcpy(cat + strlen(both), salt, 4);
            char hex[33];
            md5_hex(cat, strlen(both) + 4, hex);
            char want[40]; snprintf(want, sizeof want, "md5%s", hex);
            ok = !strcmp(want, (const char *)body);
        }
        if (!ok) {
            send_error_like(s, 'E', "FATAL", "28P01",
                            "password authentication failed for user \"%s\"", NULL, NULL, 0);
            char msg[256];
            snprintf(msg, sizeof msg, "password authentication failed for user \"%s\"", s->user);
            buf_reset(&W->out);
            send_error_like(s, 'E', "FATAL", "28P01", msg, NULL, NULL, 0);
            wflush();
            return false;
        }
        Msg m2 = msg_start('R'); put32(&W->out, 0); msg_end(m2);
        wflush();
        return true;
    }
    /* SCRAM-SHA-256, RFC 5802 / RFC 7677 as PostgreSQL implements it */
    {
        Msg m = msg_start('R'); put32(&W->out, 10);
        putstr(&W->out, "SCRAM-SHA-256");
        put8(&W->out, 0);
        msg_end(m);
        wflush();
        if (!read_message(&type, &body, &blen) || type != 'p') return false;
        /* SASLInitialResponse: mechanism name, length, client-first-message */
        const char *mech = (const char *)body;
        size_t off = strlen(mech) + 1;
        if (off + 4 > blen) return false;
        uint32 ilen = rd32(body + off);
        off += 4;
        char *cfirst = xstrndup((const char *)body + off, ilen);
        const char *bare = strstr(cfirst, "n=");
        if (!bare) return false;
        const char *rpos = strstr(cfirst, "r=");
        if (!rpos) return false;
        char cnonce[256];
        snprintf(cnonce, sizeof cnonce, "%s", rpos + 2);
        for (char *q = cnonce; *q; q++) if (*q == ',') { *q = '\0'; break; }
        ScramVerifier ver;
        if (!role || !role->password || !scram_parse_verifier(role->password, &ver)) {
            char msg[256];
            snprintf(msg, sizeof msg, "password authentication failed for user \"%s\"", s->user);
            send_error_like(s, 'E', "FATAL", "28P01", msg, NULL, NULL, 0);
            wflush();
            return false;
        }
        char snonce[64];
        snprintf(snonce, sizeof snonce, "%s%s", cnonce, "PGVCSERVERNONCE0001");
        char saltb64[128];
        b64_encode(ver.salt, ver.salt_len, saltb64);
        char sfirst[512];
        snprintf(sfirst, sizeof sfirst, "r=%s,s=%s,i=%d", snonce, saltb64, ver.iterations);
        Msg m2 = msg_start('R'); put32(&W->out, 11);
        buf_add(&W->out, sfirst, strlen(sfirst));
        msg_end(m2);
        wflush();
        if (!read_message(&type, &body, &blen) || type != 'p') return false;
        char *cfinal = xstrndup((const char *)body, blen);
        char *pp = strstr(cfinal, ",p=");
        if (!pp) return false;
        char cfinal_wo[512];
        snprintf(cfinal_wo, sizeof cfinal_wo, "%.*s", (int)(pp - cfinal), cfinal);
        char authmsg[2048];
        const char *cfirst_bare = strstr(cfirst, "n=");
        snprintf(authmsg, sizeof authmsg, "%s,%s,%s", cfirst_bare, sfirst, cfinal_wo);
        unsigned char csig[32], proof[32], want_proof[32];
        scram_client_signature(ver.stored_key, authmsg, strlen(authmsg), csig);
        int plen = b64_decode(pp + 3, strlen(pp + 3), proof);
        /* ClientKey = ClientProof XOR ClientSignature; StoredKey = H(ClientKey) */
        unsigned char ckey[32], skey[32];
        for (int i = 0; i < 32; i++) ckey[i] = (unsigned char)(proof[i] ^ csig[i]);
        sha256(ckey, 32, skey);
        (void)want_proof;
        if (plen != 32 || memcmp(skey, ver.stored_key, 32) != 0) {
            char msg[256];
            snprintf(msg, sizeof msg, "password authentication failed for user \"%s\"", s->user);
            send_error_like(s, 'E', "FATAL", "28P01", msg, NULL, NULL, 0);
            wflush();
            return false;
        }
        unsigned char ssig[32];
        scram_server_signature(ver.server_key, authmsg, strlen(authmsg), ssig);
        char sigb64[128];
        b64_encode(ssig, 32, sigb64);
        char sfinal[256];
        snprintf(sfinal, sizeof sfinal, "v=%s", sigb64);
        Msg m3 = msg_start('R'); put32(&W->out, 12);
        buf_add(&W->out, sfinal, strlen(sfinal));
        msg_end(m3);
        Msg m4 = msg_start('R'); put32(&W->out, 0); msg_end(m4);
        wflush();
        return true;
    }
}

/* ------------------------------------------------------- query execution */
static void run_one(Session *s, Node *n, const char *sqltext, int16 *fmts, int nfmts,
                    bool want_rowdesc, int maxrows, bool *suspended)
{
    QueryResult *q = exec_node(s, n);
    if (q->ncols > 0) {
        if (want_rowdesc) send_row_desc(s, q, fmts, nfmts);
        int start = 0, end = q->nrows;
        if (maxrows > 0 && end > maxrows) { end = maxrows; if (suspended) *suspended = true; }
        for (int i = start; i < end; i++) send_data_row(s, q, q->rows[i], fmts, nfmts);
        if (suspended && *suspended) return;
        if (!q->tag[0]) snprintf(q->tag, sizeof q->tag, "SELECT %d", q->nrows);
    }
    if (n->tag == N_EMPTY) { Msg m = msg_start('I'); msg_end(m); return; }
    send_complete(q->tag[0] ? q->tag : "SELECT 0");
    (void)sqltext;
}

static void simple_query(Session *s, const char *sql)
{
    const char *rest = sql;
    bool any = false;
    act_set(s, "active", sql, NULL);
    {
        const char *q = sql;
        while (*q && isspace((unsigned char)*q)) q++;
        if (!*q) { Msg m = msg_start('I'); msg_end(m); act_set(s, "idle", NULL, NULL); return; }
    }
    while (rest && *rest) {
        if (setjmp(s->jmp)) {
            engine_lock();
            catalog_undo_apply();
            engine_unlock_all();
            if (s->xact_state == 1) s->xact_state = 2;
            else if (s->xact_state == 0) xact_rollback(s);
            send_error(s);
            engine_wake();
            act_set(s, "idle", NULL, NULL);
            return;
        }
        memset(&s->err, 0, sizeof s->err);
        const char *next = NULL;
        engine_lock();
        Node *n = parse_sql(s, rest, &next);
        if (n->tag == N_EMPTY && !next) {
            engine_unlock();
            if (!any) { Msg m = msg_start('I'); msg_end(m); }
            break;
        }
        if (s->xact_state == 2 && n->tag != N_TRANSACTION) {
            engine_unlock();
            ereport(s, "25P02", "current transaction is aborted, commands ignored until end of transaction block");
        }
        bool implicit = (s->xact_state == 0);
        if (implicit && n->tag != N_TRANSACTION) {
            xact_begin(s, s->isolation, false);
            s->xact_state = 0;
            catalog_undo_begin();
        }
        xact_snapshot_stmt(s);
        run_one(s, n, rest, NULL, 0, true, 0, NULL);
        if (implicit && n->tag != N_TRANSACTION) {
            catalog_undo_clear();
            xact_commit(s); s->xact_state = 0;
        }
        engine_unlock();
        engine_wake();
        any = true;
        rest = next;
    }
    act_set(s, "idle", NULL, NULL);
}

/* ------------------------------------------------ extended query protocol */
static PreparedStmt *find_stmt(Session *s, const char *name)
{
    for (int i = 0; i < s->nstmts; i++) if (!strcmp(s->stmts[i]->name, name)) return s->stmts[i];
    return NULL;
}
static Portal *find_portal(Session *s, const char *name)
{
    for (int i = 0; i < s->nportals; i++) if (!strcmp(s->portals[i]->name, name)) return s->portals[i];
    return NULL;
}

Datum binary_to_datum(Session *s, Oid type, const unsigned char *p, int len);

static void handle_parse(Session *s, const unsigned char *body, uint32 blen)
{
    const char *name = (const char *)body;
    const char *sql = name + strlen(name) + 1;
    const unsigned char *p = (const unsigned char *)sql + strlen(sql) + 1;
    uint16 nparams = rd16(p); p += 2;
    PreparedStmt *ps = xmalloc(sizeof *ps);
    memset(ps, 0, sizeof *ps);
    ps->name = xstrdup(name);
    ps->sql = xstrdup(sql);
    ps->nparams = nparams;
    ps->paramtypes = xmalloc((size_t)(nparams ? nparams : 1) * sizeof(Oid));
    for (int i = 0; i < nparams; i++) { ps->paramtypes[i] = rd32(p); p += 4; }
    ps->tree = parse_sql(s, sql, NULL);
    if (*name) {
        PreparedStmt *old = find_stmt(s, name);
        if (old) ereport(s, "42P05", "prepared statement \"%s\" already exists", name);
    }
    for (int i = 0; i < s->nstmts; i++)
        if (!strcmp(s->stmts[i]->name, name)) { s->stmts[i] = ps; goto done; }
    if (s->nstmts < 64) s->stmts[s->nstmts++] = ps;
done:
    { Msg m = msg_start('1'); msg_end(m); }
    (void)blen;
}

static void handle_bind(Session *s, const unsigned char *body, uint32 blen)
{
    const char *pname = (const char *)body;
    const char *sname = pname + strlen(pname) + 1;
    const unsigned char *p = (const unsigned char *)sname + strlen(sname) + 1;
    PreparedStmt *ps = find_stmt(s, sname);
    if (!ps) ereport(s, "26000", "prepared statement \"%s\" does not exist", sname);
    uint16 nfmt = rd16(p); p += 2;
    int16 *pfmt = xmalloc((size_t)(nfmt ? nfmt : 1) * sizeof(int16));
    for (int i = 0; i < nfmt; i++) { pfmt[i] = (int16)rd16(p); p += 2; }
    uint16 nvals = rd16(p); p += 2;
    Datum *params = xmalloc((size_t)(nvals ? nvals : 1) * sizeof(Datum));
    for (int i = 0; i < nvals; i++) {
        uint32 len = rd32(p); p += 4;
        Oid ty = (i < ps->nparams && ps->paramtypes[i]) ? ps->paramtypes[i] : UNKNOWNOID;
        if (len == 0xFFFFFFFFu) { params[i] = d_null(ty ? ty : TEXTOID); continue; }
        int16 f = (nfmt == 0) ? 0 : (nfmt == 1 ? pfmt[0] : (i < nfmt ? pfmt[i] : 0));
        if (f == 1) params[i] = binary_to_datum(s, ty ? ty : TEXTOID, p, (int)len);
        else params[i] = in_func(s, ty ? ty : UNKNOWNOID, -1, (const char *)p, (int32)len);
        p += len;
    }
    uint16 nres = rd16(p); p += 2;
    int16 *rfmt = xmalloc((size_t)(nres ? nres : 1) * sizeof(int16));
    for (int i = 0; i < nres; i++) { rfmt[i] = (int16)rd16(p); p += 2; }
    Portal *po = xmalloc(sizeof *po);
    memset(po, 0, sizeof *po);
    po->name = xstrdup(pname);
    po->stmt = ps; po->params = params; po->nparams = nvals;
    po->resfmt = rfmt; po->nresfmt = nres;
    for (int i = 0; i < s->nportals; i++)
        if (!strcmp(s->portals[i]->name, pname)) { s->portals[i] = po; goto done; }
    if (s->nportals < 64) s->portals[s->nportals++] = po;
done:
    { Msg m = msg_start('2'); msg_end(m); }
    (void)blen;
}

QueryResult *exec_with_params(Session *s, Node *n, Datum *params, int nparams);

static void handle_describe(Session *s, const unsigned char *body, uint32 blen)
{
    char kind = (char)body[0];
    const char *name = (const char *)body + 1;
    if (kind == 'S') {
        PreparedStmt *ps = find_stmt(s, name);
        if (!ps) ereport(s, "26000", "prepared statement \"%s\" does not exist", name);
        Msg m = msg_start('t');
        put16(&W->out, (uint16)ps->nparams);
        for (int i = 0; i < ps->nparams; i++)
            put32(&W->out, ps->paramtypes[i] ? ps->paramtypes[i] : TEXTOID);
        msg_end(m);
        if (ps->tree->tag == N_SELECT || ps->tree->tag == N_SHOW || ps->tree->tag == N_EXPLAIN) {
            Datum *dummy = xmalloc((size_t)(ps->nparams ? ps->nparams : 1) * sizeof(Datum));
            for (int i = 0; i < ps->nparams; i++)
                dummy[i] = d_null(ps->paramtypes[i] ? ps->paramtypes[i] : TEXTOID);
            QueryResult *q = exec_with_params(s, ps->tree, dummy, ps->nparams);
            if (q->ncols) send_row_desc(s, q, NULL, 0);
            else { Msg n2 = msg_start('n'); msg_end(n2); }
        } else { Msg n2 = msg_start('n'); msg_end(n2); }
        return;
    }
    Portal *po = find_portal(s, name);
    if (!po) ereport(s, "34000", "portal \"%s\" does not exist", name);
    QueryResult *q;
    if (po->is_cursor) q = po->res;
    else { q = exec_with_params(s, po->stmt->tree, po->params, po->nparams);
           po->res = q; po->next_row = 0; }
    if (q->ncols) send_row_desc(s, q, po->resfmt, po->nresfmt);
    else { Msg n2 = msg_start('n'); msg_end(n2); }
    (void)blen;
}

static void handle_execute(Session *s, const unsigned char *body, uint32 blen)
{
    const char *name = (const char *)body;
    uint32 maxrows = rd32(body + strlen(name) + 1);
    Portal *po = find_portal(s, name);
    if (!po) ereport(s, "34000", "portal \"%s\" does not exist", name);
    if (!po->res && !po->is_cursor) {
        po->res = exec_with_params(s, po->stmt->tree, po->params, po->nparams);
        po->next_row = 0;
    }
    QueryResult *q = po->res;
    if (q->ncols) {
        int end = q->nrows;
        if (maxrows > 0 && po->next_row + (int)maxrows < end) end = po->next_row + (int)maxrows;
        for (int i = po->next_row; i < end; i++) send_data_row(s, q, q->rows[i], po->resfmt, po->nresfmt);
        bool more = (end < q->nrows);
        po->next_row = end;
        if (more) { Msg m = msg_start('s'); msg_end(m); return; }
    }
    if (!po->is_cursor && po->stmt->tree->tag == N_EMPTY)
        { Msg m = msg_start('I'); msg_end(m); po->res = NULL; return; }
    if (po->is_cursor) { char tag[64]; snprintf(tag, sizeof tag, "FETCH %d", po->next_row);
                         send_complete(tag); return; }
    send_complete(q->tag[0] ? q->tag : "SELECT 0");
    po->res = NULL;
    (void)blen;
}

static void extended_loop(Session *s, unsigned char type, const unsigned char *body, uint32 blen)
{
    switch (type) {
    case 'P': handle_parse(s, body, blen); break;
    case 'B': handle_bind(s, body, blen); break;
    case 'D': handle_describe(s, body, blen); break;
    case 'E': handle_execute(s, body, blen); break;
    case 'C': {
        char kind = (char)body[0];
        const char *name = (const char *)body + 1;
        if (kind == 'S') {
            for (int i = 0; i < s->nstmts; i++)
                if (!strcmp(s->stmts[i]->name, name)) {
                    memmove(&s->stmts[i], &s->stmts[i+1], (size_t)(s->nstmts-i-1)*sizeof(PreparedStmt*));
                    s->nstmts--; break;
                }
        } else {
            for (int i = 0; i < s->nportals; i++)
                if (!strcmp(s->portals[i]->name, name)) {
                    memmove(&s->portals[i], &s->portals[i+1], (size_t)(s->nportals-i-1)*sizeof(Portal*));
                    s->nportals--; break;
                }
        }
        Msg m = msg_start('3'); msg_end(m);
        break;
    }
    default: break;
    }
}

/* --------------------------------------------------------- main loop */
static void session_loop(Session *s)
{
    for (;;) {
        unsigned char type; const unsigned char *body; uint32 blen;
        if (!read_message(&type, &body, &blen)) return;
        if (type == 'X') return;
        if (s->ignore_till_sync && type != 'S') continue;
        switch (type) {
        case 'Q':
            simple_query(s, (const char *)body);
            send_ready(s);
            break;
        case 'P': case 'B': case 'D': case 'E': case 'C': {
            if (setjmp(s->jmp)) {
                engine_lock();
                catalog_undo_apply();
                engine_unlock_all();
                if (s->xact_state == 1) s->xact_state = 2;
                else if (s->xact_state == 0) xact_rollback(s);
                send_error(s);
                engine_wake();
                s->ignore_till_sync = true;
                break;
            }
            memset(&s->err, 0, sizeof s->err);
            engine_lock();
            bool implicit = (s->xact_state == 0);
            if (implicit && (type == 'E' || type == 'D')) {
                xact_begin(s, s->isolation, false);
                s->xact_state = 0;
            }
            if (s->xact_state == 2 && type != 'S')
                { engine_unlock();
                  ereport(s, "25P02", "current transaction is aborted, commands ignored until end of transaction block"); }
            xact_snapshot_stmt(s);
            extended_loop(s, type, body, blen);
            if (implicit && type == 'E' && s->xact_state == 0) { xact_commit(s); s->xact_state = 0; }
            engine_unlock();
            engine_wake();
            break;
        }
        case 'H': wflush(); break;
        case 'S':
            s->ignore_till_sync = false;
            engine_lock();
            if (s->xact_state == 1) { /* implicit transaction ends at Sync */ }
            engine_unlock();
            send_ready(s);
            break;
        case 'd': case 'c': case 'f': break;
        default: {
            char msg[128];
            snprintf(msg, sizeof msg, "invalid frontend message type %d", type);
            send_error_like(s, 'E', "FATAL", "08P01", msg, NULL, NULL, 0);
            wflush();
            return;
        }
        }
    }
}

/* --------------------------------------------------------- connection */
void store_load_into(Database *db);

static uint32 g_next_pid = 10000;

static void *conn_thread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    Wire w; memset(&w, 0, sizeof w);
    w.fd = fd; buf_init(&w.in); buf_init(&w.out);
    W = &w;
    Session *s = xmalloc(sizeof *s);
    memset(s, 0, sizeof *s);
    s->fd = fd;
    pthread_mutex_lock(&g_slock);
    s->pid = ++g_next_pid;
    pthread_mutex_unlock(&g_slock);
    s->secret = 0x5eed ^ (s->pid * 2654435761u);
    buf_init(&s->out);

    /* startup packet */
    for (;;) {
        if (!wfill(4)) goto done;
        const unsigned char *p = (const unsigned char *)w.in.data + w.inpos;
        uint32 len = rd32(p);
        if (len < 8 || len > 1u<<20) goto done;
        if (!wfill(len)) goto done;
        p = (const unsigned char *)w.in.data + w.inpos;
        uint32 code = rd32(p + 4);
        if (code == SSL_REQUEST_CODE || code == GSSENC_REQUEST_CODE) {
            w.inpos += len;
            buf_addc(&w.out, 'N');
            wflush();
            continue;
        }
        if (code == CANCEL_REQUEST_CODE) {
            /* CancelRequest carries the backend pid and secret from the
             * BackendKeyData of the connection to interrupt. */
            if (len >= 16) {
                uint32 cpid = rd32(p + 8), ckey = rd32(p + 12);
                pthread_mutex_lock(&g_slock);
                for (int i = 0; i < g_nsessions; i++)
                    if (g_sessions[i]->pid == cpid && g_sessions[i]->secret == ckey)
                        g_sessions[i]->cancel_pending = 1;
                pthread_mutex_unlock(&g_slock);
            }
            w.inpos += len;
            goto done;
        }
        int major = (int)(code >> 16), minor = (int)(code & 0xffff);
        s->proto_major = major; s->proto_minor = minor;
        const char *q = (const char *)p + 8;
        const char *end = (const char *)p + len;
        char *unsupported[32]; int nunsup = 0;
        while (q < end && *q) {
            const char *k = q; q += strlen(q) + 1;
            if (q >= end) break;
            const char *v = q; q += strlen(q) + 1;
            if (!strcmp(k, "user")) s->user = xstrdup(v);
            else if (!strcmp(k, "database")) s->dbname = xstrdup(v);
            else if (!strcmp(k, "application_name")) s->appname = xstrdup(v);
            else if (!strcmp(k, "replication")) s->replication = (*v == 't' || *v == '1' ||
                                                                  !strcmp(v, "database") || !strcmp(v, "on"));
            else if (!strncmp(k, "_pq_.", 5)) { if (nunsup < 32) unsupported[nunsup++] = xstrdup(k); }
            else if (!strcmp(k, "options")) { }
        }
        w.inpos += len;
        if (major != 3) {
            /* A pre-3 startup packet gets the protocol-2 style error: a bare
             * 'E', the text, and a NUL, with no length word.  PostgreSQL 14
             * removed the v2 parser, which changes the text. */
            char msg[160];
#if PGVC_VN < 140
            snprintf(msg, sizeof msg,
                     "FATAL:  no PostgreSQL user name specified in startup packet\n");
#else
            snprintf(msg, sizeof msg,
                     "FATAL:  unsupported frontend protocol %d.%d: server supports 3.0 to %s\n",
                     major, minor, PGVC_VN >= 180 ? "3.2" : "3.0");
#endif
            buf_addc(&W->out, 'E');
            buf_adds(&W->out, msg);
            buf_addc(&W->out, '\0');
            wflush();
            goto done;
        }
        {
            /* NegotiateProtocolVersion, which every 3.x server sends when the
             * requested minor is newer than it knows or a _pq_ option is
             * unrecognised. */
            int supported = (PGVC_VN >= 180) ? 2 : 0;
            if (minor > supported || nunsup) {
                Msg m = msg_start('v');
                put32(&W->out, (uint32)(minor > supported ? supported : minor));
                put32(&W->out, (uint32)nunsup);
                for (int i = 0; i < nunsup; i++) putstr(&W->out, unsupported[i]);
                msg_end(m);
                if (minor > supported) s->proto_minor = supported;
            }
        }
        break;
    }
    if (!s->user) s->user = xstrdup("postgres");
    if (!s->dbname) s->dbname = xstrdup(s->user);
    if (!do_auth(s)) goto done;

    engine_lock();
    s->db = db_find(s->dbname);
    engine_unlock();
    if (!s->db) {
        char msg[256];
        snprintf(msg, sizeof msg, "database \"%s\" does not exist", s->dbname);
        send_error_like(s, 'E', "FATAL", "3D000", msg, NULL, NULL, 0);
        wflush();
        goto done;
    }
    if (!role_find(s->user)) {
        char msg[256];
        snprintf(msg, sizeof msg, "role \"%s\" does not exist", s->user);
        send_error_like(s, 'E', "FATAL", "28000", msg, NULL, NULL, 0);
        wflush();
        goto done;
    }
    send_startup_params(s);
    { Msg m = msg_start('K'); put32(&W->out, s->pid); put32(&W->out, s->secret); msg_end(m); }
    act_register(s);
    pthread_mutex_lock(&g_slock);
    if (g_nsessions < 512) g_sessions[g_nsessions++] = s;
    pthread_mutex_unlock(&g_slock);
    send_ready(s);
    session_loop(s);
done:
    engine_lock();
    if (s->xact_state) xact_rollback(s);
    engine_unlock();
    engine_wake();
    act_unregister(s);
    pthread_mutex_lock(&g_slock);
    for (int i = 0; i < g_nsessions; i++)
        if (g_sessions[i] == s) { memmove(&g_sessions[i], &g_sessions[i+1],
                                          (size_t)(g_nsessions-i-1)*sizeof(Session*)); g_nsessions--; break; }
    pthread_mutex_unlock(&g_slock);
    close(fd);
    return NULL;
}

void serve_connection(int fd)
{
    pthread_t th;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16 * 1024 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, conn_thread, (void *)(intptr_t)fd) != 0) close(fd);
    pthread_attr_destroy(&attr);
}

#define _GNU_SOURCE
#include "api.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------------
 * Kubernetes protobuf request bodies.
 *
 * A body of media type application/vnd.kubernetes.protobuf is the four-byte
 * magic "k8s\0" followed by a runtime.Unknown message:
 *
 *     Unknown { TypeMeta typeMeta = 1; bytes raw = 2;
 *               string contentEncoding = 3; string contentType = 4; }
 *     TypeMeta { string apiVersion = 1; string kind = 2; }
 *
 * `raw` is the object in its own generated schema. Only the kinds a client
 * actually writes this way are described here; everything else is refused, and
 * clients that send JSON are unaffected.
 * -------------------------------------------------------------------------- */

typedef enum { PF_STR, PF_INT, PF_BOOL, PF_MSG, PF_MAP, PF_STRLIST } pftype;

typedef struct pfield pfield;
struct pdesc { const char *name; const pfield *fields; };
typedef struct pdesc pdesc;
struct pfield {
    int num;
    const char *name;
    pftype t;
    const pdesc *sub;
};

static const pfield F_OBJECTMETA[] = {
    {1, "name", PF_STR, NULL},
    {2, "generateName", PF_STR, NULL},
    {3, "namespace", PF_STR, NULL},
    {11, "labels", PF_MAP, NULL},
    {12, "annotations", PF_MAP, NULL},
    {14, "finalizers", PF_STRLIST, NULL},
    {0, NULL, 0, NULL}
};
static const pdesc D_OBJECTMETA = {"ObjectMeta", F_OBJECTMETA};

static const pfield F_CONFIGMAP[] = {
    {1, "metadata", PF_MSG, &D_OBJECTMETA},
    {2, "data", PF_MAP, NULL},
    {4, "immutable", PF_BOOL, NULL},
    {0, NULL, 0, NULL}
};
static const pdesc D_CONFIGMAP = {"ConfigMap", F_CONFIGMAP};

static const pfield F_SSRR_SPEC[] = {
    {1, "namespace", PF_STR, NULL},
    {0, NULL, 0, NULL}
};
static const pdesc D_SSRR_SPEC = {"SelfSubjectRulesReviewSpec", F_SSRR_SPEC};

static const pfield F_SSRR[] = {
    {1, "metadata", PF_MSG, &D_OBJECTMETA},
    {2, "spec", PF_MSG, &D_SSRR_SPEC},
    {0, NULL, 0, NULL}
};
static const pdesc D_SSRR = {"SelfSubjectRulesReview", F_SSRR};

static const pfield F_RESATTR[] = {
    {1, "namespace", PF_STR, NULL},
    {2, "verb", PF_STR, NULL},
    {3, "group", PF_STR, NULL},
    {4, "version", PF_STR, NULL},
    {5, "resource", PF_STR, NULL},
    {6, "subresource", PF_STR, NULL},
    {7, "name", PF_STR, NULL},
    {0, NULL, 0, NULL}
};
static const pdesc D_RESATTR = {"ResourceAttributes", F_RESATTR};

static const pfield F_NONRESATTR[] = {
    {1, "path", PF_STR, NULL},
    {2, "verb", PF_STR, NULL},
    {0, NULL, 0, NULL}
};
static const pdesc D_NONRESATTR = {"NonResourceAttributes", F_NONRESATTR};

static const pfield F_SSAR_SPEC[] = {
    {1, "resourceAttributes", PF_MSG, &D_RESATTR},
    {2, "nonResourceAttributes", PF_MSG, &D_NONRESATTR},
    {0, NULL, 0, NULL}
};
static const pdesc D_SSAR_SPEC = {"SelfSubjectAccessReviewSpec", F_SSAR_SPEC};

static const pfield F_SSAR[] = {
    {1, "metadata", PF_MSG, &D_OBJECTMETA},
    {2, "spec", PF_MSG, &D_SSAR_SPEC},
    {0, NULL, 0, NULL}
};
static const pdesc D_SSAR = {"SelfSubjectAccessReview", F_SSAR};

static const pfield F_SAR_SPEC[] = {
    {1, "resourceAttributes", PF_MSG, &D_RESATTR},
    {2, "nonResourceAttributes", PF_MSG, &D_NONRESATTR},
    {3, "user", PF_STR, NULL},
    {4, "groups", PF_STRLIST, NULL},
    {0, NULL, 0, NULL}
};
static const pdesc D_SAR_SPEC = {"SubjectAccessReviewSpec", F_SAR_SPEC};

static const pfield F_SAR[] = {
    {1, "metadata", PF_MSG, &D_OBJECTMETA},
    {2, "spec", PF_MSG, &D_SAR_SPEC},
    {0, NULL, 0, NULL}
};
static const pdesc D_SAR = {"SubjectAccessReview", F_SAR};

static const pfield F_NAMESPACE[] = {
    {1, "metadata", PF_MSG, &D_OBJECTMETA},
    {0, NULL, 0, NULL}
};
static const pdesc D_NAMESPACE = {"Namespace", F_NAMESPACE};

static const pdesc *KINDS[] = {
    &D_CONFIGMAP, &D_SSRR, &D_SSAR, &D_SAR, &D_NAMESPACE, NULL
};

/* ------------------------------ wire format -------------------------------- */

typedef struct { const unsigned char *p, *end; int bad; } pbuf;

static unsigned long long pb_varint(pbuf *b){
    unsigned long long v = 0;
    int shift = 0;
    while(b->p < b->end){
        unsigned char c = *b->p++;
        v |= (unsigned long long)(c & 0x7f) << shift;
        if(!(c & 0x80)) return v;
        shift += 7;
        if(shift > 63){ b->bad = 1; return 0; }
    }
    b->bad = 1;
    return 0;
}

static int pb_bytes(pbuf *b, const unsigned char **out, size_t *len){
    unsigned long long n = pb_varint(b);
    if(b->bad || n > (unsigned long long)(b->end - b->p)){ b->bad = 1; return 0; }
    *out = b->p; *len = (size_t)n;
    b->p += n;
    return 1;
}

static int pb_skip(pbuf *b, int wire){
    const unsigned char *q; size_t n;
    switch(wire){
        case 0: pb_varint(b); return !b->bad;
        case 1: if(b->end - b->p < 8){ b->bad = 1; return 0; } b->p += 8; return 1;
        case 2: return pb_bytes(b, &q, &n);
        case 5: if(b->end - b->p < 4){ b->bad = 1; return 0; } b->p += 4; return 1;
        default: b->bad = 1; return 0;
    }
}

static json *pb_decode(const unsigned char *p, size_t n, const pdesc *d);

/* map<string,string> is a repeated message of {1: key, 2: value} */
static void pb_map_entry(json *into, const unsigned char *p, size_t n){
    pbuf b = { p, p + n, 0 };
    char *k = NULL, *v = NULL;
    while(b.p < b.end && !b.bad){
        unsigned long long tag = pb_varint(&b);
        if(b.bad) break;
        int num = (int)(tag >> 3), wire = (int)(tag & 7);
        if(wire == 2){
            const unsigned char *q; size_t l;
            if(!pb_bytes(&b, &q, &l)) break;
            char *s = malloc(l + 1);
            memcpy(s, q, l); s[l] = 0;
            if(num == 1){ free(k); k = s; }
            else if(num == 2){ free(v); v = s; }
            else free(s);
        } else if(!pb_skip(&b, wire)) break;
    }
    if(k) j_set(into, k, j_str(v ?: ""));
    free(k); free(v);
}

static json *pb_decode(const unsigned char *p, size_t n, const pdesc *d){
    json *o = j_obj();
    pbuf b = { p, p + n, 0 };
    while(b.p < b.end && !b.bad){
        unsigned long long tag = pb_varint(&b);
        if(b.bad) break;
        int num = (int)(tag >> 3), wire = (int)(tag & 7);
        const pfield *f = NULL;
        for(int i = 0; d->fields[i].name; i++)
            if(d->fields[i].num == num){ f = &d->fields[i]; break; }
        if(!f){ if(!pb_skip(&b, wire)) break; continue; }
        if(f->t == PF_INT || f->t == PF_BOOL){
            if(wire != 0){ if(!pb_skip(&b, wire)) break; continue; }
            unsigned long long v = pb_varint(&b);
            j_set(o, f->name, f->t == PF_BOOL ? j_bool(v != 0) : j_int((long long)v));
            continue;
        }
        if(wire != 2){ if(!pb_skip(&b, wire)) break; continue; }
        const unsigned char *q; size_t l;
        if(!pb_bytes(&b, &q, &l)) break;
        if(f->t == PF_STR){
            char *s = malloc(l + 1);
            memcpy(s, q, l); s[l] = 0;
            j_set(o, f->name, j_str(s));
            free(s);
        } else if(f->t == PF_STRLIST){
            json *a = j_get(o, f->name);
            if(!a){ a = j_arr(); j_set(o, f->name, a); }
            char *s = malloc(l + 1);
            memcpy(s, q, l); s[l] = 0;
            j_push(a, j_str(s));
            free(s);
        } else if(f->t == PF_MAP){
            json *m = j_get(o, f->name);
            if(!m){ m = j_obj(); j_set(o, f->name, m); }
            pb_map_entry(m, q, l);
        } else if(f->t == PF_MSG){
            json *sub = pb_decode(q, l, f->sub);
            if(!sub){ j_free(o); return NULL; }
            j_set(o, f->name, sub);
        }
    }
    if(b.bad){ j_free(o); return NULL; }
    return o;
}

json *k8s_protobuf_decode(const char *body, size_t len){
    if(len < 8 || memcmp(body, "k8s\0", 4)) return NULL;
    pbuf b = { (const unsigned char*)body + 4, (const unsigned char*)body + len, 0 };
    char *apiVersion = NULL, *kind = NULL;
    const unsigned char *raw = NULL; size_t rawlen = 0;
    while(b.p < b.end && !b.bad){
        unsigned long long tag = pb_varint(&b);
        if(b.bad) break;
        int num = (int)(tag >> 3), wire = (int)(tag & 7);
        if(wire != 2){ if(!pb_skip(&b, wire)) break; continue; }
        const unsigned char *q; size_t l;
        if(!pb_bytes(&b, &q, &l)) break;
        if(num == 1){                       /* TypeMeta */
            pbuf t = { q, q + l, 0 };
            while(t.p < t.end && !t.bad){
                unsigned long long tt = pb_varint(&t);
                if(t.bad) break;
                int tn = (int)(tt >> 3), tw = (int)(tt & 7);
                if(tw != 2){ if(!pb_skip(&t, tw)) break; continue; }
                const unsigned char *s; size_t sl;
                if(!pb_bytes(&t, &s, &sl)) break;
                char *v = malloc(sl + 1);
                memcpy(v, s, sl); v[sl] = 0;
                if(tn == 1){ free(apiVersion); apiVersion = v; }
                else if(tn == 2){ free(kind); kind = v; }
                else free(v);
            }
        } else if(num == 2){ raw = q; rawlen = l; }
    }
    json *out = NULL;
    if(kind && raw){
        for(int i = 0; KINDS[i]; i++)
            if(!strcmp(KINDS[i]->name, kind)){ out = pb_decode(raw, rawlen, KINDS[i]); break; }
        if(out){
            if(apiVersion) j_set(out, "apiVersion", j_str(apiVersion));
            j_set(out, "kind", j_str(kind));
        }
    }
    free(apiVersion); free(kind);
    return out;
}

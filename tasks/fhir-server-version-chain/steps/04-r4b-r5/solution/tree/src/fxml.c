#include "fxml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------- writing ---------------- */

static void xesc(buf *b, const char *s, int attr) {
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        switch (*p) {
            case '&': buf_puts(b, "&amp;"); break;
            case '<': buf_puts(b, "&lt;"); break;
            case '>': buf_puts(b, "&gt;"); break;
            case '"': if (attr) buf_puts(b, "&quot;"); else buf_putc(b, '"'); break;
            default: buf_putc(b, (char)*p);
        }
    }
}

static const char *prim_lexeme(jval *v) {
    if (j_is(v, J_STR)) return j_str_of(v);
    if (j_is(v, J_NUM)) return j_num_of(v);
    if (j_is(v, J_BOOL)) return v->u.b ? "true" : "false";
    return NULL;
}

static int is_prim_type(fhirdefs *d, const char *t) {
    sdef *s = defs_sd(d, t);
    return s && str_eq(s->kind, "primitive-type");
}

static void write_res(arena *a, fhirdefs *d, jval *res, buf *out, int indent, int root);

static void write_children(arena *a, fhirdefs *d, jval *obj, sdef *s, edef *parent,
                           buf *out, int indent);

static void indentf(buf *out, int n) { buf_putc(out, '\n'); for (int i = 0; i < n; i++) buf_puts(out, "  "); }

static void write_element(arena *a, fhirdefs *d, const char *name, jval *v, jval *shadow,
                          sdef *owner, edef *e, const char *type, buf *out, int indent) {
    if (type && str_eq(type, "xhtml")) {
        indentf(out, indent);
        buf_printf(out, "<%s xmlns=\"http://www.w3.org/1999/xhtml\">", name);
        const char *x = prim_lexeme(v);
        /* the narrative is already XHTML: strip an outer <div ...> wrapper */
        if (x) {
            const char *st = strchr(x, '>');
            const char *en = strrchr(x, '<');
            if (st && en && st < en) buf_put(out, st + 1, (size_t)(en - st - 1));
            else buf_puts(out, x);
        }
        buf_printf(out, "</%s>", name);
        return;
    }
    if (type && is_prim_type(d, type)) {
        const char *lex = prim_lexeme(v);
        indentf(out, indent);
        buf_printf(out, "<%s", name);
        if (lex) { buf_puts(out, " value=\""); xesc(out, lex, 1); buf_putc(out, '"'); }
        if (shadow && j_is(shadow, J_OBJ) && shadow->u.obj.n) {
            buf_putc(out, '>');
            sdef *es = defs_sd(d, "Element");
            write_children(a, d, shadow, es, es ? es->root : NULL, out, indent + 1);
            indentf(out, indent);
            buf_printf(out, "</%s>", name);
        } else buf_puts(out, "/>");
        return;
    }
    if (type && (str_eq(type, "Resource") || defs_resource(d, type))) {
        indentf(out, indent);
        buf_printf(out, "<%s>", name);
        write_res(a, d, v, out, indent + 1, 0);
        indentf(out, indent);
        buf_printf(out, "</%s>", name);
        return;
    }
    indentf(out, indent);
    buf_printf(out, "<%s", name);
    /* Element.id and Extension.url are XML attributes */
    const char *eid = j_is(v, J_OBJ) ? j_gets(v, "id") : NULL;
    const char *eurl = (type && str_eq(type, "Extension") && j_is(v, J_OBJ)) ? j_gets(v, "url") : NULL;
    if (eid) { buf_puts(out, " id=\""); xesc(out, eid, 1); buf_putc(out, '"'); }
    if (eurl) { buf_puts(out, " url=\""); xesc(out, eurl, 1); buf_putc(out, '"'); }
    edef *anchor = NULL;
    sdef *asd = NULL;
    ed_children_of(d, owner, e, type, &asd, &anchor);
    if (!j_is(v, J_OBJ)) { buf_puts(out, "/>"); return; }
    int any = 0;
    for (jmem *m = v->u.obj.first; m; m = m->next) {
        if (str_eq(m->key, "id")) continue;
        if (eurl && str_eq(m->key, "url")) continue;
        if (m->key[0] == '_') continue;
        any = 1;
    }
    if (!any) { buf_puts(out, "/>"); return; }
    buf_putc(out, '>');
    write_children(a, d, v, asd, anchor, out, indent + 1);
    indentf(out, indent);
    buf_printf(out, "</%s>", name);
}

static void write_one_member(arena *a, fhirdefs *d, jval *obj, sdef *s, edef *e,
                             const char *key, jval *val, const char *type, buf *out, int indent) {
    char shadow_key[256];
    snprintf(shadow_key, sizeof shadow_key, "_%s", key);
    jval *shadow = j_get(obj, shadow_key);
    if (j_is(val, J_ARR)) {
        for (int i = 0; i < j_len(val); i++) {
            jval *item = j_at(val, i);
            jval *sh = shadow ? j_at(shadow, i) : NULL;
            if (j_is(item, J_NULL) && (!sh || j_is(sh, J_NULL))) continue;
            write_element(a, d, key, item, sh, s, e, type, out, indent);
        }
    } else {
        write_element(a, d, key, val, shadow, s, e, type, out, indent);
    }
}

/* FHIR XML is order-significant: children appear in the order the
   StructureDefinition declares them (xml.html). */
static void write_children(arena *a, fhirdefs *d, jval *obj, sdef *s, edef *parent, buf *out, int indent) {
    if (!j_is(obj, J_OBJ)) return;
    int done_cap = 0;
    for (jmem *m = obj->u.obj.first; m; m = m->next) done_cap++;
    const char **done = calloc((size_t)done_cap + 1, sizeof(char *));
    size_t ndone = 0;
    for (edef *e = parent ? parent->first_child : NULL; e; e = e->next) {
        size_t tl = strlen(e->tail);
        int choice = tl > 3 && strcmp(e->tail + tl - 3, "[x]") == 0;
        for (jmem *m = obj->u.obj.first; m; m = m->next) {
            const char *key = m->key;
            if (str_eq(key, "resourceType") || key[0] == '_') continue;
        if (str_eq(key, "fhir_comments")) continue;
            /* `fhir_comments` is DSTU2's JSON-only carrier for what the XML
               form keeps as XML comments; it is not an element. */
            if (str_eq(key, "fhir_comments")) continue;
            if (str_eq(key, "id") && parent && parent->parent) continue;
            if (str_eq(key, "url") && s && str_eq(s->type, "Extension")) continue;
            const char *type = NULL;
            if (choice) {
                type = ed_choice_type(e, key);
                if (!type) continue;
            } else {
                if (!str_eq(key, e->tail)) continue;
                if (e->types.n >= 1) type = (const char *)e->types.v[0];
            }
            done[ndone++] = key;
            if (!type && str_eq(key, "id")) type = "string";
            /* an element defined by contentReference carries no type of its own;
               its children come from the path it points at */
            if (!type && e->content_ref) type = "BackboneElement";
            if (!type) continue;
            write_one_member(a, d, obj, s, e, key, m->val, type, out, indent);
        }
    }
    /* anything the definitions do not describe is written where it stands */
    for (jmem *m = obj->u.obj.first; m; m = m->next) {
        const char *key = m->key;
        if (str_eq(key, "resourceType") || key[0] == '_') continue;
        if (str_eq(key, "fhir_comments")) continue;
        if (str_eq(key, "id") && parent && parent->parent) continue;
        if (str_eq(key, "url") && s && str_eq(s->type, "Extension")) continue;
        int seen = 0;
        for (size_t i = 0; i < ndone; i++) if (str_eq(done[i], key)) seen = 1;
        if (seen) continue;
        const char *type = NULL;
        edef *e = parent ? ed_child(d, s, parent, key, &type) : NULL;
        if (!type && e && e->types.n >= 1) type = (const char *)e->types.v[0];
        if (!type && str_eq(key, "id")) type = "string";
        if (!type && e && e->content_ref) type = "BackboneElement";
        if (!type) continue;
        write_one_member(a, d, obj, s, e, key, m->val, type, out, indent);
    }
    free(done);
}

static void write_res(arena *a, fhirdefs *d, jval *res, buf *out, int indent, int root) {
    const char *rt = j_gets(res, "resourceType");
    if (!rt) return;
    sdef *s = defs_resource(d, rt);
    indentf(out, indent);
    buf_printf(out, "<%s", rt);
    if (root) buf_puts(out, " xmlns=\"http://hl7.org/fhir\"");
    buf_putc(out, '>');
    write_children(a, d, res, s, s ? s->root : NULL, out, indent + 1);
    indentf(out, indent);
    buf_printf(out, "</%s>", rt);
}

void fhir_to_xml(arena *a, fhirdefs *d, jval *res, buf *out) {
    buf_puts(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    write_res(a, d, res, out, 0, 1);
    buf_putc(out, '\n');
}

/* ---------------- parsing ---------------- */

typedef struct xnode {
    const char   *name;
    const char   *text;       /* raw inner text for xhtml */
    vec           attrs;      /* const char* alternating name,value */
    vec           kids;       /* struct xnode* */
    const char   *raw_inner;  /* verbatim inner XML */
    vec           comments;   /* const char*: XML comments preceding this element */
} xnode;

typedef struct {
    const char *s, *e;
    arena      *a;
    char       *err;
    size_t      errlen;
} xp;

static void xskip(xp *p) { while (p->s < p->e && isspace((unsigned char)*p->s)) p->s++; }

static char *xunesc(arena *a, const char *s, size_t n) {
    buf b; buf_init(&b);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '&') {
            if (!strncmp(s + i, "&amp;", 5)) { buf_putc(&b, '&'); i += 4; continue; }
            if (!strncmp(s + i, "&lt;", 4)) { buf_putc(&b, '<'); i += 3; continue; }
            if (!strncmp(s + i, "&gt;", 4)) { buf_putc(&b, '>'); i += 3; continue; }
            if (!strncmp(s + i, "&quot;", 6)) { buf_putc(&b, '"'); i += 5; continue; }
            if (!strncmp(s + i, "&apos;", 6)) { buf_putc(&b, '\''); i += 5; continue; }
            if (s[i+1] == '#') {
                size_t k = i + 2;
                int base = 10;
                if (s[k] == 'x' || s[k] == 'X') { base = 16; k++; }
                unsigned cp = 0;
                while (k < n && s[k] != ';') { int h = hex_val((unsigned char)s[k]); if (h < 0 || (base == 10 && h > 9)) break; cp = cp * (unsigned)base + (unsigned)h; k++; }
                if (k < n && s[k] == ';') {
                    if (cp < 0x80) buf_putc(&b, (char)cp);
                    else if (cp < 0x800) { buf_putc(&b, (char)(0xC0 | (cp >> 6))); buf_putc(&b, (char)(0x80 | (cp & 0x3F))); }
                    else { buf_putc(&b, (char)(0xE0 | (cp >> 12))); buf_putc(&b, (char)(0x80 | ((cp >> 6) & 0x3F))); buf_putc(&b, (char)(0x80 | (cp & 0x3F))); }
                    i = k;
                    continue;
                }
            }
        }
        buf_putc(&b, s[i]);
    }
    char *out = arena_strndup(a, b.p ? b.p : "", b.n);
    buf_free(&b);
    return out;
}

static xnode *xparse_elem(xp *p) {
    xskip(p);
    if (p->s >= p->e || *p->s != '<') { snprintf(p->err, p->errlen, "expected '<'"); return NULL; }
    p->s++;
    const char *ns = p->s;
    while (p->s < p->e && !isspace((unsigned char)*p->s) && *p->s != '>' && *p->s != '/') p->s++;
    xnode *n = arena_calloc(p->a, sizeof(xnode));
    const char *rawname = arena_strndup(p->a, ns, (size_t)(p->s - ns));
    const char *colon = strchr(rawname, ':');
    n->name = colon ? colon + 1 : rawname;
    vec_init(&n->attrs); vec_init(&n->kids); vec_init(&n->comments);
    vec pending; vec_init(&pending);
    for (;;) {
        xskip(p);
        if (p->s >= p->e) { snprintf(p->err, p->errlen, "unterminated element"); return NULL; }
        if (*p->s == '/') { p->s++; if (p->s < p->e && *p->s == '>') p->s++; return n; }
        if (*p->s == '>') { p->s++; break; }
        const char *as = p->s;
        while (p->s < p->e && *p->s != '=' && !isspace((unsigned char)*p->s) && *p->s != '>') p->s++;
        const char *aname = arena_strndup(p->a, as, (size_t)(p->s - as));
        xskip(p);
        if (p->s < p->e && *p->s == '=') {
            p->s++;
            xskip(p);
            char q = (p->s < p->e) ? *p->s : '"';
            if (q == '"' || q == '\'') {
                p->s++;
                const char *vs = p->s;
                while (p->s < p->e && *p->s != q) p->s++;
                const char *aval = xunesc(p->a, vs, (size_t)(p->s - vs));
                if (p->s < p->e) p->s++;
                vec_push(&n->attrs, (void *)aname);
                vec_push(&n->attrs, (void *)aval);
            }
        }
    }
    const char *inner_start = p->s;
    buf text; buf_init(&text);
    for (;;) {
        if (p->s >= p->e) { snprintf(p->err, p->errlen, "unterminated element '%s'", n->name); buf_free(&text); return NULL; }
        if (*p->s == '<') {
            if (p->s + 1 < p->e && p->s[1] == '/') {
                /* A comment with no element after it belongs to the one before
                   it; that is where the reference implementation puts it and
                   there is nowhere else for it to go. */
                if (pending.n && n->kids.n) {
                    xnode *last = n->kids.v[n->kids.n - 1];
                    for (size_t ci = 0; ci < pending.n; ci++) vec_push(&last->comments, pending.v[ci]);
                    pending.n = 0;
                }
                n->raw_inner = arena_strndup(p->a, inner_start, (size_t)(p->s - inner_start));
                p->s += 2;
                while (p->s < p->e && *p->s != '>') p->s++;
                if (p->s < p->e) p->s++;
                break;
            }
            if (p->s + 3 < p->e && !strncmp(p->s, "<!--", 4)) {
                /* DSTU2 `json.html` keeps XML comments in the JSON form as the
                   `fhir_comments` property of the element they precede. */
                const char *end = strstr(p->s, "-->");
                if (!end) { snprintf(p->err, p->errlen, "unterminated comment"); buf_free(&text); return NULL; }
                vec_push(&pending, (void *)arena_strndup(p->a, p->s + 4, (size_t)(end - (p->s + 4))));
                p->s = end + 3;
                continue;
            }
            xnode *k = xparse_elem(p);
            if (!k) { buf_free(&text); return NULL; }
            for (size_t ci = 0; ci < pending.n; ci++) vec_push(&k->comments, pending.v[ci]);
            pending.n = 0;
            vec_push(&n->kids, k);
            continue;
        }
        buf_putc(&text, *p->s);
        p->s++;
    }
    n->text = arena_strndup(p->a, text.p ? text.p : "", text.n);
    buf_free(&text);
    return n;
}

static const char *xattr(xnode *n, const char *name) {
    for (size_t i = 0; i + 1 < n->attrs.n; i += 2)
        if (str_eq((const char *)n->attrs.v[i], name)) return (const char *)n->attrs.v[i + 1];
    return NULL;
}

static jval *xml_to_json(arena *a, fhirdefs *d, xnode *n, sdef *s, edef *parent, const char *type);

static jval *prim_value(arena *a, const char *type, const char *lex) {
    if (!lex) return NULL;
    if (str_eq(type, "boolean")) return j_bool(a, str_eq(lex, "true"));
    if (str_eq(type, "integer") || str_eq(type, "unsignedInt") || str_eq(type, "positiveInt") || str_eq(type, "decimal"))
        return j_num(a, lex);
    return j_str(a, lex);
}

static void add_member(arena *a, jval *obj, const char *key, jval *v, int repeats) {
    if (!repeats) { j_set(a, obj, key, v); return; }
    jval *cur = j_get(obj, key);
    if (!cur) { cur = j_arr(a); j_set(a, obj, key, cur); }
    j_push(a, cur, v);
}

/* `json.html`: a repeating primitive and its `_`-prefixed shadow are two arrays
   of the SAME length, padded with null wherever one of them has nothing. */
static void add_shadow(arena *a, jval *obj, const char *key, jval *v, int repeats, int index) {
    if (!repeats) { j_set(a, obj, key, v); return; }
    jval *cur = j_get(obj, key);
    if (!cur) { cur = j_arr(a); j_set(a, obj, key, cur); }
    while (j_len(cur) < index) j_push(a, cur, j_null(a));
    j_push(a, cur, v);
}

static jval *xml_to_json(arena *a, fhirdefs *d, xnode *n, sdef *s, edef *parent, const char *type) {
    if (type && str_eq(type, "xhtml"))
        return j_str(a, arena_printf(a, "<div xmlns=\"http://www.w3.org/1999/xhtml\">%s</div>", n->raw_inner ? n->raw_inner : ""));
    if (type && is_prim_type(d, type)) {
        const char *v = xattr(n, "value");
        return prim_value(a, type, v);
    }
    jval *obj = j_obj(a);
    /* DSTU2 `json.html`: XML comments survive into the JSON form as the
       `fhir_comments` property of the element they precede.  Later releases
       dropped the property. */
    int keep_comments = d && str_eq(d->ver, "1.0.2");
    xnode *csrc = n;
    sdef *asd = NULL;
    edef *anchor = NULL;
    if (type && (str_eq(type, "Resource") || defs_resource(d, type))) {
        if (n->kids.n == 0) return obj;
        xnode *inner = n->kids.v[0];
        sdef *rs = defs_resource(d, inner->name);
        if (!rs) return obj;
        j_set(a, obj, "resourceType", j_str(a, inner->name));
        asd = rs; anchor = rs->root;
        n = inner;
        if (inner->comments.n) csrc = inner;
    } else if (parent) {
        if (!ed_children_of(d, s, parent, type, &asd, &anchor)) { asd = s; anchor = NULL; }
    } else {
        asd = type ? defs_sd(d, type) : s;
        anchor = asd ? asd->root : NULL;
    }
    if (keep_comments && csrc->comments.n) {
        jval *cm = j_arr(a);
        for (size_t ci = 0; ci < csrc->comments.n; ci++)
            j_push(a, cm, j_str(a, (const char *)csrc->comments.v[ci]));
        j_set(a, obj, "fhir_comments", cm);
    }
    const char *xid = xattr(n, "id");
    if (xid) j_set(a, obj, "id", j_str(a, xid));
    const char *xurl = xattr(n, "url");
    if (xurl) j_set(a, obj, "url", j_str(a, xurl));
    for (size_t i = 0; i < n->kids.n; i++) {
        xnode *k = n->kids.v[i];
        const char *ctype = NULL;
        edef *ce = anchor ? ed_child(d, asd, anchor, k->name, &ctype) : NULL;
        if (!ctype && ce && ce->types.n >= 1) ctype = (const char *)ce->types.v[0];
        if (!ce) continue;
        int repeats = ce->max && (str_eq(ce->max, "*") || atoi(ce->max) > 1);
        jval *v = xml_to_json(a, d, k, asd, ce, ctype);
        if (!v) continue;
        /* primitive with extensions: split into value and shadow */
        if (ctype && !str_eq(ctype, "xhtml") && is_prim_type(d, ctype) &&
            (k->kids.n || xattr(k, "id") || (keep_comments && k->comments.n))) {
            jval *sh = j_obj(a);
            const char *kid = xattr(k, "id");
            if (kid) j_set(a, sh, "id", j_str(a, kid));
            if (keep_comments && k->comments.n) {
                jval *cm = j_arr(a);
                for (size_t ci = 0; ci < k->comments.n; ci++)
                    j_push(a, cm, j_str(a, (const char *)k->comments.v[ci]));
                j_set(a, sh, "fhir_comments", cm);
            }
            sdef *es = defs_sd(d, "Element");
            for (size_t z = 0; z < k->kids.n; z++) {
                xnode *kk = k->kids.v[z];
                /* A comment inside a primitive element belongs to the primitive,
                   not to the extension that happens to follow it. */
                kk->comments.n = 0;
                edef *ee = es && es->root ? ed_child(d, es, es->root, kk->name, NULL) : NULL;
                jval *ev = xml_to_json(a, d, kk, es, ee, "Extension");
                if (ev) add_member(a, sh, kk->name, ev, 1);
            }
            char shk[256];
            snprintf(shk, sizeof shk, "_%s", k->name);
            jval *valarr = j_get(obj, k->name);
            add_shadow(a, obj, shk, sh, repeats, repeats && valarr ? j_len(valarr) : 0);
        }
        add_member(a, obj, k->name, v, repeats);
    }
    /* `json.html`: where a repeating primitive has a shadow array, the two are
       the same length; pad the tail the way the head is padded. */
    for (jmem *m = obj->u.obj.first; m; m = m->next) {
        if (m->key[0] != '_' || !j_is(m->val, J_ARR)) continue;
        jval *vals = j_get(obj, m->key + 1);
        if (!vals || !j_is(vals, J_ARR)) continue;
        while (j_len(m->val) < j_len(vals)) j_push(a, m->val, j_null(a));
    }
    return obj;
}

jval *fhir_from_xml(arena *a, fhirdefs *d, const char *text, size_t len, char *err, size_t errlen) {
    xp p = { text, text + len, a, err, errlen };
    if (err && errlen) err[0] = 0;
    /* skip the prolog and any comments */
    for (;;) {
        xskip(&p);
        if (p.s + 1 < p.e && p.s[0] == '<' && p.s[1] == '?') {
            const char *end = strstr(p.s, "?>");
            if (!end) { snprintf(err, errlen, "unterminated XML declaration"); return NULL; }
            p.s = end + 2;
            continue;
        }
        if (p.s + 3 < p.e && !strncmp(p.s, "<!--", 4)) {
            const char *end = strstr(p.s, "-->");
            if (!end) { snprintf(err, errlen, "unterminated comment"); return NULL; }
            p.s = end + 3;
            continue;
        }
        break;
    }
    xnode *root = xparse_elem(&p);
    if (!root) return NULL;
    sdef *s = defs_resource(d, root->name);
    if (!s) { snprintf(err, errlen, "unknown resource type '%s'", root->name); return NULL; }
    jval *obj = j_obj(a);
    j_set(a, obj, "resourceType", j_str(a, root->name));
    jval *body = xml_to_json(a, d, root, s, NULL, NULL);
    for (jmem *m = body->u.obj.first; m; m = m->next)
        if (!str_eq(m->key, "resourceType")) j_set(a, obj, m->key, m->val);
    return obj;
}

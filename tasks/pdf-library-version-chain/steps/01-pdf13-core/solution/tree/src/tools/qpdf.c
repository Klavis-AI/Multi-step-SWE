/* qpdf.c -- show a single PDF object, matching qpdf 11.3.0's --show-object.
 *
 *   qpdf --show-object=N[,G] [--filtered-stream-data|--raw-stream-data] FILE
 *
 * Serialization follows qpdf's unparse() methods (libqpdf/QPDF_*.cc):
 *   dict   "<< " + entries + ">>", keys sorted, entries that resolve to null
 *          are omitted entirely (this includes dangling indirect references)
 *   array  "[ " + each element + " " + "]"      (nulls are kept in arrays)
 *   ref    "N G R"          bool "true"/"false"          null "null"
 *   name   "/" + chars, escaping <33, >126 and ()<>[]{}/% as lowercase #xx
 *   string literal "(...)" unless QPDF_String::useHexString() says otherwise,
 *          in which case lowercase "<hex>"
 * Exit status: 0 on success, 2 on error.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf.h"

#define MAX_DEPTH 500

static const char hexdigits[] = "0123456789abcdef";

static void emit_obj(pdf_doc *doc, pdf_obj *o, pdf_buf *b, int depth);

static void usage_error(const char *msg, const char *arg)
{
    fprintf(stderr, "\nqpdf: %s%s\n\n", msg, arg != NULL ? arg : "");
    fprintf(stderr, "For help:\n");
    fprintf(stderr, "  qpdf --help=usage       usage information\n");
    fprintf(stderr, "  qpdf --help=topic       help on a topic\n");
    fprintf(stderr, "  qpdf --help=--option    help on an option\n");
    fprintf(stderr, "  qpdf --help             general help and a topic list\n");
}

/* QPDF_String::useHexString().  Note qpdf iterates over `char`, which is
 * signed on the platforms we care about: bytes >= 128 therefore compare as
 * negative and are counted through the `ch < 0` arm. */
static int str_use_hex(const char *p, size_t n)
{
    size_t i;
    size_t non_ascii = 0;

    for (i = 0; i < n; i++) {
        signed char ch = (signed char)p[i];

        if (ch > 126)
            non_ascii++;
        else if (ch >= 32)
            continue;
        else if (ch < 0 || ch >= 24)
            non_ascii++;
        else if (!(ch == '\n' || ch == '\r' || ch == '\t' || ch == '\b'
                   || ch == '\f'))
            return 1;
    }
    return 5 * non_ascii > n;
}

/* qpdf's is_iso_latin1_printable(). */
static int latin1_printable(char ch)
{
    return ((ch >= 32 && ch <= 126) || (unsigned char)ch >= 160);
}

static void emit_string(const char *p, size_t n, pdf_buf *b)
{
    size_t i;

    if (str_use_hex(p, n)) {
        pdf_buf_putc(b, '<');
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)p[i];
            pdf_buf_putc(b, hexdigits[c >> 4]);
            pdf_buf_putc(b, hexdigits[c & 0x0f]);
        }
        pdf_buf_putc(b, '>');
        return;
    }

    pdf_buf_putc(b, '(');
    for (i = 0; i < n; i++) {
        char ch = p[i];

        switch (ch) {
        case '\n': pdf_buf_put(b, "\\n", 2); break;
        case '\r': pdf_buf_put(b, "\\r", 2); break;
        case '\t': pdf_buf_put(b, "\\t", 2); break;
        case '\b': pdf_buf_put(b, "\\b", 2); break;
        case '\f': pdf_buf_put(b, "\\f", 2); break;
        case '(':  pdf_buf_put(b, "\\(", 2); break;
        case ')':  pdf_buf_put(b, "\\)", 2); break;
        case '\\': pdf_buf_put(b, "\\\\", 2); break;
        default:
            if (latin1_printable(ch))
                pdf_buf_putc(b, ch);
            else
                pdf_buf_printf(b, "\\%03o", (unsigned)(unsigned char)ch);
            break;
        }
    }
    pdf_buf_putc(b, ')');
}

/* QPDF_Name::normalizeName(), applied to the name without its leading '/'. */
static void emit_name(const char *nm, pdf_buf *b)
{
    size_t i;

    pdf_buf_putc(b, '/');
    if (nm == NULL)
        return;
    for (i = 0; nm[i] != '\0'; i++) {
        unsigned char c = (unsigned char)nm[i];

        if (c < 33 || c > 126 || c == '/' || c == '(' || c == ')'
            || c == '{' || c == '}' || c == '<' || c == '>' || c == '['
            || c == ']' || c == '%') {
            pdf_buf_putc(b, '#');
            pdf_buf_putc(b, hexdigits[c >> 4]);
            pdf_buf_putc(b, hexdigits[c & 0x0f]);
        } else {
            pdf_buf_putc(b, (char)c);
        }
    }
}

typedef struct { const char *k; pdf_obj *v; } dict_ent;

/* std::map<std::string,...> ordering: unsigned byte-wise comparison. */
static int dict_ent_cmp(const void *A, const void *B)
{
    const unsigned char *p = (const unsigned char *)((const dict_ent *)A)->k;
    const unsigned char *q = (const unsigned char *)((const dict_ent *)B)->k;

    while (*p != '\0' && *p == *q) {
        p++;
        q++;
    }
    return (int)*p - (int)*q;
}

static void emit_dict(pdf_doc *doc, pdf_obj *d, pdf_buf *b, int depth)
{
    size_t n;
    size_t i;
    size_t m = 0;
    dict_ent *ents;

    pdf_buf_put(b, "<< ", 3);
    n = pdf_dict_len(d);
    if (n > 0 && (ents = malloc(n * sizeof *ents)) != NULL) {
        for (i = 0; i < n; i++) {
            pdf_obj *v = pdf_dict_val_raw(d, i);
            const char *k = pdf_dict_key(d, i);

            /* qpdf checks isNull() on the handle, which resolves indirect
             * references first; a reference to a missing or null object is
             * therefore dropped along with a direct null. */
            if (k == NULL || pdf_is_null(pdf_resolve(doc, v)))
                continue;
            ents[m].k = k;
            ents[m].v = v;
            m++;
        }
        qsort(ents, m, sizeof *ents, dict_ent_cmp);
        for (i = 0; i < m; i++) {
            emit_name(ents[i].k, b);
            pdf_buf_putc(b, ' ');
            emit_obj(doc, ents[i].v, b, depth + 1);
            pdf_buf_putc(b, ' ');
        }
        free(ents);
    }
    pdf_buf_put(b, ">>", 2);
}

static void emit_obj(pdf_doc *doc, pdf_obj *o, pdf_buf *b, int depth)
{
    const char *s;
    size_t slen;
    size_t i;
    size_t n;

    if (o == NULL || depth > MAX_DEPTH) {
        pdf_buf_put(b, "null", 4);
        return;
    }

    switch (o->kind) {
    case PDF_NULL:
        pdf_buf_put(b, "null", 4);
        break;
    case PDF_BOOL:
        if (o->u.b)
            pdf_buf_put(b, "true", 4);
        else
            pdf_buf_put(b, "false", 5);
        break;
    case PDF_INT:
        pdf_buf_printf(b, "%lld", (long long)o->u.i);
        break;
    case PDF_REAL:
        /* QPDF_Real keeps the string it was parsed from and unparse() hands
         * that string straight back, so a real is reproduced exactly as the
         * file spelled it: ".25" stays ".25", "2.50" keeps its trailing zero
         * and "1.0" does not collapse to "1".  Only a real this library built
         * itself has no spelling to reproduce; that one is formatted. */
        if (o->u.real.lit != NULL)
            pdf_buf_put(b, o->u.real.lit, strlen(o->u.real.lit));
        else
            pdf_fmt_real(b, o->u.real.v);
        break;
    case PDF_STRING:
        s = pdf_to_string(o, &slen);
        emit_string(s != NULL ? s : "", s != NULL ? slen : 0, b);
        break;
    case PDF_NAME:
        emit_name(pdf_to_name(o), b);
        break;
    case PDF_ARRAY:
        pdf_buf_put(b, "[ ", 2);
        n = pdf_array_len(o);
        for (i = 0; i < n; i++) {
            emit_obj(doc, pdf_array_get_raw(o, i), b, depth + 1);
            pdf_buf_putc(b, ' ');
        }
        pdf_buf_putc(b, ']');
        break;
    case PDF_DICT:
        emit_dict(doc, o, b, depth);
        break;
    case PDF_STREAM:
        emit_dict(doc, o->u.stm.dict, b, depth);
        break;
    case PDF_REF:
        pdf_buf_printf(b, "%d %d R", o->u.ref.num, o->u.ref.gen);
        break;
    default:
        pdf_buf_put(b, "null", 4);
        break;
    }
}

static void write_buf(const pdf_buf *b)
{
    if (b->n > 0)
        fwrite(b->p, 1, b->n, stdout);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *spec = NULL;
    int filtered = 0;
    int rawdata = 0;
    int argi;
    char *errmsg = NULL;
    pdf_doc *doc;
    pdf_obj *obj;
    pdf_buf out;
    FILE *probe;
    long num = 0;
    long gen = 0;

    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];

        if (strncmp(a, "--show-object=", 14) == 0) {
            spec = a + 14;
        } else if (strcmp(a, "--filtered-stream-data") == 0) {
            filtered = 1;
            rawdata = 0;
        } else if (strcmp(a, "--raw-stream-data") == 0) {
            rawdata = 1;
            filtered = 0;
        } else if (a[0] == '-' && a[1] != '\0') {
            usage_error("unrecognized argument ", a);
            return 2;
        } else if (path == NULL) {
            path = a;
        } else {
            usage_error("unrecognized argument ", a);
            return 2;
        }
    }

    if (path == NULL) {
        usage_error("an input file name is required", NULL);
        return 2;
    }
    if (spec == NULL) {
        usage_error("an output file name is required; use - for standard output",
                    NULL);
        return 2;
    }

    probe = fopen(path, "rb");
    if (probe == NULL) {
        fprintf(stderr, "qpdf: open %s: %s\n", path, strerror(errno));
        return 2;
    }
    fclose(probe);

    doc = pdf_open(path, &errmsg);
    if (doc == NULL) {
        fprintf(stderr, "qpdf: %s: %s\n", path,
                errmsg != NULL ? errmsg : "unable to read the PDF file");
        return 2;
    }
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc)
        && pdf_authenticate(doc, "") < 0) {
        fprintf(stderr, "qpdf: %s: invalid password\n", path);
        pdf_close(doc);
        return 2;
    }

    if (strcmp(spec, "trailer") == 0) {
        obj = pdf_trailer(doc);
    } else {
        char *end;

        errno = 0;
        num = strtol(spec, &end, 10);
        if (end == spec || num <= 0) {
            /* qpdf prints nothing at all for object 0 or an unparsable
             * object id, and still exits 0. */
            pdf_close(doc);
            return 0;
        }
        if (*end == ',') {
            const char *g = end + 1;
            gen = strtol(g, &end, 10);
            if (end == g)
                gen = 0;
        }
        obj = pdf_get_object(doc, (int)num, (int)gen);
    }

    pdf_buf_init(&out);

    if (pdf_is_stream(obj) && (filtered || rawdata)) {
        if (pdf_stream_data(doc, obj, &out, filtered ? 1 : 0) < 0) {
            fprintf(stderr, "qpdf: %s: unable to filter stream data\n", path);
            pdf_buf_free(&out);
            pdf_close(doc);
            return 2;
        }
        write_buf(&out);
    } else if (pdf_is_stream(obj)) {
        printf("Object is stream.  Dictionary:\n");
        emit_dict(doc, obj->u.stm.dict, &out, 0);
        pdf_buf_putc(&out, '\n');
        write_buf(&out);
    } else {
        emit_obj(doc, obj, &out, 0);
        pdf_buf_putc(&out, '\n');
        write_buf(&out);
    }

    pdf_buf_free(&out);
    pdf_close(doc);
    return 0;
}

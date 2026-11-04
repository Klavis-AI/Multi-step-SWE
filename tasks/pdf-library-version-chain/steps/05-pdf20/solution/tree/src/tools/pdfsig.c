/* pdfsig.c -- report the digital signatures of a PDF, matching poppler 22.12.0.
 *
 *   pdfsig FILE
 *
 * ISO 32000-1 clause 12.8.  NOTHING HERE VERIFIES A SIGNATURE.  No digest is
 * computed over any byte range, /Contents is never parsed as a cryptographic
 * object, no certificate or revocation list is consulted, and /ByteRange is
 * reported as written rather than checked against the file.  That is not a
 * simplification: 12.8 makes the structure the readable part, and a reader that
 * claims a verdict it cannot support is wrong in a way that reporting the
 * structure faithfully is not.
 *
 * The lines this prints are the ones poppler prints that say something about
 * the file.  poppler also prints a signer certificate name, a signing time, a
 * hash algorithm and a validation verdict; all four are properties of a
 * certificate database rather than of the document, they are identical for
 * every file when there is no such database, and they are deliberately not
 * produced here.
 *
 * Exit status: 2 when, and only when, the file contains no signature field at
 * all -- however malformed a field that IS there turns out to be.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf.h"

#define SIG_MAX_FIELDS 4096
#define SIG_MAX_DEPTH  64

/* 12.8.2 and table 252.  A /SubFilter poppler has a handler for is echoed; any
 * other name -- including a perfectly legal one a developer registered -- reads
 * back as "unknown".  That is a statement about the tool, not about the file,
 * and 12.8.2 requires the name itself to be read and reported either way. */
static int subfilter_known(const char *nm)
{
    static const char *const known[] = {
        "adbe.pkcs7.detached", "adbe.pkcs7.sha1", "adbe.x509.rsa_sha1",
        "ETSI.CAdES.detached", "ETSI.RFC3161", NULL
    };
    int i;

    for (i = 0; known[i] != NULL; i++)
        if (strcmp(nm, known[i]) == 0)
            return 1;
    return 0;
}

/* 12.7.3.2: the fully qualified name is the /T values from the root down,
 * joined by periods.  A field with no /T contributes nothing, not an empty
 * component. */
static void field_name(pdf_doc *doc, pdf_obj *field, pdf_buf *out)
{
    pdf_obj *chain[SIG_MAX_DEPTH];
    pdf_obj *node = field;
    int n = 0, i;

    while (pdf_is_dict(node) && n < SIG_MAX_DEPTH) {
        chain[n++] = node;
        node = pdf_dict_get(doc, node, "Parent");
    }
    for (i = n - 1; i >= 0; i--) {
        pdf_obj *t = pdf_dict_get(doc, chain[i], "T");
        size_t len = 0;
        const char *s;

        if (!pdf_is_string(t))
            continue;
        s = pdf_to_string(t, &len);
        if (out->n > 0)
            pdf_buf_putc(out, '.');
        pdf_text_string_to_utf8(s, len, out);
    }
}

/* A terminal field is one with no field children.  /Kids may hold widget
 * annotations instead, which are not fields; a kid is a field when it has /T,
 * /FT or /Kids of its own (12.7.3.1). */
static int kid_is_field(pdf_doc *doc, pdf_obj *kid)
{
    if (!pdf_is_dict(kid))
        return 0;
    if (!pdf_is_null(pdf_dict_get(doc, kid, "T")))
        return 1;
    if (!pdf_is_null(pdf_dict_get(doc, kid, "FT")))
        return 1;
    return pdf_is_array(pdf_dict_get(doc, kid, "Kids"));
}

/* /FT is inheritable (table 220), so a signature field may carry it on an
 * ancestor.  Walk up rather than assume. */
static int is_sig_field(pdf_doc *doc, pdf_obj *field)
{
    pdf_obj *node = field;
    int d;

    for (d = 0; d < SIG_MAX_DEPTH && pdf_is_dict(node); d++) {
        pdf_obj *ft = pdf_dict_get(doc, node, "FT");

        if (pdf_is_name(ft, NULL))
            return pdf_is_name(ft, "Sig");
        node = pdf_dict_get(doc, node, "Parent");
    }
    return 0;
}

static void collect(pdf_doc *doc, pdf_obj *arr, pdf_obj **out, int *n, int depth)
{
    size_t i, cnt;

    if (depth > SIG_MAX_DEPTH || !pdf_is_array(arr))
        return;
    for (i = 0, cnt = pdf_array_len(arr); i < cnt && *n < SIG_MAX_FIELDS; i++) {
        pdf_obj *f = pdf_array_get(doc, arr, i);
        pdf_obj *kids;
        int terminal = 1;
        size_t k, kn;

        if (!pdf_is_dict(f))
            continue;
        kids = pdf_dict_get(doc, f, "Kids");
        if (pdf_is_array(kids)) {
            terminal = 0;
            for (k = 0, kn = pdf_array_len(kids); k < kn; k++) {
                if (!kid_is_field(doc, pdf_array_get(doc, kids, k))) {
                    terminal = 1;       /* widgets only: this IS the field */
                    break;
                }
            }
        }
        if (!terminal) {
            collect(doc, kids, out, n, depth + 1);
            continue;
        }
        if (is_sig_field(doc, f))
            out[(*n)++] = f;
    }
}

/* Table 252 /ByteRange holds start-and-length pairs; poppler reports them as
 * start and END offsets, which is the only transformation it applies. */
static void print_ranges(pdf_doc *doc, pdf_obj *br)
{
    size_t i, n;

    n = pdf_array_len(br);
    if (n < 2)
        return;
    printf("  - Signed Ranges:");
    for (i = 0; i + 1 < n; i += 2) {
        long long start = pdf_to_int(pdf_array_get(doc, br, i));
        long long len   = pdf_to_int(pdf_array_get(doc, br, i + 1));

        printf("%s [%lld - %lld]", i ? "," : "", start, start + len);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    char *errmsg = NULL;
    pdf_doc *doc;
    pdf_obj *acro, *fields;
    pdf_obj *sigs[SIG_MAX_FIELDS];
    int nsigs = 0, i, argi;
    FILE *probe;

    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];

        if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "pdfsig version 22.12.0\n");
            fprintf(stderr, "Usage: pdfsig [options] <PDF-file>\n");
            return 99;
        }
        if (path != NULL) {
            fprintf(stderr, "pdfsig version 22.12.0\n");
            fprintf(stderr, "Usage: pdfsig [options] <PDF-file>\n");
            return 99;
        }
        path = a;
    }
    if (path == NULL) {
        fprintf(stderr, "pdfsig version 22.12.0\n");
        fprintf(stderr, "Usage: pdfsig [options] <PDF-file>\n");
        return 99;
    }

    probe = fopen(path, "rb");
    if (probe == NULL) {
        fprintf(stderr, "I/O Error: Couldn't open file '%s': %s.\n", path,
                strerror(errno));
        return 1;
    }
    fclose(probe);

    doc = pdf_open(path, &errmsg);
    if (doc == NULL) {
        fprintf(stderr, "Syntax Error: %s\n",
                errmsg != NULL ? errmsg : "Couldn't read xref table");
        return 1;
    }
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc)
        && pdf_authenticate(doc, "") < 0) {
        fprintf(stderr, "Command Line Error: Incorrect password\n");
        pdf_close(doc);
        return 1;
    }

    acro = pdf_dict_get(doc, pdf_catalog(doc), "AcroForm");
    fields = pdf_dict_get(doc, acro, "Fields");
    collect(doc, fields, sigs, &nsigs, 0);

    /* 12.7.4.5: a signature field with no value is an unsigned field, which is
     * a state to report -- not a missing field and not an error.  Only a file
     * with no signature FIELD at all is "no signatures". */
    if (nsigs == 0) {
        printf("File '%s' does not contain any signatures\n", path);
        pdf_close(doc);
        return 2;
    }

    printf("Digital Signature Info of: %s\n", path);
    for (i = 0; i < nsigs; i++) {
        pdf_obj *v = pdf_dict_get(doc, sigs[i], "V");
        pdf_obj *sub, *br;
        pdf_buf nm;

        printf("Signature #%d:\n", i + 1);

        pdf_buf_init(&nm);
        field_name(doc, sigs[i], &nm);
        printf("  - Signature Field Name: %.*s\n", (int)nm.n,
               nm.n ? (const char *)nm.p : "");
        pdf_buf_free(&nm);

        if (!pdf_is_dict(v)) {
            printf("  The signature form field is not signed.\n");
            continue;
        }

        sub = pdf_dict_get(doc, v, "SubFilter");
        printf("  - Signature Type: %s\n",
               (pdf_is_name(sub, NULL) && subfilter_known(pdf_to_name(sub)))
                   ? pdf_to_name(sub) : "unknown");

        br = pdf_dict_get(doc, v, "ByteRange");
        if (pdf_is_array(br))
            print_ranges(doc, br);
    }

    pdf_close(doc);
    return 0;
}

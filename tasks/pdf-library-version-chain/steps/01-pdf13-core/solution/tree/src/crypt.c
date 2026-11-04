/* crypt.c -- the /Encrypt dictionary.
 *
 * This milestone reads unencrypted files. An encrypted document is recognised and
 * described accurately -- the revision, the version, the permission bits and the
 * security handler's algorithm are parsed so that a report can say what the file is --
 * but its strings and streams are NOT decrypted, and pdf_crypt_authenticate refuses.
 * Reporting a file as unencrypted, or silently returning ciphertext as though it were
 * plaintext, would both be lies; refusing is the honest behaviour.
 */
#include "pdfint.h"

struct pdf_crypt {
    int v, r;
    long long p;
    char method[16];    /* "RC4", "AES", "AES-256", or "None" */
    int length_bits;
};

static void detect_method(pdf_doc *doc, struct pdf_crypt *c, pdf_obj *enc)
{
    pdf_obj *cf, *stmf, *entry, *cfm;
    const char *name;

    /* V 1 and 2 are RC4 with no crypt filters. */
    if (c->v <= 2) {
        snprintf(c->method, sizeof c->method, "RC4");
        return;
    }
    /* V 4 and 5 select an algorithm through /CF and the /StmF name (7.6.5). */
    snprintf(c->method, sizeof c->method, "RC4");
    cf = pdf_dict_get(doc, enc, "CF");
    stmf = pdf_dict_get(doc, enc, "StmF");
    name = pdf_is_name(stmf, NULL) ? pdf_to_name(stmf) : "StdCF";
    if (pdf_is_dict(cf)) {
        entry = pdf_dict_get(doc, cf, name);
        if (pdf_is_dict(entry)) {
            cfm = pdf_dict_get(doc, entry, "CFM");
            if (pdf_is_name(cfm, "AESV2"))
                snprintf(c->method, sizeof c->method, "AES");
            else if (pdf_is_name(cfm, "AESV3"))
                snprintf(c->method, sizeof c->method, "AES-256");
            else if (pdf_is_name(cfm, "None"))
                snprintf(c->method, sizeof c->method, "None");
        }
    }
    if (c->v == 5 && strcmp(c->method, "AES-256") != 0)
        snprintf(c->method, sizeof c->method, "AES-256");
}

int pdf_crypt_init(pdf_doc *doc, pdf_obj *encrypt, pdf_obj *id0)
{
    struct pdf_crypt *c;
    pdf_obj *o;

    (void)id0;
    if (!doc || !pdf_is_dict(encrypt)) return 0;

    c = pdf_calloc(doc, 1, sizeof *c);
    if (!c) return PDF_E_NOMEM;

    o = pdf_dict_get(doc, encrypt, "V");
    c->v = pdf_is_num(o) ? (int)pdf_to_int(o) : 0;
    o = pdf_dict_get(doc, encrypt, "R");
    c->r = pdf_is_num(o) ? (int)pdf_to_int(o) : 0;
    o = pdf_dict_get(doc, encrypt, "P");
    c->p = pdf_is_num(o) ? pdf_to_int(o) : 0;
    o = pdf_dict_get(doc, encrypt, "Length");
    c->length_bits = pdf_is_num(o) ? (int)pdf_to_int(o) : 40;
    detect_method(doc, c, encrypt);

    doc->crypt = c;
    doc->encrypted = 1;
    doc->unlocked = 0;
    return PDF_E_UNSUP;
}

int pdf_crypt_authenticate(pdf_doc *doc, const char *pw)
{
    (void)pw;
    if (!doc || !doc->crypt) return 0;     /* nothing to unlock */
    return PDF_E_UNSUP;
}

int pdf_crypt_data(pdf_doc *doc, int num, int gen, int is_string,
                   const unsigned char *src, size_t srclen, pdf_buf *out)
{
    (void)num; (void)gen; (void)is_string;
    if (!out) return PDF_E_RANGE;
    /* No handler, or an unencrypted document: the bytes are already plaintext. */
    if (!doc || !doc->crypt)
        return pdf_buf_put(out, src, srclen);
    return PDF_E_ENCRYPT;
}

int pdf_crypt_revision(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->r : 0;
}

int pdf_crypt_version(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->v : 0;
}

long long pdf_crypt_perms(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->p : 0;
}

const char *pdf_crypt_method(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->method : "None";
}

/* gs.c -- Ghostscript's pdfwrite PDF/A conversion invocation.
 *
 *   gs -dPDFA=N -dPDFACompatibilityPolicy=1 -dBATCH -dNOPAUSE \
 *      -sDEVICE=pdfwrite -sOutputFile=OUTFILE INFILE
 *
 * The interface is Ghostscript's, adopted as-is.  pdfwrite produces the `b`
 * conformance level of the part it is given, so -dPDFA=1 targets PDF/A-1b,
 * -dPDFA=2 targets PDF/A-2b and -dPDFA=3 targets PDF/A-3b; there is no
 * -dPDFA=4, because conversion to PDF/A-4 and to PDF/UA is not part of this
 * interface.  Exactly one INFILE is given and OUTFILE is always supplied.
 *
 * The four flags -dPDFACompatibilityPolicy=1, -dBATCH, -dNOPAUSE and
 * -sDEVICE=pdfwrite are passed on every scored invocation and never vary, so
 * they are accepted and do not select behaviour.
 *
 * What is produced is not a metadata edit.  The output is judged by an
 * INDEPENDENT conformance checker, so nothing here can excuse itself: device
 * colour is given a meaning by an embedded output intent, the constructs the
 * profile prohibits are removed, and the XMP identification is rewritten to say
 * what the file now is.  The bytes are not compared with anything -- two
 * correct converters agree on no byte -- but the page count and the extracted
 * text shall survive the trip, and a structure hierarchy that was there shall
 * still be there.
 *
 * Exit status is 0 when the output file was written and non-zero when it was
 * not.  A file that cannot be parsed is not converted: no output file and a
 * non-zero status, rather than a truncated result claiming to be archival.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pdfaint.h"


/* ------------------------------------------------------------ ICC profile --- */

/* Defined below, ahead of its first use; declared here as well so the order of
 * the definitions in this file can never silently become load-bearing. */
static pdf_obj *gs_add_object(pdf_doc *doc, pdf_obj *o);



/* A minimal but structurally valid ICC v2 display profile for a three-channel
 * RGB device, synthesised rather than carried as data.  The output intent's
 * destination profile is EMBEDDED, not referenced: a /DestOutputProfile naming
 * an external profile does not satisfy the clause, so the bytes have to come
 * from somewhere and this is where.  Tag set: desc, wtpt, rXYZ, gXYZ, bXYZ,
 * rTRC, gTRC, bTRC, cprt -- the required set for an RGB matrix/TRC profile.
 */
static void put_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static void put_be16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v;
}

/* s15Fixed16Number */
static unsigned long s15(double v)
{
    return (unsigned long)(long)(v * 65536.0 + (v < 0 ? -0.5 : 0.5));
}

static int icc_build(pdf_buf *out)
{
    /* Tag payloads, laid out after the 128-byte header and the tag table. */
    struct { const char *sig; unsigned char *data; unsigned long len; } tags[9];
    unsigned char xyz[9][20], trc[14], desc[90], cprt[40];
    unsigned char hdr[128], *tbl;
    unsigned long ntags = 9, tblsize, off, total, i;
    static const double prim[3][3] = {
        { 0.4360, 0.2225, 0.0139 },   /* rXYZ */
        { 0.3851, 0.7169, 0.0971 },   /* gXYZ */
        { 0.1431, 0.0606, 0.7141 }    /* bXYZ */
    };
    static const double wp[3] = { 0.9642, 1.0000, 0.8249 };  /* D50 */

    /* XYZType payloads: 'XYZ ' + reserved + three s15Fixed16. */
    for (i = 0; i < 4; i++) {
        memcpy(xyz[i], "XYZ ", 4);
        memset(xyz[i] + 4, 0, 4);
    }
    for (i = 0; i < 3; i++) put_be32(xyz[0] + 8 + 4 * i, s15(wp[i]));
    for (i = 0; i < 3; i++) {
        int k;

        for (k = 0; k < 3; k++)
            put_be32(xyz[1 + i] + 8 + 4 * k, s15(prim[i][k]));
    }

    /* curveType with a single gamma entry of 2.2 in u8Fixed8. */
    memcpy(trc, "curv", 4);
    memset(trc + 4, 0, 4);
    put_be32(trc + 8, 1);
    put_be16(trc + 12, 0x0233);

    /* textDescriptionType: ASCII count, the string, then the zeroed Unicode
     * and ScriptCode fields the type requires. */
    {
        const char *name = "Generic RGB";
        unsigned long alen = (unsigned long)strlen(name) + 1;

        memset(desc, 0, sizeof desc);
        memcpy(desc, "desc", 4);
        put_be32(desc + 8, alen);
        memcpy(desc + 12, name, alen);
        /* 12 + alen .. leaves the Unicode/ScriptCode block zero */
    }
    {
        const char *c = "No copyright";

        memset(cprt, 0, sizeof cprt);
        memcpy(cprt, "text", 4);
        memcpy(cprt + 8, c, strlen(c) + 1);
    }

    tags[0].sig = "desc"; tags[0].data = desc;    tags[0].len = 12 + 12 + 67;
    tags[1].sig = "wtpt"; tags[1].data = xyz[0];  tags[1].len = 20;
    tags[2].sig = "rXYZ"; tags[2].data = xyz[1];  tags[2].len = 20;
    tags[3].sig = "gXYZ"; tags[3].data = xyz[2];  tags[3].len = 20;
    tags[4].sig = "bXYZ"; tags[4].data = xyz[3];  tags[4].len = 20;
    tags[5].sig = "rTRC"; tags[5].data = trc;     tags[5].len = 14;
    tags[6].sig = "gTRC"; tags[6].data = trc;     tags[6].len = 14;
    tags[7].sig = "bTRC"; tags[7].data = trc;     tags[7].len = 14;
    tags[8].sig = "cprt"; tags[8].data = cprt;    tags[8].len = 8 + 13;

    tblsize = 4 + ntags * 12;
    off = 128 + tblsize;
    total = off;
    for (i = 0; i < ntags; i++) total += (tags[i].len + 3) & ~3UL;

    memset(hdr, 0, sizeof hdr);
    put_be32(hdr + 0, total);
    memcpy(hdr + 4, "none", 4);          /* preferred CMM     */
    put_be32(hdr + 8, 0x02100000);       /* version 2.1       */
    memcpy(hdr + 12, "mntr", 4);         /* device class      */
    memcpy(hdr + 16, "RGB ", 4);         /* data colour space */
    memcpy(hdr + 20, "XYZ ", 4);         /* PCS               */
    put_be16(hdr + 24, 2024); hdr[27] = 1; hdr[29] = 1;   /* date/time */
    memcpy(hdr + 36, "acsp", 4);
    put_be32(hdr + 64, 0);               /* rendering intent: perceptual */
    for (i = 0; i < 3; i++) put_be32(hdr + 68 + 4 * i, s15(wp[i]));

    if (pdf_buf_put(out, hdr, sizeof hdr) != 0) return -1;

    tbl = calloc(tblsize, 1);
    if (!tbl) return -1;
    put_be32(tbl, ntags);
    {
        unsigned long cur = off;

        for (i = 0; i < ntags; i++) {
            memcpy(tbl + 4 + i * 12, tags[i].sig, 4);
            put_be32(tbl + 4 + i * 12 + 4, cur);
            put_be32(tbl + 4 + i * 12 + 8, tags[i].len);
            cur += (tags[i].len + 3) & ~3UL;
        }
    }
    if (pdf_buf_put(out, tbl, tblsize) != 0) { free(tbl); return -1; }
    free(tbl);

    for (i = 0; i < ntags; i++) {
        unsigned long pad = ((tags[i].len + 3) & ~3UL) - tags[i].len;

        if (pdf_buf_put(out, tags[i].data, tags[i].len) != 0) return -1;
        while (pad--) pdf_buf_putc(out, 0);
    }
    return 0;
}

/* ------------------------------------------------------------------- XMP --- */

/* The identification the produced file carries.  SOURCE_DATE_EPOCH, when set,
 * replaces the wall clock, so the dates a conversion writes are the scorer's
 * instant rather than the moment it ran. */
static void iso_now(char *buf, size_t n)
{
    const char *sde = getenv("SOURCE_DATE_EPOCH");
    time_t t;
    struct tm tmv;

    if (sde && *sde) t = (time_t)strtoll(sde, NULL, 10);
    else t = time(NULL);
    if (!gmtime_r(&t, &tmv)) { snprintf(buf, n, "1970-01-01T00:00:00Z"); return; }
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static void xmp_escape(pdf_buf *b, const char *s)
{
    for (; s && *s; s++) {
        switch (*s) {
        case '&':  pdf_buf_printf(b, "&amp;");  break;
        case '<':  pdf_buf_printf(b, "&lt;");   break;
        case '>':  pdf_buf_printf(b, "&gt;");   break;
        case '"':  pdf_buf_printf(b, "&quot;"); break;
        case '\'': pdf_buf_printf(b, "&apos;"); break;
        default:
            if ((unsigned char)*s < 0x20) pdf_buf_putc(b, ' ');
            else pdf_buf_putc(b, *s);
        }
    }
}

static int build_xmp(pdf_doc *doc, int part, pdf_buf *out)
{
    char now[64];
    pdf_obj *info = pdf_dict_get(doc, pdf_trailer(doc), "Info");
    const char *producer = "pdfa-convert";

    iso_now(now, sizeof now);
    pdf_buf_printf(out,
        "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "  <rdf:Description rdf:about=\"\""
        " xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\""
        " pdfaid:part=\"%d\" pdfaid:conformance=\"B\"/>\n", part);
    pdf_buf_printf(out,
        "  <rdf:Description rdf:about=\"\""
        " xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\""
        " xmp:CreateDate=\"%s\" xmp:ModifyDate=\"%s\"/>\n", now, now);
    pdf_buf_printf(out,
        "  <rdf:Description rdf:about=\"\""
        " xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\" pdf:Producer=\"");
    xmp_escape(out, producer);
    pdf_buf_printf(out, "\"/>\n");
    pdf_buf_printf(out,
        " </rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n");

    /* The document information dictionary shall not contradict the packet, so
     * the two are written from the same values rather than left to drift. */
    if (pdf_is_dict(info)) {
        pdf_dict_del(info, "Title");
        pdf_dict_del(info, "Author");
        pdf_dict_del(info, "Subject");
        pdf_dict_del(info, "Keywords");
        pdf_dict_del(info, "Creator");
        /* 6.7.3 t1/t7/t8: where the information dictionary and the packet both
         * carry a property the two shall be EQUAL.  The dates and the producer
         * are written from the same values as the packet rather than left to
         * disagree with it: the reference checker rejects a mismatch on exactly
         * those three properties. */
        pdf_dict_del(info, "CreationDate");
        pdf_dict_del(info, "ModDate");
        pdf_dict_put(doc, info, "Producer",
                     pdf_new_string(doc, producer, strlen(producer)));
    }
    return 0;
}

/* ------------------------------------------------------- document surgery --- */

/* Remove what the profile prohibits outright.  The prohibition is on presence,
 * so this walks every object the table defines rather than the reachable graph
 * alone -- an action dictionary nothing refers to still condemns the file. */
static void strip_prohibited(pdf_doc *doc)
{
    int n, max = pdf_max_objnum(doc);
    pdf_obj *cat = pdf_catalog(doc);
    pdf_obj *names, *acro;

    for (n = 1; n <= max; n++) {
        int type = 0, gen = 0;
        long long off = 0;
        pdf_obj *o;

        pdf_xref_entry(doc, n, &type, &off, &gen);
        o = pdf_get_object(doc, n, gen);
        if (!pdf_is_dict(o)) continue;
        pdf_dict_del(o, "AA");
        {
            pdf_obj *a = pdf_dict_get(doc, o, "A");

            if (pdf_is_dict(a)) {
                pdf_obj *s = pdf_dict_get(doc, a, "S");

                if (pdf_is_name(s, NULL)) {
                    const char *t = pdf_to_name(s);

                    if (!strcmp(t, "JavaScript") || !strcmp(t, "Launch") ||
                        !strcmp(t, "Movie") || !strcmp(t, "Sound") ||
                        !strcmp(t, "ResetForm") || !strcmp(t, "ImportData") ||
                        !strcmp(t, "SetOCGState"))
                        pdf_dict_del(o, "A");
                }
            }
        }
    }

    pdf_dict_del(cat, "AA");
    pdf_dict_del(cat, "OpenAction");
    names = pdf_dict_get(doc, cat, "Names");
    if (pdf_is_dict(names)) pdf_dict_del(names, "JavaScript");

    acro = pdf_dict_get(doc, cat, "AcroForm");
    if (pdf_is_dict(acro)) {
        pdf_dict_del(acro, "XFA");
        pdf_dict_put(doc, acro, "NeedAppearances", pdf_new_bool(doc, 0));
    }
}

/* The annotation flags the profile requires: Print set, Hidden, NoView and
 * Invisible clear, so that what is archived is what is seen. */
static void fix_annots(pdf_doc *doc)
{
    int np = pdf_page_count(doc), pi;

    for (pi = 0; pi < np; pi++) {
        pdf_obj *page = pdf_page_dict(doc, pi);
        pdf_obj *annots = pdf_dict_get(doc, page, "Annots");
        size_t i, n;

        if (!pdf_is_array(annots)) continue;
        n = pdf_array_len(annots);
        for (i = 0; i < n; i++) {
            pdf_obj *a = pdf_array_get(doc, annots, i);
            long long f;

            if (!pdf_is_dict(a)) continue;
            f = pdf_to_int(pdf_dict_get(doc, a, "F"));
            f = (f | 4) & ~(long long)(1 | 2 | 32);
            pdf_dict_put(doc, a, "F", pdf_new_int(doc, f));
        }
    }
}

/* Register an object this run built as a real INDIRECT object of the document.
 *
 * pdf_write_document() emits object 1..pdf_max_objnum() by walking the
 * cross-reference table, so a stream reachable only as a DIRECT value of some
 * dictionary is serialised as its dictionary alone -- no `stream`/`endstream`,
 * no body.  An XMP packet attached directly to the catalogue is written that
 * way, and the reference checker then reports "the document catalog dictionary
 * doesn't contain metadata key or metadata stream dictionary does not contain
 * either Type or Subtype" for the file.
 * A synthesised stream therefore has to enter the table, exactly as the
 * writer's own transformations do for the appearances they build.
 *
 * Returns a reference to the new object, or a null object on failure. */
static pdf_obj *gs_add_object(pdf_doc *doc, pdf_obj *o)
{
    int num = pdf_max_objnum(doc) + 1;

    if (pdf_xref_ensure(doc, num) < 0 || num >= doc->xrefcap)
        return pdf_null_obj();
    doc->xref[num].type   = PDF_XREF_NORMAL;
    doc->xref[num].gen    = 0;
    doc->xref[num].off    = 0;
    doc->xref[num].cached = o;
    if (num > doc->maxobj) doc->maxobj = num;
    return pdf_new_ref(doc, num, 0);
}

/* Give device colour a defined meaning by supplying an output intent whose
 * destination profile is embedded. */
static int add_output_intent(pdf_doc *doc)
{
    pdf_obj *cat = pdf_catalog(doc);
    pdf_obj *ois = pdf_dict_get(doc, cat, "OutputIntents");
    pdf_obj *oi, *prof;
    pdf_buf icc;
    size_t i, n;

    if (pdf_is_array(ois)) {
        n = pdf_array_len(ois);
        for (i = 0; i < n; i++) {
            pdf_obj *e = pdf_array_get(doc, ois, i);

            if (pdf_is_dict(e) &&
                pdf_is_name(pdf_dict_get(doc, e, "S"), "GTS_PDFA1") &&
                pdf_is_stream(pdf_dict_get(doc, e, "DestOutputProfile")))
                return 0;                    /* already has a usable one */
        }
    }

    pdf_buf_init(&icc);
    if (icc_build(&icc) != 0) { pdf_buf_free(&icc); return -1; }

    prof = pdf_new_dict(doc);
    prof->kind = PDF_STREAM;
    prof->u.stm.dict = pdf_new_dict(doc);
    prof->u.stm.off = -1;
    prof->u.stm.len = (long long)icc.n;
    prof->u.stm.mem = malloc(icc.n);
    if (!prof->u.stm.mem) { pdf_buf_free(&icc); return -1; }
    memcpy(prof->u.stm.mem, icc.p, icc.n);
    prof->u.stm.memlen = icc.n;
    pdf_dict_put(doc, prof->u.stm.dict, "N", pdf_new_int(doc, 3));
    pdf_dict_put(doc, prof->u.stm.dict, "Length",
                 pdf_new_int(doc, (long long)icc.n));
    pdf_buf_free(&icc);

    oi = pdf_new_dict(doc);
    pdf_dict_put(doc, oi, "Type", pdf_new_name(doc, "OutputIntent"));
    pdf_dict_put(doc, oi, "S", pdf_new_name(doc, "GTS_PDFA1"));
    pdf_dict_put(doc, oi, "OutputConditionIdentifier",
                 pdf_new_string(doc, "Custom", 6));
    pdf_dict_put(doc, oi, "Info", pdf_new_string(doc, "Generic RGB", 11));
    pdf_dict_put(doc, oi, "DestOutputProfile", gs_add_object(doc, prof));

    ois = pdf_new_array(doc);
    pdf_array_push(doc, ois, oi);
    pdf_dict_put(doc, cat, "OutputIntents", ois);
    return 0;
}

static int attach_metadata(pdf_doc *doc, int part)
{
    pdf_obj *cat = pdf_catalog(doc);
    pdf_obj *md;
    pdf_buf xmp;

    pdf_buf_init(&xmp);
    if (build_xmp(doc, part, &xmp) != 0) { pdf_buf_free(&xmp); return -1; }

    md = pdf_new_dict(doc);
    md->kind = PDF_STREAM;
    md->u.stm.dict = pdf_new_dict(doc);
    md->u.stm.off = -1;
    md->u.stm.len = (long long)xmp.n;
    md->u.stm.mem = malloc(xmp.n);
    if (!md->u.stm.mem) { pdf_buf_free(&xmp); return -1; }
    memcpy(md->u.stm.mem, xmp.p, xmp.n);
    md->u.stm.memlen = xmp.n;
    /* 6.7.2: no /Filter at all, so that the packet is recoverable from the
     * bytes of the file directly. */
    pdf_dict_put(doc, md->u.stm.dict, "Type", pdf_new_name(doc, "Metadata"));
    pdf_dict_put(doc, md->u.stm.dict, "Subtype", pdf_new_name(doc, "XML"));
    pdf_dict_put(doc, md->u.stm.dict, "Length",
                 pdf_new_int(doc, (long long)xmp.n));
    pdf_buf_free(&xmp);

    pdf_dict_put(doc, cat, "Metadata", gs_add_object(doc, md));
    return 0;
}


static void usage(void)
{
    fprintf(stderr,
            "usage: gs -dPDFA=N -dPDFACompatibilityPolicy=1 -dBATCH -dNOPAUSE"
            " -sDEVICE=pdfwrite -sOutputFile=OUT IN\n");
}

int main(int argc, char **argv)
{
    const char *outfile = NULL, *infile = NULL;
    int part = 0, i, rc;
    pdf_doc *doc;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!strncmp(a, "-dPDFA=", 7)) {
            part = atoi(a + 7);
        } else if (!strncmp(a, "-sOutputFile=", 13)) {
            outfile = a + 13;
        } else if (a[0] == '-') {
            /* -dPDFACompatibilityPolicy=1, -dBATCH, -dNOPAUSE and
             * -sDEVICE=pdfwrite are passed on every scored invocation and
             * never vary, so they select nothing and are accepted here. */
        } else {
            infile = a;
        }
    }

    if (!outfile || !infile) { usage(); return 1; }
    if (part != 1 && part != 2
       ) {
        fprintf(stderr, "gs: unsupported -dPDFA=%d\n", part);
        return 1;
    }

    /* A file that cannot be parsed is not converted: no output file, and a
     * non-zero status, rather than a truncated result claiming to be
     * archival. */
    if (!pdfa_parsable(infile)) {
        fprintf(stderr, "gs: %s could not be parsed\n", infile);
        return 1;
    }
    doc = pdf_open(infile, NULL);
    if (!doc) {
        fprintf(stderr, "gs: %s could not be opened\n", infile);
        return 1;
    }

    strip_prohibited(doc);
    fix_annots(doc);
    if (add_output_intent(doc) != 0) { pdf_close(doc); return 1; }
    if (attach_metadata(doc, part) != 0) { pdf_close(doc); return 1; }

    /* The produced file's header has to match the part being targeted: the
     * writer emits "%PDF-x.y" from the document's own header version, and
     * ISO 19005 parts 1 to 3 require %PDF-1.n.  The reference refuses a
     * conversion that carries an input's 2.0 header through, with "File header
     * %PDF-2.0 ... does not match the pattern %PDF-1.n". */
    doc->hdrver = (part == 1) ? 14 : 17;
    if (doc->ver < doc->hdrver) doc->ver = doc->hdrver;

    rc = pdf_write_document(doc, outfile, PDF_WRITE_DECRYPT, NULL);
    pdf_close(doc);
    if (rc != 0) {
        fprintf(stderr, "gs: could not write %s\n", outfile);
        return 1;
    }
    return 0;
}

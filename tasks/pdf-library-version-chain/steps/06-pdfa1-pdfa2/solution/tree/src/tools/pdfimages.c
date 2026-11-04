/* pdfimages.c -- list or extract the images of a PDF, matching poppler 22.12.0.
 *
 *   pdfimages -list [-f N] [-l N] [-opw PW] [-upw PW] FILE
 *   pdfimages [-jbig2] [-f N] [-l N] [-opw PW] [-upw PW] FILE IMAGE-ROOT
 *
 * -list reports what each image IS without decoding it, so a corrupt payload
 * and a good one produce the same row.  Without -list the images are written
 * out: netpbm rasters by default, and with -jbig2 the JBIG2 codestream as the
 * file stores it.
 *
 * Exit status is 0 whenever the document could be read, INCLUDING when a codec
 * fails completely: a failed decode produces a file on disk and a diagnostic on
 * standard error, never a non-zero status.  Exit status is not a decode-success
 * signal anywhere in this program.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf.h"

static int first_page = 1, last_page = 0;
static int do_list = 0, want_jbig2 = 0;
static const char *owner_pw = "";
static const char *user_pw = "";

static void usage(void)
{
    fprintf(stderr, "pdfimages version 22.12.0\n");
    fprintf(stderr, "Usage: pdfimages [options] <PDF-file> <image-root>\n");
    fprintf(stderr, "  -f <int>      : first page to convert\n");
    fprintf(stderr, "  -l <int>      : last page to convert\n");
    fprintf(stderr, "  -list         : print image metadata\n");
    fprintf(stderr, "  -jbig2        : write JBIG2 images as JBIG2 files\n");
    fprintf(stderr, "  -opw <string> : owner password (for encrypted files)\n");
    fprintf(stderr, "  -upw <string> : user password (for encrypted files)\n");
}

static int parse_int(const char *s, int *out)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0)
        return -1;
    *out = (int)v;
    return 0;
}

static pdf_doc *open_doc(const char *path)
{
    char *errmsg = NULL;
    pdf_doc *doc;
    FILE *probe = fopen(path, "rb");

    if (probe == NULL) {
        fprintf(stderr, "I/O Error: Couldn't open file '%s': %s.\n", path,
                strerror(errno));
        exit(1);
    }
    fclose(probe);
    doc = pdf_open(path, &errmsg);
    if (doc == NULL) {
        fprintf(stderr, "Syntax Error: %s\n",
                errmsg != NULL ? errmsg : "Couldn't read xref table");
        exit(1);
    }
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc)) {
        if (*owner_pw) (void)pdf_authenticate(doc, owner_pw);
        if (!pdf_is_unlocked(doc) && *user_pw)
            (void)pdf_authenticate(doc, user_pw);
    }
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc)
        && pdf_authenticate(doc, "") < 0) {
        fprintf(stderr, "Command Line Error: Incorrect password\n");
        exit(1);
    }
    return doc;
}

/* ---------------------------------------------------------------- the list */

static void fmt_size(long long n, char *out, size_t outn)
{
    if (n < 0)
        snprintf(out, outn, "-");
    else if (n < 1024)
        snprintf(out, outn, "%lldB", n);
    else if (n < 1024 * 1024)
        snprintf(out, outn, "%.1fK", (double)n / 1024.0);
    else
        snprintf(out, outn, "%.1fM", (double)n / (1024.0 * 1024.0));
}

/* The ratio of stored bytes to the bytes the samples would occupy -- which is
 * why it exceeds 100% for a payload that grew, as a small JBIG2 page does. */
static void fmt_ratio(const pdf_imageinfo *r, char *out, size_t outn)
{
    double raw = (double)r->width * (double)r->height
               * (double)(r->comps > 0 ? r->comps : 1)
               * (double)(r->bpc > 0 ? r->bpc : 8) / 8.0;
    double pct;

    if (r->size < 0 || raw < 1.0) {
        snprintf(out, outn, "-");
        return;
    }
    pct = 100.0 * (double)r->size / raw;
    if (pct > 9.95)
        snprintf(out, outn, "%.0f%%", pct);
    else
        snprintf(out, outn, "%.1f%%", pct);
}

static void print_list(const pdf_imageinfo *v, size_t n)
{
    size_t i;

    printf("page   num  type   width height color comp bpc  enc interp  "
           "object ID x-ppi y-ppi size ratio\n");
    printf("------------------------------------------------------------"
           "--------------------------------\n");
    for (i = 0; i < n; i++) {
        const pdf_imageinfo *r = &v[i];
        char sz[32], ratio[32];

        fmt_size(r->size, sz, sizeof sz);
        fmt_ratio(r, ratio, sizeof ratio);
        printf("%4d %5d %-5s %5d %5d  %-4s %4d %4d  %-5s %-3s %6d %2d %5.0f "
               "%5.0f %5s %5s\n",
               r->page, r->num, r->type, r->width, r->height, r->color,
               r->comps, r->bpc, r->enc, r->interp ? "yes" : "no",
               r->objnum, r->objgen, r->xppi, r->yppi, sz, ratio);
    }
}

/* ------------------------------------------------------------- writing out */

static int write_file(const char *path, const unsigned char *p, size_t n)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        fprintf(stderr, "I/O Error: Couldn't open '%s': %s\n", path,
                strerror(errno));
        return -1;
    }
    if (n > 0)
        fwrite(p, 1, n, f);
    fclose(f);
    return 0;
}

/* 7.4.7.  The globals are a SEPARATE stream and the reference writes them to a
 * separate file: an implementation that concatenates them, omits them, or
 * writes them into the page file produces the wrong bytes under the wrong
 * name.  /DecodeParms runs parallel to /Filter, so the entry carrying
 * /JBIG2Globals is the one belonging to the JBIG2Decode stage. */
static pdf_obj *jbig2_globals(pdf_doc *doc, pdf_obj *stm)
{
    pdf_obj *filter = pdf_dict_geta(doc, stm, "Filter", "F");
    pdf_obj *parms  = pdf_dict_geta(doc, stm, "DecodeParms", "DP");
    size_t i, n;

    if (pdf_is_dict(parms) && !pdf_is_array(parms))
        return pdf_dict_get(doc, parms, "JBIG2Globals");
    if (!pdf_is_array(parms))
        return pdf_null_obj();

    n = pdf_array_len(filter);
    for (i = 0; i < n; i++) {
        if (!pdf_is_name(pdf_array_get(doc, filter, i), "JBIG2Decode"))
            continue;
        return pdf_dict_get(doc, pdf_array_get(doc, parms, i), "JBIG2Globals");
    }
    for (i = 0, n = pdf_array_len(parms); i < n; i++) {
        pdf_obj *g = pdf_dict_get(doc, pdf_array_get(doc, parms, i),
                                  "JBIG2Globals");
        if (pdf_is_stream(g))
            return g;
    }
    return pdf_null_obj();
}

/* Plain netpbm: the header, then the samples, with no compression and no
 * encoder-dependent choice anywhere -- which is what makes byte equality a
 * fair comparison between two implementations that both decoded correctly. */
static void write_netpbm(const char *root, int idx, const pdf_imageinfo *r,
                         const unsigned char *samples, size_t nsamples)
{
    char path[4096];
    pdf_buf out;
    int w = r->width, h = r->height;
    int comps = r->comps > 0 ? r->comps : 1;
    int bpc = r->bpc > 0 ? r->bpc : 8;
    const char *ext;
    size_t need, y, x;

    if (w <= 0 || h <= 0)
        return;

    pdf_buf_init(&out);
    if (bpc == 1 && comps == 1) {
        ext = "pbm";
        pdf_buf_printf(&out, "P4\n%d %d\n", w, h);
        need = (size_t)((w + 7) / 8) * (size_t)h;
        for (y = 0; y < (size_t)h; y++) {
            for (x = 0; x < (size_t)((w + 7) / 8); x++) {
                size_t o = y * (size_t)((w + 7) / 8) + x;
                /* In DeviceGray a 0 sample is black; in PBM a 1 BIT is black,
                 * so the sample byte is inverted.  An /ImageMask already uses
                 * 1 for the painted (black) sample and is not inverted. */
                unsigned char b = (o < nsamples) ? samples[o] : 0x00;

                pdf_buf_putc(&out, r->is_mask ? b : (unsigned char)~b);
            }
        }
        (void)need;
    } else if (comps == 1) {
        ext = "pgm";
        pdf_buf_printf(&out, "P5\n%d %d\n255\n", w, h);
        need = (size_t)w * (size_t)h;
        for (y = 0; y < need; y++)
            pdf_buf_putc(&out, y < nsamples ? samples[y] : 0xff);
    } else {
        ext = "ppm";
        pdf_buf_printf(&out, "P6\n%d %d\n255\n", w, h);
        need = (size_t)w * (size_t)h * 3u;
        if (comps == 3 && bpc == 8) {
            for (y = 0; y < need; y++)
                pdf_buf_putc(&out, y < nsamples ? samples[y] : 0xff);
        } else {
            /* Anything else is written as white rather than as a guess: the
             * file must exist and be the right shape, and inventing samples
             * would be a wrong answer rather than a missing one. */
            for (y = 0; y < need; y++)
                pdf_buf_putc(&out, 0xff);
        }
    }

    snprintf(path, sizeof path, "%s-%03d.%s", root, idx, ext);
    write_file(path, out.p, out.n);
    pdf_buf_free(&out);
}

static void extract(pdf_doc *doc, const pdf_imageinfo *r, const char *root)
{
    pdf_obj *stm = pdf_get_object(doc, r->objnum, r->objgen);
    char path[4096];
    pdf_buf raw, dec;

    if (!pdf_is_stream(stm))
        return;

    pdf_buf_init(&raw);
    (void)pdf_stream_data(doc, stm, &raw, 0);

    if (want_jbig2 && strcmp(r->enc, "jbig2") == 0) {
        pdf_obj *g = jbig2_globals(doc, stm);

        snprintf(path, sizeof path, "%s-%03d.jb2e", root, r->num);
        write_file(path, raw.p, raw.n);
        if (pdf_is_stream(g)) {
            pdf_buf gb;

            pdf_buf_init(&gb);
            if (pdf_stream_data(doc, g, &gb, 0) == 0) {
                snprintf(path, sizeof path, "%s-%03d.jb2g", root, r->num);
                write_file(path, gb.p, gb.n);
            }
            pdf_buf_free(&gb);
        }
        pdf_buf_free(&raw);
        return;
    }

    /* The default raster.  A stream whose chain this library refuses (a codec
     * it does not apply) yields nothing, and the image is written white --
     * exactly the "file on disk, diagnostic on stderr, status 0" the interface
     * requires of a failed decode. */
    pdf_buf_init(&dec);
    if (pdf_stream_data(doc, stm, &dec, 1) < 0) {
        fprintf(stderr, "Syntax Error: Could not decode image %d\n", r->num);
        dec.n = 0;
    }
    write_netpbm(root, r->num, r, dec.p, dec.n);
    pdf_buf_free(&dec);
    pdf_buf_free(&raw);
}

int main(int argc, char **argv)
{
    const char *path = NULL, *root = NULL;
    pdf_doc *doc;
    pdf_imageinfo *v = NULL;
    size_t n = 0, i;
    int npages, argi;

    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];

        if (strcmp(a, "-f") == 0 && argi + 1 < argc) {
            if (parse_int(argv[++argi], &first_page) < 0) { usage(); return 99; }
        } else if (strcmp(a, "-l") == 0 && argi + 1 < argc) {
            if (parse_int(argv[++argi], &last_page) < 0) { usage(); return 99; }
        } else if (strcmp(a, "-list") == 0) {
            do_list = 1;
        } else if (strcmp(a, "-jbig2") == 0) {
            want_jbig2 = 1;
        } else if (strcmp(a, "-opw") == 0 && argi + 1 < argc) {
            owner_pw = argv[++argi];
        } else if (strcmp(a, "-upw") == 0 && argi + 1 < argc) {
            user_pw = argv[++argi];
        } else if (strcmp(a, "-p") == 0 || strcmp(a, "-q") == 0
                   || strcmp(a, "-all") == 0 || strcmp(a, "-png") == 0
                   || strcmp(a, "-tiff") == 0 || strcmp(a, "-j") == 0
                   || strcmp(a, "-jp2") == 0 || strcmp(a, "-ccitt") == 0) {
            /* Part of the published interface; nothing here is scored through
             * them, and each still selects the default raster below. */
            continue;
        } else if (a[0] == '-' && a[1] != '\0') {
            usage();
            return 99;
        } else if (path == NULL) {
            path = a;
        } else if (root == NULL) {
            root = a;
        } else {
            usage();
            return 99;
        }
    }
    if (path == NULL || (!do_list && root == NULL)) {
        usage();
        return 99;
    }

    doc = open_doc(path);
    npages = pdf_page_count(doc);

    if (first_page < 1)
        first_page = 1;
    if (last_page < 1 || last_page > npages)
        last_page = npages;
    if (last_page < first_page) {
        fprintf(stderr, "Command Line Error: Wrong page range given: the "
                "first page (%d) can not be after the last page (%d).\n",
                first_page, last_page);
        pdf_close(doc);
        return 99;
    }

    if (pdf_list_images(doc, first_page, last_page, &v, &n) < 0) {
        v = NULL;
        n = 0;
    }

    if (do_list) {
        print_list(v, n);
    } else {
        for (i = 0; i < n; i++)
            extract(doc, &v[i], root);
    }

    free(v);
    pdf_close(doc);
    return 0;
}

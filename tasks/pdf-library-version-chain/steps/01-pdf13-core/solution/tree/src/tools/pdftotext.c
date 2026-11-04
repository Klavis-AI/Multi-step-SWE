/* pdftotext.c -- extract the text of a PDF.
 *
 * Reproduces poppler 22.12.0's pdftotext(1) for the option subset
 *   pdftotext [-f N] [-l N] [-raw] [-enc ENC] [-nopgbrk] FILE [OUTFILE|-]
 *
 * Output file naming follows poppler exactly (utils/pdftotext.cc): with no
 * OUTFILE a trailing ".pdf" or ".PDF" is stripped from the input path and
 * ".txt" appended, so the result lands next to the input, not in the cwd.
 * A form feed is written after every page unless -nopgbrk is given -- poppler
 * emits one per page including the last, so a one page document ends in \f.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf.h"

static int first_page = 1;
static int last_page = 0;
static int raw_order = 0;
static int no_page_breaks = 0;
static const char *text_enc = "UTF-8";

static void usage(void)
{
    fprintf(stderr, "pdftotext version 22.12.0\n");
    fprintf(stderr, "Usage: pdftotext [options] <PDF-file> [<text-file>]\n");
    fprintf(stderr, "  -f <int>  : first page to convert\n");
    fprintf(stderr, "  -l <int>  : last page to convert\n");
    fprintf(stderr, "  -raw      : keep strings in content stream order\n");
    fprintf(stderr, "  -enc <string> : output text encoding name\n");
    fprintf(stderr, "  -nopgbrk  : don't insert page breaks between pages\n");
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
    FILE *probe;

    probe = fopen(path, "rb");
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
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc)
        && pdf_authenticate(doc, "") < 0) {
        fprintf(stderr, "Command Line Error: Incorrect password\n");
        exit(1);
    }
    return doc;
}

/* poppler: strip a trailing ".pdf"/".PDF" then append ".txt". */
static char *make_text_name(const char *path)
{
    size_t n = strlen(path);
    size_t keep = n;
    char *out;

    if (n >= 4 && (strcmp(path + n - 4, ".pdf") == 0
                   || strcmp(path + n - 4, ".PDF") == 0))
        keep = n - 4;

    out = malloc(keep + 5);
    if (out == NULL)
        return NULL;
    memcpy(out, path, keep);
    memcpy(out + keep, ".txt", 5);
    return out;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *outarg = NULL;
    char *outname = NULL;
    pdf_doc *doc;
    FILE *f;
    int npages;
    int argi;
    int pg;

    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];

        if (strcmp(a, "-f") == 0 && argi + 1 < argc) {
            if (parse_int(argv[++argi], &first_page) < 0) {
                usage();
                return 99;
            }
        } else if (strcmp(a, "-l") == 0 && argi + 1 < argc) {
            if (parse_int(argv[++argi], &last_page) < 0) {
                usage();
                return 99;
            }
        } else if (strcmp(a, "-raw") == 0) {
            raw_order = 1;
        } else if (strcmp(a, "-nopgbrk") == 0) {
            no_page_breaks = 1;
        } else if (strcmp(a, "-enc") == 0 && argi + 1 < argc) {
            text_enc = argv[++argi];
        } else if (a[0] == '-' && a[1] != '\0' && strcmp(a, "-") != 0) {
            usage();
            return 99;
        } else if (path == NULL) {
            path = a;
        } else if (outarg == NULL) {
            outarg = a;
        } else {
            usage();
            return 99;
        }
    }
    if (path == NULL) {
        usage();
        return 99;
    }
    (void)text_enc;   /* only UTF-8 output is implemented */

    doc = open_doc(path);
    npages = pdf_page_count(doc);

    if (outarg != NULL) {
        outname = malloc(strlen(outarg) + 1);
        if (outname != NULL)
            strcpy(outname, outarg);
    } else {
        outname = make_text_name(path);
    }
    if (outname == NULL) {
        fprintf(stderr, "I/O Error: Couldn't allocate output file name\n");
        pdf_close(doc);
        return 2;
    }

    /* Page range: clamp, then reject an inverted range (poppler exits 99). */
    if (first_page < 1)
        first_page = 1;
    if (last_page < 1 || last_page > npages)
        last_page = npages;
    if (last_page < first_page) {
        fprintf(stderr,
                "Command Line Error: Wrong page range given: the first page "
                "(%d) can not be after the last page (%d).\n",
                first_page, last_page);
        free(outname);
        pdf_close(doc);
        return 99;
    }

    if (strcmp(outname, "-") == 0) {
        f = stdout;
    } else if ((f = fopen(outname, "wb")) == NULL) {
        fprintf(stderr, "I/O Error: Couldn't open text file '%s'\n", outname);
        free(outname);
        pdf_close(doc);
        return 2;
    }

    for (pg = first_page; pg <= last_page; pg++) {
        pdf_obj *page = pdf_page_dict(doc, pg - 1);
        pdf_glyphs glyphs;
        pdf_buf text;

        memset(&glyphs, 0, sizeof glyphs);
        pdf_buf_init(&text);

        if (pdf_extract_glyphs(doc, page, &glyphs) >= 0)
            pdf_glyphs_to_text(&glyphs, raw_order, &text);
        if (text.n > 0)
            fwrite(text.p, 1, text.n, f);

        pdf_buf_free(&text);
        pdf_glyphs_free(&glyphs);

        if (!no_page_breaks)
            fputc('\f', f);
    }

    if (f != stdout)
        fclose(f);
    free(outname);
    pdf_close(doc);
    return 0;
}

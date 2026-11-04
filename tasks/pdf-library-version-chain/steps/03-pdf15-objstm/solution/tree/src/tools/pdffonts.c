/* pdffonts.c -- list the fonts used by a PDF.
 *
 * Reproduces the output of poppler 22.12.0's pdffonts(1).  The column layout
 * comes straight from poppler's utils/pdffonts.cc:
 *
 *   printf("name                                 type              "
 *          "encoding         emb sub uni object ID\n");
 *   printf("------------------------------------ ----------------- "
 *          "---------------- --- --- --- ---------\n");
 *   printf("%-36s %-17s %-16s %-3s %-3s %-3s", ...);
 *   gen >= 100000 ? printf(" [none]\n") : printf(" %6d %2d\n", num, gen);
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf.h"

static int first_page = 1;
static int last_page = 0;

static void usage(void)
{
    fprintf(stderr, "pdffonts version 22.12.0\n");
    fprintf(stderr, "Usage: pdffonts [options] <PDF-file>\n");
    fprintf(stderr, "  -f <int> : first page to examine\n");
    fprintf(stderr, "  -l <int> : last page to examine\n");
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

/* Open the document the way poppler's tools do: an unopenable file is an I/O
 * error (exit 1); a file we cannot parse is a syntax error (exit 1); an
 * encrypted file we cannot unlock with the empty password is a command line
 * error (exit 1). */
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

int main(int argc, char **argv)
{
    const char *path = NULL;
    pdf_doc *doc;
    pdf_fontinfo *fonts = NULL;
    size_t nfonts = 0;
    size_t i;
    int npages;
    int argi;

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
        } else if (a[0] == '-' && a[1] != '\0') {
            usage();
            return 99;
        } else if (path == NULL) {
            path = a;
        } else {
            usage();
            return 99;
        }
    }
    if (path == NULL) {
        usage();
        return 99;
    }

    doc = open_doc(path);
    npages = pdf_page_count(doc);

    /* Page range clamping, exactly as poppler does it. */
    if (first_page < 1)
        first_page = 1;
    if (last_page < 1 || last_page > npages)
        last_page = npages;
    if (last_page < first_page) {
        fprintf(stderr,
                "Command Line Error: Wrong page range given: the first page "
                "(%d) can not be after the last page (%d).\n",
                first_page, last_page);
        pdf_close(doc);
        return 99;
    }

    if (pdf_list_fonts(doc, first_page, last_page, &fonts, &nfonts) < 0) {
        fonts = NULL;
        nfonts = 0;
    }

    /* The header is printed even when the document has no fonts at all. */
    printf("name                                 type              "
           "encoding         emb sub uni object ID\n");
    printf("------------------------------------ ----------------- "
           "---------------- --- --- --- ---------\n");

    for (i = 0; i < nfonts; i++) {
        const pdf_fontinfo *f = &fonts[i];

        printf("%-36s %-17s %-16s %-3s %-3s %-3s",
               f->name[0] != '\0' ? f->name : "[none]",
               f->type, f->encoding,
               f->emb ? "yes" : "no",
               f->sub ? "yes" : "no",
               f->uni ? "yes" : "no");
        if (f->objgen >= 100000)
            printf(" [none]\n");
        else
            printf(" %6d %2d\n", f->objnum, f->objgen);
    }

    pdf_close(doc);
    return 0;
}

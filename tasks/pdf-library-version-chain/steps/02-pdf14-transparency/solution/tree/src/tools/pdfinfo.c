/* pdfinfo.c -- report the metadata of a PDF, matching poppler 22.12.0.
 *
 *   pdfinfo [-f N] [-l N] [-box] [-enc ENC] FILE
 *
 * Labels are 17 chars wide so values start in column 18.  Field order and the
 * lines omitted when a value is absent follow utils/pdfinfo.cc.  Paper names:
 *   letter  when |w-612|<1 && |h-792|<1, either orientation
 *   A0..A6  computed, not tabulated: hISO = sqrt(sqrt(2))*7200/2.54,
 *           wISO = hISO/sqrt(2), threshold = hISO*0.003, and all three are
 *           divided by sqrt(2) on each of the seven iterations.
 * The "Page %4d size:" form is selected by -l alone: multiPage = (lastPage!=0).
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf.h"

static int first_page = 1, last_page = 0, print_boxes = 0;
static const char *text_enc = "UTF-8";

static const char *const wday_name[7] = { "Sun", "Mon", "Tue", "Wed", "Thu",
                                          "Fri", "Sat" };
static const char *const mon_name[12] = { "Jan", "Feb", "Mar", "Apr", "May",
                                          "Jun", "Jul", "Aug", "Sep", "Oct",
                                          "Nov", "Dec" };

static void usage(void)
{
    fprintf(stderr, "pdfinfo version 22.12.0\n"
            "Usage: pdfinfo [options] <PDF-file>\n"
            "  -f <int>      : first page to convert\n"
            "  -l <int>      : last page to convert\n"
            "  -box          : print the page bounding boxes\n"
            "  -enc <string> : output text encoding name\n");
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
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc)
        && pdf_authenticate(doc, "") < 0) {
        fprintf(stderr, "Command Line Error: Incorrect password\n");
        exit(1);
    }
    return doc;
}

/* ------------------------------------------------------------ text output */

static void print_text_string(const pdf_obj *o)
{
    size_t n;
    const char *s = pdf_to_string(o, &n);
    pdf_buf b;

    if (s == NULL)
        return;
    pdf_buf_init(&b);
    if (pdf_text_string_to_utf8(s, n, &b) >= 0 && b.n > 0)
        fwrite(b.p, 1, b.n, stdout);
    pdf_buf_free(&b);
}

static void print_info_string(pdf_doc *doc, pdf_obj *info, const char *key,
                              const char *label)
{
    pdf_obj *o = pdf_dict_get(doc, info, key);

    if (!pdf_is_string(o))
        return;
    fputs(label, stdout);
    print_text_string(o);
    fputc('\n', stdout);
}

/* ------------------------------------------------------------ date output */

static long long days_from_civil(int y, int m, int d)
{
    long long era;
    unsigned yoe, doy, doe;

    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

static void civil_from_days(long long z, int *y, int *m, int *d)
{
    long long era, yy;
    unsigned doe, yoe, doy, mp, dd, mm;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yy = (long long)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    dd = doy - (153 * mp + 2) / 5 + 1;
    mm = mp + (mp < 10 ? 3 : (unsigned)-9);
    *y = (int)(yy + (mm <= 2));
    *m = (int)mm;
    *d = (int)dd;
}

/* Read up to `max` digits; poppler is equally forgiving about a truncated
 * field, so D:2016021516192 yields seconds == 2. */
static int read_digits(const char **p, int max)
{
    int v = 0, i;

    for (i = 0; i < max && (*p)[0] >= '0' && (*p)[0] <= '9'; i++)
        v = v * 10 + *(*p)++ - '0';
    return i == 0 ? -1 : v;
}

static int parse_pdf_date(const char *s, int *Y, int *Mo, int *D, int *H,
                          int *Mi, int *S, long *offsec)
{
    int v;

    if (s[0] == 'D' && s[1] == ':')
        s += 2;
    v = read_digits(&s, 4);
    if (v < 0)
        return -1;
    *Y = v;
    *Mo = 1; *D = 1; *H = 0; *Mi = 0; *S = 0; *offsec = 0;
    if ((v = read_digits(&s, 2)) >= 0) *Mo = v;
    if ((v = read_digits(&s, 2)) >= 0) *D = v;
    if ((v = read_digits(&s, 2)) >= 0) *H = v;
    if ((v = read_digits(&s, 2)) >= 0) *Mi = v;
    if ((v = read_digits(&s, 2)) >= 0) *S = v;
    if (*s == '+' || *s == '-') {
        int sign = (*s == '-') ? -1 : 1, th, tm = 0;

        s++;
        th = read_digits(&s, 2);
        if (th < 0)
            th = 0;
        if (*s == '\'')
            s++;
        if ((v = read_digits(&s, 2)) >= 0)
            tm = v;
        *offsec = sign * (long)(th * 3600 + tm * 60);
    }
    if (*Mo < 1 || *Mo > 12 || *D < 1 || *D > 31)
        return -1;
    return 0;
}

static void print_info_date(pdf_doc *doc, pdf_obj *info, const char *key,
                            const char *label)
{
    pdf_obj *o = pdf_dict_get(doc, info, key);
    const char *s;
    size_t n;
    char tmp[128];
    int Y, Mo, D, H, Mi, S;
    long off;

    if (!pdf_is_string(o))
        return;
    fputs(label, stdout);
    s = pdf_to_string(o, &n);
    if (s != NULL && n < sizeof tmp) {
        memcpy(tmp, s, n);
        tmp[n] = '\0';
        if (parse_pdf_date(tmp, &Y, &Mo, &D, &H, &Mi, &S, &off) == 0) {
            long long days = days_from_civil(Y, Mo, D);
            long long secs = days * 86400LL + H * 3600LL + Mi * 60LL + S - off;
            long long d2 = secs / 86400, rem = secs % 86400;
            int wd;

            if (rem < 0) {
                rem += 86400;
                d2--;
            }
            civil_from_days(d2, &Y, &Mo, &D);
            wd = (int)(((d2 % 7) + 11) % 7);   /* 1970-01-01 was a Thursday */
            printf("%s %s %2d %02d:%02d:%02d %d UTC\n", wday_name[wd],
                   mon_name[Mo - 1], D, (int)(rem / 3600),
                   (int)(rem / 60 % 60), (int)(rem % 60), Y);
            return;
        }
    }
    print_text_string(o);
    fputc('\n', stdout);
}

/* -------------------------------------------------------------- page bits */

static void get_box(pdf_doc *doc, pdf_obj *page, const char *name, pdf_rect *r)
{
    if (pdf_page_box(doc, page, name, r) < 0)
        r->x0 = r->y0 = r->x1 = r->y1 = 0;
}

static void print_box(const char *label, const pdf_rect *r)
{
    printf("%s%8.2f %8.2f %8.2f %8.2f\n", label, r->x0, r->y0, r->x1, r->y1);
}

static void print_paper_name(double w, double h)
{
    double wISO, hISO, thr;
    int i;

    if ((fabs(w - 612) < 1 && fabs(h - 792) < 1)
        || (fabs(w - 792) < 1 && fabs(h - 612) < 1)) {
        printf(" (letter)");
        return;
    }
    hISO = sqrt(sqrt(2.0)) * 7200 / 2.54;
    wISO = hISO / sqrt(2.0);
    thr = hISO * 0.003;
    for (i = 0; i <= 6; i++) {
        if ((fabs(w - wISO) < thr && fabs(h - hISO) < thr)
            || (fabs(w - hISO) < thr && fabs(h - wISO) < thr)) {
            printf(" (A%d)", i);
            return;
        }
        hISO = wISO;
        wISO /= sqrt(2.0);
        thr /= sqrt(2.0);
    }
}

/* -------------------------------------------------------------- javascript */

static int is_js_action(pdf_doc *doc, pdf_obj *act, int depth)
{
    pdf_obj *nx;
    size_t i, n;

    if (depth > 16 || !pdf_is_dict(act))
        return 0;
    if (pdf_is_name(pdf_dict_get(doc, act, "S"), "JavaScript"))
        return 1;
    nx = pdf_dict_get(doc, act, "Next");
    if (!pdf_is_array(nx))
        return is_js_action(doc, nx, depth + 1);
    for (i = 0, n = pdf_array_len(nx); i < n; i++)
        if (is_js_action(doc, pdf_array_get(doc, nx, i), depth + 1))
            return 1;
    return 0;
}

/* The /A action plus every value of an /AA additional-actions dictionary. */
static int aa_has_js(pdf_doc *doc, pdf_obj *holder)
{
    pdf_obj *aa = pdf_dict_get(doc, holder, "AA");
    size_t i, n;

    if (is_js_action(doc, pdf_dict_get(doc, holder, "A"), 0))
        return 1;
    if (!pdf_is_dict(aa))
        return 0;
    for (i = 0, n = pdf_dict_len(aa); i < n; i++)
        if (is_js_action(doc, pdf_resolve(doc, pdf_dict_val_raw(aa, i)), 0))
            return 1;
    return 0;
}

static int name_tree_nonempty(pdf_doc *doc, pdf_obj *node, int depth)
{
    pdf_obj *names, *kids;
    size_t i, n;

    if (depth > 32 || !pdf_is_dict(node))
        return 0;
    names = pdf_dict_get(doc, node, "Names");
    if (pdf_is_array(names) && pdf_array_len(names) > 0)
        return 1;
    kids = pdf_dict_get(doc, node, "Kids");
    if (!pdf_is_array(kids))
        return 0;
    for (i = 0, n = pdf_array_len(kids); i < n; i++)
        if (name_tree_nonempty(doc, pdf_array_get(doc, kids, i), depth + 1))
            return 1;
    return 0;
}

static int fields_have_js(pdf_doc *doc, pdf_obj *fields, int depth)
{
    size_t i, n;

    if (depth > 32 || !pdf_is_array(fields))
        return 0;
    for (i = 0, n = pdf_array_len(fields); i < n; i++) {
        pdf_obj *f = pdf_array_get(doc, fields, i);

        if (pdf_is_dict(f) && (aa_has_js(doc, f)
            || fields_have_js(doc, pdf_dict_get(doc, f, "Kids"), depth + 1)))
            return 1;
    }
    return 0;
}

/* Mirrors JSInfo::scan(): name tree, document actions, AcroForm widgets and
 * the Link/Screen/Widget annotations of the selected pages. */
static int doc_has_js(pdf_doc *doc, pdf_obj *cat, int fpage, int lpage)
{
    pdf_obj *acro;
    int pg;

    if (name_tree_nonempty(doc, pdf_dict_geta(doc, cat, "Names", "JavaScript"), 0)
        || aa_has_js(doc, cat))
        return 1;
    acro = pdf_dict_get(doc, cat, "AcroForm");
    if (pdf_is_dict(acro)
        && fields_have_js(doc, pdf_dict_get(doc, acro, "Fields"), 0))
        return 1;

    for (pg = fpage; pg <= lpage; pg++) {
        pdf_obj *page = pdf_page_dict(doc, pg - 1), *annots;
        size_t i, n;

        if (!pdf_is_dict(page))
            continue;
        if (aa_has_js(doc, page))
            return 1;
        annots = pdf_dict_get(doc, page, "Annots");
        if (!pdf_is_array(annots))
            continue;
        for (i = 0, n = pdf_array_len(annots); i < n; i++) {
            pdf_obj *a = pdf_array_get(doc, annots, i), *st;

            if (!pdf_is_dict(a))
                continue;
            st = pdf_dict_get(doc, a, "Subtype");
            if ((pdf_is_name(st, "Link") || pdf_is_name(st, "Screen")
                 || pdf_is_name(st, "Widget")) && aa_has_js(doc, a))
                return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------- report */

static int mark_flag(pdf_doc *doc, pdf_obj *cat, const char *key)
{
    pdf_obj *mi = pdf_dict_get(doc, cat, "MarkInfo"), *v;

    if (!pdf_is_dict(mi))
        return 0;
    v = pdf_dict_get(doc, mi, key);
    return v->kind == PDF_BOOL && v->u.b;
}

/* poppler's Catalog::getNumPages(): the page count printed is the /Count of
 * the page-tree root, not the number of /Page objects the tree actually
 * reaches.  A count that is absent, non-numeric, zero or larger than the xref
 * reads as zero pages -- except that a root which is itself a /Page counts as
 * one (poppler bug 39923). */
static int catalog_num_pages(pdf_doc *doc, pdf_obj *cat)
{
    pdf_obj *pages, *cnt;
    long long n;

    if (!pdf_is_dict(cat))
        return 0;
    pages = pdf_dict_get(doc, cat, "Pages");
    if (!pdf_is_dict(pages))
        return 0;
    cnt = pdf_dict_get(doc, pages, "Count");
    if (!pdf_is_num(cnt))
        return pdf_is_name(pdf_dict_get(doc, pages, "Type"), "Page") ? 1 : 0;

    /* "/Count 9.0" happens; poppler truncates a real to an int. */
    n = (long long)pdf_to_real(cnt);
    if (n <= 0)
        return 0;
    if (n > (long long)pdf_max_objnum(doc) + 1)
        return 0;
    return (int)n;
}

/* poppler calls a file linearized when the linearization dictionary's /L
 * equals the file size.  We have no "first object in the file" accessor, so
 * we look for the (unique) object carrying /Linearized. */
static int is_linearized(pdf_doc *doc)
{
    long long size = pdf_file_size(doc);
    int maxnum = pdf_max_objnum(doc), n;

    for (n = 1; n <= maxnum; n++) {
        pdf_obj *o = pdf_get_object(doc, n, 0);

        if (pdf_is_dict(o) && !pdf_is_null(pdf_dict_get(doc, o, "Linearized"))
            && pdf_to_int(pdf_dict_get(doc, o, "L")) == size)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const char *const std_keys[9] = { "Title", "Author", "Subject",
        "Keywords", "Creator", "Producer", "CreationDate", "ModDate",
        "Trapped" };
    static const char *const bn[5] = { "MediaBox", "CropBox", "BleedBox",
                                       "TrimBox", "ArtBox" };
    static const char *const single[5] = { "MediaBox:        ",
        "CropBox:         ", "BleedBox:        ", "TrimBox:         ",
        "ArtBox:          " };
    static const char *const multi[5] = { "Page %4d MediaBox:  ",
        "Page %4d CropBox:   ", "Page %4d BleedBox:  ",
        "Page %4d TrimBox:   ", "Page %4d ArtBox:    " };
    const char *path = NULL;
    pdf_doc *doc;
    pdf_obj *info, *cat, *acro, *xfa;
    int npages, argi, pg, k, multi_page, has_custom = 0;
    size_t i, j;

    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];

        if (strcmp(a, "-f") == 0 && argi + 1 < argc) {
            if (parse_int(argv[++argi], &first_page) < 0) { usage(); return 99; }
        } else if (strcmp(a, "-l") == 0 && argi + 1 < argc) {
            if (parse_int(argv[++argi], &last_page) < 0) { usage(); return 99; }
        } else if (strcmp(a, "-box") == 0) {
            print_boxes = 1;
        } else if (strcmp(a, "-enc") == 0 && argi + 1 < argc) {
            text_enc = argv[++argi];
        } else if ((a[0] == '-' && a[1] != '\0') || path != NULL) {
            usage();
            return 99;
        } else {
            path = a;
        }
    }
    if (path == NULL) {
        usage();
        return 99;
    }
    (void)text_enc;   /* only UTF-8 output is implemented */

    doc = open_doc(path);
    cat = pdf_catalog(doc);
    npages = catalog_num_pages(doc, cat);
    info = pdf_dict_get(doc, pdf_trailer(doc), "Info");

    if (first_page < 1)
        first_page = 1;
    multi_page = (last_page != 0);         /* poppler: driven by -l alone */
    if (last_page < 1 || last_page > npages)
        last_page = npages;
    if (last_page < first_page) {
        fprintf(stderr, "Command Line Error: Wrong page range given: the "
                "first page (%d) can not be after the last page (%d).\n",
                first_page, last_page);
        pdf_close(doc);
        return 99;
    }
    if (!multi_page)
        last_page = 1;

    if (pdf_is_dict(info)) {
        print_info_string(doc, info, "Title", "Title:           ");
        print_info_string(doc, info, "Subject", "Subject:         ");
        print_info_string(doc, info, "Keywords", "Keywords:        ");
        print_info_string(doc, info, "Author", "Author:          ");
        print_info_string(doc, info, "Creator", "Creator:         ");
        print_info_string(doc, info, "Producer", "Producer:        ");
        print_info_date(doc, info, "CreationDate", "CreationDate:    ");
        print_info_date(doc, info, "ModDate", "ModDate:         ");
        for (i = 0; i < pdf_dict_len(info) && !has_custom; i++) {
            const char *key = pdf_dict_key(info, i);

            if (key == NULL)
                continue;
            for (j = 0; j < 9 && strcmp(key, std_keys[j]) != 0; j++)
                ;
            has_custom = (j == 9);
        }
    }

    printf("Custom Metadata: %s\n", has_custom ? "yes" : "no");
    printf("Metadata Stream: %s\n",
           pdf_is_stream(pdf_dict_get(doc, cat, "Metadata")) ? "yes" : "no");
    printf("Tagged:          %s\n", mark_flag(doc, cat, "Marked") ? "yes" : "no");
    printf("UserProperties:  %s\n",
           mark_flag(doc, cat, "UserProperties") ? "yes" : "no");
    printf("Suspects:        %s\n", mark_flag(doc, cat, "Suspects") ? "yes" : "no");

    acro = pdf_dict_get(doc, cat, "AcroForm");
    if (!pdf_is_dict(acro)) {
        printf("Form:            none\n");
    } else {
        xfa = pdf_dict_get(doc, acro, "XFA");
        printf("Form:            %s\n",
               (pdf_is_stream(xfa) || pdf_is_array(xfa)) ? "XFA" : "AcroForm");
    }

    printf("JavaScript:      %s\n",
           doc_has_js(doc, cat, first_page, last_page) ? "yes" : "no");
    printf("Pages:           %d\n", npages);

    printf("Encrypted:       ");
    if (pdf_is_encrypted(doc)) {
        long long p = pdf_crypt_perms(doc);
        const char *alg = pdf_crypt_method(doc);

        printf("yes (print:%s copy:%s change:%s addNotes:%s algorithm:%s)\n",
               (p & 4) ? "yes" : "no", (p & 16) ? "yes" : "no",
               (p & 8) ? "yes" : "no", (p & 32) ? "yes" : "no",
               alg != NULL ? alg : "unknown");
    } else {
        printf("no\n");
    }

    for (pg = first_page; pg <= last_page; pg++) {
        pdf_obj *page = pdf_page_dict(doc, pg - 1);
        pdf_rect r;
        double w, h;

        get_box(doc, page, "CropBox", &r);
        w = r.x1 - r.x0;
        h = r.y1 - r.y0;
        if (multi_page)
            printf("Page %4d size:  %g x %g pts", pg, w, h);
        else
            printf("Page size:       %g x %g pts", w, h);
        print_paper_name(w, h);
        printf("\n");
        if (multi_page)
            printf("Page %4d rot:   %d\n", pg, pdf_page_rotate(doc, page));
        else
            printf("Page rot:        %d\n", pdf_page_rotate(doc, page));
    }

    if (print_boxes) {
        char buf[256];
        pdf_rect r;

        if (multi_page) {
            for (pg = first_page; pg <= last_page; pg++) {
                pdf_obj *page = pdf_page_dict(doc, pg - 1);

                for (k = 0; k < 5; k++) {
                    snprintf(buf, sizeof buf, multi[k], pg);
                    get_box(doc, page, bn[k], &r);
                    print_box(buf, &r);
                }
            }
        } else {
            pdf_obj *page = pdf_page_dict(doc, first_page - 1);

            for (k = 0; k < 5; k++) {
                get_box(doc, page, bn[k], &r);
                print_box(single[k], &r);
            }
        }
    }

    printf("File size:       %lld bytes\n", (long long)pdf_file_size(doc));
    printf("Optimized:       %s\n", is_linearized(doc) ? "yes" : "no");
    printf("PDF version:     %d.%d\n", pdf_version(doc) / 10,
           pdf_version(doc) % 10);

    pdf_close(doc);
    return 0;
}

/* pdftoppm -- render pages of a PDF to images.
 *
 * The interface is fixed by /app/TEST_INTERFACE.md section 2.1 and
 * /app/SPEC/raster-linearize.md section 1, which together are its whole
 * definition here: no manual page for this program ships in this image.
 *
 *   pdftoppm -png -r 150 -aa yes -aaVector yes -freetype yes \
 *            -thinlinemode none -f N -l M INFILE OUTPREFIX
 *   ... -f N -l N -singlefile INFILE OUTPREFIX
 *
 * One file per page requested, named OUTPREFIX-NNN.png, or exactly
 * OUTPREFIX.png when -singlefile is given.  The zero padding of NNN is not
 * compared; the number of files and what is in them are.
 *
 * Three of the six render flags name this program's own default and are passed
 * anyway, so that the command line is the whole specification of the rendering
 * and nothing is left to a default that could change.  They are accepted and
 * checked rather than ignored: a value other than the frozen one would select a
 * rendering this implementation does not produce, and silently rendering the
 * frozen one anyway would be a lie about what was drawn.
 */

#include "pdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
            "usage: pdftoppm -png -r RES [-aa yes] [-aaVector yes] "
            "[-freetype yes]\n"
            "                [-thinlinemode none] [-f N] [-l M] "
            "[-singlefile]\n"
            "                INFILE OUTPREFIX\n");
}

static int want_value(const char *a)
{
    return !strcmp(a, "-r") || !strcmp(a, "-rx") || !strcmp(a, "-ry")
        || !strcmp(a, "-aa") || !strcmp(a, "-aaVector")
        || !strcmp(a, "-freetype") || !strcmp(a, "-thinlinemode")
        || !strcmp(a, "-f") || !strcmp(a, "-l");
}

int main(int argc, char **argv)
{
    const char *infile = NULL, *prefix = NULL;
    double res = 150.0;
    int first = 0, last = 0, singlefile = 0;
    int i, npages, pg, digits, rc = 0;
    pdf_doc *doc;
    char *errmsg = NULL;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (a[0] == '-' && a[1] != '\0') {
            const char *v = NULL;

            if (want_value(a)) {
                if (i + 1 >= argc) {
                    usage();
                    return 1;
                }
                v = argv[++i];
            }
            if (!strcmp(a, "-png")) {
                continue;
            } else if (!strcmp(a, "-singlefile")) {
                singlefile = 1;
            } else if (!strcmp(a, "-r") || !strcmp(a, "-rx")
                       || !strcmp(a, "-ry")) {
                res = atof(v);
                if (!(res > 0.0)) {
                    usage();
                    return 1;
                }
            } else if (!strcmp(a, "-f")) {
                first = atoi(v);
            } else if (!strcmp(a, "-l")) {
                last = atoi(v);
            } else if (!strcmp(a, "-aa") || !strcmp(a, "-aaVector")
                       || !strcmp(a, "-freetype")) {
                /* The frozen value is "yes", which is also the default.  "no"
                 * selects a different rendering, and this program produces
                 * only the antialiased one, so it says so instead of drawing
                 * something the flag did not ask for. */
                if (strcmp(v, "yes") != 0) {
                    fprintf(stderr, "pdftoppm: %s %s is not supported\n", a, v);
                    return 1;
                }
            } else if (!strcmp(a, "-thinlinemode")) {
                /* "none", "solid" and "shape" give three different rasters on
                 * strokes narrower than one device pixel.  Only the first is
                 * implemented. */
                if (strcmp(v, "none") != 0) {
                    fprintf(stderr, "pdftoppm: -thinlinemode %s is not "
                            "supported\n", v);
                    return 1;
                }
            } else {
                fprintf(stderr, "pdftoppm: unrecognized option %s\n", a);
                usage();
                return 1;
            }
        } else if (infile == NULL) {
            infile = a;
        } else if (prefix == NULL) {
            prefix = a;
        } else {
            usage();
            return 1;
        }
    }
    if (infile == NULL || prefix == NULL) {
        usage();
        return 1;
    }

    doc = pdf_open(infile, &errmsg);
    if (doc == NULL) {
        fprintf(stderr, "pdftoppm: %s: %s\n", infile,
                errmsg != NULL ? errmsg : "unable to read the PDF file");
        return 1;
    }
    if (pdf_is_encrypted(doc) && !pdf_is_unlocked(doc))
        (void)pdf_authenticate(doc, "");

    npages = pdf_page_count(doc);
    if (first < 1) first = 1;
    if (last < 1 || last > npages) last = npages;

    digits = 1;
    for (pg = last; pg >= 10; pg /= 10)
        digits++;

    for (pg = first; pg <= last; pg++) {
        unsigned char *rgb = NULL;
        char path[4096];
        int w = 0, h = 0;

        if (pdf_render_page(doc, pg - 1, res, &rgb, &w, &h) != 0 || rgb == NULL) {
            /* A page that could not be drawn leaves NO file.  Writing a blank
             * one would be worse than writing nothing: a blank page and a page
             * that failed to render are indistinguishable afterwards. */
            fprintf(stderr, "pdftoppm: %s: page %d could not be rendered\n",
                    infile, pg);
            rc = 1;
            continue;
        }
        if (singlefile)
            snprintf(path, sizeof path, "%s.png", prefix);
        else
            snprintf(path, sizeof path, "%s-%0*d.png", prefix, digits, pg);
        if (pdf_png_write(path, rgb, w, h) != 0) {
            fprintf(stderr, "pdftoppm: %s: unable to write the image\n", path);
            rc = 1;
        }
        free(rgb);
    }
    pdf_close(doc);
    return rc;
}

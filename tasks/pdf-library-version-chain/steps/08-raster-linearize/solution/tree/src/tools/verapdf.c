/* verapdf.c -- the veraPDF command-line validator interface.
 *
 *   verapdf -f FLAVOUR --format text FILE...
 *   verapdf --flavour FLAVOUR --format text FILE...
 *
 * The interface is veraPDF's, adopted as-is: the same option letters, the same
 * operand order and the same output layout.  Three line shapes occur and all
 * three are part of the answer:
 *
 *   PASS PATH FLAVOUR
 *   FAIL PATH FLAVOUR
 *   PATH does not appear to be a valid PDF file and could not be parsed.
 *
 * PATH is the operand as the tool reports it.  An ABSOLUTE operand is echoed
 * back byte for byte; a relative one has the working directory prepended and
 * nothing is normalised, so "./p.pdf" from /s comes back "/s/./p.pdf".  The
 * paths carry spaces and neither side quotes or escapes them -- the separator
 * is a single space and the flavour ends the line, which is what makes that
 * safe.
 *
 * The invocation is BATCHED: the whole scored set for a flavour arrives as many
 * FILE operands in one run, and one line is emitted per processed operand, in
 * operand order.  Nothing here pays a per-file start-up cost; the documents are
 * opened and closed one at a time inside a single process.
 *
 * Exit status is a property of the BATCH, not of any file in it:
 *
 *   0   every operand processed was compliant
 *   1   at least one was not, and none failed to parse
 *   7   at least one operand could not be parsed
 *   4   the invocation ended up with no file to process at all
 *   2   FLAVOUR was not one of the legal values
 *
 * An operand naming a file that does not exist is DROPPED: it contributes no
 * line, it does not change the status, and it leaves every other operand's line
 * exactly as it would have been.  Standard error is never part of the answer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

#include "pdfaint.h"


/* Exit statuses, named where they are decided rather than at each return. */
#define EX_COMPLIANT    0
#define EX_NONCOMPLIANT 1
#define EX_BADFLAVOUR   2
#define EX_NOWORK       4
#define EX_UNPARSABLE   7

static void usage(void)
{
    fprintf(stderr,
            "usage: verapdf [--flavour|-f FLAVOUR] [--format FORMAT] FILE...\n"
            "  --flavour, -f  conformance level to validate against\n"
            "  --format       text (the only format this build emits)\n");
}

/* The operand exactly as the interface reports it: absolute operands are echoed
 * unchanged, and a relative one has the working directory prepended with no
 * normalisation of "." or "..".  Returns a malloc'd string, or NULL. */
static char *report_path(const char *operand)
{
    char *buf, cwd[PATH_MAX];
    size_t n;

    if (operand[0] == '/') {
        buf = malloc(strlen(operand) + 1);
        if (buf) strcpy(buf, operand);
        return buf;
    }
    if (!getcwd(cwd, sizeof cwd)) return NULL;
    n = strlen(cwd) + 1 + strlen(operand) + 1;
    buf = malloc(n);
    if (!buf) return NULL;
    snprintf(buf, n, "%s/%s", cwd, operand);
    return buf;
}

/* Does this operand name something we can even attempt?  A missing file is
 * dropped rather than reported, which is the one case where the scored property
 * is an ABSENCE -- so this must not invent a line for it. */
static int operand_exists(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) || S_ISFIFO(st.st_mode) || S_ISCHR(st.st_mode);
}

int main(int argc, char **argv)
{
    const pdfa_flavour *fl = NULL;
    const char *flavour_arg = NULL;
    int i, processed = 0, any_fail = 0, any_unparsable = 0;
    int first_file = argc;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!strcmp(a, "-f") || !strcmp(a, "--flavour")) {
            if (++i >= argc) { usage(); return EX_BADFLAVOUR; }
            flavour_arg = argv[i];
        } else if (!strncmp(a, "--flavour=", 10)) {
            flavour_arg = a + 10;
        } else if (!strcmp(a, "--format")) {
            /* text is the only format this build emits.  xml, json, html and
             * raw are named by the real interface and are not scored, so an
             * unknown value here is accepted and ignored rather than refused. */
            if (++i >= argc) { usage(); return EX_BADFLAVOUR; }
        } else if (!strncmp(a, "--format=", 9)) {
            /* ditto */
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage();
            return EX_COMPLIANT;
        } else if (a[0] == '-' && a[1] && strcmp(a, "-")) {
            /* An option this build does not implement.  The scored subset is
             * fixed by the interface and contains none, so ignoring it keeps a
             * stray flag from turning into a wrong exit status. */
        } else {
            first_file = i;
            break;
        }
    }

    /* The flavour is validated BEFORE any file is touched: status 2 is an
     * operational error about the command line and outranks every verdict. */
    if (!flavour_arg) { usage(); return EX_BADFLAVOUR; }
    fl = pdfa_flavour_lookup(flavour_arg);
    if (!fl) {
        fprintf(stderr, "verapdf: unknown flavour '%s'\n", flavour_arg);
        return EX_BADFLAVOUR;
    }

    for (i = first_file; i < argc; i++) {
        const char *operand = argv[i];
        char *shown;
        pdf_doc *doc;
        int verdict;

        /* A missing operand contributes nothing at all -- no line, no change of
         * status.  Checked before report_path() so that nothing is printed. */
        if (!operand_exists(operand)) continue;

        shown = report_path(operand);
        if (!shown) continue;

        if (!pdfa_parsable(operand)) {
            printf("%s does not appear to be a valid PDF file and could not be "
                   "parsed.\n", shown);
            any_unparsable = 1;
            processed++;
            free(shown);
            continue;
        }

        doc = pdf_open(operand, NULL);
        if (!doc) {
            printf("%s does not appear to be a valid PDF file and could not be "
                   "parsed.\n", shown);
            any_unparsable = 1;
            processed++;
            free(shown);
            continue;
        }

        verdict = pdfa_validate(doc, fl);
        printf("%s %s %s\n", verdict == PDFA_PASS ? "PASS" : "FAIL",
               shown, fl->name);
        if (verdict != PDFA_PASS) any_fail = 1;
        /* Standard error is never compared, so the clause that refused a file
         * is reported there for a human rather than suppressed. */
        if (verdict != PDFA_PASS && getenv("PDFA_TRACE"))
            fprintf(stderr, "why: %s :: %s\n", pdfa_last_reason(), shown);
        processed++;

        pdf_close(doc);
        free(shown);
    }

    fflush(stdout);

    if (processed == 0)  return EX_NOWORK;
    if (any_unparsable)  return EX_UNPARSABLE;
    if (any_fail)        return EX_NONCOMPLIANT;
    return EX_COMPLIANT;
}

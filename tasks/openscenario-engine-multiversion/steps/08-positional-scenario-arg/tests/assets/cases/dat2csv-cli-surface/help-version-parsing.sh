#!/usr/bin/env bash
source /tests/caselib.sh

# dat2csv now parses its own command line. --help prints a usage block and
# --version prints the build version; both are recognised even with no input
# file to convert, so a missing filename is no longer the only thing it knows
# how to report.

run_tool "$DAT2CSV" --help
need_status 1 $? "--help exit status"
need_grep '^Usage: dat2csv' "$WORK/last.out" "usage banner names the tool"
need_grep '\[--file\] <filename>' "$WORK/last.out" "usage lists the file option"
need_grep '\-\-csv <name>' "$WORK/last.out" "usage lists the csv option"
need_grep '\-\-extended' "$WORK/last.out" "usage lists the extended option"
need_grep '\-\-print_csv' "$WORK/last.out" "usage lists --print_csv"
need_grep '\-\-file_refs' "$WORK/last.out" "usage lists --file_refs"

run_tool "$DAT2CSV" -h
need_status 1 $? "-h exit status"
need_grep '^Usage: dat2csv' "$WORK/last.out" "short help prints the same banner"

run_tool "$DAT2CSV" --version
need_status 2 $? "--version exit status"
need_grep 'GIT REV' "$WORK/last.out" "version names the revision"
need_grep 'BUILD VERSION' "$WORK/last.out" "version names the build"

# A bare invocation still prints usage, and now it exits with the usage status
# rather than being mistaken for a filename.
run_tool "$DAT2CSV"
need_status 255 $? "no arguments exit status"
need_grep '^Usage: dat2csv' "$WORK/last.out" "no arguments prints usage"

# instruction.md requirement 3 says the exit status is "the sum of what was
# asked for", so asking for both help and version is the case that tells a real
# sum apart from a lookup table of one-flag answers.
run_tool "$DAT2CSV" --help --version
need_status 3 $? "--help --version exit status is 1 + 2"
need_grep '^Usage: dat2csv' "$WORK/last.out" "--help --version still prints usage"
need_grep 'BUILD VERSION' "$WORK/last.out" "--help --version still prints the version"

# ...and the sum does not depend on the order the two were written in.
run_tool "$DAT2CSV" --version --help
need_status 3 $? "--version --help exit status is also 3"

# An unknown option is named back to the caller, the usage block follows it, and
# the status is -1 as the shell sees it.
run_tool "$DAT2CSV" --bogus
need_status 255 $? "unknown option exit status"
need_grep '^Unrecognized arguments:' "$WORK/last.out" "unknown option is reported as unrecognised"
need_grep '^ *--bogus' "$WORK/last.out" "the offending argument is named back"
need_grep '^Usage: dat2csv' "$WORK/last.out" "the usage block follows the complaint"

# The complaint comes from the parser, before anything is opened: an unknown
# option alongside a filename that does not exist is still an argument error,
# not the "Cannot open" file error that dat2csv-rejects-old-dat owns. Requirement
# 3 promises unknown arguments are "reported with Unrecognized arguments:", and
# a tool that opened the file first could not keep that promise.
run_tool "$DAT2CSV" --file no_such_recording.dat --bogus
need_status 255 $? "unknown option wins over a missing file"
need_grep '^Unrecognized arguments:' "$WORK/last.out" "parse error is reported, not the file error"
need_not_grep 'Cannot open' "$WORK/last.out" "the file was never opened"

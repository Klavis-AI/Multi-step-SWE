# R11.6: the shell's own usage text names the program the user is expected to
# type. What is compared is only whether each of the two names is present:
# the installed name `tursodb` must appear somewhere in what `--help` writes,
# and the retired name `limbo` must appear nowhere in it. That is the whole
# assertion -- no line number, no wording, no ordering and no layout of the
# help text is pinned, so the case stays inside TEST_INTERFACE.md's "Not
# scored" entry for the wording of help text while still observing that the
# rename reached the program and not only the installed file name.
#
# An earlier version of this case pinned lines 1 and 3 of the output verbatim
# (`The Turso interactive SQL shell` and `Usage: tursodb [OPTIONS] [DATABASE]
# [SQL]`). Only the reference binary produces those bytes, so it could not
# discriminate a conforming shell from a wrong one -- it graded the reference
# implementation instead of the contract.
#
# There is no second opinion to take here: stock sqlite3's help text describes
# stock sqlite3, and the fact under test is which name *this* program answers
# to.
#
# Note `--version` still reports `limbo 0.1.0` at this checkpoint -- the
# version banner lags the rename -- so the banner is deliberately not read
# here; the help text is what moved.
#
# Standard error is discarded rather than merged: this is graded engine
# output, and a warning folded into it would change the answer.
set -u
help_text=$("$CLI" --help 2>/dev/null)
case "$help_text" in
    *tursodb*) echo "help_names_installed_program yes" ;;
    *)         echo "help_names_installed_program no"  ;;
esac
case "$help_text" in
    *limbo*|*Limbo*|*LIMBO*) echo "help_names_retired_program yes" ;;
    *)                       echo "help_names_retired_program no"  ;;
esac

# R01.10: the page-1 header of a freshly created database.
#
# R01.10 names the five header fields `spec/PROFILE.md` rule 1 chooses -- the
# page size at offset 16, the write and read version bytes at 18 and 19 that
# put the file in WAL mode, the reserved-space byte at 20, and the text
# encoding at 56 -- then names the eight the format itself fixes for a database
# created this way, and says that a page-1 field outside those thirteen is not
# graded. This case is that row read off the file. The long note below is the
# field-by-field justification, and the rule it applies is exactly R01.10's
# last sentence.
#
# No stock sqlite3 cross-read: stock sqlite3 writes a *different* page-1 header
# for this workload -- a rollback-journal file with version bytes 01/01 unless
# told otherwise, and a change counter of 2 where this engine writes 1 -- so it
# is not a second opinion about these bytes but a different answer by design.
# The second opinion that does exist is `sqlite-reads-our-database.sh`, which
# proves stock sqlite3 accepts the file this header describes.
# The first 100 bytes of page 1 are the SQLite database header. Every field
# graded below is fixed by one of two things, and by nothing else: either the
# file format fixes it outright, so that anything which knows the format can
# read the file, or spec/PROFILE.md rule 1 chooses it where the format leaves
# the writer free. Five fields are of the second kind and each is named in
# that rule -- the page size at offset 16, the read and write version bytes at
# 18 and 19, the reserved-space byte at 20 and the text encoding at 56. The
# other eight follow from the format itself.
#
# Three of those eight are worth spelling out in the other direction, because
# at a glance they look like writer's choices and are not. The format fixes
# each of them at zero for a database created by this step's statements, so a
# format-conforming writer passes them without choosing anything:
#
#   * largest_root_page at offset 52 -- "the page number of the largest root
#     b-tree page when in auto-vacuum or incremental-vacuum modes, or zero
#     otherwise". Nothing at this step engages either mode.
#   * incremental_vacuum at offset 64 -- "True (non-zero) for
#     incremental-vacuum mode. False (zero) otherwise." Same reason.
#   * application_id at offset 68 -- the value `PRAGMA application_id` sets,
#     and nothing at this step sets it, so it is the zero a fresh header
#     carries.
#
# They are graded because a build that leaves any of them uninitialised writes
# a file the format calls malformed, not because a task document picks a value.
# R01.10 names all three explicitly for that reason: its closing sentence
# excludes fields the format leaves free and no document chooses, and read
# without them a candidate could conclude application_id was one of those.
#
# The text encoding at offset 56 is worth spelling out, because it is the one
# field here a conforming writer could legitimately have written differently.
# The format permits 1, 2 and 3 -- UTF-8, UTF-16le, UTF-16be -- and stock
# sqlite3 reads all three. Nothing about this step's statements forces one. So
# it is graded here only because PROFILE rule 1 picks UTF-8 and says the tests
# assert it; if the rule were withdrawn the field would have to come out of
# this case with it. That is the test for every field below: a document says
# what it must be, or the format does.
#
# Seven further header fields are not graded here. The version-valid-for number
# at offset 92 is not: the format defines it as "the value of the change
# counter when the version number was stored" -- the same value as offset 24 --
# so the only reading a conforming writer can produce is a duplicate of the
# field above it, and asserting the duplicate would pin a constant rather than
# a decision.
#
# Three more are left free by the format AND unchosen by any task document, so
# asserting any of them would pin this engine's choice rather than the
# contract. The change counter at offset 24: the format says it "might not be
# incremented on each transaction in WAL mode", the profile requires WAL, and
# the two engines differ exactly as that sentence allows -- stock sqlite3
# writes 00 00 00 02 for this workload where this engine writes 00 00 00 01.
# The database size in pages at offset 28: the interface does not score file
# size, provided the contents are correct and stock sqlite3 accepts the file,
# so a field that is a function of the page count would contradict that. The
# schema format number at offset 44: the format supports 1, 2, 3 and 4, no task
# document chooses one, and a copy of this file patched to 1 still returns
# integrity_check = ok, still re-parses its schema and still returns every row
# through both engines. That last rationale is the same shape as the one for
# offset 56 above, and the two land differently for one reason only: the
# profile chooses the text encoding and does not choose the schema format.
#
# The three remaining (user_version, the default page-cache size and the schema
# cookie) are corrected later in the ladder and are graded at the steps that
# correct them.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE hdr(a);
-- Durability barrier. The writing invocation ends by reading back every table
-- it wrote; reading the data pages is what forces a pending write to complete,
-- and reading only sqlite_schema touches page 1 and is not sufficient.
-- It is not needed at this step's own checkpoint -- v0.0.13 finalises its WAL
-- at process exit, measured clean without the barrier -- and it is here
-- because the generator re-measures every case at every later checkpoint in
-- the chain and rejects one whose answer moves there, and v0.0.16 through
-- v0.0.18 do not finalise it at exit: a cross-process
-- write-then-read corrupts 5-25% of the time there. It asserts nothing and
-- weakens nothing; the same rows are read back either way.
SELECT count(*) FROM hdr;
SQL
hex() { od -An -tx1 -j "$1" -N "$2" main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'; }
echo "magic              $(hex 0 16)"
echo "page_size          $(hex 16 2)"
echo "write_version      $(hex 18 1)"
echo "read_version       $(hex 19 1)"
echo "reserved_space     $(hex 20 1)"
echo "payload_fractions  $(hex 21 3)"
echo "freelist_trunk     $(hex 32 4)"
echo "freelist_count     $(hex 36 4)"
echo "largest_root_page  $(hex 52 4)"
echo "text_encoding      $(hex 56 4)"
echo "incremental_vacuum $(hex 64 4)"
echo "application_id     $(hex 68 4)"
echo "reserved_zeros     $(hex 72 20)"

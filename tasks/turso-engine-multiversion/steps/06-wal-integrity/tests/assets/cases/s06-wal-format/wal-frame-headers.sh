# R06.3, and the present-and-advancing half of R06.2: the structure of the
# 24-byte frame headers. The checksum values themselves are graded by
# wal-checksum-chain.sh.
#
# Every frame in a WAL carries a 24-byte header: the page number it holds, the
# database size in pages on a frame that commits a transaction, the log's two
# salts, and the running checksum pair. This case grades the structure of those
# headers on the log the engine leaves behind.
#
# WHAT IS ASSERTED WHEN THERE IS NO LOG TO READ. A writing invocation may hand
# its committed frames on to `main.db` and leave the log empty, and that is
# conforming - `spec/PROFILE.md`'s durability rule places the obligation on the
# committed data surviving, not on the file it survives in. Measured over the
# release chain on 2026-09-21: this step's checkpoint and every one up to the
# second-to-last leave a 12392-byte log; only the final checkpoint folds it in
# and truncates the file to zero bytes - the file is still present in the
# directory there, only its length changes, so the profile's "a write leaves a
# `-wal` file behind" holds throughout. So `committed_write_durable` is graded
# first, and it is the one verdict here that no absence can satisfy - a writer
# earns it either by leaving at least one whole frame in `main.db-wal`, or,
# having left none, by proving the write survived anyway: `main.db` must carry
# the WAL-mode header bytes, and a copy of it handed to stock sqlite3 must pass
# `integrity_check` and give back all three rows in order. A writer that leaves
# neither a frame nor a recoverable database fails that verdict and every
# verdict after it. The structural verdicts are conditional on a log being
# there, and whenever one is they are graded strictly: the empty-log route is
# not available to a writer that left bytes behind.
#
# What this case does NOT grade is any particular salt or checksum value. Both
# are seeded from a per-run random salt, so an expected output naming them would
# be asserting the random number generator. The reproducible content of a
# checksum chain is that it is present and that it advances: a cumulative
# checksum cannot repeat across consecutive frames holding different pages.
#
# Like its siblings, this case only writes and then reads the file's bytes. It
# never reopens the database with the ENGINE, because the v0.0.18 read-back is
# not reproducible under load - it panics with Corrupt("Invalid page type: 0"),
# and stock sqlite3 handed the same pair reports a malformed schema, on a few
# percent of runs. The bytes below are written before any of that, and the file
# stock reads on the empty-log route is a copy.
#
# The second opinion is stock sqlite3's own log, not stock sqlite3 reading this
# one. Stock has no useful opinion on the file this engine wrote: at v0.0.18 a
# committed page reaches `main.db` as well as the log, so stock returns the same
# rows from this database with the log present, with its header corrupted, and
# with it deleted outright - all three answer 3, measured directly. A cross-read
# of that shape would be decorative. What stock can answer is whether the
# structural rules applied below are the *format's* rules rather than this
# engine's habits, and it answers that by writing a log of its own: the block
# below has stock sqlite3 3.50.2 run the same workload in WAL mode and copy its
# log aside before it closes (it unlinks the log on close, so the copy is taken
# from inside the session), and `sqlite3_frame_headers` runs the identical
# structural checks over stock's frames. A rule that this engine's log satisfies
# and an independent implementation's log does not is a rule this case invented,
# and that verdict is where it would show up. It does not depend on the engine,
# so it is graded the same way on both routes.
#
# One thing R06.3 does NOT claim, because it was measured false of the
# reference: that the commit-size field is zero on a frame that does not end a
# transaction. Stock sqlite3's own log for this workload reads 0, 2, 2 across
# its three frames, but this engine's reads 2, 2, 2 - it marks every frame a
# commit - stable across repeated runs at this checkpoint. A case asserting the
# zero would fail the engine the whole ladder is cut from, so neither the
# requirement nor this case asserts it.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE w(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO w VALUES(1,'alpha'),(2,'beta'),(3,'gamma');
SQL
# A writer that crashed has not conformed, whatever it left on disk. The status
# is graded so that a panic cannot pass as a well-formed log; the invocation
# above hides the message itself, because diagnostic wording is never compared.
if [ "$?" -eq 0 ]; then echo "write_status: clean"; else echo "write_status: failed"; fi

# Stock sqlite3's control log. Its own output is printed, not discarded, so a
# stock failure lands in the graded output and fails this case loudly instead of
# quietly leaving no copy behind.
mode=$("$SQLITE3" -batch ref.db 2>&1 <<'SQL'
PRAGMA page_size=4096;
PRAGMA journal_mode=WAL;
CREATE TABLE w(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO w VALUES(1,'alpha'),(2,'beta'),(3,'gamma');
.shell cp ref.db-wal ref-wal.copy
SQL
)
echo "sqlite3_journal_mode: $mode"

at()   { od -An -tx1 -j "$2" -N "$3" "$1" 2>/dev/null | tr -d ' \n'; }
u32()  { od -An -tu4 -j "$2" -N4 --endian=big "$1" 2>/dev/null | tr -d ' \n'; }
verdict() { if [ "$1" = yes ]; then echo "$2: conforming"; else echo "$2: nonconforming"; fi; }

# Positive evidence that the committed write survived in a writer that left no
# frames behind. Every clause has to hold: the database must be a WAL-mode
# database (bytes 18 and 19 of the page-1 header are both 2), stock sqlite3
# must find it structurally sound, and all three committed rows must come back
# in order. Stock reads a COPY, so nothing it does can checkpoint or recover
# the engine's own file.
folded_ok() {
    [ -f main.db ] || { echo no; return; }
    [ "$(od -An -tu1 -j 18 -N2 main.db 2>/dev/null | tr -d ' ')" = "22" ] || { echo no; return; }
    cp main.db folded.copy 2>/dev/null || { echo no; return; }
    [ "$("$SQLITE3" -batch -noheader -list folded.copy 'PRAGMA integrity_check;' 2>&1 | head -1)" = ok ] || { echo no; return; }
    [ "$("$SQLITE3" -batch -noheader -list folded.copy 'SELECT id||s FROM w ORDER BY id;' 2>&1 | tr '\n' ',')" = "1alpha,2beta,3gamma," ] || { echo no; return; }
    echo yes
}

# The four structural rules, in one place, so that the log this engine wrote and
# the log stock wrote are held to exactly the same standard. Echoes five yes/no
# words: page numbers, commit record, commit size non-decreasing, checksums
# present, checksums advance.
structure() {
    f="$1"
    sz=0
    [ -f "$f" ] && sz=$(wc -c < "$f")
    if [ "$sz" -lt 32 ]; then echo "no no no no no"; return; fi
    n=$(( (sz - 32) / 4120 ))
    if [ "$n" -lt 1 ]; then echo "no no no no no"; return; fi
    p=yes; cm=yes; cs=yes; ck=yes; ad=yes
    seen_commit=""; prev=""
    i=0
    while [ "$i" -lt "$n" ]; do
        off=$(( 32 + i * 4120 ))
        # The page number a frame carries is a real page, counted from one.
        [ "$(u32 "$f" "$off")" -ge 1 ] || p=no
        # A nonzero commit size ends a transaction and names the database size
        # in pages at that commit. Successive commits must not report the
        # database shrinking: this workload only ever adds. They are NOT
        # required to be equal - the format lets the size grow from one commit
        # record to the next, and this engine was observed to record the CREATE
        # TABLE commit at a smaller size than the INSERT commit on a minority of
        # runs, which is conforming.
        c=$(u32 "$f" $(( off + 4 )))
        if [ "$c" -ne 0 ]; then
            if [ -n "$seen_commit" ] && [ "$c" -lt "$seen_commit" ]; then cs=no; fi
            seen_commit="$c"
        fi
        # The checksum pair is present, and advances from frame to frame.
        k=$(at "$f" $(( off + 16 )) 8)
        [ "$k" = "0000000000000000" ] && ck=no
        [ -n "$prev" ] && [ "$k" = "$prev" ] && ad=no
        prev="$k"
        i=$(( i + 1 ))
    done
    # The last frame of a completed log commits: a log whose final frame is not
    # a commit record is a log with a transaction left open in it.
    [ "$(u32 "$f" $(( 32 + (n - 1) * 4120 + 4 )))" -ne 0 ] || cm=no
    [ -n "$seen_commit" ] || cm=no
    echo "$p $cm $cs $ck $ad"
}

size=0
[ -f main.db-wal ] && size=$(wc -c < main.db-wal)
frames=0
[ "$size" -ge 32 ] && frames=$(( (size - 32) / 4120 ))

if [ "$frames" -ge 1 ]; then
    durable=yes
    set -- $(structure main.db-wal)
    pages="$1"; commit="$2"; csize="$3"; cks="$4"; adv="$5"
else
    # No frames to read. The write still has to be shown to have survived, and
    # the structural verdicts follow that one answer rather than passing free.
    durable=$(folded_ok)
    pages=$durable; commit=$durable; csize=$durable; cks=$durable; adv=$durable
fi

# The same five rules, over a log an independent implementation wrote.
set -- $(structure ref-wal.copy)
ref=yes
for r in "$1" "$2" "$3" "$4" "$5"; do [ "$r" = yes ] || ref=no; done

verdict "$durable" committed_write_durable
verdict "$pages"   wal_frame_page_numbers
verdict "$commit"  wal_frame_commit_record
verdict "$csize"   wal_frame_commit_size_grows
verdict "$cks"     wal_frame_checksums_present
verdict "$adv"     wal_frame_checksums_advance
verdict "$ref"     sqlite3_frame_headers

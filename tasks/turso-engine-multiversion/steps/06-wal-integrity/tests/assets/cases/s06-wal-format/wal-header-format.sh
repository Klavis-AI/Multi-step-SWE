# R06.1: the 32-byte WAL header's fields at their specified offsets, and the
# salts repeated unchanged in every frame header of the log the writer left.
#
# The write-ahead log this engine leaves behind must be in SQLite's WAL format:
# one of the two header magics, format number 3007000, the database page size,
# a checkpoint sequence of zero in a log that has never been checkpointed, and
# a salt pair that is not all zeros and that every frame header repeats.
#
# WHAT IS ASSERTED WHEN THERE IS NO LOG TO READ. A writing invocation may hand
# its committed frames on to `main.db` and leave the log empty, and that is
# conforming - `spec/PROFILE.md`'s durability rule places the obligation on the
# committed data surviving, not on the file it survives in. Measured over the
# release chain on 2026-09-21, the checkpoint this step is cut from leaves a
# 12392-byte log, every checkpoint from here to the second-to-last leaves the
# same, and only the final one folds the log into the database and truncates it
# to zero bytes. The file is never gone: `main.db-wal` is present in the
# directory after the write at every checkpoint measured, the final one
# included, so the profile's "a write leaves a `-wal` file behind" holds all
# along - what changes at the end of the chain is the file's length, not its
# existence. So this case cannot require a log unconditionally without failing
# a later checkpoint for doing something the profile expressly allows.
#
# What it does instead is refuse to let "no log" mean "nothing to check".
# `committed_write_durable` is graded first and is the one verdict here that is
# never satisfied by an absence: a writer earns it either by leaving at least
# one whole frame in `main.db-wal`, or - having left none - by proving the write
# survived anyway, which this case checks by reading `main.db`'s WAL-mode
# header bytes and handing a copy of the file to stock sqlite3 for
# `integrity_check` and for all three rows back in order. A writer that leaves
# neither a frame nor a recoverable database fails that verdict AND every
# verdict after it: an invocation that wrote nothing at all scores nonconforming
# on the whole case, which is the behaviour the round-1 review asked for. The
# field verdicts below it are conditional on a log being there, and they are
# graded strictly whenever one is - a log that exists but is malformed fails
# them, and the empty-log route is not available to a writer that left bytes
# behind. `TEST_INTERFACE.md` and `PROFILE.md` state the same split.
#
# This case deliberately does nothing to `main.db` after the write but read its
# bytes. It does not hand the database to a second ENGINE invocation, and it
# does not checkpoint. Both of those were measured to be unreliable at the
# v0.0.18 checkpoint once the machine is busy - reading back or checkpointing
# panics there with Corrupt("Invalid page type: 0") on a few percent of runs,
# and the frame-count triple a checkpoint reports is not reproducible - so a
# case built on either would be grading a reference that cannot reproduce
# itself. The plain write is stable: 120/120 runs under four-way load left a
# 12392-byte log with an identical header. The copy stock sqlite3 reads on the
# empty-log route is a copy, so stock cannot checkpoint or recover the engine's
# own file underneath the verdicts above.
#
# The second opinion on the FORMAT is stock sqlite3's own log, not stock
# sqlite3 reading this one. Stock has no useful opinion on the log this engine
# wrote: at v0.0.18 a committed page reaches `main.db` as well as the log, so
# stock returns the same three rows from this database with the log present,
# with the log's header corrupted, and with the log deleted outright - measured
# directly, all three answer 3. A cross-read of that shape would be decorative.
# What stock can answer is what the format's own constants are, by writing a log
# of its own: the block below has stock sqlite3 3.50.2 run the same workload in
# WAL mode and copy its log aside before it closes (it unlinks the log on close,
# so the copy is taken from inside the session), and `sqlite3_log_agrees` checks
# the magic, format number and page-size field this case requires against the
# ones an independent implementation of the same format put at the same offsets.
# That verdict does not depend on the engine at all, which is the point: it is
# what keeps the three constants above from being numbers copied out of one
# implementation's diff. The salts are not compared: they are random per log by
# design.
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
# stock failure lands in the graded output and fails this case loudly instead
# of quietly leaving no copy behind.
mode=$("$SQLITE3" -batch ref.db 2>&1 <<'SQL'
PRAGMA page_size=4096;
PRAGMA journal_mode=WAL;
CREATE TABLE w(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO w VALUES(1,'alpha'),(2,'beta'),(3,'gamma');
.shell cp ref.db-wal ref-wal.copy
SQL
)
echo "sqlite3_journal_mode: $mode"

at() { od -An -tx1 -j "$2" -N "$3" "$1" 2>/dev/null | tr -d ' \n'; }
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

size=0
[ -f main.db-wal ] && size=$(wc -c < main.db-wal)
frames=0
[ "$size" -ge 32 ] && frames=$(( (size - 32) / 4120 ))

if [ "$frames" -ge 1 ]; then
    durable=yes
    magic=yes; format=yes; pgsz=yes; seq=yes; salts=yes
    m=$(at main.db-wal 0 4)
    case "$m" in 377f0682|377f0683) magic=yes ;; *) magic=no ;; esac
    [ "$(at main.db-wal 4 4)" = "002de218" ] && format=yes || format=no
    [ "$(at main.db-wal 8 4)" = "00001000" ] && pgsz=yes || pgsz=no
    [ "$(at main.db-wal 12 4)" = "00000000" ] && seq=yes || seq=no
    hsalt=$(at main.db-wal 16 8)
    if [ "$hsalt" = "0000000000000000" ]; then
        # The salt pair identifies one incarnation of the log and is what makes
        # a stale frame recognisable. A fixed zero pair identifies nothing.
        salts=no
    else
        i=0
        while [ "$i" -lt "$frames" ]; do
            off=$(( 32 + i * 4120 + 8 ))
            [ "$(at main.db-wal "$off" 8)" = "$hsalt" ] || salts=no
            i=$(( i + 1 ))
        done
    fi
else
    # No frames to read. The write still has to be shown to have survived, and
    # the field verdicts follow that one answer rather than passing for free.
    durable=$(folded_ok)
    magic=$durable; format=$durable; pgsz=$durable; seq=$durable; salts=$durable
fi

# The three constants this case requires, read out of a log stock sqlite3 wrote
# itself. Independent of the engine, so it is graded the same way on both routes.
agrees=yes
rsize=0
[ -f ref-wal.copy ] && rsize=$(wc -c < ref-wal.copy)
if [ "$rsize" -lt 32 ]; then
    agrees=no
else
    case "$(at ref-wal.copy 0 4)" in 377f0682|377f0683) : ;; *) agrees=no ;; esac
    [ "$(at ref-wal.copy 4 4)" = "002de218" ] || agrees=no
    [ "$(at ref-wal.copy 8 4)" = "00001000" ] || agrees=no
fi

verdict "$durable" committed_write_durable
# The profile requires the write-ahead log to still be there after the write
# ("the engine does not unlink it when the last connection goes away"), and a
# logless writer -- one that folds and unlinks -- would otherwise pass on the
# folded route alone. The header above records the measurement: the file is
# present after the write at every checkpoint in the chain.
verdict "$([ -f main.db-wal ] && echo yes || echo no)" wal_file_present
verdict "$magic"   wal_magic
verdict "$format"  wal_format_number
verdict "$pgsz"    wal_page_size
verdict "$seq"     wal_checkpoint_sequence
verdict "$salts"   wal_frame_salts
verdict "$agrees"  sqlite3_log_agrees

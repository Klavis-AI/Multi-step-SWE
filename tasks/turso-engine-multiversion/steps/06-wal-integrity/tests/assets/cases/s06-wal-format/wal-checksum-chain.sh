# R06.2: the WAL's cumulative Fibonacci-weighted checksum pair, recomputed from
# the log's own bytes in the byte order the header magic selects, and compared
# with what the engine stored.
#
# Its sibling wal-frame-headers.sh grades that a checksum pair is present and
# that it advances; any counter satisfies that. This case grades the value. The
# algorithm is in spec/sqlite/fileformat2.html, "Write-Ahead Log":
#
#     s0 = s1 = 0
#     for i from 0 to n-1 step 2: s0 += x(i) + s1; s1 += x(i+1) + s0
#
# over 32-bit unsigned words, wrapping. The header's pair is the checksum of
# the first 24 bytes of the header. Each frame's pair continues the running
# value - seeded by the header's stored pair, then carried from frame to frame -
# over the first 8 bytes of that frame's header followed by its whole page
# image. Every integer IN the header and frame headers is big-endian; the byte
# order the *checksum* reads its input in is chosen by the magic's low bit:
# 0x377f0682 selects little-endian, 0x377f0683 big-endian. Both are conforming,
# so the case reads the magic and follows it rather than pinning one.
#
# Nothing here is sampled or approximated: the whole computation is a
# deterministic function of bytes the case already holds.
#
# Like both its siblings this case only writes and then reads the file's bytes.
# It never reopens the database with the engine and never checkpoints - at the
# v0.0.18 checkpoint either of those is nondeterministic under load, panicking
# Corrupt("Invalid page type: 0") on a few percent of runs. The write itself is
# stable, and the bytes below are written before any of that could happen.
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
# verdict after it. The checksum verdicts are conditional on a log being there,
# and whenever one is they are graded strictly: the empty-log route is not
# available to a writer that left bytes behind. The file stock reads on that
# route is a COPY, so stock cannot checkpoint or recover the engine's own file
# underneath the verdicts here. `TEST_INTERFACE.md` states the same split.
#
# The second opinion on the ARITHMETIC is stock sqlite3's own log, not stock
# sqlite3 reading this one - stock cannot discriminate on this engine's files,
# because at v0.0.18 a committed page reaches `main.db` too and stock answers
# identically with the log present, corrupted, or deleted. What stock can do is
# supply a log that an independent implementation of this same algorithm
# checksummed, and that is a real test of the recomputation below:
# `sqlite3_checksum_chain` runs the identical awk over the log stock sqlite3
# 3.50.2 wrote and copied aside from inside its own session (it unlinks the log
# on close). If the arithmetic here were wrong - the wrong seed, the wrong span,
# the wrong byte order - it would have to be wrong in the same direction as this
# engine's bug to keep that verdict conforming, and stock's log is what makes
# that coincidence visible. That verdict does not depend on the engine, so it is
# graded the same way on both routes.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE w(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO w VALUES(1,'alpha'),(2,'beta'),(3,'gamma');
SQL
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

verdict() { if [ "$1" = yes ]; then echo "$2: conforming"; else echo "$2: nonconforming"; fi; }

# The whole recomputation, in one place, so that the log this engine wrote and
# the log stock wrote are checked by exactly the same arithmetic. Echoes three
# yes/no words: byte order recognised, header checksum, frame chain.
chainck() {
    f="$1"
    sz=0
    [ -f "$f" ] && sz=$(wc -c < "$f")
    if [ "$sz" -lt 32 ]; then echo "no no no"; return; fi
    sw=0
    case "$(od -An -tx1 -j 0 -N 4 "$f" 2>/dev/null | tr -d ' \n')" in
        377f0682) sw=1 ;;   # low bit clear: checksum input is little-endian
        377f0683) sw=0 ;;   # low bit set:   checksum input is big-endian
        *)        echo "no no no"; return ;;
    esac
    n=$(( (sz - 32) / 4120 ))
    # -v is not optional: od collapses runs of identical lines to `*` by
    # default, and a page image is mostly zeros, so without it the word
    # stream silently loses most of its input.
    r=$(od -An -v -tu4 --endian=big -N $(( 32 + n * 4120 )) "$f" 2>/dev/null \
        | awk -v frames="$n" -v swap="$sw" '
            function sw(x,   a,b,c,d) {
                a = x % 256; x = int(x / 256)
                b = x % 256; x = int(x / 256)
                c = x % 256; d = int(x / 256)
                return a * 16777216 + b * 65536 + c * 256 + d
            }
            function W(k) { return swap ? sw(w[k]) : w[k] }
            function acc(from, pairs,   j) {
                for (j = 0; j < pairs; j++) {
                    s0 = (s0 + W(from + 2 * j)     + s1) % M
                    s1 = (s1 + W(from + 2 * j + 1) + s0) % M
                }
            }
            BEGIN { M = 4294967296; n = 0 }
            { for (i = 1; i <= NF; i++) w[++n] = $i }
            END {
                hdr = "yes"; ch = "yes"
                # Header: words 1..6 are its first 24 bytes; 7..8 hold the
                # pair those 24 bytes must check to.
                s0 = 0; s1 = 0; acc(1, 3)
                if (s0 != w[7] || s1 != w[8]) hdr = "no"
                # The stored header pair seeds the frame chain, per the
                # format. Seeding from the stored value rather than the
                # computed one keeps a wrong header checksum from cascading
                # into every frame verdict.
                s0 = w[7]; s1 = w[8]
                for (f = 0; f < frames; f++) {
                    b = 9 + f * 1030          # first word of frame f
                    acc(b, 1)                 # page number + commit size
                    acc(b + 6, 512)           # the 4096-byte page image
                    if (s0 != w[b + 4] || s1 != w[b + 5]) ch = "no"
                }
                print hdr, ch
            }')
    set -- $r
    echo "yes ${1:-no} ${2:-no}"
}

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
    set -- $(chainck main.db-wal)
    endian="$1"; hdrck="$2"; chain="$3"
else
    # No frames to read. The write still has to be shown to have survived, and
    # the checksum verdicts follow that one answer rather than passing free.
    durable=$(folded_ok)
    endian=$durable; hdrck=$durable; chain=$durable
fi

# The same arithmetic, over a log an independent implementation checksummed.
set -- $(chainck ref-wal.copy)
ref=yes
for r in "$1" "$2" "$3"; do [ "$r" = yes ] || ref=no; done

verdict "$durable" committed_write_durable
verdict "$endian"  wal_checksum_byte_order
verdict "$hdrck"   wal_header_checksum
verdict "$chain"   wal_frame_checksum_chain
verdict "$ref"     sqlite3_checksum_chain

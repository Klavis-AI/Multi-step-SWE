# R15.11, R15.5 and R15.6 for the two ciphers the rest of the step never
# engages. R15.11 names three recognised ciphers -- aegis256, aes256gcm and
# aegis128l -- and every other case here selects aegis256, so a build that
# implemented one cipher and refused the other two would pass everything else
# at this step. This case engages all three in turn.
#
# For each: the cipher is selected, a key of the size that cipher takes is
# supplied -- 32 bytes for aegis256 and aes256gcm, 16 bytes for aegis128l, as
# R15.12 and TEST_INTERFACE.md state -- two rows are written, and a second
# process supplying the same pair reads them back. Three things are then
# asserted per cipher: the rows came back, the row text is not findable in the
# file, and the page-1 reserved-space byte at offset 20 carries the per-page
# room that cipher's nonce and tag take. TEST_INTERFACE.md fixes those three
# values at 48 for aegis256, 32 for aegis128l and 28 for aes256gcm; they are
# printed in decimal here so that the case reads against that sentence
# directly rather than against a hex spelling of it.
#
# No ciphertext byte is compared. The nonce is fresh per page, so the files
# differ on every run; every assertion below is about what comes back out,
# what cannot be found inside, and a plaintext header field.
#
# Stock sqlite3 is not a second reader for anything this case asserts: it
# cannot read a database written through any of these three ciphers, so there
# is no second opinion to be had on the rows that come back or on the row text
# being absent from the file. That inability is a graded requirement in its
# own right (R15.8) and is asserted by
# s15-encryption-roundtrip/stock-sqlite3-rejects-encrypted-file.
set -u
K32=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
K16=000102030405060708090a0b0c0d0e0f

probe() {
    cipher="$1"
    key="$2"
    mkdir -p "$cipher"
    ( cd "$cipher" && "$CLI" -m "$CLI_MODE" -q enc.db > /dev/null 2>&1 <<SQL
PRAGMA cipher = '$cipher';
PRAGMA hexkey = '$key';
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'cipher-probe-alpha'),(2,'cipher-probe-bravo');
-- Fold the committed rows into the database file. The absence assertion
-- below reads the database file past the page-1 header, where
-- spec/PROFILE.md's durability rule leaves a writer free to keep the rows in
-- the log instead and names this statement as the only way to require
-- otherwise. Without it the grep would be searching a file that holds no rows
-- at all, and would report the row text absent for the wrong reason.
PRAGMA wal_checkpoint(TRUNCATE);
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
    )
    echo "$cipher:"
    printf '  reserved_space %d\n' \
        "0x$(od -An -tx1 -j 20 -N 1 "$cipher/enc.db" | tr -d ' \n')"
    if grep -aq 'cipher-probe' "$cipher/enc.db"; then
        echo "  row text readable in the file: yes"
    else
        echo "  row text readable in the file: no"
    fi
    echo "  rows through a second process holding the pair:"
    ( cd "$cipher" && "$CLI" -m "$CLI_MODE" -q enc.db 2>/dev/null <<SQL | sed '/^[[:space:]]*$/d;s/^/    /'
PRAGMA cipher = '$cipher';
PRAGMA hexkey = '$key';
SELECT id, s FROM t ORDER BY id;
SQL
    )
}

probe aegis256  "$K32"
probe aes256gcm "$K32"
probe aegis128l "$K16"

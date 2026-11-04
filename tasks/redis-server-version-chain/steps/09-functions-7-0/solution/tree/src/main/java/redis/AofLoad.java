package redis;

import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.file.*;

/**
 * Replay one AOF file into the dataset (aof.c loadSingleAppendOnlyFile()).
 *
 * The file is either an RDB snapshot (a BASE file written with
 * aof-use-rdb-preamble, or an old style AOF with an RDB preamble followed by a
 * RESP tail), or a plain RESP command stream, possibly carrying `#TS:`
 * annotations.  The parser reproduces redis's own error handling: a malformed
 * header is a "Bad file format" fatal error, a command cut in half is an
 * unexpected end of file, which aof-load-truncated turns into a truncation of
 * the file at the end of the last well formed command.
 */
public final class AofLoad {

    /** aof.c loadSingleAppendOnlyFile(): returns one of the Aof.AOF_* codes. */
    public static int loadSingle(Aof aof, String filename) {
        Server s = aof.s;
        Path path = aof.dirFile(filename);
        if (!Files.exists(path)) {
            s.logWarn("The append log file " + filename + " doesn't exist: No such file or directory");
            return Aof.AOF_NOT_EXIST;
        }
        byte[] data;
        try {
            data = Files.readAllBytes(path);
        } catch (IOException e) {
            s.logWarn("Fatal error: can't open the append log file " + filename
                    + " for reading: " + e.getMessage());
            return Aof.AOF_OPEN_ERR;
        }
        if (data.length == 0) return Aof.AOF_EMPTY;

        int oldState = aof.state;
        aof.state = Aof.OFF;                 /* never feed what we are replaying */
        try {
            return replay(s, aof, data, path, filename);
        } finally {
            aof.state = oldState;
        }
    }

    private static int replay(Server s, Aof aof, byte[] data, Path file, String filename) {
        Client fake = new Client();
        fake.fake = true;
        fake.db = 0;
        boolean truncated = s.config.getBool("aof-load-truncated");
        int p = 0;
        int validUpTo = 0;
        int validBeforeMulti = 0;
        final int n = data.length;

        /* An RDB snapshot: either the whole BASE file, or the preamble of an
         * old style AOF whose RESP tail follows it. */
        if (n >= 5 && data[0] == 'R' && data[1] == 'E' && data[2] == 'D'
                && data[3] == 'I' && data[4] == 'S') {
            boolean oldStyle = filename.equals(aof.aofFilename());
            if (oldStyle) s.logNotice("Reading RDB preamble from AOF file...");
            else s.logNotice("Reading RDB base file on AOF loading...");
            try {
                p = Rdb.loadPreamble(s, data);
            } catch (Exception e) {
                if (oldStyle) {
                    s.logWarn("Error reading the RDB preamble of the AOF file " + filename
                            + ", AOF loading aborted");
                } else {
                    s.logWarn("Error reading the RDB base file " + filename
                            + ", AOF loading aborted");
                }
                return uxeof(s, file, filename, truncated, validUpTo);
            }
            if (oldStyle) s.logNotice("Reading the remaining AOF tail...");
            validUpTo = p;
        }

        while (true) {
            if (p >= n) break;                        /* clean EOF */
            if (data[p] == '#') {                     /* annotation, skip the line */
                int e = findEol(data, p, n);
                p = (e < 0) ? n : e + 2;
                continue;
            }
            if (data[p] != '*') return fmterr(s, filename);
            int eol = findEol(data, p, n);
            if (eol < 0) return uxeof(s, file, filename, truncated, validUpTo);
            if (eol == p + 1) return uxeof(s, file, filename, truncated, validUpTo);
            int argc = (int) atoi(data, p + 1, eol);
            if (argc < 1) return fmterr(s, filename);
            p = eol + 2;

            byte[][] argv = new byte[argc][];
            boolean short_ = false;
            for (int j = 0; j < argc; j++) {
                if (p >= n) { short_ = true; break; }
                if (data[p] != '$') return fmterr(s, filename);
                int e2 = findEol(data, p, n);
                if (e2 < 0) { short_ = true; break; }
                long len = atoi(data, p + 1, e2);
                if (len < 0) return fmterr(s, filename);
                p = e2 + 2;
                if (p + len > n) { short_ = true; break; }
                argv[j] = new byte[(int) len];
                System.arraycopy(data, p, argv[j], 0, (int) len);
                p += len;
                if (p + 2 > n) { short_ = true; break; }
                p += 2;                                /* discard CRLF */
            }
            if (short_) return uxeof(s, file, filename, truncated, validUpTo);

            Cmd cmd = CommandTable.lookup(Util.lower(argv[0]));
            if (cmd == null) {
                s.logWarn("Unknown command '" + Util.s(argv[0])
                        + "' reading the append only file " + filename);
                return Aof.AOF_FAILED;
            }

            if (Util.equalsIgnoreCase(argv[0], "multi")) validBeforeMulti = validUpTo;

            fake.argv = argv;
            fake.argc = argc;
            fake.out.clear();
            if (fake.inMulti && !Util.equalsIgnoreCase(argv[0], "exec")) {
                fake.multiQueue.add(argv);
            } else {
                try {
                    cmd.handler.run(fake);
                } catch (RedisError ignored) {
                } catch (Exception ignored) {
                }
            }
            fake.out.clear();
            validUpTo = p;
        }

        /* EOF reached in the middle of a MULTI block. */
        if (fake.inMulti) {
            s.logWarn("Revert incomplete MULTI/EXEC transaction in AOF file " + filename);
            return uxeof(s, file, filename, truncated, validBeforeMulti);
        }
        fake.out.clear();
        return Aof.AOF_OK;
    }

    private static int fmterr(Server s, String filename) {
        s.logWarn("Bad file format reading the append only file " + filename
                + ": make a backup of your AOF file, then use ./redis-check-aof --fix <filename.manifest>");
        return Aof.AOF_FAILED;
    }

    /** aof.c uxeof: truncate when aof-load-truncated is on, otherwise fail. */
    private static int uxeof(Server s, Path file, String filename, boolean truncated, int validUpTo) {
        if (truncated) {
            s.logWarn("!!! Warning: short read while loading the AOF file " + filename + "!!!");
            s.logWarn("!!! Truncating the AOF " + filename + " at offset " + validUpTo + " !!!");
            boolean ok = false;
            try (FileChannel ch = FileChannel.open(file, StandardOpenOption.WRITE)) {
                ch.truncate(validUpTo);
                ok = true;
            } catch (IOException e) {
                s.logWarn("Error truncating the AOF file " + filename + ": " + e.getMessage());
            }
            if (ok) {
                s.logWarn("AOF " + filename + " loaded anyway because aof-load-truncated is enabled");
                return Aof.AOF_TRUNCATED;
            }
        }
        s.logWarn("Unexpected end of file reading the append only file " + filename + ". You can: "
                + "1) Make a backup of your AOF file, then use ./redis-check-aof --fix <filename.manifest>. "
                + "2) Alternatively you can set the 'aof-load-truncated' configuration option to yes "
                + "and restart the server.");
        return Aof.AOF_FAILED;
    }

    private static int findEol(byte[] a, int from, int n) {
        for (int i = from; i + 1 < n; i++) if (a[i] == '\r' && a[i + 1] == '\n') return i;
        return -1;
    }

    private static long atoi(byte[] a, int from, int to) {
        long v = 0;
        boolean neg = false;
        int i = from;
        if (i < to && (a[i] == '-' || a[i] == '+')) { neg = a[i] == '-'; i++; }
        for (; i < to; i++) {
            int c = a[i] & 0xff;
            if (c < '0' || c > '9') break;
            v = v * 10 + (c - '0');
        }
        return neg ? -v : v;
    }
}

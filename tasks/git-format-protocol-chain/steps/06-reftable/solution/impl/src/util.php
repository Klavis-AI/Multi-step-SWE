<?php
// Byte, path, zlib and identity helpers shared by every command.

/**
 * Inside a guarded region -- a lazy fetch from a promisor remote, say -- a
 * fatal error is an exception the caller handles, not the end of the process.
 */
class GitDie extends RuntimeException {
    public int $exitCode;
    public function __construct(string $msg, int $code) { parent::__construct($msg); $this->exitCode = $code; }
}
$GIT_GUARD_DEPTH = 0;

function die_git(string $msg, int $code = 128) {
    global $GIT_GUARD_DEPTH;
    if ($GIT_GUARD_DEPTH > 0) throw new GitDie($msg, $code);
    fwrite(STDERR, "fatal: $msg\n");
    out_flush();
    exit($code);
}

/** Run $fn with fatal errors turned into a null result. */
function git_guard(callable $fn) {
    global $GIT_GUARD_DEPTH;
    $GIT_GUARD_DEPTH++;
    try {
        return $fn();
    } catch (Throwable $e) {
        return null;
    } finally {
        $GIT_GUARD_DEPTH--;
    }
}
function err_git(string $msg) { fwrite(STDERR, "error: $msg\n"); }
function warn_git(string $msg) { fwrite(STDERR, "warning: $msg\n"); }

function usage_die(string $msg, int $code = 129) {
    fwrite(STDERR, "$msg\n");
    exit($code);
}

/** zlib (RFC1950) deflate as git writes loose objects and pack entries. */
function zdeflate(string $data, int $level = 1): string {
    $r = gzcompress($data, $level);
    if ($r === false) die_git('deflate failed');
    return $r;
}
function zinflate(string $data) {
    $ctx = inflate_init(ZLIB_ENCODING_DEFLATE);
    if ($ctx === false) return false;
    $out = @inflate_add($ctx, $data, ZLIB_FINISH);
    if ($out === false) return false;
    return $out;
}

/** Inflate from a file handle; returns [data, compressed_bytes_consumed]. */
function zinflate_stream($fh, int $max_out = -1) {
    $ctx = inflate_init(ZLIB_ENCODING_DEFLATE);
    $out = '';
    $consumed = 0;
    $start = ftell($fh);
    while (true) {
        $chunk = fread($fh, 8192);
        if ($chunk === '' || $chunk === false) {
            $piece = @inflate_add($ctx, '', ZLIB_FINISH);
            if ($piece === false) return false;
            $out .= $piece;
            break;
        }
        $piece = @inflate_add($ctx, $chunk, ZLIB_NO_FLUSH);
        if ($piece === false) return false;
        $out .= $piece;
        $status = inflate_get_status($ctx);
        if ($status === ZLIB_STREAM_END) {
            $consumed = inflate_get_read_len($ctx);
            fseek($fh, $start + $consumed, SEEK_SET);
            return [$out, $consumed];
        }
        if ($max_out >= 0 && strlen($out) > $max_out + 64) return false;
    }
    $consumed = inflate_get_read_len($ctx);
    fseek($fh, $start + $consumed, SEEK_SET);
    return [$out, $consumed];
}

function is_hex(string $s): bool { return $s !== '' && preg_match('/^[0-9a-f]+$/', $s) === 1; }

/** Normalize a path lexically (no filesystem access): a/./b, a/b/../c, //. */
function path_normalize(string $p): string {
    $abs = ($p !== '' && $p[0] === '/');
    $parts = [];
    foreach (explode('/', $p) as $seg) {
        if ($seg === '' || $seg === '.') continue;
        if ($seg === '..') {
            if ($parts && end($parts) !== '..') array_pop($parts);
            elseif (!$abs) $parts[] = '..';
            continue;
        }
        $parts[] = $seg;
    }
    $r = implode('/', $parts);
    return $abs ? '/' . $r : ($r === '' ? '.' : $r);
}

function path_absolute(string $p): string {
    if ($p === '') return path_normalize(getcwd());
    if ($p[0] === '/') return path_normalize($p);
    return path_normalize(getcwd() . '/' . $p);
}

/** Realpath that tolerates a missing final component. */
function path_real(string $p): string {
    $r = @realpath($p);
    if ($r !== false) return $r;
    $dir = dirname($p);
    $base = basename($p);
    $rd = @realpath($dir);
    if ($rd === false) return path_absolute($p);
    return rtrim($rd, '/') . '/' . $base;
}

function mkdir_p(string $dir, int $mode = 0777) {
    if (is_dir($dir)) return true;
    return @mkdir($dir, $mode, true) || is_dir($dir);
}

function rm_rf(string $path) {
    if (is_link($path) || (file_exists($path) && !is_dir($path))) { @unlink($path); return; }
    if (!is_dir($path)) return;
    foreach (scandir($path) as $e) {
        if ($e === '.' || $e === '..') continue;
        rm_rf("$path/$e");
    }
    @rmdir($path);
}

/** Write a file through a temp file + rename, as git does. */
function write_file_atomic(string $path, string $data, int $mode = 0666): bool {
    $dir = dirname($path);
    if (!mkdir_p($dir)) return false;
    $tmp = $dir . '/.tmp-' . getmypid() . '-' . bin2hex(random_bytes(4));
    if (@file_put_contents($tmp, $data) !== strlen($data)) { @unlink($tmp); return false; }
    @chmod($tmp, $mode & ~umask_value());
    if (!@rename($tmp, $path)) { @unlink($tmp); return false; }
    return true;
}

function umask_value(): int {
    static $u = null;
    if ($u === null) { $u = umask(); umask($u); }
    return $u;
}

/** git's "Name <email> timestamp tz" identity line. */
function ident_string(string $name, string $email, int $ts, string $tz): string {
    return "$name <$email> $ts $tz";
}

function tz_offset_string(int $ts): string {
    $tzenv = getenv('TZ');
    $dt = new DateTime('@' . $ts);
    $dt->setTimezone(new DateTimeZone($tzenv !== false && $tzenv !== '' ? @timezone_name_from_abbr('', 0, 0) ?: date_default_timezone_get() : date_default_timezone_get()));
    $off = $dt->getOffset();
    $sign = $off < 0 ? '-' : '+';
    $off = abs($off);
    return sprintf('%s%02d%02d', $sign, intdiv($off, 3600), intdiv($off % 3600, 60));
}

/** Parse a git date string (subset: "@ts tz", ISO-ish, RFC2822-ish). */
function parse_date(string $s) {
    $s = trim($s);
    if ($s === '') return false;
    if (preg_match('/^@?(-?\d+)\s+([+-]\d{4})$/', $s, $m)) return [(int)$m[1], $m[2]];
    if (preg_match('/^@(-?\d+)$/', $s, $m)) return [(int)$m[1], '+0000'];
    $ts = @strtotime($s);
    if ($ts === false) return false;
    $tz = '+0000';
    if (preg_match('/([+-]\d{4})\s*$/', $s, $m)) $tz = $m[1];
    else {
        $d = new DateTime($s);
        $off = $d->getOffset();
        $sign = $off < 0 ? '-' : '+'; $a = abs($off);
        $tz = sprintf('%s%02d%02d', $sign, intdiv($a, 3600), intdiv($a % 3600, 60));
    }
    return [$ts, $tz];
}

function now_tz(): string {
    $off = (new DateTime('now'))->getOffset();
    $sign = $off < 0 ? '-' : '+'; $a = abs($off);
    return sprintf('%s%02d%02d', $sign, intdiv($a, 3600), intdiv($a % 3600, 60));
}

/** Big-endian helpers (PHP has no unsigned ints; keep everything in int64). */
function be32(int $v): string { return pack('N', $v & 0xffffffff); }
function be16(int $v): string { return pack('n', $v & 0xffff); }
function be64(int $v): string { return pack('J', $v); }
function rd32(string $s, int $off): int { return unpack('N', substr($s, $off, 4))[1]; }
function rd16(string $s, int $off): int { return unpack('n', substr($s, $off, 2))[1]; }
function rd64(string $s, int $off): int {
    $hi = rd32($s, $off); $lo = rd32($s, $off + 4);
    return ($hi << 32) | $lo;
}

function fread_exact($fh, int $n): string {
    $buf = '';
    while (strlen($buf) < $n) {
        $chunk = fread($fh, $n - strlen($buf));
        if ($chunk === '' || $chunk === false) break;
        $buf .= $chunk;
    }
    return $buf;
}

/** Terminal/locale independent output. */
/**
 * Standard output is buffered the way C stdio buffers a pipe: nothing reaches
 * the pipe until the buffer fills or the process ends, so a run that writes to
 * both streams interleaves them the way git does.  A terminal is line
 * buffered, and anything that has to reach a peer immediately calls
 * out_flush() itself.
 */
$GIT_OUT_BUF = '';
$GIT_OUT_TTY = null;

function out(string $s) {
    global $GIT_OUT_BUF, $GIT_OUT_TTY;
    if ($GIT_OUT_TTY === null) $GIT_OUT_TTY = (function_exists('posix_isatty') && @posix_isatty(STDOUT));
    if ($GIT_OUT_TTY) { fwrite(STDOUT, $s); return; }
    $GIT_OUT_BUF .= $s;
    if (strlen($GIT_OUT_BUF) >= 8192) out_flush();
}

function out_flush() {
    global $GIT_OUT_BUF;
    if ($GIT_OUT_BUF !== '') { fwrite(STDOUT, $GIT_OUT_BUF); $GIT_OUT_BUF = ''; }
    @fflush(STDOUT);
}

function env_bool(string $name, bool $default = false): bool {
    $v = getenv($name);
    if ($v === false || $v === '') return $default;
    $v = strtolower($v);
    if ($v === '0' || $v === 'false' || $v === 'no' || $v === 'off') return false;
    return true;
}

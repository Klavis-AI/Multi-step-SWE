<?php
// reftable.php — the reftable ref backend (gitformat-reftable(5)) and the
// files<->reftable migration.  Plugs in behind RefStore.  A reftable stack is
// $GIT_DIR/reftable/{tables.list, *.ref}; each table is the binary format with
// the 'REFT' header, ref/obj/log blocks, restart tables, prefix compression,
// varints and a CRC32 footer.  Tables written here are unaligned single-block
// where they fit and aligned multi-block with a ref index when they do not;
// both shapes are readable by real git per the spec.

// ---- low-level encoding (network byte order, git's ofs-delta varint) --------

function rt_be24(int $v): string {
    return chr(($v >> 16) & 0xff) . chr(($v >> 8) & 0xff) . chr($v & 0xff);
}
function rt_rd24(string $s, int $o): int {
    return (ord($s[$o]) << 16) | (ord($s[$o + 1]) << 8) | ord($s[$o + 2]);
}

/** git put_var_int (reftable/record.c): little end first, filled from the back. */
function rt_varint(int $value): string {
    $buf = array_fill(0, 10, 0);
    $pos = 9;
    $buf[9] = $value & 0x7f;
    while (($value = $value >> 7) != 0) {
        $value -= 1;
        $pos -= 1;
        $buf[$pos] = 0x80 | ($value & 0x7f);
    }
    $out = '';
    for ($i = $pos; $i < 10; $i++) $out .= chr($buf[$i]);
    return $out;
}

/** Decoder from gitformat-reftable(5). Advances $pos past the varint. */
function rt_get_varint(string $s, int &$pos): int {
    $b = ord($s[$pos]);
    $val = $b & 0x7f;
    while ($b & 0x80) {
        $pos++;
        $b = ord($s[$pos]);
        $val = (($val + 1) << 7) | ($b & 0x7f);
    }
    $pos++;
    return $val;
}

function rt_common_prefix(string $a, string $b): int {
    $n = min(strlen($a), strlen($b));
    $i = 0;
    while ($i < $n && $a[$i] === $b[$i]) $i++;
    return $i;
}
function rt_encode_string(string $s): string { return rt_varint(strlen($s)) . $s; }

/** reverse_int64(t) = 0xffff...ffff - t == bitwise complement of the 8 BE bytes. */
function rt_reverse_int64(int $ui): string {
    $b = pack('J', $ui);
    $out = '';
    for ($i = 0; $i < 8; $i++) $out .= chr(0xff ^ ord($b[$i]));
    return $out;
}
function rt_hash_id_bytes(string $algo): int {
    return 0x73686131; /* 'sha1' -- SHA-1 only build */
}

// ---- table writer -----------------------------------------------------------

class ReftableWriter {
    /**
     * Build one reftable file.
     *   $refRecs: sorted-by-name list of
     *     ['name','type'(0|1|2|3),'oid'?,'peeled'?,'target'?,'ui']
     *   $logRecs: sorted-by-key list of
     *     ['name','ui','type'(0|1),'old','new','cname','email','time','tz','msg']
     * Returns the file bytes.
     */
    public static function build(array $refRecs, array $logRecs, int $minUI, int $maxUI,
                                 int $rawsz, int $version = 1, string $algo = 'sha1'): string {
        $blockSize = self::pickBlockSize($refRecs, $rawsz);
        $hdr = 'REFT' . chr($version) . rt_be24($blockSize) . be64($minUI) . be64($maxUI);
        if ($version === 2) $hdr .= be32(rt_hash_id_bytes($algo));
        $hdrSize = strlen($hdr);

        // Ref section: first block shares the file header (header_off = hdrSize).
        $refEntries = [];
        foreach ($refRecs as $r) {
            $val = rt_varint($r['ui'] - $minUI);
            switch ($r['type']) {
                case 1: $val .= hex2bin($r['oid']); break;
                case 2: $val .= hex2bin($r['oid']) . hex2bin($r['peeled']); break;
                case 3: $val .= rt_encode_string($r['target']); break;
                case 0: break; // deletion tombstone: no value
            }
            $refEntries[] = [$r['name'], $val, $r['type']];
        }

        $file = $hdr;
        $refIndexPos = 0; $logPos = 0; $logIndexPos = 0;

        if ($refEntries) {
            [$refBytes, $refIndex] = self::buildAligned('r', $refEntries, $blockSize, 0, $hdrSize);
            // buildAligned reserved $hdrSize leading zero bytes for the header.
            $file = $hdr . substr($refBytes, $hdrSize);
            $nRefBlocks = count($refIndex);
            if ($nRefBlocks >= 2) {
                $refIndexPos = strlen($file);
                $idxEntries = [];
                foreach ($refIndex as [$lastKey, $absPos]) {
                    $idxEntries[] = [$lastKey, rt_varint($absPos), 0];
                }
                [$idxBytes, ] = self::buildAligned('i', $idxEntries, $blockSize, $refIndexPos, 0);
                $file .= $idxBytes;
            }
        }

        if ($logRecs) {
            $logPos = strlen($file);
            $file .= self::buildLogBlock($logRecs, $minUI, $rawsz);
        }

        // Footer
        $footer = $hdr
            . be64($refIndexPos)
            . be64((0 << 5) | 0)      // obj_position / obj_id_len: no obj blocks
            . be64(0)                  // obj_index_position
            . be64($logPos)
            . be64($logIndexPos);
        $footer .= be32(crc32($footer) & 0xffffffff);
        $file .= $footer;
        return $file;
    }

    private static function pickBlockSize(array $refRecs, int $rawsz): int {
        $max = 0;
        foreach ($refRecs as $r) {
            $sz = strlen($r['name']) + 2 * $rawsz + strlen($r['target'] ?? '') + 16;
            if ($sz > $max) $max = $sz;
        }
        $bs = 4096;
        while ($bs - 64 < $max) $bs <<= 1;   // block must exceed the longest record
        return min($bs, 0xffffff);
    }

    /**
     * Aligned block section: each non-log block is padded to $blockSize.
     * The first block reserves $headerOff bytes for a shared file header.
     * Returns [bytes, [[lastKey, absBlockPos], ...]].
     */
    private static function buildAligned(string $type, array $entries, int $blockSize,
                                         int $fileStart, int $headerOff0): array {
        $out = '';
        $index = [];
        $i = 0; $n = count($entries); $blockIdx = 0;
        while ($i < $n) {
            $headerOff = ($blockIdx === 0) ? $headerOff0 : 0;
            $recs = ''; $restarts = []; $lastKey = ''; $cnt = 0; $blockLast = '';
            while ($i < $n) {
                [$key, $val, $extra] = $entries[$i];
                $isRestart = ($cnt % 16 === 0);
                $prev = $isRestart ? '' : $lastKey;
                $pfx = rt_common_prefix($prev, $key);
                $sfx = substr($key, $pfx);
                $rec = rt_varint($pfx) . rt_varint((strlen($sfx) << 3) | $extra) . $sfx . $val;
                $recOff = $headerOff + 4 + strlen($recs);
                $addR = $isRestart ? 1 : 0;
                $need = $headerOff + 4 + strlen($recs) + strlen($rec)
                        + 3 * (count($restarts) + $addR) + 2;
                if ($cnt > 0 && $need > $blockSize) break;
                if ($isRestart) $restarts[] = $recOff;
                $recs .= $rec; $lastKey = $key; $blockLast = $key; $cnt++; $i++;
            }
            $body = $recs;
            foreach ($restarts as $r) $body .= rt_be24($r);
            $body .= be16(count($restarts));
            $blockLen = $headerOff + 4 + strlen($body);
            $blk = str_repeat("\0", $headerOff) . $type . rt_be24($blockLen) . $body;
            if (strlen($blk) < $blockSize) $blk .= str_repeat("\0", $blockSize - strlen($blk));
            $index[] = [$blockLast, $fileStart + strlen($out)];
            $out .= $blk;
            $blockIdx++;
        }
        return [$out, $index];
    }

    /** A single deflated log block ('g'); block_len is the inflated size incl the 4-byte header. */
    private static function buildLogBlock(array $logRecs, int $minUI, int $rawsz): string {
        $recs = ''; $restarts = []; $lastKey = ''; $cnt = 0;
        foreach ($logRecs as $r) {
            $key = $r['name'] . "\0" . rt_reverse_int64($r['ui']);
            $extra = $r['type'];
            if ($r['type'] === 0) {
                $val = '';
            } else {
                $tz = $r['tz'];
                $val = hex2bin($r['old']) . hex2bin($r['new'])
                     . rt_encode_string($r['cname']) . rt_encode_string($r['email'])
                     . rt_varint($r['time']) . pack('n', $tz & 0xffff)
                     . rt_encode_string($r['msg']);
            }
            $isRestart = ($cnt % 16 === 0);
            $prev = $isRestart ? '' : $lastKey;
            $pfx = rt_common_prefix($prev, $key);
            $sfx = substr($key, $pfx);
            $rec = rt_varint($pfx) . rt_varint((strlen($sfx) << 3) | $extra) . $sfx . $val;
            if ($isRestart) $restarts[] = 4 + strlen($recs);
            $recs .= $rec; $lastKey = $key; $cnt++;
        }
        $body = $recs;
        foreach ($restarts as $r) $body .= rt_be24($r);
        $body .= be16(count($restarts));
        $blockLen = 4 + strlen($body);          // inflated size including the 4-byte header
        return 'g' . rt_be24($blockLen) . gzcompress($body, 9);
    }
}

// ---- table reader -----------------------------------------------------------

class ReftableReader {
    /**
     * Parse one reftable file.  Returns
     *   ['refs'=>[name=>rec], 'logs'=>[ [name,ui,type,old,new,ident,msg], ... ],
     *    'minUI'=>int,'maxUI'=>int,'version'=>int,'rawsz'=>int]
     * where a ref rec is ['name','type','ui','oid','peeled','target'].
     */
    public static function parse(string $data): array {
        if (strlen($data) < 4 || substr($data, 0, 4) !== 'REFT') {
            throw new RuntimeException('not a reftable');
        }
        $version = ord($data[4]);
        $blockSize = rt_rd24($data, 5);
        $minUI = rd64($data, 8);
        $maxUI = rd64($data, 16);
        $hdrSize = ($version === 2) ? 28 : 24;
        $rawsz = 20;
        if ($version === 2) { $rawsz = (rd32($data, 24) === 0x73323536) ? 32 : 20; }

        $footerSize = ($version === 2) ? 72 : 68;
        $flen = strlen($data);
        $foff = $flen - $footerSize;
        $refIndexPos = rd64($data, $foff + $hdrSize);
        $objField    = rd64($data, $foff + $hdrSize + 8);
        $objPos      = $objField >> 5;
        $objIndexPos = rd64($data, $foff + $hdrSize + 16);
        $logPos      = rd64($data, $foff + $hdrSize + 24);
        $logIndexPos = rd64($data, $foff + $hdrSize + 32);

        // Rawsz can still be 20/32 for v1 (hash unknown from header): infer from a
        // ref value later if needed; sha1 is the safe default and all v1 tables
        // we and the pinned peers write for sha1 use 20-byte ids.

        $refEnd = $foff;
        foreach ([$refIndexPos, $objPos, $objIndexPos, $logPos, $logIndexPos] as $p) {
            if ($p > 0 && $p < $refEnd) $refEnd = $p;
        }

        $firstType = $data[$hdrSize];
        $refs = [];
        if ($firstType === 'r') {
            $pos = 0; $blockIdx = 0;
            while ($pos < $refEnd) {
                $headerOff = ($blockIdx === 0) ? $hdrSize : 0;
                if ($pos + $headerOff >= $flen) break;
                $type = $data[$pos + $headerOff];
                if ($type !== 'r') break;
                $blockLen = rt_rd24($data, $pos + $headerOff + 1);
                self::parseRecordBlock($data, $pos, $headerOff, $blockLen, $rawsz, $minUI, $refs);
                $adv = ($blockSize > 0) ? $blockSize : $blockLen;
                if ($adv <= 0) break;
                $pos += $adv;
                $blockIdx++;
            }
        }

        $logs = [];
        if ($logPos > 0 || $firstType === 'g') {
            $starts = [];
            if ($logIndexPos > 0) {
                $starts = self::indexPositions($data, $logIndexPos, $blockSize, $hdrSize);
            } else {
                $starts = [$logPos > 0 ? $logPos : $hdrSize];
            }
            foreach ($starts as $bp) self::parseLogBlock($data, $bp, $rawsz, $minUI, $logs);
        }

        return ['refs' => $refs, 'logs' => $logs, 'minUI' => $minUI,
                'maxUI' => $maxUI, 'version' => $version, 'rawsz' => $rawsz];
    }

    /** Parse a ref/index/obj record block (linear scan; restart table skipped). */
    private static function parseRecordBlock(string $data, int $pos, int $headerOff,
                                             int $blockLen, int $rawsz, int $minUI, array &$refs): void {
        $rc = rd16($data, $pos + $blockLen - 2);
        $recEnd = $pos + $blockLen - 2 - 3 * $rc;
        $p = $pos + $headerOff + 4;
        $prevKey = '';
        while ($p < $recEnd) {
            $prefix = rt_get_varint($data, $p);
            $x = rt_get_varint($data, $p);
            $suffixLen = $x >> 3;
            $type = $x & 0x7;
            $suffix = substr($data, $p, $suffixLen); $p += $suffixLen;
            $name = substr($prevKey, 0, $prefix) . $suffix;
            $prevKey = $name;
            $uiDelta = rt_get_varint($data, $p);
            $ui = $minUI + $uiDelta;
            $rec = ['name' => $name, 'type' => $type, 'ui' => $ui,
                    'oid' => null, 'peeled' => null, 'target' => null];
            if ($type === 1) {
                $rec['oid'] = bin2hex(substr($data, $p, $rawsz)); $p += $rawsz;
            } elseif ($type === 2) {
                $rec['oid'] = bin2hex(substr($data, $p, $rawsz)); $p += $rawsz;
                $rec['peeled'] = bin2hex(substr($data, $p, $rawsz)); $p += $rawsz;
            } elseif ($type === 3) {
                $tl = rt_get_varint($data, $p);
                $rec['target'] = substr($data, $p, $tl); $p += $tl;
            }
            // type 0: deletion, no value
            $refs[$name] = $rec;
        }
    }

    /** Read index records from an 'i' block, returning the block_position values. */
    private static function indexPositions(string $data, int $pos, int $blockSize, int $hdrSize): array {
        $out = [];
        // index blocks may themselves be multi-level; follow one level of leaves.
        $headerOff = 0;
        $type = $data[$pos];
        if ($type !== 'i') return $out;
        $blockLen = rt_rd24($data, $pos + 1);
        $rc = rd16($data, $pos + $blockLen - 2);
        $recEnd = $pos + $blockLen - 2 - 3 * $rc;
        $p = $pos + 4;
        $prevKey = '';
        while ($p < $recEnd) {
            $prefix = rt_get_varint($data, $p);
            $x = rt_get_varint($data, $p);
            $suffixLen = $x >> 3;
            $suffix = substr($data, $p, $suffixLen); $p += $suffixLen;
            $prevKey = substr($prevKey, 0, $prefix) . $suffix;
            $blockPos = rt_get_varint($data, $p);
            $out[] = $blockPos;
        }
        return $out;
    }

    /** Inflate and parse one log block, appending records to $logs. */
    private static function parseLogBlock(string $data, int $pos, int $rawsz, int $minUI, array &$logs): void {
        if ($pos + 4 > strlen($data) || $data[$pos] !== 'g') return;
        $blockLen = rt_rd24($data, $pos + 1);
        $ctx = inflate_init(ZLIB_ENCODING_DEFLATE);
        $body = @inflate_add($ctx, substr($data, $pos + 4), ZLIB_NO_FLUSH);
        if ($body === false) return;
        $body = substr($body, 0, $blockLen - 4);
        $rc = rd16($body, strlen($body) - 2);
        $recEnd = strlen($body) - 2 - 3 * $rc;
        $p = 0; $prevKey = '';
        while ($p < $recEnd) {
            $prefix = rt_get_varint($body, $p);
            $x = rt_get_varint($body, $p);
            $suffixLen = $x >> 3;
            $logType = $x & 0x7;
            $suffix = substr($body, $p, $suffixLen); $p += $suffixLen;
            $key = substr($prevKey, 0, $prefix) . $suffix;
            $prevKey = $key;
            // key = name \0 reverse_int64(ui) : name is all but the last 9 bytes
            $klen = strlen($key);
            $name = substr($key, 0, $klen - 9);
            $rev = substr($key, $klen - 8);
            $ui = 0;
            for ($i = 0; $i < 8; $i++) $ui = ($ui << 8) | (0xff ^ ord($rev[$i]));
            if ($logType === 0) {
                $logs[] = ['name' => $name, 'ui' => $ui, 'type' => 0,
                           'old' => null, 'new' => null, 'ident' => null, 'msg' => ''];
                continue;
            }
            $old = bin2hex(substr($body, $p, $rawsz)); $p += $rawsz;
            $new = bin2hex(substr($body, $p, $rawsz)); $p += $rawsz;
            $nl = rt_get_varint($body, $p); $cname = substr($body, $p, $nl); $p += $nl;
            $el = rt_get_varint($body, $p); $email = substr($body, $p, $el); $p += $el;
            $time = rt_get_varint($body, $p);
            $tz = unpack('n', substr($body, $p, 2))[1]; $p += 2;
            if ($tz >= 0x8000) $tz -= 0x10000;      // signed
            $ml = rt_get_varint($body, $p); $msg = substr($body, $p, $ml); $p += $ml;
            $logs[] = ['name' => $name, 'ui' => $ui, 'type' => 1, 'old' => $old, 'new' => $new,
                       'cname' => $cname, 'email' => $email, 'time' => $time, 'tz' => $tz, 'msg' => $msg];
        }
    }
}

// ---- the stack --------------------------------------------------------------

class ReftableStack {
    public Repo $repo;
    public string $dir;
    private ?array $tablesCache = null;
    private array $parsed = [];

    public function __construct(Repo $repo) {
        $this->repo = $repo;
        $this->dir = $repo->commonPath('reftable');
    }

    public function ensureDir(): void { mkdir_p($this->dir); }
    private function invalidate(): void { $this->tablesCache = null; $this->parsed = []; }

    public function tables(): array {
        if ($this->tablesCache !== null) return $this->tablesCache;
        $out = [];
        $c = @file_get_contents($this->dir . '/tables.list');
        if ($c !== false) {
            foreach (explode("\n", $c) as $l) { $l = trim($l); if ($l !== '') $out[] = $l; }
        }
        return $this->tablesCache = $out;
    }

    public function parseTable(string $name): array {
        if (isset($this->parsed[$name])) return $this->parsed[$name];
        $data = @file_get_contents($this->dir . '/' . $name);
        if ($data === false) {
            return $this->parsed[$name] = ['refs' => [], 'logs' => [], 'minUI' => 0, 'maxUI' => 0];
        }
        try { $p = ReftableReader::parse($data); }
        catch (Throwable $e) { $p = ['refs' => [], 'logs' => [], 'minUI' => 0, 'maxUI' => 0]; }
        return $this->parsed[$name] = $p;
    }

    public function maxUpdateIndex(): int {
        $m = 0;
        foreach ($this->tables() as $t) {
            $p = $this->parseTable($t);
            if ($p['maxUI'] > $m) $m = $p['maxUI'];
        }
        return $m;
    }

    /** Newest-first lookup; returns the ref record (possibly a tombstone) or null. */
    public function lookupRef(string $name): ?array {
        $tables = $this->tables();
        for ($i = count($tables) - 1; $i >= 0; $i--) {
            $p = $this->parseTable($tables[$i]);
            if (isset($p['refs'][$name])) return $p['refs'][$name];
        }
        return null;
    }

    /** Live refs (tombstones removed), name => record, oldest-table-first merge. */
    public function mergedRefs(): array {
        $merged = [];
        foreach ($this->tables() as $t) {
            foreach ($this->parseTable($t)['refs'] as $name => $rec) {
                if ($rec['type'] === 0) unset($merged[$name]);
                else $merged[$name] = $rec;
            }
        }
        return $merged;
    }

    /** All log records across the stack, newest table shadowing older per (name,ui). */
    public function mergedLogs(): array {
        $byKey = [];
        foreach ($this->tables() as $t) {
            foreach ($this->parseTable($t)['logs'] as $lr) {
                $byKey[$lr['name'] . "\0" . $lr['ui']] = $lr;
            }
        }
        return array_values($byKey);
    }

    private static function logKey(array $l): string {
        return $l['name'] . "\0" . rt_reverse_int64($l['ui']);
    }

    /** Append one new table (a transaction) to the stack. */
    public function append(array $refRecs, array $logRecs): void {
        if (!$refRecs && !$logRecs) return;
        $this->ensureDir();
        $ui = $this->maxUpdateIndex() + 1;
        foreach ($refRecs as &$r) $r['ui'] = $ui; unset($r);
        foreach ($logRecs as &$l) $l['ui'] = $ui; unset($l);
        usort($refRecs, fn($a, $b) => strcmp($a['name'], $b['name']));
        usort($logRecs, fn($a, $b) => strcmp(self::logKey($a), self::logKey($b)));
        $bytes = ReftableWriter::build($refRecs, $logRecs, $ui, $ui,
                                       $this->repo->rawsz, 1, $this->repo->hashAlgo);
        $ext = $refRecs ? '.ref' : '.log';
        $name = sprintf("0x%012x-0x%012x-%08x%s", $ui, $ui, mt_rand(0, 0x7fffffff), $ext);
        $this->writeTableFile($name, $bytes);
        $list = $this->tables();
        $list[] = $name;
        $this->writeList($list);
        $this->invalidate();
    }

    /** Merge the whole stack into a single table (git pack-refs / auto-compaction). */
    public function compact(): void {
        $refs = array_values($this->mergedRefs());
        $logs = $this->mergedLogs();
        if (!$refs && !$logs) return;
        $minU = PHP_INT_MAX; $maxU = 0;
        foreach ($refs as $r) { $minU = min($minU, $r['ui']); $maxU = max($maxU, $r['ui']); }
        foreach ($logs as $l) { $minU = min($minU, $l['ui']); $maxU = max($maxU, $l['ui']); }
        if ($minU === PHP_INT_MAX) $minU = 1;
        if ($maxU === 0) $maxU = $minU;
        usort($refs, fn($a, $b) => strcmp($a['name'], $b['name']));
        usort($logs, fn($a, $b) => strcmp(self::logKey($a), self::logKey($b)));
        $bytes = ReftableWriter::build($refs, $logs, $minU, $maxU,
                                       $this->repo->rawsz, 1, $this->repo->hashAlgo);
        $ext = $refs ? '.ref' : '.log';
        $name = sprintf("0x%012x-0x%012x-%08x%s", $minU, $maxU, mt_rand(0, 0x7fffffff), $ext);
        $old = $this->tables();
        $this->writeTableFile($name, $bytes);
        $this->writeList([$name]);
        foreach ($old as $o) if ($o !== $name) @unlink($this->dir . '/' . $o);
        $this->invalidate();
    }

    private function writeTableFile(string $name, string $bytes): void {
        $tmp = $this->dir . '/' . $name . '.tmp' . getmypid();
        file_put_contents($tmp, $bytes);
        @chmod($tmp, 0444);
        rename($tmp, $this->dir . '/' . $name);
    }
    private function writeList(array $list): void {
        $lock = $this->dir . '/tables.list.lock';
        file_put_contents($lock, $list ? implode("\n", $list) . "\n" : '');
        rename($lock, $this->dir . '/tables.list');
    }
}

// ---- the RefStore backend ---------------------------------------------------

class ReftableRefStore extends RefStore {
    public ReftableStack $stack;

    public function __construct(Repo $r) {
        parent::__construct($r);
        $this->stack = new ReftableStack($r);
    }

    public function format(): string { return 'reftable'; }

    public function readRaw(string $name): ?array {
        $rec = $this->stack->lookupRef($name);
        if ($rec === null || $rec['type'] === 0) return null;
        if ($rec['type'] === 3) return [str_repeat('0', $this->repo->hexsz), $rec['target']];
        return [$rec['oid'], null];
    }

    public function listRefs(string $prefix = ''): array {
        $out = [];
        foreach ($this->stack->mergedRefs() as $name => $rec) {
            if ($name === 'HEAD') continue;                     // HEAD is not listed by for-each-ref
            if ($prefix !== '' && strncmp($name, $prefix, strlen($prefix)) !== 0) continue;
            if ($rec['type'] === 3) {
                $res = $this->resolve($name);
                if ($res !== null) $out[$name] = $res;
            } else {
                $out[$name] = $rec['oid'];
            }
        }
        ksort($out, SORT_STRING);
        return $out;
    }

    private function peeledOf(?string $oid): ?string {
        if ($oid === null) return null;
        $obj = $this->repo->odb->read($oid);
        if ($obj === null || $obj[0] !== 'tag') return null;
        $peeled = TagObj::parse($obj[1])->object;
        while (true) {
            $x = $this->repo->odb->read($peeled);
            if ($x === null || $x[0] !== 'tag') break;
            $peeled = TagObj::parse($x[1])->object;
        }
        return $peeled;
    }

    private static function tzMinutes(string $tz): int {
        if (!preg_match('/^([+-])(\d{2})(\d{2})$/', $tz, $m)) return 0;
        $v = (int)$m[2] * 60 + (int)$m[3];
        return $m[1] === '-' ? -$v : $v;
    }
    private static function tzString(int $min): string {
        $sign = $min < 0 ? '-' : '+';
        $min = abs($min);
        return sprintf('%s%02d%02d', $sign, intdiv($min, 60), $min % 60);
    }

    private function makeLog(string $name, ?string $old, ?string $new, string $msg): ?array {
        $null = str_repeat('0', $this->repo->hexsz);
        $always = $this->repo->config->getBool('core.logallrefupdates', !$this->repo->isBare());
        if (!$this->reflogExists($name)) {
            if (!$always) return null;
            $ok = ($name === 'HEAD' || strncmp($name, 'refs/heads/', 11) === 0
                || strncmp($name, 'refs/remotes/', 13) === 0 || strncmp($name, 'refs/notes/', 11) === 0);
            if (!$ok) return null;
        }
        [$cn, $ce, $ts, $tz] = committer_ident($this->repo);
        return ['name' => $name, 'ui' => 0, 'type' => 1,
                'old' => $old ?? $null, 'new' => $new ?? $null,
                'cname' => $cn, 'email' => $ce, 'time' => $ts,
                'tz' => self::tzMinutes($tz), 'msg' => str_replace("\n", ' ', $msg)];
    }

    public function writeRef(string $name, ?string $oid, ?string $symTarget, ?string $oldOid,
                             string $reason, bool $deref = true): bool {
        $target = $name;
        if ($deref && $symTarget === null) {
            $raw = $this->readRaw($name);
            if ($raw !== null && $raw[1] !== null) {
                $chain = $this->resolveRefName($name);
                if ($chain !== null) $target = $chain;
            }
        }
        $old = $this->resolve($target);
        if ($oldOid !== null) {
            $nullOid = str_repeat('0', $this->repo->hexsz);
            if ($oldOid === $nullOid) {
                if ($old !== null) {
                    fwrite(STDERR, "fatal: cannot lock ref '$target': reference already exists\n");
                    return false;
                }
            } elseif ($old !== $oldOid) {
                $cur = $old === null ? 'unborn' : $old;
                fwrite(STDERR, "fatal: cannot lock ref '$target': is at $cur but expected $oldOid\n");
                return false;
            }
        }
        if ($symTarget !== null) {
            $rec = ['name' => $target, 'type' => 3, 'target' => $symTarget, 'ui' => 0];
        } else {
            $peeled = $this->peeledOf($oid);
            $rec = $peeled !== null
                ? ['name' => $target, 'type' => 2, 'oid' => $oid, 'peeled' => $peeled, 'ui' => 0]
                : ['name' => $target, 'type' => 1, 'oid' => $oid, 'ui' => 0];
        }
        $logs = [];
        if ($symTarget === null) {
            $lr = $this->makeLog($target, $old, $oid, $reason);
            if ($lr) $logs[] = $lr;
            if ($target !== $name) { $lr2 = $this->makeLog($name, $old, $oid, $reason); if ($lr2) $logs[] = $lr2; }
        }
        $this->stack->append([$rec], $logs);
        return true;
    }

    public function deleteRef(string $name, ?string $oldOid, string $reason, bool $deref = true): bool {
        $target = $name;
        $raw = $this->readRaw($name);
        if ($deref && $raw !== null && $raw[1] !== null) {
            $chain = $this->resolveRefName($name);
            if ($chain !== null) $target = $chain;
        }
        $old = $this->resolve($target);
        if ($oldOid !== null && $old !== $oldOid) {
            fwrite(STDERR, "error: cannot lock ref '$target': is at " . ($old ?? 'unborn') . " but expected $oldOid\n");
            return false;
        }
        $this->stack->append([['name' => $target, 'type' => 0, 'ui' => 0]], []);
        return true;
    }

    public function reflogRead(string $name): array {
        $recs = [];
        foreach ($this->stack->mergedLogs() as $lr) {
            if ($lr['name'] === $name && $lr['type'] === 1) $recs[] = $lr;
        }
        usort($recs, fn($a, $b) => $a['ui'] <=> $b['ui']);   // oldest first, like the files log
        $out = [];
        foreach ($recs as $lr) {
            $id = new Ident();
            $id->name = $lr['cname']; $id->email = $lr['email'];
            $id->ts = $lr['time']; $id->tz = self::tzString($lr['tz']);
            $id->raw = $lr['cname'] . ' <' . $lr['email'] . '> ' . $lr['time'] . ' ' . $id->tz;
            $out[] = ['old' => $lr['old'], 'new' => $lr['new'], 'ident' => $id, 'msg' => $lr['msg']];
        }
        return $out;
    }

    public function reflogExists(string $name): bool {
        foreach ($this->stack->mergedLogs() as $lr) if ($lr['name'] === $name) return true;
        return false;
    }
    public function reflogCreate(string $name) { /* logs appear when appended */ }
    public function reflogDelete(string $name) { /* handled by compaction/expire; out of scope */ }

    public function reflogAppend(string $name, ?string $old, ?string $new, string $msg) {
        $lr = $this->makeLog($name, $old, $new, $msg);
        if ($lr) $this->stack->append([], [$lr]);
    }
}

// ---- init / migration helpers ----------------------------------------------

/**
 * Lay down a fresh reftable repository: the stack with HEAD as a symref, and
 * the backward-compatibility dummy files (gitformat-reftable "Backward
 * compatibility").  Called from git-init after the config is written.
 */
function reftable_init_repo(string $gitdir, string $headTarget, string $algo = 'sha1'): void {
    $rt = $gitdir . '/reftable';
    mkdir_p($rt);
    $rawsz = 20;
    $version = 1;
    $rec = ['name' => 'HEAD', 'type' => 3, 'target' => $headTarget, 'ui' => 1];
    $bytes = ReftableWriter::build([$rec], [], 1, 1, $rawsz, $version, $algo);
    $name = sprintf("0x%012x-0x%012x-%08x.ref", 1, 1, mt_rand(0, 0x7fffffff));
    file_put_contents($rt . '/' . $name . '.tmp', $bytes);
    rename($rt . '/' . $name . '.tmp', $rt . '/' . $name);
    file_put_contents($rt . '/tables.list', $name . "\n");
    // dummy files so older clients still recognise the directory as a git repo
    if (is_dir($gitdir . '/refs/heads')) @rmdir($gitdir . '/refs/heads');
    if (is_dir($gitdir . '/refs/tags')) @rmdir($gitdir . '/refs/tags');
    file_put_contents($gitdir . '/HEAD', "ref: refs/heads/.invalid\n");
    file_put_contents($gitdir . '/refs/heads', "this repository uses the reftable format\n");
}

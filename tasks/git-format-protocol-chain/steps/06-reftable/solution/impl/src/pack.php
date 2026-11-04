<?php
// Packfiles and pack indexes (gitformat-pack(5)): reading v2 packs with both
// delta forms, writing packs and .idx v2, and the object lookup that layers
// packs over the loose object store.

class PackIndex {
    public string $packPath;
    public string $idxPath;
    public int $nobjects = 0;
    public int $version = 2;
    private $idxFh = null;
    private ?string $fanout = null;
    private ?string $oidTable = null;
    private array $offsetCache = [];
    public Repo $repo;
    private $packFh = null;
    private array $objCache = [];

    public function __construct(Repo $repo, string $packPath) {
        $this->repo = $repo;
        $this->packPath = $packPath;
        $this->idxPath = preg_replace('/\.pack$/', '.idx', $packPath);
    }

    public function open(): bool {
        if ($this->idxFh !== null) return true;
        $fh = @fopen($this->idxPath, 'rb');
        if ($fh === false) return false;
        $magic = fread_exact($fh, 4);
        if ($magic !== "\xfftOc") {
            // version 1 index: fanout directly, then 4-byte offset + oid pairs
            fclose($fh);
            return false;
        }
        $this->version = rd32(fread_exact($fh, 4), 0);
        $this->fanout = fread_exact($fh, 1024);
        $this->nobjects = rd32($this->fanout, 255 * 4);
        $this->oidTable = fread_exact($fh, $this->nobjects * $this->repo->rawsz);
        // gitformat-pack(5): the index ends with the checksum of the packfile
        // it describes, followed by its own checksum.  A pack that does not
        // match its index must not be used -- otherwise a truncated or
        // rewritten pack is read as if its offsets still meant something.
        if (!$this->packMatchesIdx($fh)) {
            fclose($fh);
            fwrite(STDERR, "error: packfile " . $this->packPath . " does not match index\n");
            return false;
        }
        $this->idxFh = $fh;
        return true;
    }

    /** Compare the pack checksum recorded in the index with the pack's own. */
    private function packMatchesIdx($fh): bool {
        $sz = $this->repo->rawsz;
        $idxSize = @filesize($this->idxPath);
        if ($idxSize === false || $idxSize < 8 + 1024 + 2 * $sz) return false;
        if (fseek($fh, $idxSize - 2 * $sz) !== 0) return false;
        $want = fread($fh, $sz);
        $packSize = @filesize($this->packPath);
        if ($packSize === false || $packSize < 12 + $sz) return false;
        $pfh = @fopen($this->packPath, 'rb');
        if ($pfh === false) return false;
        fseek($pfh, $packSize - $sz);
        $have = fread($pfh, $sz);
        fclose($pfh);
        return $want !== false && $have !== false && $want === $have;
    }

    private function fanoutStart(int $b): int { return $b === 0 ? 0 : rd32($this->fanout, ($b - 1) * 4); }
    private function fanoutEnd(int $b): int { return rd32($this->fanout, $b * 4); }

    /** Binary search the sorted object table. */
    public function find(string $oid): ?int {
        if (!$this->open()) return null;
        $raw = @hex2bin($oid);
        if ($raw === false || strlen($raw) !== $this->repo->rawsz) return null;
        $b = ord($raw[0]);
        $lo = $this->fanoutStart($b);
        $hi = $this->fanoutEnd($b);
        $sz = $this->repo->rawsz;
        while ($lo < $hi) {
            $mid = intdiv($lo + $hi, 2);
            $cand = substr($this->oidTable, $mid * $sz, $sz);
            $cmp = strcmp($cand, $raw);
            if ($cmp === 0) return $mid;
            if ($cmp < 0) $lo = $mid + 1; else $hi = $mid;
        }
        return null;
    }

    public function oidAt(int $i): string {
        return bin2hex(substr($this->oidTable, $i * $this->repo->rawsz, $this->repo->rawsz));
    }

    public function crcAt(int $i): int {
        $base = 8 + 1024 + $this->nobjects * $this->repo->rawsz;
        fseek($this->idxFh, $base + $i * 4);
        return rd32(fread_exact($this->idxFh, 4), 0);
    }

    public function offsetAt(int $i): int {
        if (isset($this->offsetCache[$i])) return $this->offsetCache[$i];
        $sz = $this->repo->rawsz;
        $base = 8 + 1024 + $this->nobjects * $sz + $this->nobjects * 4;
        fseek($this->idxFh, $base + $i * 4);
        $v = rd32(fread_exact($this->idxFh, 4), 0);
        if ($v & 0x80000000) {
            $big = $v & 0x7fffffff;
            fseek($this->idxFh, $base + $this->nobjects * 4 + $big * 8);
            $v = rd64(fread_exact($this->idxFh, 8), 0);
        }
        $this->offsetCache[$i] = $v;
        return $v;
    }

    public function allOids(): array {
        if (!$this->open()) return [];
        $r = [];
        for ($i = 0; $i < $this->nobjects; $i++) $r[] = $this->oidAt($i);
        return $r;
    }

    private function packHandle() {
        if ($this->packFh === null) {
            $this->packFh = @fopen($this->packPath, 'rb');
        }
        return $this->packFh;
    }

    /** Read the object stored at a pack offset, resolving deltas. */
    public function readAt(int $offset, int $depth = 0): ?array {
        if ($depth > 64) return null;
        if (isset($this->objCache[$offset])) return $this->objCache[$offset];
        $fh = $this->packHandle();
        if ($fh === false || $fh === null) return null;
        fseek($fh, $offset);
        [$type, $size, $hdrLen] = $this->readObjectHeader($fh);
        if ($type === null) return null;
        if ($type === 6) {  // OBJ_OFS_DELTA
            $neg = $this->readOffsetEncoding($fh);
            $baseOffset = $offset - $neg;
            $pos = ftell($fh);
            $base = $this->readAt($baseOffset, $depth + 1);
            if ($base === null) return null;
            fseek($fh, $pos);
            $delta = zinflate_stream($fh, $size);
            if ($delta === false) return null;
            $content = apply_delta($base[1], $delta[0]);
            $r = [$base[0], $content];
        } elseif ($type === 7) {  // OBJ_REF_DELTA
            $baseOid = bin2hex(fread_exact($fh, $this->repo->rawsz));
            $pos = ftell($fh);
            $base = $this->repo->odb->read($baseOid);
            if ($base === null) return null;
            fseek($fh, $pos);
            $delta = zinflate_stream($fh, $size);
            if ($delta === false) return null;
            $content = apply_delta($base[1], $delta[0]);
            $r = [$base[0], $content];
        } else {
            $data = zinflate_stream($fh, $size);
            if ($data === false) return null;
            $r = [pack_type_name($type), $data[0]];
        }
        if (count($this->objCache) < 256) $this->objCache[$offset] = $r;
        return $r;
    }

    /** Type and size, plus the raw type for delta detection. */
    public function readObjectHeader($fh): array {
        $start = ftell($fh);
        $b = fread($fh, 1);
        if ($b === '' || $b === false) return [null, 0, 0];
        $c = ord($b);
        $type = ($c >> 4) & 7;
        $size = $c & 15;
        $shift = 4;
        while ($c & 0x80) {
            $b = fread($fh, 1);
            if ($b === '' || $b === false) return [null, 0, 0];
            $c = ord($b);
            $size |= ($c & 0x7f) << $shift;
            $shift += 7;
        }
        return [$type, $size, ftell($fh) - $start];
    }

    public function readOffsetEncoding($fh): int {
        $c = ord(fread($fh, 1));
        $ofs = $c & 0x7f;
        while ($c & 0x80) {
            $ofs += 1;
            $c = ord(fread($fh, 1));
            $ofs = ($ofs << 7) + ($c & 0x7f);
        }
        return $ofs;
    }

    /** Raw entry info used by verify-pack: type, size, delta base. */
    public function entryInfo(int $i): ?array {
        $offset = $this->offsetAt($i);
        $fh = $this->packHandle();
        fseek($fh, $offset);
        [$type, $size, $hdrLen] = $this->readObjectHeader($fh);
        $baseOid = null;
        $depth = 0;
        if ($type === 6) {
            $neg = $this->readOffsetEncoding($fh);
            $baseOff = $offset - $neg;
            $baseIdx = $this->indexOfOffset($baseOff);
            $baseOid = $baseIdx === null ? null : $this->oidAt($baseIdx);
        } elseif ($type === 7) {
            $baseOid = bin2hex(fread_exact($fh, $this->repo->rawsz));
        }
        $next = $this->nextOffset($offset);
        return [
            'type' => $type, 'size' => $size, 'offset' => $offset,
            'base' => $baseOid, 'stored' => $next - $offset,
        ];
    }

    public function indexOfOffset(int $off): ?int {
        for ($i = 0; $i < $this->nobjects; $i++) if ($this->offsetAt($i) === $off) return $i;
        return null;
    }

    public function nextOffset(int $off): int {
        $best = filesize($this->packPath) - $this->repo->rawsz;
        for ($i = 0; $i < $this->nobjects; $i++) {
            $o = $this->offsetAt($i);
            if ($o > $off && $o < $best) $best = $o;
        }
        return $best;
    }
}

function pack_type_name(int $t): string {
    switch ($t) {
        case 1: return 'commit';
        case 2: return 'tree';
        case 3: return 'blob';
        case 4: return 'tag';
    }
    return 'unknown';
}
function pack_type_id(string $t): int {
    switch ($t) {
        case 'commit': return 1;
        case 'tree': return 2;
        case 'blob': return 3;
        case 'tag': return 4;
    }
    return 0;
}

/** Apply a git delta (gitformat-pack: "Deltified representation"). */
function apply_delta(string $base, string $delta): string {
    $pos = 0;
    [$srcSize, $pos] = delta_varint($delta, $pos);
    [$dstSize, $pos] = delta_varint($delta, $pos);
    $out = '';
    $n = strlen($delta);
    while ($pos < $n) {
        $c = ord($delta[$pos++]);
        if ($c & 0x80) {
            $cpOff = 0; $cpSize = 0;
            if ($c & 0x01) $cpOff = ord($delta[$pos++]);
            if ($c & 0x02) $cpOff |= ord($delta[$pos++]) << 8;
            if ($c & 0x04) $cpOff |= ord($delta[$pos++]) << 16;
            if ($c & 0x08) $cpOff |= ord($delta[$pos++]) << 24;
            if ($c & 0x10) $cpSize = ord($delta[$pos++]);
            if ($c & 0x20) $cpSize |= ord($delta[$pos++]) << 8;
            if ($c & 0x40) $cpSize |= ord($delta[$pos++]) << 16;
            if ($cpSize === 0) $cpSize = 0x10000;
            $out .= substr($base, $cpOff, $cpSize);
        } elseif ($c) {
            $out .= substr($delta, $pos, $c);
            $pos += $c;
        } else {
            break;
        }
    }
    return $out;
}

function delta_varint(string $s, int $pos): array {
    $v = 0; $shift = 0;
    do {
        $c = ord($s[$pos++]);
        $v |= ($c & 0x7f) << $shift;
        $shift += 7;
    } while ($c & 0x80);
    return [$v, $pos];
}

/** Encode a delta of $target against $base (copy/insert instructions). */
function make_delta(string $base, string $target): string {
    $out = delta_varint_encode(strlen($base)) . delta_varint_encode(strlen($target));
    // index the base by 16-byte blocks for a simple greedy matcher
    $blocks = [];
    $bs = 16;
    for ($i = 0; $i + $bs <= strlen($base); $i += $bs) {
        $blocks[substr($base, $i, $bs)][] = $i;
    }
    $i = 0;
    $pending = '';
    $tlen = strlen($target);
    while ($i < $tlen) {
        $match = null;
        if ($i + $bs <= $tlen) {
            $key = substr($target, $i, $bs);
            if (isset($blocks[$key])) {
                foreach ($blocks[$key] as $cand) {
                    $len = $bs;
                    while ($i + $len < $tlen && $cand + $len < strlen($base)
                           && $target[$i + $len] === $base[$cand + $len] && $len < 0xffffff) {
                        $len++;
                    }
                    if ($match === null || $len > $match[1]) $match = [$cand, $len];
                }
            }
        }
        if ($match !== null && $match[1] >= $bs) {
            if ($pending !== '') { $out .= delta_insert($pending); $pending = ''; }
            $out .= delta_copy($match[0], $match[1]);
            $i += $match[1];
            continue;
        }
        $pending .= $target[$i];
        $i++;
        if (strlen($pending) === 127) { $out .= delta_insert($pending); $pending = ''; }
    }
    if ($pending !== '') $out .= delta_insert($pending);
    return $out;
}

function delta_varint_encode(int $v): string {
    $s = '';
    do {
        $b = $v & 0x7f;
        $v >>= 7;
        $s .= chr($v ? ($b | 0x80) : $b);
    } while ($v);
    return $s;
}

function delta_copy(int $off, int $size): string {
    $cmd = 0x80;
    $data = '';
    for ($i = 0; $i < 4; $i++) {
        $byte = ($off >> (8 * $i)) & 0xff;
        if ($byte) { $cmd |= (1 << $i); $data .= chr($byte); }
    }
    for ($i = 0; $i < 3; $i++) {
        $byte = ($size >> (8 * $i)) & 0xff;
        if ($byte) { $cmd |= (0x10 << $i); $data .= chr($byte); }
    }
    if ($size === 0x10000) { $cmd &= ~0x70; $data = substr($data, 0, strlen($data)); }
    return chr($cmd) . $data;
}

function delta_insert(string $chunk): string {
    return chr(strlen($chunk)) . $chunk;
}

/** Pack entry header: type and size, variable length. */
function pack_object_header(int $type, int $size): string {
    $c = ($type << 4) | ($size & 15);
    $size >>= 4;
    $out = '';
    while ($size) {
        $out .= chr($c | 0x80);
        $c = $size & 0x7f;
        $size >>= 7;
    }
    return $out . chr($c);
}

function pack_offset_encoding(int $ofs): string {
    $bytes = chr($ofs & 0x7f);
    $ofs >>= 7;
    while ($ofs > 0) {
        $ofs -= 1;
        $bytes = chr(0x80 | ($ofs & 0x7f)) . $bytes;
        $ofs >>= 7;
    }
    return $bytes;
}

/** Write a packfile (and optionally its .idx) for the given object ids. */
class PackWriter {
    public Repo $repo;
    public bool $useDeltas = true;
    public function __construct(Repo $repo) { $this->repo = $repo; }

    /** @return array{0:string,1:array} [pack bytes, oid => offset] */
    public function build(array $oids, bool $allowOfsDelta = true): array {
        $entries = [];
        foreach ($oids as $oid) {
            $o = $this->repo->odb->read($oid);
            if ($o === null) die_git("unable to read $oid");
            $entries[] = ['oid' => $oid, 'type' => $o[0], 'data' => $o[1]];
        }
        return $this->buildFrom($entries, $allowOfsDelta);
    }

    /** Build a pack from objects that are not (yet) in the object database. */
    public function buildFrom(array $entries, bool $allowOfsDelta = true): array {
        // git's ordering heuristic: commits, then trees, then blobs and tags,
        // each group by size descending so deltas point backwards to bases
        $rank = ['commit' => 0, 'tag' => 1, 'tree' => 2, 'blob' => 3];
        usort($entries, function ($a, $b) use ($rank) {
            $ra = $rank[$a['type']] ?? 4; $rb = $rank[$b['type']] ?? 4;
            if ($ra !== $rb) return $ra <=> $rb;
            $sa = strlen($a['data']); $sb = strlen($b['data']);
            if ($sa !== $sb) return $sb <=> $sa;
            return strcmp($a['oid'], $b['oid']);
        });
        $body = 'PACK' . be32(2) . be32(count($entries));
        $offsets = [];
        $written = [];   // type => list of [index, data, offset]
        foreach ($entries as $i => $e) {
            $offset = strlen($body);
            $offsets[$e['oid']] = $offset;
            $deltaBase = null;
            if ($this->useDeltas && $allowOfsDelta && strlen($e['data']) > 64) {
                foreach (array_reverse($written[$e['type']] ?? [], true) as $cand) {
                    if (abs(strlen($cand['data']) - strlen($e['data'])) > strlen($e['data'])) continue;
                    $delta = make_delta($cand['data'], $e['data']);
                    if (strlen($delta) < strlen($e['data']) * 0.7) {
                        $deltaBase = ['offset' => $cand['offset'], 'delta' => $delta];
                    }
                    break;
                }
            }
            if ($deltaBase !== null) {
                $body .= pack_object_header(6, strlen($deltaBase['delta']));
                $body .= pack_offset_encoding($offset - $deltaBase['offset']);
                $body .= zdeflate($deltaBase['delta'], 6);
            } else {
                $body .= pack_object_header(pack_type_id($e['type']), strlen($e['data']));
                $body .= zdeflate($e['data'], 6);
            }
            $written[$e['type']][] = ['data' => $e['data'], 'offset' => $offset];
            if (count($written[$e['type']]) > 8) array_shift($written[$e['type']]);
        }
        $body .= hash('sha1', $body, true);
        return [$body, $offsets];
    }

    /** The pack reverse index (.rev): index positions ordered by pack offset. */
    public function buildRev(string $packData, array $offsets): string {
        $rawsz = $this->repo->rawsz;
        $algo = 'sha1';  // SHA-1 only build
        $oids = array_keys($offsets);
        sort($oids, SORT_STRING);
        $pos = [];
        foreach ($oids as $i => $oid) $pos[$oid] = $i;
        $byOffset = $offsets;
        asort($byOffset);
        $body = "RIDX" . be32(1) . be32(1);
        foreach (array_keys($byOffset) as $oid) $body .= be32($pos[$oid]);
        $body .= substr($packData, -$rawsz);
        $body .= hash($algo, $body, true);
        return $body;
    }

    /** Build the .idx v2 for a pack whose object offsets are known. */
    public function buildIdx(string $packData, array $offsets): string {
        $rawsz = $this->repo->rawsz;
        $algo = 'sha1';  // SHA-1 only build
        $oids = array_keys($offsets);
        sort($oids, SORT_STRING);
        $fanout = array_fill(0, 256, 0);
        foreach ($oids as $oid) {
            $fanout[hexdec(substr($oid, 0, 2))]++;
        }
        $acc = 0;
        $fanoutBytes = '';
        for ($i = 0; $i < 256; $i++) { $acc += $fanout[$i]; $fanoutBytes .= be32($acc); }
        $idx = "\xfftOc" . be32(2) . $fanoutBytes;
        foreach ($oids as $oid) $idx .= hex2bin($oid);
        // crc32 of the packed representation of each object
        $crcs = '';
        $sorted = $offsets;
        asort($sorted);
        $bounds = array_values($sorted);
        $end = strlen($packData) - $rawsz;
        foreach ($oids as $oid) {
            $start = $offsets[$oid];
            $next = $end;
            foreach ($bounds as $b) if ($b > $start && $b < $next) $next = $b;
            $crcs .= be32(crc32(substr($packData, $start, $next - $start)));
        }
        $idx .= $crcs;
        $large = '';
        $largeCount = 0;
        foreach ($oids as $oid) {
            $off = $offsets[$oid];
            if ($off < 0x80000000) $idx .= be32($off);
            else { $idx .= be32(0x80000000 | $largeCount); $large .= be64($off); $largeCount++; }
        }
        $idx .= $large;
        $packHash = substr($packData, -$rawsz);
        $idx .= $packHash;
        $idx .= hash($algo, $idx, true);
        return $idx;
    }
}

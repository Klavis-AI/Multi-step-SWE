<?php
// The multi-pack-index (gitformat-pack(5) MIDX section): one index over every
// pack.  Advisory, like the commit-graph -- object lookup with it present must
// match lookup without it.

const MIDX_SIGNATURE = "MIDX";
const MIDX_LARGE_OFFSET_NEEDED = 0x80000000;

class MultiPackIndex {
    public Repo $repo;
    public array $packNames = [];   // sorted basenames (pack-*.idx)
    public array $objects = [];     // sorted list of [oid, packIndex, offset]

    public function __construct(Repo $repo) { $this->repo = $repo; }
    public function path(): string { return $this->repo->commonPath('objects/pack/multi-pack-index'); }
    public function exists(): bool { return is_file($this->path()); }

    public function build() {
        $packs = $this->repo->odb->packs();   // PackIndex[]
        $named = [];
        foreach ($packs as $p) $named[basename($p->idxPath)] = $p;
        ksort($named, SORT_STRING);
        $this->packNames = array_keys($named);
        $packList = array_values($named);
        $seen = [];
        $objs = [];
        foreach ($packList as $pi => $p) {
            for ($i = 0; $i < $p->nobjects; $i++) {
                $oid = $p->oidAt($i);
                if (isset($seen[$oid])) continue;   // first pack wins (preferred)
                $seen[$oid] = true;
                $objs[] = [$oid, $pi, $p->offsetAt($i)];
            }
        }
        usort($objs, fn($a, $b) => strcmp($a[0], $b[0]));
        $this->objects = $objs;
    }

    public function load(): bool {
        $f = $this->path();
        if (!is_file($f)) return false;
        $d = @file_get_contents($f);
        if ($d === false || strlen($d) < 12 || substr($d, 0, 4) !== MIDX_SIGNATURE) return false;
        $sz = $this->repo->rawsz;
        $numChunks = ord($d[6]);
        $numPacks = rd32($d, 8);
        $off = 12;
        $chunks = [];
        for ($i = 0; $i <= $numChunks; $i++) {
            $chunks[substr($d, $off, 4)] = rd64($d, $off + 4);
            // last (terminating) entry has id "\0\0\0\0"
            $off += 12;
        }
        $pnam = $chunks["PNAM"] ?? null;
        $fan = $chunks["OIDF"] ?? null;
        $lookup = $chunks["OIDL"] ?? null;
        $ooff = $chunks["OOFF"] ?? null;
        $loff = $chunks["LOFF"] ?? null;
        if ($pnam === null || $fan === null || $lookup === null || $ooff === null) return false;
        // pack names: NUL-separated
        $end = min(array_filter([$fan, $lookup, $ooff, strlen($d)], fn($x) => $x > $pnam));
        $names = explode("\0", rtrim(substr($d, $pnam, $end - $pnam), "\0"));
        $this->packNames = array_values(array_filter($names, fn($x) => $x !== ''));
        $n = rd32($d, $fan + 255 * 4);
        $this->objects = [];
        for ($i = 0; $i < $n; $i++) {
            $oid = bin2hex(substr($d, $lookup + $i * $sz, $sz));
            $pid = rd32($d, $ooff + $i * 8);
            $o = rd32($d, $ooff + $i * 8 + 4);
            if ($o & MIDX_LARGE_OFFSET_NEEDED && $loff !== null) {
                $o = rd64($d, $loff + ($o & ~MIDX_LARGE_OFFSET_NEEDED) * 8);
            }
            $this->objects[] = [$oid, $pid, $o];
        }
        return true;
    }

    public function write() {
        $sz = $this->repo->rawsz;
        $n = count($this->objects);
        $pnam = '';
        foreach ($this->packNames as $nm) $pnam .= $nm . "\0";
        while (strlen($pnam) % 4 !== 0) $pnam .= "\0";   // 4-byte aligned
        $counts = array_fill(0, 256, 0);
        foreach ($this->objects as $o) $counts[hexdec(substr($o[0], 0, 2))]++;
        $fan = ''; $cum = 0;
        for ($b = 0; $b < 256; $b++) { $cum += $counts[$b]; $fan .= be32($cum); }
        $lookup = '';
        foreach ($this->objects as $o) $lookup .= hex2bin($o[0]);
        $ooff = ''; $loff = '';
        foreach ($this->objects as $o) {
            $offset = $o[2];
            if ($offset >= 0x80000000) {
                $idx = strlen($loff) / 8;
                $loff .= be64($offset);
                $ooff .= be32($o[1]) . be32(MIDX_LARGE_OFFSET_NEEDED | $idx);
            } else {
                $ooff .= be32($o[1]) . be32($offset);
            }
        }
        $chunks = [["PNAM", $pnam], ["OIDF", $fan], ["OIDL", $lookup], ["OOFF", $ooff]];
        if ($loff !== '') $chunks[] = ["LOFF", $loff];
        $numChunks = count($chunks);
        $header = MIDX_SIGNATURE . chr(1) . chr(1) . chr($numChunks) . chr(0) . be32(count($this->packNames));
        $tableSize = ($numChunks + 1) * 12;
        $off = strlen($header) + $tableSize;
        $table = '';
        foreach ($chunks as $c) { $table .= $c[0] . be64($off); $off += strlen($c[1]); }
        $table .= "\0\0\0\0" . be64($off);
        $body = $header . $table;
        foreach ($chunks as $c) $body .= $c[1];
        $algo = 'sha1';  // SHA-1 only build
        $body .= hash($algo, $body, true);
        write_file_atomic($this->path(), $body, 0444);
    }
}

<?php
// The index file (gitformat-index(5)): header, cache entries, extensions, trailer.

class IndexEntry {
    public int $ctime_s = 0, $ctime_n = 0, $mtime_s = 0, $mtime_n = 0;
    public int $dev = 0, $ino = 0, $mode = 0, $uid = 0, $gid = 0, $size = 0;
    public string $oid = '';
    public int $flags = 0;     // assume-valid | extended | stage | namelen
    public int $flags2 = 0;    // v3+: skip-worktree, intent-to-add
    public string $path = '';

    public function stage(): int { return ($this->flags >> 12) & 3; }
    public function setStage(int $s) { $this->flags = ($this->flags & ~0x3000) | (($s & 3) << 12); }
    public function assumeValid(): bool { return ($this->flags & 0x8000) !== 0; }
    public function skipWorktree(): bool { return ($this->flags2 & 0x4000) !== 0; }
    public function intentToAdd(): bool { return ($this->flags2 & 0x2000) !== 0; }
    public function isSparseDir(): bool { return $this->mode === 0040000; }

    public function fillStat(array $st) {
        $this->ctime_s = $st['ctime'] ?? 0;
        $this->mtime_s = $st['mtime'] ?? 0;
        $this->ctime_n = 0;
        $this->mtime_n = 0;
        $this->dev = $st['dev'] ?? 0;
        $this->ino = $st['ino'] ?? 0;
        $this->uid = $st['uid'] ?? 0;
        $this->gid = $st['gid'] ?? 0;
        $this->size = $st['size'] ?? 0;
    }
}

class CacheTreeNode {
    public string $name = '';
    public int $entryCount = -1;
    public string $oid = '';
    /** @var CacheTreeNode[] */
    public array $children = [];
}

class Index {
    public Repo $repo;
    public int $version = 2;
    public bool $versionExplicit = false;
    /** @var IndexEntry[] ordered */
    public array $entries = [];
    public ?CacheTreeNode $cacheTree = null;
    public array $rawExtensions = [];   // signature => raw payload (round-tripped when unchanged)
    public bool $dirty = false;
    public bool $exists = false;

    public function __construct(Repo $r) { $this->repo = $r; }

    public static function read(Repo $r, ?string $path = null): Index {
        $idx = new Index($r);
        $path = $path ?? $r->indexPath();
        $raw = @file_get_contents($path);
        if ($raw === false || strlen($raw) < 12) return $idx;
        if (substr($raw, 0, 4) !== 'DIRC') die_git('index file corrupt');
        $idx->exists = true;
        $idx->version = rd32($raw, 4);
        $count = rd32($raw, 8);
        $rawsz = $r->rawsz;
        $off = 12;
        $prev = '';
        for ($i = 0; $i < $count; $i++) {
            $e = new IndexEntry();
            $e->ctime_s = rd32($raw, $off); $e->ctime_n = rd32($raw, $off + 4);
            $e->mtime_s = rd32($raw, $off + 8); $e->mtime_n = rd32($raw, $off + 12);
            $e->dev = rd32($raw, $off + 16); $e->ino = rd32($raw, $off + 20);
            $e->mode = rd32($raw, $off + 24);
            $e->uid = rd32($raw, $off + 28); $e->gid = rd32($raw, $off + 32);
            $e->size = rd32($raw, $off + 36);
            $e->oid = bin2hex(substr($raw, $off + 40, $rawsz));
            $p = $off + 40 + $rawsz;
            $e->flags = rd16($raw, $p);
            $p += 2;
            if ($e->flags & 0x4000) { $e->flags2 = rd16($raw, $p); $p += 2; }
            $namelen = $e->flags & 0xfff;
            if ($idx->version < 4) {
                if ($namelen < 0xfff) {
                    $e->path = substr($raw, $p, $namelen);
                    $p += $namelen;
                } else {
                    $nul = strpos($raw, "\0", $p);
                    $e->path = substr($raw, $p, $nul - $p);
                    $p = $nul;
                }
                // pad with NULs to a multiple of 8 including the 62-byte prologue
                $entryLen = $p - $off;
                $pad = 8 - ($entryLen % 8);
                $p += $pad;
            } else {
                // v4: strip N bytes from the previous path, then a NUL-terminated suffix
                [$strip, $used] = index_v4_varint($raw, $p);
                $p += $used;
                $nul = strpos($raw, "\0", $p);
                $suffix = substr($raw, $p, $nul - $p);
                $p = $nul + 1;
                $e->path = substr($prev, 0, strlen($prev) - $strip) . $suffix;
            }
            $prev = $e->path;
            $idx->entries[] = $e;
            $off = $p;
        }
        // extensions
        $end = strlen($raw) - $rawsz;
        while ($off + 8 <= $end) {
            $sig = substr($raw, $off, 4);
            $size = rd32($raw, $off + 4);
            $data = substr($raw, $off + 8, $size);
            if ($sig === 'TREE') $idx->cacheTree = cache_tree_parse($data, $rawsz);
            else $idx->rawExtensions[$sig] = $data;
            $off += 8 + $size;
        }
        return $idx;
    }

    /** path => stage => IndexEntry, kept in step with $entries. */
    private ?array $map = null;
    private bool $needSort = false;

    private function buildMap() {
        $this->map = [];
        foreach ($this->entries as $e) $this->map[$e->path][$e->stage()] = $e;
    }

    public function find(string $path, int $stage = 0): ?IndexEntry {
        if ($this->map === null) $this->buildMap();
        return $this->map[$path][$stage] ?? null;
    }

    public function findAnyStage(string $path): array {
        if ($this->map === null) $this->buildMap();
        return array_values($this->map[$path] ?? []);
    }

    public function remove(string $path, ?int $stage = null) {
        if ($this->map === null) $this->buildMap();
        if (!isset($this->map[$path])) return;
        if ($stage === null) unset($this->map[$path]);
        else {
            unset($this->map[$path][$stage]);
            if (!$this->map[$path]) unset($this->map[$path]);
        }
        $this->rebuildFromMap();
        $this->dirty = true;
    }

    public function removeTree(string $prefix) {
        $p = rtrim($prefix, '/') . '/';
        if ($this->map === null) $this->buildMap();
        foreach (array_keys($this->map) as $path) {
            if (strncmp($path, $p, strlen($p)) === 0) unset($this->map[$path]);
        }
        $this->rebuildFromMap();
        $this->dirty = true;
    }

    private function rebuildFromMap() {
        $this->entries = [];
        foreach ($this->map as $path => $stages) {
            foreach ($stages as $st => $e) $this->entries[] = $e;
        }
        $this->needSort = true;
    }

    public function put(IndexEntry $e) {
        if ($this->map === null) $this->buildMap();
        $existing = $this->map[$e->path][$e->stage()] ?? null;
        $this->map[$e->path][$e->stage()] = $e;
        if ($existing !== null) {
            foreach ($this->entries as $i => $x) {
                if ($x === $existing) { $this->entries[$i] = $e; $this->dirty = true; return; }
            }
        }
        $this->entries[] = $e;
        $this->needSort = true;
        $this->dirty = true;
    }

    public function sort() {
        usort($this->entries, function ($a, $b) {
            $c = strcmp($a->path, $b->path);
            if ($c !== 0) return $c;
            return $a->stage() <=> $b->stage();
        });
        $this->needSort = false;
    }

    /** Call before iterating in index order. */
    public function sorted(): array {
        if ($this->needSort) $this->sort();
        return $this->entries;
    }

    /** Entries were replaced wholesale; drop the cached map. */
    public function reindex() { $this->map = null; $this->needSort = true; }

    public function hasConflicts(): bool {
        foreach ($this->entries as $e) if ($e->stage() !== 0) return true;
        return false;
    }

    public function write(?string $path = null) {
        $path = $path ?? $this->repo->indexPath();
        $this->map = null;
        $this->sort();
        $ver = $this->version;
        // GIT_INDEX_VERSION sets the version of a newly written index (e.g. the
        // one read-tree builds), unless the caller pinned one with
        // --index-version.
        if (!$this->versionExplicit) {
            $env = getenv('GIT_INDEX_VERSION');
            if ($env !== false && ctype_digit($env)) {
                $e = (int)$env;
                if ($e >= 2 && $e <= 4) $ver = $e;
            }
        }
        if ($ver < 2 || $ver > 4) $ver = 2;
        // v3 is only needed when an extended flag is set; auto-adjust only when
        // the version was the default, never when v3/v4 was asked for
        $needExt = false;
        foreach ($this->entries as $e) if ($e->flags2 !== 0) $needExt = true;
        if ($ver === 2 && $needExt) $ver = 3;
        if ($ver === 3 && !$needExt && !$this->versionExplicit
            && (getenv('GIT_INDEX_VERSION') === false)) $ver = 2;
        $body = 'DIRC' . be32($ver) . be32(count($this->entries));
        $prev = '';
        foreach ($this->entries as $e) {
            $namelen = strlen($e->path);
            $flags = ($e->flags & ~0x0fff) | min($namelen, 0xfff);
            if ($e->flags2 !== 0 || $ver >= 3 && ($e->flags & 0x4000)) {
                if ($e->flags2 !== 0) $flags |= 0x4000; else $flags &= ~0x4000;
            } else {
                $flags &= ~0x4000;
            }
            $rec = be32($e->ctime_s) . be32($e->ctime_n) . be32($e->mtime_s) . be32($e->mtime_n)
                 . be32($e->dev) . be32($e->ino) . be32($e->mode) . be32($e->uid) . be32($e->gid)
                 . be32($e->size) . hex2bin($e->oid) . be16($flags);
            if ($flags & 0x4000) $rec .= be16($e->flags2);
            if ($ver < 4) {
                $rec .= $e->path;
                $pad = 8 - (strlen($rec) % 8);
                $rec .= str_repeat("\0", $pad);
            } else {
                $common = 0;
                $max = min(strlen($prev), strlen($e->path));
                while ($common < $max && $prev[$common] === $e->path[$common]) $common++;
                $strip = strlen($prev) - $common;
                $rec .= index_v4_varint_encode($strip) . substr($e->path, $common) . "\0";
            }
            $prev = $e->path;
            $body .= $rec;
        }
        if ($this->cacheTree !== null) {
            $d = cache_tree_format($this->cacheTree);
            $body .= 'TREE' . be32(strlen($d)) . $d;
        }
        foreach ($this->rawExtensions as $sig => $data) {
            $body .= $sig . be32(strlen($data)) . $data;
        }
        $algo = $this->repo->hashAlgo === 'sha256' ? 'sha256' : 'sha1';
        $body .= hash($algo, $body, true);
        if (!write_file_atomic($path, $body, 0666)) die_git("unable to write index file");
        $this->dirty = false;
    }
}

function index_v4_varint(string $buf, int $off): array {
    // git's offset encoding, reversed: see index-format.txt ("strip amount")
    $i = $off;
    $c = ord($buf[$i++]);
    $val = $c & 0x7f;
    while ($c & 0x80) {
        $val += 1;
        $c = ord($buf[$i++]);
        $val = ($val << 7) + ($c & 0x7f);
    }
    return [$val, $i - $off];
}

function index_v4_varint_encode(int $value): string {
    $bytes = chr($value & 0x7f);
    $value >>= 7;
    while ($value > 0) {
        $value -= 1;
        $bytes = chr(0x80 | ($value & 0x7f)) . $bytes;
        $value >>= 7;
    }
    return $bytes;
}

function cache_tree_parse(string $data, int $rawsz): ?CacheTreeNode {
    $off = 0;
    $parse = function (&$off) use ($data, $rawsz, &$parse) {
        $nul = strpos($data, "\0", $off);
        if ($nul === false) return null;
        $name = substr($data, $off, $nul - $off);
        $off = $nul + 1;
        $nl = strpos($data, "\n", $off);
        if ($nl === false) return null;
        $nums = substr($data, $off, $nl - $off);
        $off = $nl + 1;
        $parts = explode(' ', $nums);
        $n = new CacheTreeNode();
        $n->name = $name;
        $n->entryCount = (int)$parts[0];
        $subcount = (int)($parts[1] ?? 0);
        if ($n->entryCount >= 0) {
            $n->oid = bin2hex(substr($data, $off, $rawsz));
            $off += $rawsz;
        }
        for ($i = 0; $i < $subcount; $i++) {
            $c = $parse($off);
            if ($c === null) break;
            $n->children[] = $c;
        }
        return $n;
    };
    return $parse($off);
}

function cache_tree_format(CacheTreeNode $n): string {
    $s = $n->name . "\0" . $n->entryCount . ' ' . count($n->children) . "\n";
    if ($n->entryCount >= 0) $s .= hex2bin($n->oid);
    foreach ($n->children as $c) $s .= cache_tree_format($c);
    return $s;
}

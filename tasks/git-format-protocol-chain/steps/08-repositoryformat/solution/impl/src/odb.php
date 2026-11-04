<?php
// Object database: loose objects (zlib "type size\0content"), alternates,
// abbreviation expansion (gitrepository-layout(5), gitformat-* docs).

class ObjectDB {
    public Repo $repo;
    private array $altDirs = [];
    private bool $altLoaded = false;
    private array $cache = [];

    public function __construct(Repo $r) { $this->repo = $r; }

    public function dirs(): array {
        if (!$this->altLoaded) {
            $this->altLoaded = true;
            $main = $this->repo->objectsDir();
            $this->altDirs = [$main];
            $altEnv = getenv('GIT_ALTERNATE_OBJECT_DIRECTORIES');
            if ($altEnv !== false && $altEnv !== '') {
                foreach (explode(':', $altEnv) as $d) if ($d !== '') $this->altDirs[] = path_absolute($d);
            }
            $altFile = $main . '/info/alternates';
            if (is_file($altFile)) {
                foreach (explode("\n", (string)file_get_contents($altFile)) as $line) {
                    $line = trim($line);
                    if ($line === '' || $line[0] === '#') continue;
                    $this->altDirs[] = $line[0] === '/' ? path_normalize($line) : path_normalize($main . '/' . $line);
                }
            }
        }
        return $this->altDirs;
    }

    public function loosePath(string $oid, ?string $dir = null): string {
        $dir = $dir ?? $this->repo->objectsDir();
        return $dir . '/' . substr($oid, 0, 2) . '/' . substr($oid, 2);
    }

    public function hasLoose(string $oid): bool {
        foreach ($this->dirs() as $d) if (is_file($this->loosePath($oid, $d))) return true;
        return false;
    }

    /** @var PackIndex[]|null */
    private ?array $packs = null;

    /** Every pack index reachable from this object database. */
    public function packs(): array {
        if ($this->packs !== null) return $this->packs;
        $this->packs = [];
        foreach ($this->dirs() as $d) {
            $dir = $d . '/pack';
            if (!is_dir($dir)) continue;
            $files = @scandir($dir);
            if ($files === false) continue;
            sort($files, SORT_STRING);
            foreach ($files as $f) {
                if (substr($f, -4) !== '.idx') continue;
                $packPath = $dir . '/' . substr($f, 0, -4) . '.pack';
                if (!is_file($packPath)) continue;
                $p = new PackIndex($this->repo, $packPath);
                if ($p->open()) $this->packs[] = $p;
            }
        }
        return $this->packs;
    }

    public function resetPacks() { $this->packs = null; $this->cache = []; }

    public function has(string $oid): bool {
        if ($this->hasLoose($oid)) return true;
        foreach ($this->packs() as $p) if ($p->find($oid) !== null) return true;
        return false;
    }

    /** @return array{0:PackIndex,1:int}|null */
    public function findInPacks(string $oid): ?array {
        foreach ($this->packs() as $p) {
            $i = $p->find($oid);
            if ($i !== null) return [$p, $i];
        }
        return null;
    }

    /** @return array{0:string,1:string}|null [type, content] */
    public function read(string $oid): ?array {
        if (isset($this->cache[$oid])) return $this->cache[$oid];
        $r = $this->readLocally($oid);
        if ($r === null && function_exists('promisor_fetch') && promisor_fetch($this->repo, $oid)) {
            // a partial clone may legitimately be missing an object; ask the
            // promisor remote for it once, then look again
            $r = $this->readLocally($oid);
        }
        if ($r !== null && count($this->cache) < 512) $this->cache[$oid] = $r;
        return $r;
    }

    private function readLocally(string $oid): ?array {
        $r = $this->readLoose($oid);
        if ($r === null) {
            $found = $this->findInPacks($oid);
            if ($found !== null) $r = $found[0]->readAt($found[0]->offsetAt($found[1]));
        }
        return $r;
    }

    public function readLoose(string $oid): ?array {
        foreach ($this->dirs() as $d) {
            $p = $this->loosePath($oid, $d);
            if (!is_file($p)) continue;
            $raw = @file_get_contents($p);
            if ($raw === false) return null;
            $ctx = inflate_init(ZLIB_ENCODING_DEFLATE);
            $data = $ctx === false ? false : @inflate_add($ctx, $raw, ZLIB_FINISH);
            if ($data === false) {
                fwrite(STDERR, "error: inflate: data stream error (incorrect data check)\n");
                $this->dieCorrupt($oid, $p);
            }
            $parsed = self::parseLoose($data);
            if ($parsed === null) $this->dieCorrupt($oid, $p);
            return $parsed;
        }
        return null;
    }

    /** Header-only read: enough of the stream to see "type size\0". */
    public function readLooseHeader(string $oid): ?array {
        foreach ($this->dirs() as $d) {
            $p = $this->loosePath($oid, $d);
            if (!is_file($p)) continue;
            $fh = @fopen($p, 'rb');
            if ($fh === false) return null;
            $ctx = inflate_init(ZLIB_ENCODING_DEFLATE);
            $head = '';
            // small chunks: the header must come out before a corrupt tail
            while (!feof($fh) && strlen($head) < 256) {
                $chunk = fread($fh, 32);
                if ($chunk === '' || $chunk === false) break;
                $piece = @inflate_add($ctx, $chunk, ZLIB_SYNC_FLUSH);
                if ($piece === false) break;
                $head .= $piece;
                if (strpos($head, "\0") !== false) break;
            }
            fclose($fh);
            $nul = strpos($head, "\0");
            if ($nul === false) return null;
            $hdr = substr($head, 0, $nul);
            $sp = strpos($hdr, ' ');
            if ($sp === false) return null;
            $type = substr($hdr, 0, $sp);
            $size = (int)substr($hdr, $sp + 1);
            if (!in_array($type, ['blob', 'tree', 'commit', 'tag'], true)) return null;
            return [$type, $size];
        }
        return null;
    }

    private function dieCorrupt(string $oid, string $path) {
        $rel = $this->relativePath($path);
        fwrite(STDERR, "error: corrupt loose object '$oid'\n");
        fwrite(STDERR, "fatal: loose object $oid (stored in $rel) is corrupt\n");
        exit(128);
    }

    /** git prints object paths relative to the current directory when it can. */
    public function relativePath(string $path): string {
        $cwd = path_real(getcwd());
        $real = path_real($path);
        if (strncmp($real, $cwd . '/', strlen($cwd) + 1) === 0) return substr($real, strlen($cwd) + 1);
        return $path;
    }

    public static function parseLoose(string $data): ?array {
        $nul = strpos($data, "\0");
        if ($nul === false) return null;
        $hdr = substr($data, 0, $nul);
        $sp = strpos($hdr, ' ');
        if ($sp === false) return null;
        $type = substr($hdr, 0, $sp);
        $size = (int)substr($hdr, $sp + 1);
        $content = substr($data, $nul + 1);
        if (strlen($content) !== $size) return null;
        if (!in_array($type, ['blob', 'tree', 'commit', 'tag'], true)) return null;
        return [$type, $content];
    }

    /** @return array{0:string,1:int}|null [type, size] */
    public function readHeader(string $oid): ?array {
        if (isset($this->cache[$oid])) return [$this->cache[$oid][0], strlen($this->cache[$oid][1])];
        $h = $this->readLooseHeader($oid);
        if ($h !== null) return $h;
        $o = $this->read($oid);
        return $o === null ? null : [$o[0], strlen($o[1])];
    }

    public function write(string $type, string $content, bool $persist = true): string {
        $oid = $this->repo->hashData($type, $content);
        if (!$persist) return $oid;
        if ($this->hasLoose($oid)) return $oid;
        $store = "$type " . strlen($content) . "\0" . $content;
        $path = $this->loosePath($oid);
        $dir = dirname($path);
        if (!mkdir_p($dir, 0777)) die_git("unable to create directory $dir");
        $level = $this->repo->config->getInt('core.loosecompression',
                 $this->repo->config->getInt('core.compression', 1));
        if ($level < 0 || $level > 9) $level = 1;
        $tmp = $dir . '/tmp_obj_' . getmypid() . '_' . bin2hex(random_bytes(4));
        if (@file_put_contents($tmp, zdeflate($store, $level)) === false) die_git("unable to write object $oid");
        @chmod($tmp, 0444 & ~umask_value());
        if (!@rename($tmp, $path)) { @unlink($tmp); }
        return $oid;
    }

    /** Iterate every loose object id in the primary object directory. */
    public function forEachLoose(callable $cb) {
        $base = $this->repo->objectsDir();
        if (!is_dir($base)) return;
        foreach (scandir($base) as $sub) {
            if (!preg_match('/^[0-9a-f]{2}$/', $sub)) continue;
            $d = "$base/$sub";
            if (!is_dir($d)) continue;
            foreach (scandir($d) as $f) {
                if ($f === '.' || $f === '..') continue;
                if (!preg_match('/^[0-9a-f]+$/', $f)) continue;
                if (strlen($sub . $f) !== $this->repo->hexsz) continue;
                $cb($sub . $f);
            }
        }
    }

    public function allObjects(): array {
        $r = [];
        $this->forEachLoose(function ($oid) use (&$r) { $r[] = $oid; });
        foreach ($this->packs() as $p) foreach ($p->allOids() as $oid) $r[] = $oid;
        return array_values(array_unique($r));
    }

    /** Expand an unambiguous abbreviated object name. */
    public function expandPrefix(string $prefix): array {
        $found = [];
        $len = strlen($prefix);
        if ($len >= $this->repo->hexsz) {
            return $this->has($prefix) ? [$prefix] : [];
        }
        $found = [];
        if ($len < 2) {
            $this->forEachLoose(function ($oid) use ($prefix, &$found) {
                if (strncmp($oid, $prefix, strlen($prefix)) === 0) $found[$oid] = true;
            });
            return array_keys($found);
        }
        $sub = substr($prefix, 0, 2);
        $rest = substr($prefix, 2);
        foreach ($this->packs() as $p) {
            foreach ($p->allOids() as $oid) {
                if (strncmp($oid, $prefix, strlen($prefix)) === 0) $found[$oid] = true;
            }
        }
        foreach ($this->dirs() as $base) {
            $d = "$base/$sub";
            if (!is_dir($d)) continue;
            foreach (scandir($d) as $f) {
                if ($f === '.' || $f === '..') continue;
                if ($rest === '' || strncmp($f, $rest, strlen($rest)) === 0) $found[$sub . $f] = true;
            }
        }
        return array_keys($found);
    }
}

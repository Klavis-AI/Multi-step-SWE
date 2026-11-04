<?php
// Ref storage. The files backend (loose refs + packed-refs + reflogs) is the
// default; a reftable backend plugs in behind the same interface.

abstract class RefStore {
    public Repo $repo;
    public function __construct(Repo $r) { $this->repo = $r; }

    public static function create(Repo $r): RefStore {
        if ($r->refFormat === 'reftable' && class_exists('ReftableRefStore')) {
            return new ReftableRefStore($r);
        }
        return new FilesRefStore($r);
    }

    /** @return array{0:string,1:?string}|null [oid, symref-target] */
    abstract public function readRaw(string $name): ?array;
    abstract public function listRefs(string $prefix = ''): array;   // name => oid
    abstract public function writeRef(string $name, ?string $oid, ?string $symTarget, ?string $oldOid, string $reason, bool $deref = true): bool;
    abstract public function deleteRef(string $name, ?string $oldOid, string $reason, bool $deref = true): bool;
    abstract public function reflogRead(string $name): array;
    abstract public function reflogExists(string $name): bool;
    abstract public function reflogCreate(string $name);
    abstract public function reflogDelete(string $name);
    abstract public function format(): string;

    /** Follow symrefs to an object id. */
    public function resolve(string $name, int $depth = 0) {
        if ($depth > 5) return null;
        $raw = $this->readRaw($name);
        if ($raw === null) return null;
        if ($raw[1] !== null) return $this->resolve($raw[1], $depth + 1);
        return $raw[0];
    }

    /** Resolve to the final ref name a symref chain points at. */
    public function resolveRefName(string $name, int $depth = 0): ?string {
        if ($depth > 5) return null;
        $raw = $this->readRaw($name);
        if ($raw === null) return null;
        if ($raw[1] !== null) {
            $r = $this->resolveRefName($raw[1], $depth + 1);
            return $r === null ? $raw[1] : $r;
        }
        return $name;
    }

    public function exists(string $name): bool { return $this->readRaw($name) !== null; }

    /**
     * The full name of the ref a short name means, in gitrevisions(7) order:
     * the name itself, then refs/, refs/tags/, refs/heads/, refs/remotes/,
     * refs/remotes/<name>/HEAD.  Null when nothing matches.
     */
    public function expandRefName(string $name): ?string {
        if ($name === 'HEAD') return 'HEAD';
        foreach (['', 'refs/', 'refs/tags/', 'refs/heads/', 'refs/remotes/'] as $p) {
            if ($this->readRaw($p . $name) !== null) return $p . $name;
        }
        if ($this->readRaw('refs/remotes/' . $name . '/HEAD') !== null) {
            return 'refs/remotes/' . $name . '/HEAD';
        }
        return null;
    }

    public function log(string $name, ?string $old, ?string $new, string $msg) {
        $this->reflogAppend($name, $old, $new, $msg);
    }
    abstract public function reflogAppend(string $name, ?string $old, ?string $new, string $msg);
}

class FilesRefStore extends RefStore {
    private ?array $packed = null;

    public function format(): string { return 'files'; }

    /** Per-worktree refs live in $GIT_DIR, everything else in the common dir. */
    public function refPath(string $name): string {
        if ($this->isPerWorktree($name)) return $this->repo->path($name);
        return $this->repo->commonPath($name);
    }

    private function isPerWorktree(string $name): bool {
        if (strpos($name, '/') === false) return true;               // HEAD, FETCH_HEAD, ...
        if (strncmp($name, 'refs/bisect/', 12) === 0) return true;
        if (strncmp($name, 'refs/worktree/', 14) === 0) return true;
        if (strncmp($name, 'refs/rewritten/', 15) === 0) return true;
        return false;
    }

    public function packedRefs(): array {
        if ($this->packed !== null) return $this->packed;
        $this->packed = [];
        $p = $this->repo->commonPath('packed-refs');
        $c = @file_get_contents($p);
        if ($c === false) return $this->packed;
        $last = null;
        foreach (explode("\n", $c) as $line) {
            if ($line === '') continue;
            if ($line[0] === '#') continue;
            if ($line[0] === '^') { continue; }
            $sp = strpos($line, ' ');
            if ($sp === false) continue;
            $oid = substr($line, 0, $sp);
            $name = substr($line, $sp + 1);
            if (!is_hex($oid)) continue;
            $this->packed[$name] = $oid;
        }
        return $this->packed;
    }

    public function readRaw(string $name): ?array {
        $p = $this->refPath($name);
        $c = @file_get_contents($p);
        if ($c !== false) {
            $c = rtrim($c, " \t\r\n");
            if (strncmp($c, 'ref:', 4) === 0) {
                $t = trim(substr($c, 4));
                return [str_repeat('0', $this->repo->hexsz), $t];
            }
            if (preg_match('/^[0-9a-f]{40}$|^[0-9a-f]{64}$/', $c)) return [$c, null];
            return null;
        }
        $packed = $this->packedRefs();
        if (isset($packed[$name])) return [$packed[$name], null];
        return null;
    }

    public function listRefs(string $prefix = ''): array {
        $out = [];
        foreach ($this->packedRefs() as $n => $o) {
            if ($prefix === '' || strncmp($n, $prefix, strlen($prefix)) === 0) $out[$n] = $o;
        }
        $roots = ['refs'];
        foreach ($roots as $root) {
            $base = $this->repo->commonPath($root);
            $this->walk($base, $root, $out, $prefix);
            // per-worktree refs shadow the common ones
            $wbase = $this->repo->path($root);
            if ($wbase !== $base) {
                $tmp = [];
                $this->walk($wbase . '/bisect', 'refs/bisect', $tmp, $prefix);
                $this->walk($wbase . '/worktree', 'refs/worktree', $tmp, $prefix);
                foreach ($tmp as $k => $v) $out[$k] = $v;
            }
        }
        ksort($out, SORT_STRING);
        return $out;
    }

    private function walk(string $dir, string $name, array &$out, string $prefix) {
        if (!is_dir($dir)) return;
        $entries = @scandir($dir);
        if ($entries === false) return;
        foreach ($entries as $e) {
            if ($e === '.' || $e === '..') continue;
            $p = "$dir/$e";
            $rn = "$name/$e";
            if (is_dir($p)) { $this->walk($p, $rn, $out, $prefix); continue; }
            if ($prefix !== '' && strncmp($rn, $prefix, min(strlen($prefix), strlen($rn))) !== 0) continue;
            if ($prefix !== '' && strlen($rn) >= strlen($prefix) && strncmp($rn, $prefix, strlen($prefix)) !== 0) continue;
            $c = @file_get_contents($p);
            if ($c === false) continue;
            $c = rtrim($c, " \t\r\n");
            if (strncmp($c, 'ref:', 4) === 0) {
                // a symbolic ref under refs/ (refs/remotes/<remote>/HEAD) is
                // listed at the value it resolves to, as git lists it
                $res = $this->resolve($rn);
                if ($res !== null && ($prefix === '' || strncmp($rn, $prefix, strlen($prefix)) === 0)) {
                    $out[$rn] = $res;
                }
                continue;
            }
            if (!preg_match('/^[0-9a-f]{40}$|^[0-9a-f]{64}$/', $c)) continue;
            if ($prefix === '' || strncmp($rn, $prefix, strlen($prefix)) === 0) $out[$rn] = $c;
        }
    }

    public function writeRef(string $name, ?string $oid, ?string $symTarget, ?string $oldOid, string $reason, bool $deref = true): bool {
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
        $p = $this->refPath($target);
        if (!mkdir_p(dirname($p))) return false;
        $content = $symTarget !== null ? "ref: $symTarget\n" : "$oid\n";
        if (!write_file_atomic($p, $content, 0666)) return false;
        $this->packed = null;
        if ($symTarget === null) {
            $this->reflogAppend($target, $old, $oid, $reason);
            if ($target !== $name) $this->reflogAppend($name, $old, $oid, $reason);
        }
        return true;
    }

    public function deleteRef(string $name, ?string $oldOid, string $reason, bool $deref = true): bool {
        $target = $name;
        $raw = $this->readRaw($name);
        if ($deref && $raw !== null && $raw[1] !== null) {
            // `update-ref -d HEAD` removes the branch HEAD points at
            $chain = $this->resolveRefName($name);
            if ($chain !== null) $target = $chain;
        }
        $old = $this->resolve($target);
        if ($oldOid !== null && $old !== $oldOid) {
            fwrite(STDERR, "error: cannot lock ref '$target': is at " . ($old ?? 'unborn') . " but expected $oldOid\n");
            return false;
        }
        $p = $this->refPath($target);
        @unlink($p);
        $this->prunePath(dirname($p));
        // also drop it from packed-refs
        $packed = $this->packedRefs();
        if (isset($packed[$target])) {
            unset($packed[$target]);
            $this->writePackedRefs($packed);
        }
        $this->packed = null;
        $lp = $this->reflogPath($target);
        @unlink($lp);
        $this->prunePath(dirname($lp));
        return true;
    }

    private function prunePath(string $dir) {
        $stop = $this->repo->commonPath('refs');
        while (strlen($dir) > strlen($stop) && strncmp($dir, $stop, strlen($stop)) === 0) {
            if (!@rmdir($dir)) break;
            $dir = dirname($dir);
        }
    }

    public function writePackedRefs(array $refs) {
        $out = "# pack-refs with: peeled fully-peeled sorted \n";
        ksort($refs, SORT_STRING);
        foreach ($refs as $n => $o) {
            $out .= "$o $n\n";
            $obj = $this->repo->odb->read($o);
            if ($obj !== null && $obj[0] === 'tag') {
                $t = TagObj::parse($obj[1]);
                $peeled = $t->object;
                // peel through chains of tags
                while (true) {
                    $x = $this->repo->odb->read($peeled);
                    if ($x === null || $x[0] !== 'tag') break;
                    $peeled = TagObj::parse($x[1])->object;
                }
                $out .= "^$peeled\n";
            }
        }
        write_file_atomic($this->repo->commonPath('packed-refs'), $out, 0666);
        $this->packed = null;
    }

    public function reflogPath(string $name): string {
        if ($this->isPerWorktree($name)) return $this->repo->path('logs/' . $name);
        return $this->repo->commonPath('logs/' . $name);
    }

    public function reflogExists(string $name): bool { return is_file($this->reflogPath($name)); }
    public function reflogCreate(string $name) {
        $p = $this->reflogPath($name);
        mkdir_p(dirname($p));
        if (!is_file($p)) @touch($p);
    }
    public function reflogDelete(string $name) { @unlink($this->reflogPath($name)); }

    public function reflogAppend(string $name, ?string $old, ?string $new, string $msg) {
        $null = str_repeat('0', $this->repo->hexsz);
        $always = $this->repo->config->getBool('core.logallrefupdates',
            !$this->repo->isBare());
        $want = $this->reflogExists($name);
        if (!$want) {
            if (!$always) return;
            $ok = ($name === 'HEAD' || strncmp($name, 'refs/heads/', 11) === 0
                || strncmp($name, 'refs/remotes/', 13) === 0 || strncmp($name, 'refs/notes/', 11) === 0);
            if (!$ok) return;
        }
        $p = $this->reflogPath($name);
        mkdir_p(dirname($p));
        $ident = committer_ident_string($this->repo);
        $line = ($old ?? $null) . ' ' . ($new ?? $null) . ' ' . $ident . "\t" . str_replace("\n", ' ', $msg) . "\n";
        @file_put_contents($p, $line, FILE_APPEND);
    }

    public function reflogRead(string $name): array {
        $p = $this->reflogPath($name);
        $c = @file_get_contents($p);
        if ($c === false) return [];
        $out = [];
        foreach (explode("\n", $c) as $line) {
            if ($line === '') continue;
            $tab = strpos($line, "\t");
            $head = $tab === false ? $line : substr($line, 0, $tab);
            $msg = $tab === false ? '' : substr($line, $tab + 1);
            $parts = explode(' ', $head, 3);
            if (count($parts) < 3) continue;
            $out[] = ['old' => $parts[0], 'new' => $parts[1], 'ident' => Ident::parse($parts[2]), 'msg' => $msg];
        }
        return $out;
    }
}

/** git-check-ref-format(1) rules. */
function check_ref_format(string $ref, bool $allowOnelevel = false, bool $refspecPattern = false): bool {
    if ($ref === '' || $ref === '@') return false;
    if (substr($ref, -1) === '/' || substr($ref, -5) === '.lock') return false;
    if (strpos($ref, '..') !== false || strpos($ref, '@{') !== false) return false;
    if (strpos($ref, '//') !== false) return false;
    if (preg_match('/[\x00-\x20\x7f~^:?*\[\\\\]/', $ref)) {
        if (!$refspecPattern) return false;
    }
    $parts = explode('/', $ref);
    if (!$allowOnelevel && count($parts) < 2) return false;
    $stars = 0;
    foreach ($parts as $p) {
        if ($p === '') return false;
        if ($p[0] === '.' || substr($p, -1) === '.') return false;
        if (substr($p, -5) === '.lock') return false;
        if ($refspecPattern) {
            $stars += substr_count($p, '*');
            if (preg_match('/[\x00-\x20\x7f~^:?\[\\\\]/', $p)) return false;
        }
    }
    if ($refspecPattern && $stars > 1) return false;
    return true;
}

function committer_ident_string(Repo $repo): string {
    [$n, $e, $ts, $tz] = committer_ident($repo);
    return "$n <$e> $ts $tz";
}

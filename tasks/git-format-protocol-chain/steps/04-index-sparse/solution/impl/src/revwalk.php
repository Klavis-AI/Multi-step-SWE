<?php
// Object-name resolution (gitrevisions(7)) and the commit walker used by
// rev-list, log and the transports.

function ref_dwim_candidates(string $name): array {
    return [
        $name,
        "refs/$name",
        "refs/tags/$name",
        "refs/heads/$name",
        "refs/remotes/$name",
        "refs/remotes/$name/HEAD",
    ];
}

function resolve_ref_dwim(Repo $repo, string $name): ?array {
    if ($name === '@') $name = 'HEAD';
    foreach (ref_dwim_candidates($name) as $cand) {
        if ($cand === 'HEAD' || strpos($cand, '/') !== false || $cand === $name) {
            $raw = $repo->refs->readRaw($cand);
            if ($raw !== null) {
                $oid = $repo->refs->resolve($cand);
                if ($oid !== null) return [$cand, $oid];
            }
        }
    }
    return null;
}

/** Resolve a revision expression to an object id, or null. */
function rev_parse_object(Repo $repo, string $name, bool $quiet = true): ?string {
    $r = rev_parse_full($repo, $name);
    return $r === null ? null : $r[0];
}

/** @return array{0:string,1:?string}|null [oid, refname] */
function rev_parse_full(Repo $repo, string $name) {
    if ($name === '') return null;
    // <rev>:<path>
    $colon = strpos($name, ':');
    if ($colon !== false && $colon > 0) {
        $lhs = substr($name, 0, $colon);
        $rhs = substr($name, $colon + 1);
        if (preg_match('/^[0-3]:(.*)$/s', $rhs, $m)) {
            $stage = (int)$rhs[0];
            $path = $m[1];
            $idx = $repo->index();
            $e = $idx->find($path, $stage);
            return $e === null ? null : [$e->oid, null];
        }
        if ($lhs === '') {
            $idx = $repo->index();
            $e = $idx->find($rhs, 0);
            return $e === null ? null : [$e->oid, null];
        }
        $base = rev_parse_full($repo, $lhs);
        if ($base === null) return null;
        $tree = peel_to_tree($repo, $base[0]);
        if ($tree === null) return null;
        if ($rhs === '') return [$tree, null];
        $e = tree_lookup_path($repo, $tree, $rhs);
        return $e === null ? null : [$e->oid, null];
    }
    if ($colon === 0) {
        $idx = $repo->index();
        $e = $idx->find(substr($name, 1), 0);
        return $e === null ? null : [$e->oid, null];
    }
    // suffix operators
    if (preg_match('/^(.*?)(\^\{\/.*\}|\^\{.*\}|\^[0-9]*|~[0-9]*|@\{[^}]*\})$/s', $name, $m) && $m[1] !== '') {
        $base = rev_parse_full($repo, $m[1]);
        $suffix = $m[2];
        if ($base === null) return null;
        $oid = $base[0];
        if (strncmp($suffix, '@{', 2) === 0) {
            $spec = substr($suffix, 2, -1);
            return reflog_lookup($repo, $m[1], $spec);
        }
        if (strncmp($suffix, '^{', 2) === 0) {
            $inner = substr($suffix, 2, -1);
            if ($inner === '') return [peel_object($repo, $oid), null];
            if ($inner[0] === '/') return commit_search($repo, $oid, substr($inner, 1));
            $target = $inner;
            $cur = $oid;
            for ($i = 0; $i < 10; $i++) {
                $o = $repo->odb->read($cur);
                if ($o === null) return null;
                if ($o[0] === $target) return [$cur, null];
                if ($o[0] === 'tag') { $cur = TagObj::parse($o[1])->object; continue; }
                if ($o[0] === 'commit' && $target === 'tree') return [CommitObj::parse($o[1])->tree, null];
                return null;
            }
            return null;
        }
        if ($suffix[0] === '^') {
            $n = $suffix === '^' ? 1 : (int)substr($suffix, 1);
            $oid = peel_to_commit($repo, $oid);
            if ($oid === null) return null;
            if ($n === 0) return [$oid, null];
            $o = $repo->odb->read($oid);
            if ($o === null || $o[0] !== 'commit') return null;
            $c = CommitObj::parse($o[1]);
            if (!isset($c->parents[$n - 1])) return null;
            return [$c->parents[$n - 1], null];
        }
        if ($suffix[0] === '~') {
            $n = $suffix === '~' ? 1 : (int)substr($suffix, 1);
            $cur = peel_to_commit($repo, $oid);
            for ($i = 0; $i < $n; $i++) {
                if ($cur === null) return null;
                $o = $repo->odb->read($cur);
                if ($o === null || $o[0] !== 'commit') return null;
                $c = CommitObj::parse($o[1]);
                if (!isset($c->parents[0])) return null;
                $cur = $c->parents[0];
            }
            return [$cur, null];
        }
    }
    // a ref?
    $ref = resolve_ref_dwim($repo, $name);
    if ($ref !== null) return [$ref[1], $ref[0]];
    // an object id (possibly abbreviated)
    if (is_hex($name) && strlen($name) >= 4) {
        $cands = $repo->odb->expandPrefix($name);
        if (count($cands) === 1) return [$cands[0], null];
        if (count($cands) > 1) return null;
    }
    return null;
}

function peel_object(Repo $repo, string $oid): ?string {
    for ($i = 0; $i < 10; $i++) {
        $o = $repo->odb->read($oid);
        if ($o === null) return null;
        if ($o[0] !== 'tag') return $oid;
        $oid = TagObj::parse($o[1])->object;
    }
    return $oid;
}

function peel_to_commit(Repo $repo, string $oid): ?string {
    for ($i = 0; $i < 10; $i++) {
        $o = $repo->odb->read($oid);
        if ($o === null) return null;
        if ($o[0] === 'commit') return $oid;
        if ($o[0] === 'tag') { $oid = TagObj::parse($o[1])->object; continue; }
        return null;
    }
    return null;
}

function commit_search(Repo $repo, string $start, string $pattern) {
    $walk = new RevWalk($repo);
    $walk->push($start);
    foreach ($walk->commits() as $oid => $c) {
        if (preg_match('/' . str_replace('/', '\/', $pattern) . '/', $c->message)) return [$oid, null];
    }
    return null;
}

function reflog_lookup(Repo $repo, string $refname, string $spec) {
    if ($refname === '@' || $refname === '') $refname = 'HEAD';
    $full = $refname;
    if ($refname !== 'HEAD') {
        $r = resolve_ref_dwim($repo, $refname);
        if ($r !== null) $full = $r[0];
    }
    if ($spec === 'u' || $spec === 'upstream' || $spec === 'push') {
        $short = preg_replace('#^refs/heads/#', '', $repo->refs->resolveRefName($full) ?? $full);
        $remote = $repo->config->get("branch.$short.remote", null);
        $merge = $repo->config->get("branch.$short.merge", null);
        if ($remote === null || $merge === null) return null;
        $name = 'refs/remotes/' . $remote . '/' . preg_replace('#^refs/heads/#', '', $merge);
        $oid = $repo->refs->resolve($name);
        return $oid === null ? null : [$oid, $name];
    }
    $log = $repo->refs->reflogRead($full);
    if (!$log) return null;
    if (preg_match('/^\d+$/', $spec)) {
        $n = (int)$spec;
        $idx = count($log) - 1 - $n;
        if ($idx < 0) return null;
        return [$log[$idx]['new'], null];
    }
    $d = parse_date($spec);
    if ($d !== false) {
        $want = $d[0];
        for ($i = count($log) - 1; $i >= 0; $i--) {
            if ($log[$i]['ident']->ts <= $want) return [$log[$i]['new'], null];
        }
        return [$log[0]['old'], null];
    }
    return null;
}

/** Commit walker with the ordering options rev-list(1) documents. */
class RevWalk {
    public Repo $repo;
    private array $include = [];
    private array $exclude = [];
    public bool $firstParent = false;
    public bool $topoOrder = false;
    public bool $dateOrder = false;
    public bool $reverse = false;
    public ?int $maxCount = null;
    public int $skip = 0;
    public ?int $since = null;
    public ?int $until = null;
    public array $grep = [];
    public ?string $author = null;
    public ?string $committerPat = null;
    public ?bool $merges = null;
    public array $cache = [];

    public function __construct(Repo $repo) { $this->repo = $repo; }

    public function push(string $oid) { $this->include[$oid] = true; }
    public function hide(string $oid) { $this->exclude[$oid] = true; }
    public function hasStarts(): bool { return (bool)$this->include; }

    public function commit(string $oid): ?CommitObj {
        if (isset($this->cache[$oid])) return $this->cache[$oid];
        $o = $this->repo->odb->read($oid);
        if ($o === null) return null;
        if ($o[0] === 'tag') {
            $t = TagObj::parse($o[1]);
            return $this->commit($t->object);
        }
        if ($o[0] !== 'commit') return null;
        $c = CommitObj::parse($o[1], $oid);
        $this->cache[$oid] = $c;
        return $c;
    }

    /** Mark everything reachable from the excluded tips. */
    private function excluded(): array {
        $seen = [];
        $stack = array_keys($this->exclude);
        while ($stack) {
            $oid = array_pop($stack);
            if (isset($seen[$oid])) continue;
            $seen[$oid] = true;
            $c = $this->commit($oid);
            if ($c === null) continue;
            $parents = $this->firstParent ? array_slice($c->parents, 0, 1) : $c->parents;
            foreach ($parents as $p) if (!isset($seen[$p])) $stack[] = $p;
        }
        return $seen;
    }

    /** @return CommitObj[] keyed by oid, in output order */
    public function commits(): array {
        $hidden = $this->excluded();
        $seen = [];
        $heap = [];   // [ts, seq, oid]
        $seq = 0;
        foreach (array_keys($this->include) as $oid) {
            $c = $this->commit($oid);
            if ($c === null) continue;
            if (isset($hidden[$oid])) continue;
            $heap[] = [$c->committer ? $c->committer->ts : 0, $seq++, $oid];
        }
        $out = [];
        $sortHeap = function (&$h) {
            usort($h, function ($a, $b) {
                if ($a[0] !== $b[0]) return $b[0] <=> $a[0];
                return $a[1] <=> $b[1];
            });
        };
        $sortHeap($heap);
        while ($heap) {
            $item = array_shift($heap);
            $oid = $item[2];
            if (isset($seen[$oid])) continue;
            $seen[$oid] = true;
            if (isset($hidden[$oid])) continue;
            $c = $this->commit($oid);
            if ($c === null) continue;
            $out[$oid] = $c;
            $parents = $this->firstParent ? array_slice($c->parents, 0, 1) : $c->parents;
            foreach ($parents as $p) {
                if (isset($seen[$p]) || isset($hidden[$p])) continue;
                $pc = $this->commit($p);
                if ($pc === null) continue;
                $heap[] = [$pc->committer ? $pc->committer->ts : 0, $seq++, $p];
            }
            $sortHeap($heap);
        }
        if ($this->topoOrder) $out = $this->topoSort($out);
        $out = $this->applyFilters($out);
        if ($this->reverse) $out = array_reverse($out, true);
        return $out;
    }

    private function topoSort(array $commits): array {
        // children-before-parents, preserving the incoming order among peers
        $indeg = [];
        foreach ($commits as $oid => $c) $indeg[$oid] = 0;
        foreach ($commits as $oid => $c) {
            $parents = $this->firstParent ? array_slice($c->parents, 0, 1) : $c->parents;
            foreach ($parents as $p) if (isset($indeg[$p])) $indeg[$p]++;
        }
        $ready = [];
        foreach ($commits as $oid => $c) if ($indeg[$oid] === 0) $ready[] = $oid;
        $out = [];
        while ($ready) {
            usort($ready, function ($a, $b) use ($commits) {
                $ta = $commits[$a]->committer ? $commits[$a]->committer->ts : 0;
                $tb = $commits[$b]->committer ? $commits[$b]->committer->ts : 0;
                return $tb <=> $ta;
            });
            $oid = array_shift($ready);
            $out[$oid] = $commits[$oid];
            $parents = $this->firstParent ? array_slice($commits[$oid]->parents, 0, 1) : $commits[$oid]->parents;
            foreach ($parents as $p) {
                if (!isset($indeg[$p])) continue;
                if (--$indeg[$p] === 0) $ready[] = $p;
            }
        }
        return $out;
    }

    private function applyFilters(array $commits): array {
        $out = [];
        foreach ($commits as $oid => $c) {
            if ($this->since !== null && $c->committer && $c->committer->ts <= $this->since) continue;
            if ($this->until !== null && $c->committer && $c->committer->ts > $this->until) continue;
            if ($this->merges === true && count($c->parents) < 2) continue;
            if ($this->merges === false && count($c->parents) >= 2) continue;
            if ($this->author !== null && (!$c->author || !preg_match('/' . str_replace('/', '\/', $this->author) . '/i', $c->author->format()))) continue;
            if ($this->committerPat !== null && (!$c->committer || !preg_match('/' . str_replace('/', '\/', $this->committerPat) . '/i', $c->committer->format()))) continue;
            if ($this->grep) {
                $hit = false;
                foreach ($this->grep as $g) if (preg_match('/' . str_replace('/', '\/', $g) . '/', $c->message)) { $hit = true; break; }
                if (!$hit) continue;
            }
            $out[$oid] = $c;
        }
        if ($this->skip > 0) $out = array_slice($out, $this->skip, null, true);
        if ($this->maxCount !== null) $out = array_slice($out, 0, $this->maxCount, true);
        return $out;
    }
}

/** Collect every object reachable from the given commits (rev-list --objects). */
function reachable_objects(Repo $repo, array $commitOids, bool $withTrees = true): array {
    $objs = [];
    $walk = new RevWalk($repo);
    foreach ($commitOids as $o) $walk->push($o);
    foreach ($walk->commits() as $oid => $c) {
        $objs[$oid] = '';
        if ($withTrees) collect_tree_objects($repo, $c->tree, '', $objs);
    }
    return $objs;
}

function collect_tree_objects(Repo $repo, string $treeOid, string $prefix, array &$objs, bool $requirePresent = false) {
    if (isset($objs[$treeOid])) return;
    $o = $repo->odb->read($treeOid);
    if ($o === null || $o[0] !== 'tree') {
        if ($requirePresent) die_git("missing tree object '$treeOid'");
        return;
    }
    $objs[$treeOid] = rtrim($prefix, '/');
    $t = TreeObj::parse($o[1]);
    foreach ($t->entries as $e) {
        if ($e->mode === '160000') continue;
        if ($e->mode === '40000') { collect_tree_objects($repo, $e->oid, $prefix . $e->name . '/', $objs, $requirePresent); continue; }
        if ($requirePresent && $repo->odb->readHeader($e->oid) === null) {
            die_git("missing blob object '{$e->oid}'");
        }
        $objs[$e->oid] = $prefix . $e->name;
    }
}

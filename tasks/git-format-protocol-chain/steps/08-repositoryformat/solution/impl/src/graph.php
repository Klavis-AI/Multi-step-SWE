<?php
// The commit-graph (gitformat-commit-graph(5)): the CGPH file with OIDF/OIDL/
// CDAT chunks and v1 topological generation numbers.  It is advisory -- every
// answer it accelerates must be identical to the walked answer -- so this reads
// and writes the format real git uses and never trusts it over the objects.

const GRAPH_SIGNATURE = "CGPH";
const GRAPH_PARENT_NONE = 0x70000000;
const GRAPH_EXTRA_EDGES_NEEDED = 0x80000000;
const GRAPH_LAST_EDGE = 0x80000000;

class CommitGraph {
    public Repo $repo;
    public array $oids = [];        // sorted list of commit oids
    public array $pos = [];         // oid => index
    public array $parents = [];     // oid => [parent oids]
    public array $tree = [];        // oid => tree oid
    public array $date = [];        // oid => committer timestamp
    public array $gen = [];         // oid => topo level

    public function __construct(Repo $repo) { $this->repo = $repo; }

    private function path(): string { return $this->repo->commonPath('objects/info/commit-graph'); }

    public function exists(): bool { return is_file($this->path()); }

    /** Read the on-disk commit-graph (single file form). */
    public function load(): bool {
        $f = $this->path();
        if (!is_file($f)) return false;
        $d = @file_get_contents($f);
        if ($d === false || strlen($d) < 8 || substr($d, 0, 4) !== GRAPH_SIGNATURE) return false;
        $sz = $this->repo->rawsz;
        $numChunks = ord($d[6]);
        $off = 8;
        $chunks = [];
        for ($i = 0; $i <= $numChunks; $i++) {
            $id = substr($d, $off, 4);
            $coff = rd64($d, $off + 4);
            $chunks[] = [$id, $coff];
            $off += 12;
        }
        $find = function ($id) use ($chunks) {
            foreach ($chunks as $k => $c) if ($c[0] === $id) return $chunks[$k][1];
            return null;
        };
        $lookup = $find("OIDL");
        $data = $find("CDAT");
        $edge = $find("EDGE");
        if ($lookup === null || $data === null) return false;
        // OIDF (fanout) gives the count in its last entry
        $fan = $find("OIDF");
        $n = rd32($d, $fan + 255 * 4);
        $this->oids = [];
        for ($i = 0; $i < $n; $i++) {
            $oid = bin2hex(substr($d, $lookup + $i * $sz, $sz));
            $this->oids[] = $oid;
            $this->pos[$oid] = $i;
        }
        $rec = $sz + 16;
        for ($i = 0; $i < $n; $i++) {
            $base = $data + $i * $rec;
            $oid = $this->oids[$i];
            $this->tree[$oid] = bin2hex(substr($d, $base, $sz));
            $e1 = rd32($d, $base + $sz);
            $e2 = rd32($d, $base + $sz + 4);
            $high = rd32($d, $base + $sz + 8);
            $low = rd32($d, $base + $sz + 12);
            $this->gen[$oid] = $high >> 2;
            $this->date[$oid] = (($high & 0x3) << 32) | $low;
            $par = [];
            if ($e1 !== GRAPH_PARENT_NONE) {
                if ($e2 === GRAPH_PARENT_NONE) {
                    $par[] = $this->oids[$e1];
                } elseif ($e2 & GRAPH_EXTRA_EDGES_NEEDED) {
                    $par[] = $this->oids[$e1];
                    $ei = ($e2 & ~GRAPH_EXTRA_EDGES_NEEDED);
                    while ($edge !== null) {
                        $v = rd32($d, $edge + $ei * 4);
                        $par[] = $this->oids[$v & ~GRAPH_LAST_EDGE];
                        if ($v & GRAPH_LAST_EDGE) break;
                        $ei++;
                    }
                } else {
                    $par[] = $this->oids[$e1];
                    $par[] = $this->oids[$e2];
                }
            }
            $this->parents[$oid] = $par;
        }
        return true;
    }

    /** Build the graph over a set of commit oids (reachable closure). */
    public function build(array $commitOids) {
        $set = [];
        foreach ($commitOids as $o) $set[$o] = true;
        // gather every reachable commit
        $stack = array_keys($set);
        $all = [];
        while ($stack) {
            $o = array_pop($stack);
            if (isset($all[$o])) continue;
            $obj = $this->repo->odb->read($o);
            if ($obj === null || $obj[0] !== 'commit') continue;
            $c = CommitObj::parse($obj[1], $o);
            $all[$o] = $c;
            foreach ($c->parents as $p) if (!isset($all[$p])) $stack[] = $p;
        }
        $this->oids = array_keys($all);
        sort($this->oids, SORT_STRING);
        $this->pos = [];
        foreach ($this->oids as $i => $o) $this->pos[$o] = $i;
        foreach ($this->oids as $o) {
            $c = $all[$o];
            $this->parents[$o] = $c->parents;
            $this->tree[$o] = $c->tree;
            $this->date[$o] = $c->committer ? $c->committer->ts : 0;
        }
        // topological generation: 1 + max(parent gen); roots = 1
        $this->gen = [];
        $visiting = [];
        $compute = function ($o) use (&$compute, &$visiting, $all) {
            if (isset($this->gen[$o])) return $this->gen[$o];
            $g = 1;
            foreach (($this->parents[$o] ?? []) as $p) {
                if (!isset($all[$p])) continue;
                $g = max($g, 1 + $compute($p));
            }
            $this->gen[$o] = $g;
            return $g;
        };
        foreach ($this->oids as $o) $compute($o);
    }

    /** Serialize and write objects/info/commit-graph. */
    public function write() {
        $sz = $this->repo->rawsz;
        $n = count($this->oids);
        // fanout
        $fan = '';
        $counts = array_fill(0, 256, 0);
        foreach ($this->oids as $o) $counts[hexdec(substr($o, 0, 2))]++;
        $cum = 0;
        for ($b = 0; $b < 256; $b++) { $cum += $counts[$b]; $fan .= be32($cum); }
        // lookup
        $lookup = '';
        foreach ($this->oids as $o) $lookup .= hex2bin($o);
        // data + extra edges
        $data = '';
        $edges = '';
        foreach ($this->oids as $o) {
            $data .= hex2bin($this->tree[$o]);
            $par = $this->parents[$o];
            $known = array_values(array_filter($par, fn($p) => isset($this->pos[$p])));
            if (count($known) === 0) {
                $data .= be32(GRAPH_PARENT_NONE) . be32(GRAPH_PARENT_NONE);
            } elseif (count($known) === 1) {
                $data .= be32($this->pos[$known[0]]) . be32(GRAPH_PARENT_NONE);
            } elseif (count($known) === 2) {
                $data .= be32($this->pos[$known[0]]) . be32($this->pos[$known[1]]);
            } else {
                $data .= be32($this->pos[$known[0]]);
                $data .= be32(GRAPH_EXTRA_EDGES_NEEDED | (strlen($edges) / 4));
                for ($k = 1; $k < count($known); $k++) {
                    $v = $this->pos[$known[$k]];
                    if ($k === count($known) - 1) $v |= GRAPH_LAST_EDGE;
                    $edges .= be32($v);
                }
            }
            $high = ($this->gen[$o] << 2) | (($this->date[$o] >> 32) & 0x3);
            $data .= be32($high) . be32($this->date[$o] & 0xffffffff);
        }
        $chunks = [["OIDF", $fan], ["OIDL", $lookup], ["CDAT", $data]];
        if ($edges !== '') $chunks[] = ["EDGE", $edges];
        $numChunks = count($chunks);
        $headerSize = 8 + ($numChunks + 1) * 12;
        // compute offsets
        $off = $headerSize;
        $table = '';
        foreach ($chunks as $c) { $table .= $c[0] . be64($off); $off += strlen($c[1]); }
        $table .= "\0\0\0\0" . be64($off);   // terminating entry
        $body = GRAPH_SIGNATURE . chr(1) . chr(1) . chr($numChunks) . chr(0) . $table;
        foreach ($chunks as $c) $body .= $c[1];
        $algo = $sz === 32 ? 'sha256' : 'sha1';
        $body .= hash($algo, $body, true);
        $dir = dirname($this->path());
        @mkdir($dir, 0777, true);
        write_file_atomic($this->path(), $body, 0444);
    }
}

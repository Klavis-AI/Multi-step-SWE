<?php
// Reachability bitmaps (gitformat-pack(5)).  A .bitmap beside a pack (or a midx
// bitmap) with four type bitmaps and per-commit reachability bitmaps, all EWAH.
// Bit position N == the N-th object in pack order (the .rev order); each
// commit entry names its commit by its position in the OID-sorted .idx.

const BITMAP_SIGNATURE = "BITM";
const BITMAP_OPT_FULL_DAG = 0x1;

class BitmapIndex {
    public Repo $repo;
    public array $oids = [];        // OID-sorted object positions
    public array $pos = [];         // oid => bit position (pack order)
    public array $idxPos = [];      // oid => position in the .idx (OID order)
    public string $checksum = '';   // pack/midx trailer this bitmap is for

    public function __construct(Repo $repo) { $this->repo = $repo; }

    /** Object positions from a pack's .idx (already OID-sorted). */
    public function fromPack(PackIndex $p) {
        // Bit N is the N-th object in PACK order (ascending offset), the order
        // the .rev file records -- not the .idx's OID order.  Real git reads
        // every bit position through the reverse index, so an OID-ordered
        // bitmap types and reaches the wrong objects.
        $byOffset = [];
        $this->idxPos = [];
        for ($i = 0; $i < $p->nobjects; $i++) {
            $o = $p->oidAt($i);
            $byOffset[$o] = $p->offsetAt($i);
            $this->idxPos[$o] = $i;   // an entry names its commit by .idx position
        }
        asort($byOffset, SORT_NUMERIC);
        $this->oids = array_keys($byOffset);
        $this->pos = [];
        foreach ($this->oids as $i => $o) $this->pos[$o] = $i;
        // the .bitmap checksum is the pack's trailing checksum
        $data = @file_get_contents($p->packPath);
        $this->checksum = $data === false ? '' : substr($data, -$this->repo->rawsz);
    }

    /** All objects reachable from a commit, as bit positions in this index. */
    private function reachable(string $commitOid): array {
        $bits = [];
        $walk = new RevWalk($this->repo);
        $c = peel_to_commit($this->repo, $commitOid);
        if ($c === null) return $bits;
        $walk->push($c);
        foreach ($walk->commits() as $oid => $co) {
            if (isset($this->pos[$oid])) $bits[$this->pos[$oid]] = true;
            $t = [];
            collect_tree_objects($this->repo, $co->tree, '', $t);
            foreach (array_keys($t) as $o) if (isset($this->pos[$o])) $bits[$this->pos[$o]] = true;
        }
        $b = array_keys($bits);
        sort($b);
        return $b;
    }

    /** Bit positions of each object type. */
    private function typeBits(): array {
        $t = ['commit' => [], 'tree' => [], 'blob' => [], 'tag' => []];
        foreach ($this->oids as $i => $o) {
            $h = $this->repo->odb->readHeader($o);
            if ($h !== null && isset($t[$h[0]])) $t[$h[0]][] = $i;
        }
        return $t;
    }

    /** Write <pack>.bitmap for a set of commit oids (typically ref tips). */
    public function write(string $bitmapPath, array $commitOids) {
        $n = count($this->oids);
        $types = $this->typeBits();
        $body = Ewah::fromBits($types['commit'], $n)->serialize();
        $body .= Ewah::fromBits($types['tree'], $n)->serialize();
        $body .= Ewah::fromBits($types['blob'], $n)->serialize();
        $body .= Ewah::fromBits($types['tag'], $n)->serialize();
        // one entry per selected commit, no XOR (xor_offset 0)
        $entries = '';
        $count = 0;
        $seen = [];
        foreach ($commitOids as $co) {
            $c = peel_to_commit($this->repo, $co);
            if ($c === null || !isset($this->pos[$c]) || isset($seen[$c])) continue;
            $seen[$c] = true;
            $bits = $this->reachable($c);
            $entries .= be32($this->idxPos[$c]) . chr(0) . chr(0) . Ewah::fromBits($bits, $n)->serialize();
            $count++;
        }
        $header = BITMAP_SIGNATURE . be16(1) . be16(BITMAP_OPT_FULL_DAG) . be32($count) . $this->checksum;
        $file = $header . $body . $entries;
        // git writes the .bitmap through a hashfile, so it ends with the SHA of
        // its own contents (fsck checks this).
        $algo = $this->repo->rawsz === 32 ? 'sha256' : 'sha1';
        $file .= hash($algo, $file, true);
        write_file_atomic($bitmapPath, $file, 0444);
    }
}

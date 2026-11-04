<?php
// EWAH (Enhanced Word-Aligned Hybrid) compressed bitmaps, in git's on-disk
// layout (ewah/ewah_io.c).  64-bit words; PHP ints are 64-bit, so mask/shift
// with care.  Used by reachability bitmaps (gitformat-pack(5)).

const EWAH_WORD_BITS = 64;
const EWAH_RUNNING_BITS = 32;                       // sizeof(eword)*4
// literal-word count occupies bits 33..63 (31 bits)

/** A growable set of bit positions, encoded to/from git's EWAH format. */
class Ewah {
    /** @var int[] plain 64-bit words (little bit index within a word is bit 0) */
    public array $words = [];
    public int $bitSize = 0;

    /** Build from a sorted list of set bit positions. */
    public static function fromBits(array $bits, int $nbits): Ewah {
        $e = new Ewah();
        $e->bitSize = $nbits;
        $nwords = intdiv($nbits + 63, 64);
        $w = array_fill(0, max(1, $nwords), 0);
        foreach ($bits as $b) {
            if ($b < 0 || $b >= $nbits) continue;
            $w[$b >> 6] |= (1 << ($b & 63));
        }
        $e->words = $w;
        return $e;
    }

    /** The set bit positions. */
    public function bits(): array {
        $out = [];
        foreach ($this->words as $i => $word) {
            if ($word === 0) continue;
            for ($j = 0; $j < 64; $j++) {
                if ($word & (1 << $j)) $out[] = $i * 64 + $j;
            }
        }
        return $out;
    }

    /** Serialize to git's EWAH format (big-endian). */
    public function serialize(): string {
        // compress: emit RLW runs of all-0/all-1 words + literal words
        $buffer = [];   // 64-bit compressed words
        $words = $this->words;
        $n = count($words);
        $i = 0;
        $ALL1 = -1;   // 0xFFFFFFFFFFFFFFFF as signed
        while ($i < $n) {
            // count a clean run (all 0 or all 1)
            $runBit = 0;
            $runLen = 0;
            if ($words[$i] === 0 || $words[$i] === $ALL1) {
                $runBit = ($words[$i] === $ALL1) ? 1 : 0;
                $target = $runBit ? $ALL1 : 0;
                while ($i < $n && $words[$i] === $target && $runLen < 0xFFFFFFFF) { $runLen++; $i++; }
            }
            // collect the following literal words (until next clean word)
            $lit = [];
            while ($i < $n && $words[$i] !== 0 && $words[$i] !== $ALL1 && count($lit) < 0x7FFFFFFF) {
                $lit[] = $words[$i]; $i++;
            }
            $rlw = ($runBit & 1) | (($runLen & 0xFFFFFFFF) << 1) | (count($lit) << (EWAH_RUNNING_BITS + 1));
            $buffer[] = $rlw;
            foreach ($lit as $l) $buffer[] = $l;
        }
        $out = be32($this->bitSize) . be32(count($buffer));
        foreach ($buffer as $w) $out .= self::be64s($w);
        // trailer: 32-bit position of the last RLW (git uses it for appends).
        // We track it as the index of the final RLW word; a decoder ignores it.
        $out .= be32(0);
        return $out;
    }

    /** Parse git's EWAH format; returns [Ewah, bytesConsumed]. */
    public static function parse(string $d, int $off): array {
        $bitSize = rd32($d, $off);
        $wordCount = rd32($d, $off + 4);
        $p = $off + 8;
        $e = new Ewah();
        $e->bitSize = $bitSize;
        $nwords = intdiv($bitSize + 63, 64);
        $words = array_fill(0, max(1, $nwords), 0);
        $wi = 0;
        $consumed = 0;
        while ($consumed < $wordCount) {
            $rlw = self::rd64s($d, $p + $consumed * 8);
            $consumed++;
            $runBit = $rlw & 1;
            $runLen = ($rlw >> 1) & 0xFFFFFFFF;
            $litCount = ($rlw >> (EWAH_RUNNING_BITS + 1)) & 0x7FFFFFFF;
            for ($k = 0; $k < $runLen && $wi < $nwords; $k++) $words[$wi++] = $runBit ? -1 : 0;
            for ($k = 0; $k < $litCount && $consumed < $wordCount; $k++) {
                $lw = self::rd64s($d, $p + $consumed * 8);
                $consumed++;
                if ($wi < $nwords) $words[$wi++] = $lw;
            }
        }
        $e->words = $words;
        $total = 8 + $wordCount * 8 + 4;   // header + words + trailing rlw pos
        return [$e, $total];
    }

    private static function be64s(int $v): string { return pack('J', $v); }
    private static function rd64s(string $s, int $off): int { return unpack('J', substr($s, $off, 8))[1]; }
}

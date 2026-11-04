<?php
// Content diff (Myers, linear space) and the output formats git-diff(1)
// documents: raw, patch, name-status, stat, numstat.

/** @return array list of [type, aIndex, bIndex] with type in {'=','-','+'} */
function diff_sequences(array $a, array $b): array {
    $n = count($a); $m = count($b);
    $script = [];
    $rec = function (int $a0, int $an, int $b0, int $bn) use (&$rec, &$script, $a, $b) {
        // trim common prefix/suffix
        while ($a0 < $an && $b0 < $bn && $a[$a0] === $b[$b0]) { $script[] = ['=', $a0, $b0]; $a0++; $b0++; }
        $tailA = []; $tailB = [];
        while ($a0 < $an && $b0 < $bn && $a[$an - 1] === $b[$bn - 1]) { $an--; $bn--; $tailA[] = $an; $tailB[] = $bn; }
        if ($a0 === $an) {
            for ($i = $b0; $i < $bn; $i++) $script[] = ['+', $a0, $i];
        } elseif ($b0 === $bn) {
            for ($i = $a0; $i < $an; $i++) $script[] = ['-', $i, $b0];
        } else {
            [$x, $y] = diff_middle_snake($a, $a0, $an, $b, $b0, $bn);
            $rec($a0, $x, $b0, $y);
            $rec($x, $an, $y, $bn);
        }
        for ($i = count($tailA) - 1; $i >= 0; $i--) $script[] = ['=', $tailA[$i], $tailB[$i]];
    };
    $rec(0, $n, 0, $m);
    return $script;
}

function diff_middle_snake(array $a, int $a0, int $an, array $b, int $b0, int $bn): array {
    $N = $an - $a0; $M = $bn - $b0;
    $delta = $N - $M;
    $odd = ($delta & 1) !== 0;
    $max = (int)(($N + $M + 1) / 2) + 1;
    $vf = []; $vb = [];
    $vf[1] = 0; $vb[1] = 0;
    for ($d = 0; $d <= $max; $d++) {
        for ($k = -$d; $k <= $d; $k += 2) {
            if ($k === -$d || ($k !== $d && ($vf[$k - 1] ?? -1) < ($vf[$k + 1] ?? -1))) $x = $vf[$k + 1] ?? 0;
            else $x = ($vf[$k - 1] ?? 0) + 1;
            $y = $x - $k;
            $x0 = $x; $y0 = $y;
            while ($x < $N && $y < $M && $a[$a0 + $x] === $b[$b0 + $y]) { $x++; $y++; }
            $vf[$k] = $x;
            if ($odd && $k >= $delta - ($d - 1) && $k <= $delta + ($d - 1)) {
                if (($vb[$delta - $k] ?? -1) >= 0 && $x + ($vb[$delta - $k] ?? 0) >= $N) {
                    return [$a0 + $x0, $b0 + $y0];
                }
            }
        }
        for ($k = -$d; $k <= $d; $k += 2) {
            if ($k === -$d || ($k !== $d && ($vb[$k - 1] ?? -1) < ($vb[$k + 1] ?? -1))) $x = $vb[$k + 1] ?? 0;
            else $x = ($vb[$k - 1] ?? 0) + 1;
            $y = $x - $k;
            while ($x < $N && $y < $M && $a[$an - $x - 1] === $b[$bn - $y - 1]) { $x++; $y++; }
            $vb[$k] = $x;
            if (!$odd && $k >= $delta - $d && $k <= $delta + $d) {
                if (($vf[$delta - $k] ?? -1) >= 0 && ($vf[$delta - $k] ?? 0) + $x >= $N) {
                    return [$an - $x, $bn - $y];
                }
            }
        }
    }
    return [$an, $bn];
}

function split_lines(string $s): array {
    if ($s === '') return [];
    $lines = explode("\n", $s);
    $last = array_pop($lines);
    $out = [];
    foreach ($lines as $l) $out[] = $l . "\n";
    if ($last !== '') $out[] = $last;    // no trailing newline
    return $out;
}

/** Unified hunks for two line arrays. */
function diff_unified(array $al, array $bl, int $context = 3): array {
    $script = diff_sequences($al, $bl);
    $hunks = [];
    $cur = null;
    $pending = [];
    $lastEqRun = 0;
    foreach ($script as $op) {
        if ($op[0] === '=') {
            if ($cur !== null) {
                $pending[] = $op;
                if (count($pending) > $context * 2) {
                    // close the hunk
                    $keep = array_slice($pending, 0, $context);
                    foreach ($keep as $k) { $cur['lines'][] = [' ', $al[$k[1]]]; $cur['alen']++; $cur['blen']++; }
                    $hunks[] = $cur;
                    $cur = null;
                    $pending = [];
                }
            } else {
                $pending[] = $op;
                if (count($pending) > $context) array_shift($pending);
            }
            continue;
        }
        if ($cur === null) {
            $startA = count($pending) ? $pending[0][1] : $op[1];
            $startB = count($pending) ? $pending[0][2] : $op[2];
            $cur = ['astart' => $startA, 'bstart' => $startB, 'alen' => 0, 'blen' => 0, 'lines' => []];
            foreach ($pending as $p) { $cur['lines'][] = [' ', $al[$p[1]]]; $cur['alen']++; $cur['blen']++; }
            $pending = [];
        } else {
            foreach ($pending as $p) { $cur['lines'][] = [' ', $al[$p[1]]]; $cur['alen']++; $cur['blen']++; }
            $pending = [];
        }
        if ($op[0] === '-') { $cur['lines'][] = ['-', $al[$op[1]]]; $cur['alen']++; }
        else { $cur['lines'][] = ['+', $bl[$op[2]]]; $cur['blen']++; }
    }
    if ($cur !== null) {
        $keep = array_slice($pending, 0, $context);
        foreach ($keep as $k) { $cur['lines'][] = [' ', $al[$k[1]]]; $cur['alen']++; $cur['blen']++; }
        $hunks[] = $cur;
    }
    return $hunks;
}

function is_binary_content(string $s): bool {
    $probe = substr($s, 0, 8000);
    return strpos($probe, "\0") !== false;
}

/** xdiff's default "function name" for a hunk header. */
function hunk_funcname(array $lines, int $hunkStart): string {
    for ($i = $hunkStart - 1; $i >= 0; $i--) {
        $line = rtrim($lines[$i], "\r\n");
        if ($line === '') continue;
        $c = $line[0];
        if (($c >= 'a' && $c <= 'z') || ($c >= 'A' && $c <= 'Z') || $c === '_' || $c === '$') {
            return substr(rtrim($line), 0, 40);
        }
    }
    return '';
}

function format_unified_diff(string $aPath, string $bPath, string $aData, string $bData, int $context = 3): string {
    $al = split_lines($aData);
    $bl = split_lines($bData);
    $hunks = diff_unified($al, $bl, $context);
    $out = '';
    foreach ($hunks as $h) {
        $as = $h['alen'] === 0 ? $h['astart'] : $h['astart'] + 1;
        $bs = $h['blen'] === 0 ? $h['bstart'] : $h['bstart'] + 1;
        $func = hunk_funcname($al, $h['astart']);
        $out .= '@@ -' . $as . ($h['alen'] === 1 ? '' : ',' . $h['alen'])
              . ' +' . $bs . ($h['blen'] === 1 ? '' : ',' . $h['blen']) . " @@"
              . ($func === '' ? '' : ' ' . $func) . "\n";
        foreach ($h['lines'] as $l) {
            $text = $l[1];
            $noNL = substr($text, -1) !== "\n";
            $out .= $l[0] . ($noNL ? $text . "\n\\ No newline at end of file\n" : $text);
        }
    }
    return $out;
}

function count_line_changes(string $aData, string $bData): array {
    $al = split_lines($aData); $bl = split_lines($bData);
    $script = diff_sequences($al, $bl);
    $add = 0; $del = 0;
    foreach ($script as $op) {
        if ($op[0] === '+') $add++;
        elseif ($op[0] === '-') $del++;
    }
    return [$add, $del];
}

class DiffEntry {
    public string $status = 'M';   // A, D, M, T, R, C
    public string $path = '';
    public string $oldPath = '';
    public string $oldMode = '000000';
    public string $newMode = '000000';
    public string $oldOid = '';
    public string $newOid = '';
    public int $score = 0;
}

function diff_mode_str($mode): string {
    if (is_int($mode)) return sprintf('%06o', $mode);
    if ($mode === '' || $mode === '0') return '000000';
    return sprintf('%06o', octdec($mode));
}

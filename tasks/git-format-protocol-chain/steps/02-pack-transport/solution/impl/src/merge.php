<?php
// Merge base computation, three-way tree merge and three-way content merge
// (git-merge(1), git-merge-base(1), git-merge-file(1)).

function commit_parents(Repo $repo, string $oid): array {
    $o = $repo->odb->read($oid);
    if ($o === null || $o[0] !== 'commit') return [];
    return CommitObj::parse($o[1])->parents;
}
function commit_time(Repo $repo, string $oid): int {
    $o = $repo->odb->read($oid);
    if ($o === null || $o[0] !== 'commit') return 0;
    $c = CommitObj::parse($o[1]);
    return $c->committer ? $c->committer->ts : 0;
}

/** All merge bases of two commits (git's paint-down algorithm, simplified). */
function merge_bases(Repo $repo, string $a, string $b): array {
    if ($a === $b) return [$a];
    $reach = [];
    $mark = function (string $start, int $bit) use (&$reach, $repo) {
        $stack = [$start];
        while ($stack) {
            $o = array_pop($stack);
            if (isset($reach[$o]) && ($reach[$o] & $bit)) continue;
            $reach[$o] = ($reach[$o] ?? 0) | $bit;
            foreach (commit_parents($repo, $o) as $p) $stack[] = $p;
        }
    };
    $mark($a, 1);
    $mark($b, 2);
    $common = [];
    foreach ($reach as $oid => $bits) if ($bits === 3) $common[$oid] = true;
    if (!$common) return [];
    // drop ancestors of other common commits
    $redundant = [];
    foreach (array_keys($common) as $c) {
        $stack = commit_parents($repo, $c);
        $seen = [];
        while ($stack) {
            $p = array_pop($stack);
            if (isset($seen[$p])) continue;
            $seen[$p] = true;
            if (isset($common[$p])) $redundant[$p] = true;
            foreach (commit_parents($repo, $p) as $pp) $stack[] = $pp;
        }
    }
    $out = [];
    foreach (array_keys($common) as $c) if (!isset($redundant[$c])) $out[] = $c;
    usort($out, function ($x, $y) use ($repo) { return commit_time($repo, $y) <=> commit_time($repo, $x); });
    return $out;
}

function is_ancestor(Repo $repo, string $anc, string $desc): bool {
    if ($anc === $desc) return true;
    $stack = [$desc]; $seen = [];
    while ($stack) {
        $o = array_pop($stack);
        if (isset($seen[$o])) continue;
        $seen[$o] = true;
        if ($o === $anc) return true;
        foreach (commit_parents($repo, $o) as $p) $stack[] = $p;
    }
    return false;
}

class MergeResult {
    public array $entries = [];     // path => [stage => [mode, oid]]
    public array $conflicts = [];   // path => reason
    public array $files = [];       // path => [mode, content-or-null, oid]
    public array $removed = [];
}

/** Three-way merge of flattened trees. */
function merge_trees_flat(Repo $repo, array $base, array $ours, array $theirs, string $ourLabel = 'ours', string $theirLabel = 'theirs'): MergeResult {
    $res = new MergeResult();
    $paths = array_unique(array_merge(array_keys($base), array_keys($ours), array_keys($theirs)));
    sort($paths, SORT_STRING);
    foreach ($paths as $p) {
        $b = $base[$p] ?? null;
        $o = $ours[$p] ?? null;
        $t = $theirs[$p] ?? null;
        if ($o !== null && $t !== null && $o[0] === $t[0] && $o[1] === $t[1]) {
            $res->entries[$p] = [0 => $o];
            continue;
        }
        if ($b !== null && $o !== null && $b[0] === $o[0] && $b[1] === $o[1]) {
            // ours unchanged: take theirs (possibly a deletion)
            if ($t === null) { $res->removed[$p] = true; continue; }
            $res->entries[$p] = [0 => $t];
            continue;
        }
        if ($b !== null && $t !== null && $b[0] === $t[0] && $b[1] === $t[1]) {
            if ($o === null) { $res->removed[$p] = true; continue; }
            $res->entries[$p] = [0 => $o];
            continue;
        }
        if ($b === null && $o !== null && $t === null) { $res->entries[$p] = [0 => $o]; continue; }
        if ($b === null && $o === null && $t !== null) { $res->entries[$p] = [0 => $t]; continue; }
        if ($o === null && $t === null) { $res->removed[$p] = true; continue; }
        // both sides changed
        if ($o !== null && $t !== null && $o[0] === $t[0] && $o[0] === '100644' || ($o !== null && $t !== null && octdec($o[0]) === 0100644 && octdec($t[0]) === 0100644)) {
            $bc = $b === null ? '' : ($repo->odb->read($b[1])[1] ?? '');
            $oc = $repo->odb->read($o[1])[1] ?? '';
            $tc = $repo->odb->read($t[1])[1] ?? '';
            [$merged, $conflicted] = merge_content($bc, $oc, $tc, $ourLabel, $theirLabel);
            if (!$conflicted) {
                $oid = $repo->odb->write('blob', $merged);
                $res->entries[$p] = [0 => [$o[0], $oid]];
                $res->files[$p] = [$o[0], $merged];
                continue;
            }
            $res->conflicts[$p] = 'content';
            $res->files[$p] = [$o[0], $merged];
            $stages = [];
            if ($b !== null) $stages[1] = $b;
            $stages[2] = $o; $stages[3] = $t;
            $res->entries[$p] = $stages;
            continue;
        }
        $res->conflicts[$p] = 'add/add or modify/delete';
        $stages = [];
        if ($b !== null) $stages[1] = $b;
        if ($o !== null) $stages[2] = $o;
        if ($t !== null) $stages[3] = $t;
        $res->entries[$p] = $stages;
        if ($o !== null) $res->files[$p] = [$o[0], $repo->odb->read($o[1])[1] ?? ''];
        elseif ($t !== null) $res->files[$p] = [$t[0], $repo->odb->read($t[1])[1] ?? ''];
    }
    return $res;
}

/** Change regions of a diff against the base: [baseStart, baseEnd, newLines]. */
function diff_edits(array $base, array $other): array {
    $script = diff_sequences($base, $other);
    $edits = [];
    $cur = null;
    foreach ($script as $op) {
        if ($op[0] === '=') {
            if ($cur !== null) { $edits[] = $cur; $cur = null; }
            continue;
        }
        if ($op[0] === '-') {
            if ($cur === null) $cur = [$op[1], $op[1] + 1, []];
            else $cur[1] = max($cur[1], $op[1] + 1);
        } else {
            if ($cur === null) $cur = [$op[1], $op[1], []];
            $cur[2][] = $other[$op[2]];
        }
    }
    if ($cur !== null) $edits[] = $cur;
    return $edits;
}

/** Three-way content merge (diff3); returns [text, hadConflict]. */
function merge_content(string $base, string $ours, string $theirs, string $ourLabel, string $theirLabel, bool $diff3 = false): array {
    $b = split_lines($base); $o = split_lines($ours); $t = split_lines($theirs);
    $oe = diff_edits($b, $o);
    $te = diff_edits($b, $t);
    $out = '';
    $conflict = false;
    $pos = 0;      // next base line to copy
    $i = 0; $j = 0;
    while ($i < count($oe) || $j < count($te)) {
        if ($i < count($oe) && $j < count($te)) {
            $pick = ($oe[$i][0] <= $te[$j][0]) ? 'o' : 't';
        } elseif ($i < count($oe)) $pick = 'o';
        else $pick = 't';
        $start = $pick === 'o' ? $oe[$i][0] : $te[$j][0];
        $end = $pick === 'o' ? $oe[$i][1] : $te[$j][1];
        $oGroup = []; $tGroup = [];
        if ($pick === 'o') { $oGroup[] = $oe[$i]; $i++; } else { $tGroup[] = $te[$j]; $j++; }
        // expand while the other side overlaps (or abuts) the region
        $changed = true;
        while ($changed) {
            $changed = false;
            while ($i < count($oe) && $oe[$i][0] <= $end) {
                $end = max($end, $oe[$i][1]); $oGroup[] = $oe[$i]; $i++; $changed = true;
            }
            while ($j < count($te) && $te[$j][0] <= $end) {
                $end = max($end, $te[$j][1]); $tGroup[] = $te[$j]; $j++; $changed = true;
            }
        }
        for ($k = $pos; $k < $start && $k < count($b); $k++) $out .= $b[$k];
        $pos = $end;
        $oText = apply_edits($b, $oGroup, $start, $end);
        $tText = apply_edits($b, $tGroup, $start, $end);
        if (!$oGroup) { $out .= $tText; continue; }
        if (!$tGroup) { $out .= $oText; continue; }
        if ($oText === $tText) { $out .= $oText; continue; }
        $conflict = true;
        $out .= "<<<<<<< $ourLabel\n" . $oText;
        if ($diff3) {
            $out .= "||||||| " . "base\n";
            for ($k = $start; $k < $end && $k < count($b); $k++) $out .= $b[$k];
        }
        $out .= "=======\n" . $tText . ">>>>>>> $theirLabel\n";
    }
    for ($k = $pos; $k < count($b); $k++) $out .= $b[$k];
    return [$out, $conflict];
}

/** Render base[start,end) with the given edits applied. */
function apply_edits(array $b, array $edits, int $start, int $end): string {
    $out = '';
    $pos = $start;
    foreach ($edits as $e) {
        for ($k = $pos; $k < $e[0] && $k < count($b); $k++) $out .= $b[$k];
        $out .= implode('', $e[2]);
        $pos = max($pos, $e[1]);
    }
    for ($k = $pos; $k < $end && $k < count($b); $k++) $out .= $b[$k];
    return $out;
}

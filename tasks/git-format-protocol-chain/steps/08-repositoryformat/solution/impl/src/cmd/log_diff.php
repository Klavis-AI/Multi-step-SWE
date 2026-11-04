<?php
// git-log(1), git-show(1), git-diff(1) and the diff-* plumbing commands.

function git_date_default(int $ts, string $tz): string {
    $off = (int)substr($tz, 1, 2) * 3600 + (int)substr($tz, 3, 2) * 60;
    if ($tz[0] === '-') $off = -$off;
    $dt = new DateTime('@' . ($ts + $off));
    $dt->setTimezone(new DateTimeZone('UTC'));
    return $dt->format('D M j H:i:s Y') . ' ' . $tz;
}

function git_date_iso(int $ts, string $tz): string {
    $off = (int)substr($tz, 1, 2) * 3600 + (int)substr($tz, 3, 2) * 60;
    if ($tz[0] === '-') $off = -$off;
    $dt = new DateTime('@' . ($ts + $off));
    $dt->setTimezone(new DateTimeZone('UTC'));
    return $dt->format('Y-m-d H:i:s') . ' ' . $tz;
}

function format_commit(Repo $repo, CommitObj $c, string $pretty, bool $withDiffHeader = true, ?array $opts = null): string {
    $oid = $c->oid;
    $abbrev = 7;
    if (strncmp($pretty, 'format:', 7) === 0 || strncmp($pretty, 'tformat:', 8) === 0) {
        $fmt = substr($pretty, strpos($pretty, ':') + 1);
        return expand_commit_format($repo, $c, $fmt) . "\n";
    }
    if (!in_array($pretty, ['oneline', 'short', 'medium', 'full', 'fuller', 'raw', 'reference', 'email'], true)) {
        // `--format=<string>` is `--pretty=tformat:<string>`
        return expand_commit_format($repo, $c, $pretty) . "\n";
    }
    switch ($pretty) {
        case 'oneline':
            return "$oid " . $c->subject() . "\n";
        case 'raw': {
            $s = "commit $oid\n";
            $s .= "tree {$c->tree}\n";
            foreach ($c->parents as $p) $s .= "parent $p\n";
            if ($c->author) $s .= 'author ' . $c->author->format() . "\n";
            if ($c->committer) $s .= 'committer ' . $c->committer->format() . "\n";
            $s .= "\n";
            foreach (explode("\n", rtrim($c->message, "\n")) as $l) $s .= '    ' . $l . "\n";
            return $s . "\n";
        }
        case 'short': {
            $s = "commit $oid\n";
            if ($c->author) $s .= 'Author: ' . $c->author->name . ' <' . $c->author->email . ">\n";
            $s .= "\n";
            $s .= '    ' . $c->subject() . "\n\n";
            return $s;
        }
        case 'full': case 'fuller': {
            $s = "commit $oid\n";
            if ($c->author) $s .= 'Author: ' . $c->author->name . ' <' . $c->author->email . ">\n";
            if ($pretty === 'fuller' && $c->author) $s .= 'AuthorDate: ' . git_date_default($c->author->ts, $c->author->tz) . "\n";
            if ($c->committer) $s .= 'Commit: ' . $c->committer->name . ' <' . $c->committer->email . ">\n";
            if ($pretty === 'fuller' && $c->committer) $s .= 'CommitDate: ' . git_date_default($c->committer->ts, $c->committer->tz) . "\n";
            $s .= "\n";
            foreach (explode("\n", rtrim($c->message, "\n")) as $l) $s .= '    ' . $l . "\n";
            return $s . "\n";
        }
        default: { // medium
            $s = "commit $oid\n";
            if (count($c->parents) > 1) {
                $s .= 'Merge:';
                foreach ($c->parents as $p) $s .= ' ' . substr($p, 0, $abbrev);
                $s .= "\n";
            }
            if ($c->author) $s .= 'Author: ' . $c->author->name . ' <' . $c->author->email . ">\n";
            if ($c->author) $s .= 'Date:   ' . git_date_default($c->author->ts, $c->author->tz) . "\n";
            $s .= "\n";
            foreach (explode("\n", rtrim($c->message, "\n")) as $l) $s .= '    ' . $l . "\n";
            return $s . "\n";
        }
    }
}

function expand_commit_format(Repo $repo, CommitObj $c, string $fmt): string {
    $map = [
        '%H' => $c->oid,
        '%h' => substr($c->oid, 0, 7),
        '%T' => $c->tree,
        '%t' => substr($c->tree, 0, 7),
        '%P' => implode(' ', $c->parents),
        '%p' => implode(' ', array_map(function ($p) { return substr($p, 0, 7); }, $c->parents)),
        '%an' => $c->author ? $c->author->name : '',
        '%ae' => $c->author ? $c->author->email : '',
        '%ad' => $c->author ? git_date_default($c->author->ts, $c->author->tz) : '',
        '%aI' => $c->author ? date('c', $c->author->ts) : '',
        '%at' => $c->author ? (string)$c->author->ts : '',
        '%cn' => $c->committer ? $c->committer->name : '',
        '%ce' => $c->committer ? $c->committer->email : '',
        '%cd' => $c->committer ? git_date_default($c->committer->ts, $c->committer->tz) : '',
        '%ct' => $c->committer ? (string)$c->committer->ts : '',
        '%s' => $c->subject(),
        '%b' => commit_body($c),
        '%B' => $c->message,
        '%n' => "\n",
        '%%' => '%',
        '%d' => '',
    ];
    return strtr($fmt, $map);
}

function commit_body(CommitObj $c): string {
    $m = ltrim($c->message, "\n");
    $end = strpos($m, "\n\n");
    return $end === false ? '' : substr($m, $end + 2);
}

/** Render a set of DiffEntry rows in the requested format. */
function emit_diff(Repo $repo, array $entries, array $opts): string {
    $out = '';
    $raw = $opts['raw'] ?? false;
    $patch = $opts['patch'] ?? false;
    $nameOnly = $opts['name_only'] ?? false;
    $nameStatus = $opts['name_status'] ?? false;
    $stat = $opts['stat'] ?? false;
    $numstat = $opts['numstat'] ?? false;
    $shortstat = $opts['shortstat'] ?? false;
    $z = $opts['z'] ?? false;
    $abbrev = $opts['abbrev'] ?? 7;
    $srcPrefix = $opts['src_prefix'] ?? 'a/';
    $dstPrefix = $opts['dst_prefix'] ?? 'b/';
    $context = $opts['context'] ?? 3;
    $term = $z ? "\0" : "\n";
    $statRows = [];
    $totalAdd = 0; $totalDel = 0;
    foreach ($entries as $d) {
        $path = $d->path;
        if ($nameOnly) { $out .= quote_path_maybe($path, $term) . $term; continue; }
        if ($nameStatus) {
            $st = $d->status . ($d->status === 'R' ? sprintf('%03d', $d->score) : '');
            if ($d->status === 'R') $out .= $st . ($z ? "\0" : "\t") . quote_path_maybe($d->oldPath, $term) . ($z ? "\0" : "\t") . quote_path_maybe($path, $term) . $term;
            else $out .= $st . ($z ? "\0" : "\t") . quote_path_maybe($path, $term) . $term;
            continue;
        }
        if ($raw) {
            $src = $abbrev ? substr($d->oldOid, 0, $abbrev) : $d->oldOid;
            $dst = $abbrev ? substr($d->newOid, 0, $abbrev) : $d->newOid;
            $st = $d->status . ($d->status === 'R' ? sprintf('%03d', $d->score) : '');
            $line = ':' . $d->oldMode . ' ' . $d->newMode . ' ' . $src . ' ' . $dst . ' ' . $st;
            if ($d->status === 'R') $out .= $line . ($z ? "\0" : "\t") . quote_path_maybe($d->oldPath, $term) . ($z ? "\0" : "\t") . quote_path_maybe($path, $term) . $term;
            else $out .= $line . ($z ? "\0" : "\t") . quote_path_maybe($path, $term) . $term;
            continue;
        }
        // content based formats need the blobs
        $oldData = $d->oldOid === '' || preg_match('/^0+$/', $d->oldOid) ? '' : ($repo->odb->read($d->oldOid)[1] ?? '');
        $newData = '';
        if ($d->newOid !== '' && !preg_match('/^0+$/', $d->newOid)) {
            $o = $repo->odb->read($d->newOid);
            if ($o !== null) $newData = $o[1];
            elseif (isset($opts['worktree']) && $opts['worktree'] && $repo->worktree !== null) {
                $st2 = lstat_info($repo->worktree . '/' . $path);
                if ($st2 !== null) $newData = wt_read_blob($repo->worktree . '/' . $path, $st2);
            }
        }
        if ($stat || $numstat || $shortstat) {
            if (is_binary_content($oldData) || is_binary_content($newData)) {
                $statRows[] = [quote_c_style_if_needed($path), 0, 0, true, strlen($oldData), strlen($newData)];
            } else {
                [$a, $r] = count_line_changes($oldData, $newData);
                $totalAdd += $a; $totalDel += $r;
                $statRows[] = [quote_c_style_if_needed($path), $a, $r, false, 0, 0];
            }
            continue;
        }
        if ($patch) $out .= format_patch_entry($repo, $d, $oldData, $newData, $srcPrefix, $dstPrefix, $context, $abbrev);
    }
    if (($stat || $shortstat) && $statRows) {
        if ($stat) $out .= render_diffstat($statRows, $opts);
        $n = count($statRows);
        $out .= ' ' . $n . ' file' . ($n === 1 ? '' : 's') . ' changed';
        if ($totalAdd) $out .= ', ' . $totalAdd . ' insertion' . ($totalAdd === 1 ? '' : 's') . '(+)';
        if ($totalDel) $out .= ', ' . $totalDel . ' deletion' . ($totalDel === 1 ? '' : 's') . '(-)';
        $out .= "\n";
    }
    if ($numstat) {
        foreach ($statRows as $row) {
            $p = $row[0];
            if ($row[3]) $out .= "-\t-\t" . $p . $term;
            else $out .= $row[1] . "\t" . $row[2] . "\t" . $p . $term;
        }
    }
    return $out;
}

function format_patch_entry(Repo $repo, DiffEntry $d, string $oldData, string $newData, string $sp, string $dp, int $context, int $abbrev): string {
    $path = $d->path;
    $oldPath = $d->status === 'R' ? $d->oldPath : $path;
    $out = 'diff --git ' . $sp . quote_path_maybe($oldPath, "\n") . ' ' . $dp . quote_path_maybe($path, "\n") . "\n";
    $isNew = $d->status === 'A';
    $isDel = $d->status === 'D';
    if ($isNew) $out .= 'new file mode ' . $d->newMode . "\n";
    elseif ($isDel) $out .= 'deleted file mode ' . $d->oldMode . "\n";
    elseif ($d->oldMode !== $d->newMode && $d->oldMode !== '000000' && $d->newMode !== '000000') {
        $out .= 'old mode ' . $d->oldMode . "\n" . 'new mode ' . $d->newMode . "\n";
    }
    if ($d->status === 'R') {
        $out .= 'similarity index ' . $d->score . "%\n";
        $out .= 'rename from ' . $d->oldPath . "\n" . 'rename to ' . $path . "\n";
    }
    if ($oldData === $newData) return $out;
    $bin = is_binary_content($oldData) || is_binary_content($newData);
    $oldAbbrev = substr($d->oldOid, 0, $abbrev);
    $newAbbrev = substr($d->newOid, 0, $abbrev);
    if ($isNew) $oldAbbrev = str_repeat('0', $abbrev);
    if ($isDel) $newAbbrev = str_repeat('0', $abbrev);
    $modeSuffix = '';
    if (!$isNew && !$isDel && $d->oldMode === $d->newMode) $modeSuffix = ' ' . $d->oldMode;
    $out .= 'index ' . $oldAbbrev . '..' . $newAbbrev . $modeSuffix . "\n";
    if ($bin) {
        $out .= 'Binary files ' . ($isNew ? '/dev/null' : $sp . $oldPath) . ' and ' . ($isDel ? '/dev/null' : $dp . $path) . " differ\n";
        return $out;
    }
    $out .= '--- ' . ($isNew ? '/dev/null' : $sp . quote_path_maybe($oldPath, "\n")) . "\n";
    $out .= '+++ ' . ($isDel ? '/dev/null' : $dp . quote_path_maybe($path, "\n")) . "\n";
    $out .= format_unified_diff($oldPath, $path, $oldData, $newData, $context);
    return $out;
}


function decimal_width(int $n): int { return strlen((string)$n); }

function scale_linear(int $it, int $width, int $maxChange): int {
    if (!$it) return 0;
    return 1 + intdiv($it * ($width - 1), $maxChange);
}

/** Port of git's show_stats() column layout (diff.c). */
function render_diffstat(array $rows, array $opts): string {
    $width = $opts['stat_width'] ?? 80;
    $maxLen = 0; $maxChange = 0; $numberWidth = 0; $binWidth = 0;
    foreach ($rows as $r) {
        $len = strlen($r[0]);
        if ($len > $maxLen) $maxLen = $len;
        if ($r[3]) {
            $w = 14 + decimal_width($r[5]) + decimal_width($r[4]);
            if ($binWidth < $w) $binWidth = $w;
            $numberWidth = 3;
            continue;
        }
        $change = $r[1] + $r[2];
        if ($maxChange < $change) $maxChange = $change;
    }
    $numberWidth = max(decimal_width($maxChange), $numberWidth);
    if ($width < 16 + 6 + $numberWidth) $width = 16 + 6 + $numberWidth;
    $graphWidth = ($maxChange + 4 > $binWidth) ? $maxChange : $binWidth - 4;
    $nameWidth = $maxLen;
    if ($nameWidth + $numberWidth + 6 + $graphWidth > $width) {
        if ($graphWidth > intdiv($width * 3, 8) - $numberWidth - 6) {
            $graphWidth = intdiv($width * 3, 8) - $numberWidth - 6;
            if ($graphWidth < 6) $graphWidth = 6;
        }
        if ($nameWidth > $width - $numberWidth - 6 - $graphWidth) {
            $nameWidth = $width - $numberWidth - 6 - $graphWidth;
        } else {
            $graphWidth = $width - $numberWidth - 6 - $nameWidth;
        }
    }
    $out = '';
    foreach ($rows as $r) {
        $name = $r[0];
        if (strlen($name) > $nameWidth) {
            $keep = $nameWidth - 3;
            if ($keep < 1) $keep = 1;
            $name = '...' . substr($name, strlen($name) - $keep);
        }
        $padding = $nameWidth - strlen($name);
        if ($padding < 0) $padding = 0;
        if ($r[3]) {
            $out .= ' ' . $name . str_repeat(' ', $padding) . ' | ' . str_pad('Bin', $numberWidth, ' ', STR_PAD_LEFT);
            if ($r[4] === 0 && $r[5] === 0) { $out .= "\n"; continue; }
            $out .= ' ' . $r[4] . ' -> ' . $r[5] . " bytes\n";
            continue;
        }
        $add = $r[1]; $del = $r[2];
        if ($graphWidth <= $maxChange && $maxChange > 0) {
            $total = scale_linear($add + $del, $graphWidth, $maxChange);
            if ($total < 2 && $add && $del) $total = 2;
            if ($add < $del) {
                $add = scale_linear($add, $graphWidth, $maxChange);
                $del = $total - $add;
            } else {
                $del = scale_linear($del, $graphWidth, $maxChange);
                $add = $total - $del;
            }
        }
        $count = $r[1] + $r[2];
        $out .= ' ' . $name . str_repeat(' ', $padding) . ' | '
              . str_pad((string)$count, $numberWidth, ' ', STR_PAD_LEFT)
              . ($count ? ' ' : '')
              . str_repeat('+', $add) . str_repeat('-', $del) . "\n";
    }
    return $out;
}

function parse_diff_opts(array &$argv): array {
    $opts = ['patch' => true, 'abbrev' => 7, 'context' => 3];
    $rest = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-p' || $a === '-u' || $a === '--patch': $opts['patch'] = true; break;
            case $a === '--raw': $opts['raw'] = true; $opts['patch'] = false; break;
            case $a === '--name-only': $opts['name_only'] = true; $opts['patch'] = false; break;
            case $a === '--name-status': $opts['name_status'] = true; $opts['patch'] = false; break;
            case $a === '--stat' || strncmp($a, '--stat=', 7) === 0: $opts['stat'] = true; $opts['patch'] = false; break;
            case $a === '--numstat': $opts['numstat'] = true; $opts['patch'] = false; break;
            case $a === '--shortstat': $opts['shortstat'] = true; $opts['patch'] = false; break;
            case $a === '-z': $opts['z'] = true; break;
            case $a === '--cached' || $a === '--staged': $opts['cached'] = true; break;
            case $a === '--no-patch' || $a === '-s': $opts['patch'] = false; break;
            case $a === '-M' || strncmp($a, '-M', 2) === 0 && preg_match('/^-M\d*$/', $a): $opts['rename'] = true; break;
            case $a === '--find-renames' || strncmp($a, '--find-renames=', 15) === 0: $opts['rename'] = true; break;
            case $a === '--no-renames': $opts['rename'] = false; break;
            case strncmp($a, '--abbrev=', 9) === 0: $opts['abbrev'] = (int)substr($a, 9); break;
            case $a === '--full-index': $opts['abbrev'] = 40; break;
            case preg_match('/^-U(\d+)$/', $a, $m) === 1: $opts['context'] = (int)$m[1]; break;
            case strncmp($a, '--unified=', 10) === 0: $opts['context'] = (int)substr($a, 10); break;
            case strncmp($a, '--src-prefix=', 13) === 0: $opts['src_prefix'] = substr($a, 13); break;
            case strncmp($a, '--dst-prefix=', 13) === 0: $opts['dst_prefix'] = substr($a, 13); break;
            case $a === '--no-prefix': $opts['src_prefix'] = ''; $opts['dst_prefix'] = ''; break;
            case $a === '--exit-code': $opts['exit_code'] = true; break;
            case $a === '--quiet': $opts['quiet'] = true; $opts['exit_code'] = true; $opts['patch'] = false; break;
            case $a === '--binary': break;
            case $a === '-r' || $a === '-t': $opts['recursive'] = true; break;
            case $a === '--root': $opts['root'] = true; break;
            case $a === '--color' || $a === '--no-color' || strncmp($a, '--color=', 8) === 0: break;
            default: $rest[] = $a;
        }
    }
    $argv = $rest;
    return $opts;
}

function cmd_diff(array $argv): int {
    $opts = parse_diff_opts($argv);
    $repo = need_repo();
    $revs = []; $paths = []; $afterDD = false;
    foreach ($argv as $a) {
        if ($a === '--') { $afterDD = true; continue; }
        if ($afterDD) { $paths[] = $a; continue; }
        if (rev_parse_object($repo, $a) !== null || strpos($a, '..') !== false) { $revs[] = $a; continue; }
        if (!file_exists($a) && $repo->index()->find($a, 0) === null) {
            fwrite(STDERR, "fatal: ambiguous argument '$a': unknown revision or path not in the working tree.\nUse '--' to separate paths from revisions, like this:\n'git <command> [<revision>...] -- [<file>...]'\n");
            return 128;
        }
        $paths[] = $a;
    }
    $ps = Pathspec::parse($paths, repo_prefix($repo));
    $idx = $repo->index();
    $entries = [];
    $opts['worktree'] = false;
    if (count($revs) === 0) {
        if (!empty($opts['cached'])) {
            $head = $repo->refs->resolve('HEAD');
            $tree = $head === null ? null : peel_to_tree($repo, $head);
            $entries = diff_tree_index($repo, $tree, $idx, $ps);
        } else {
            $entries = diff_index_worktree($repo, $idx, $ps);
            $opts['worktree'] = true;
        }
    } elseif (count($revs) === 1 && strpos($revs[0], '..') === false) {
        $o = rev_parse_object($repo, $revs[0]);
        $tree = peel_to_tree($repo, $o);
        if (!empty($opts['cached'])) $entries = diff_tree_index($repo, $tree, $idx, $ps);
        else {
            $entries = diff_tree_worktree($repo, $tree, $idx, $ps);
            $opts['worktree'] = true;
        }
    } else {
        $spec = $revs[0];
        if (strpos($spec, '..') !== false) {
            $three = strpos($spec, '...') !== false;
            $parts = $three ? explode('...', $spec, 2) : explode('..', $spec, 2);
            $a = rev_parse_object($repo, $parts[0] === '' ? 'HEAD' : $parts[0]);
            $b = rev_parse_object($repo, $parts[1] === '' ? 'HEAD' : $parts[1]);
            if ($three) {
                $base = merge_bases($repo, peel_to_commit($repo, $a), peel_to_commit($repo, $b));
                $a = $base[0] ?? $a;
            }
            $entries = diff_tree_tree($repo, peel_to_tree($repo, $a), peel_to_tree($repo, $b), $ps);
        } else {
            $a = rev_parse_object($repo, $revs[0]);
            $b = rev_parse_object($repo, $revs[1]);
            $entries = diff_tree_tree($repo, peel_to_tree($repo, $a), peel_to_tree($repo, $b), $ps);
        }
    }
    if (!empty($opts['rename'])) detect_renames($repo, $entries);
    $text = emit_diff($repo, $entries, $opts);
    out($text);
    if (!empty($opts['exit_code'])) return $entries ? 1 : 0;
    return 0;
}

function diff_tree_worktree(Repo $repo, ?string $tree, Index $idx, ?Pathspec $ps): array {
    $flat = [];
    if ($tree !== null) tree_flatten($repo, $tree, '', $flat);
    $out = [];
    $filemode = $repo->config->getBool('core.filemode', true);
    $seen = [];
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0) continue;
        if ($ps !== null && !$ps->matches($e->path)) continue;
        $seen[$e->path] = true;
        $full = $repo->worktree . '/' . $e->path;
        $st = lstat_info($full);
        $d = new DiffEntry();
        $d->path = $e->path;
        $old = $flat[$e->path] ?? null;
        $d->oldMode = $old === null ? '000000' : sprintf('%06o', octdec($old[0]));
        $d->oldOid = $old === null ? str_repeat('0', $repo->hexsz) : $old[1];
        if ($st === null) {
            if ($old === null) continue;
            $d->status = 'D'; $d->newMode = '000000'; $d->newOid = str_repeat('0', $repo->hexsz);
            $out[] = $d; continue;
        }
        $mode = wt_mode_for($st, $filemode);
        $content = wt_read_blob($full, $st);
        $oid = $repo->hashData('blob', $content);
        if ($old !== null && $old[1] === $oid && octdec($old[0]) === $mode) continue;
        $d->status = $old === null ? 'A' : 'M';
        $d->newMode = sprintf('%06o', $mode);
        $d->newOid = $oid;
        $out[] = $d;
    }
    foreach ($flat as $p => $v) {
        if (isset($seen[$p])) continue;
        if ($ps !== null && !$ps->matches($p)) continue;
        $d = new DiffEntry();
        $d->status = 'D'; $d->path = $p;
        $d->oldMode = sprintf('%06o', octdec($v[0])); $d->oldOid = $v[1];
        $d->newMode = '000000'; $d->newOid = str_repeat('0', $repo->hexsz);
        $out[] = $d;
    }
    usort($out, function ($a, $b) { return strcmp($a->path, $b->path); });
    return $out;
}

function cmd_diff_index(array $argv): int {
    $abbrevAsked = false;
    foreach ($argv as $a) if (strncmp($a, '--abbrev', 8) === 0) $abbrevAsked = true;
    $opts = parse_diff_opts($argv);
    if (!$abbrevAsked) $opts['abbrev'] = 40;
    if (!isset($opts['raw']) && !isset($opts['name_only']) && !isset($opts['name_status'])
        && !isset($opts['stat']) && !isset($opts['numstat']) && empty($opts['patchExplicit'])) {
        if (!in_array('-p', $argv, true) && !in_array('--patch', $argv, true)) { $opts['raw'] = true; $opts['patch'] = false; }
    }
    $repo = need_repo();
    $revs = []; $paths = []; $afterDD = false;
    foreach ($argv as $a) {
        if ($a === '--') { $afterDD = true; continue; }
        if ($afterDD) { $paths[] = $a; continue; }
        $revs[] = $a;
    }
    if (!$revs) usage_die('usage: git diff-index [<options>] <tree-ish> [<path>...]', 129);
    $ps = Pathspec::parse($paths, repo_prefix($repo));
    $o = rev_parse_object($repo, $revs[0]);
    if ($o === null) die_git("bad revision '{$revs[0]}'");
    $tree = peel_to_tree($repo, $o);
    $idx = $repo->index();
    $entries = !empty($opts['cached'])
        ? diff_tree_index($repo, $tree, $idx, $ps)
        : diff_tree_worktree($repo, $tree, $idx, $ps);
    if (!empty($opts['rename'])) detect_renames($repo, $entries);
    out(emit_diff($repo, $entries, $opts));
    if (!empty($opts['exit_code'])) return $entries ? 1 : 0;
    return 0;
}

function cmd_diff_files(array $argv): int {
    $abbrevAsked = false;
    foreach ($argv as $a) if (strncmp($a, '--abbrev', 8) === 0) $abbrevAsked = true;
    $opts = parse_diff_opts($argv);
    if (!$abbrevAsked) $opts['abbrev'] = 40;
    if (!isset($opts['raw']) && !isset($opts['name_only']) && !isset($opts['name_status'])
        && !isset($opts['stat']) && !isset($opts['numstat'])) {
        if (!in_array('-p', $argv, true) && !in_array('--patch', $argv, true)) { $opts['raw'] = true; $opts['patch'] = false; }
    }
    $repo = need_repo();
    $paths = [];
    foreach ($argv as $a) { if ($a !== '--') $paths[] = $a; }
    $ps = Pathspec::parse($paths, repo_prefix($repo));
    $entries = diff_index_worktree($repo, $repo->index(), $ps);
    $opts['worktree'] = true;
    out(emit_diff($repo, $entries, $opts));
    if (!empty($opts['exit_code'])) return $entries ? 1 : 0;
    return 0;
}

function cmd_diff_tree(array $argv): int {
    $abbrevAsked = false;
    foreach ($argv as $a) if (strncmp($a, '--abbrev', 8) === 0) $abbrevAsked = true;
    $opts = parse_diff_opts($argv);
    if (!$abbrevAsked) $opts['abbrev'] = 40;
    $stdin = in_array('--stdin', $argv, true);
    $argv = array_values(array_filter($argv, function ($a) { return $a !== '--stdin'; }));
    if (!isset($opts['raw']) && !isset($opts['name_only']) && !isset($opts['name_status'])
        && !isset($opts['stat']) && !isset($opts['numstat'])) {
        if (!in_array('-p', $argv, true) && !in_array('--patch', $argv, true)) { $opts['raw'] = true; $opts['patch'] = false; }
    }
    $repo = need_repo();
    $revs = []; $paths = []; $afterDD = false;
    foreach ($argv as $a) {
        if ($a === '--') { $afterDD = true; continue; }
        if ($afterDD) { $paths[] = $a; continue; }
        $revs[] = $a;
    }
    $ps = Pathspec::parse($paths, repo_prefix($repo));
    if (count($revs) >= 2) {
        $a = peel_to_tree($repo, rev_parse_object($repo, $revs[0]));
        $b = peel_to_tree($repo, rev_parse_object($repo, $revs[1]));
        $entries = diff_tree_tree($repo, $a, $b, $ps);
        out(emit_diff($repo, $entries, $opts));
        return 0;
    }
    if (count($revs) === 1) {
        $oid = rev_parse_object($repo, $revs[0]);
        if ($oid === null) die_git("bad revision '{$revs[0]}'");
        $o = $repo->odb->read($oid);
        if ($o !== null && $o[0] === 'commit') {
            $c = CommitObj::parse($o[1], $oid);
            $parentTree = isset($c->parents[0]) ? peel_to_tree($repo, $c->parents[0]) : null;
            if ($parentTree === null && empty($opts['root'])) { out("$oid\n"); return 0; }
            out("$oid\n");
            $entries = diff_tree_tree($repo, $parentTree, $c->tree, $ps);
            out(emit_diff($repo, $entries, $opts));
            return 0;
        }
        $entries = diff_tree_tree($repo, null, peel_to_tree($repo, $oid), $ps);
        out(emit_diff($repo, $entries, $opts));
    }
    return 0;
}

function cmd_log(array $argv): int {
    $opts = parse_diff_opts($argv);
    $opts['patch'] = false;
    $repo = need_repo();
    $walk = new RevWalk($repo);
    $pretty = 'medium'; $showPatch = false; $paths = []; $revs = []; $afterDD = false;
    $decorate = false; $abbrevCommit = false; $graph = false; $showStat = false; $nameStatus = false;
    $args = $argv;
    for ($i = 0; $i < count($args); $i++) {
        $a = $args[$i];
        if ($afterDD) { $paths[] = $a; continue; }
        switch (true) {
            case $a === '--': $afterDD = true; break;
            case $a === '--oneline': $pretty = 'oneline'; $abbrevCommit = true; break;
            case strncmp($a, '--pretty', 8) === 0:
                $pretty = strpos($a, '=') !== false ? substr($a, strpos($a, '=') + 1) : 'medium'; break;
            case strncmp($a, '--format=', 9) === 0: $pretty = substr($a, 9); break;
            case $a === '-p' || $a === '-u' || $a === '--patch': $showPatch = true; break;
            case $a === '--stat': $showStat = true; break;
            case $a === '--name-status': $nameStatus = true; break;
            case $a === '--decorate' || strncmp($a, '--decorate=', 11) === 0: $decorate = true; break;
            case $a === '--no-decorate': $decorate = false; break;
            case $a === '--graph': $graph = true; break;
            case $a === '--abbrev-commit': $abbrevCommit = true; break;
            case $a === '--all':
                foreach ($repo->refs->listRefs('refs/') as $n => $o) { $c = peel_to_commit($repo, $o); if ($c) $walk->push($c); }
                break;
            case $a === '--reverse': $walk->reverse = true; break;
            case $a === '--topo-order': $walk->topoOrder = true; break;
            case $a === '--first-parent': $walk->firstParent = true; break;
            case $a === '--no-merges': $walk->merges = false; break;
            case $a === '--merges': $walk->merges = true; break;
            case $a === '-n': $walk->maxCount = (int)($args[++$i] ?? '0'); break;
            case preg_match('/^-(\d+)$/', $a, $m) === 1: $walk->maxCount = (int)$m[1]; break;
            case strncmp($a, '--max-count=', 12) === 0: $walk->maxCount = (int)substr($a, 12); break;
            case strncmp($a, '--skip=', 7) === 0: $walk->skip = (int)substr($a, 7); break;
            case strncmp($a, '--grep=', 7) === 0: $walk->grep[] = substr($a, 7); break;
            case strncmp($a, '--author=', 9) === 0: $walk->author = substr($a, 9); break;
            case strncmp($a, '--since=', 8) === 0: $d = parse_date(substr($a, 8)); if ($d) $walk->since = $d[0]; break;
            case strncmp($a, '--until=', 8) === 0: $d = parse_date(substr($a, 8)); if ($d) $walk->until = $d[0]; break;
            case $a !== '' && $a[0] === '-': break;
            default: $revs[] = $a;
        }
    }
    $ps = Pathspec::parse($paths, repo_prefix($repo));
    if (!$revs && !$walk->hasStarts()) {
        $h = $repo->refs->resolve('HEAD');
        if ($h === null) {
            $head = $repo->refs->readRaw('HEAD');
            $name = $head && $head[1] ? $head[1] : 'HEAD';
            fwrite(STDERR, "fatal: your current branch '" . preg_replace('#^refs/heads/#', '', $name) . "' does not have any commits yet\n");
            return 128;
        }
        $walk->push($h);
    }
    foreach ($revs as $rv) {
        if (strpos($rv, '..') !== false) {
            $parts = explode('..', $rv, 2);
            $lo = rev_parse_object($repo, $parts[0] === '' ? 'HEAD' : $parts[0]);
            $ro = rev_parse_object($repo, $parts[1] === '' ? 'HEAD' : $parts[1]);
            if ($lo !== null) $walk->hide(peel_to_commit($repo, $lo));
            if ($ro !== null) $walk->push(peel_to_commit($repo, $ro));
            continue;
        }
        $neg = false;
        if ($rv[0] === '^') { $neg = true; $rv = substr($rv, 1); }
        $o = rev_parse_object($repo, $rv);
        if ($o === null) die_git("ambiguous argument '$rv': unknown revision or path not in the working tree.");
        $c = peel_to_commit($repo, $o);
        if ($c === null) continue;
        if ($neg) $walk->hide($c); else $walk->push($c);
    }
    $commits = $walk->commits();
    foreach ($commits as $oid => $c) {
        if (!$ps->isEmpty()) {
            $parentTree = isset($c->parents[0]) ? peel_to_tree($repo, $c->parents[0]) : null;
            $entries = diff_tree_tree($repo, $parentTree, $c->tree, $ps);
            if (!$entries) continue;
        }
        $text = format_commit($repo, $c, $pretty);
        if ($abbrevCommit && $pretty !== 'oneline') $text = preg_replace('/^commit ([0-9a-f]+)/m', 'commit ' . substr($oid, 0, 7), $text);
        if ($pretty === 'oneline' && $abbrevCommit) $text = substr($oid, 0, 7) . ' ' . $c->subject() . "\n";
        out($text);
        if ($showPatch || $showStat || $nameStatus) {
            $parentTree = isset($c->parents[0]) ? peel_to_tree($repo, $c->parents[0]) : null;
            $entries = diff_tree_tree($repo, $parentTree, $c->tree, $ps);
            $o2 = $opts;
            $o2['patch'] = $showPatch; $o2['stat'] = $showStat; $o2['name_status'] = $nameStatus;
            out(emit_diff($repo, $entries, $o2));
            if ($showPatch) out("\n");
        }
    }
    return 0;
}

function cmd_show(array $argv): int {
    $opts = parse_diff_opts($argv);
    $repo = need_repo();
    $pretty = 'medium';
    $revs = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if (strncmp($a, '--pretty', 8) === 0) { $pretty = strpos($a, '=') !== false ? substr($a, strpos($a, '=') + 1) : 'medium'; continue; }
        if (strncmp($a, '--format=', 9) === 0) { $pretty = substr($a, 9); continue; }
        if ($a === '--oneline') { $pretty = 'oneline'; continue; }
        if ($a === '--') continue;
        if ($a !== '' && $a[0] === '-') continue;
        $revs[] = $a;
    }
    if (!$revs) $revs[] = 'HEAD';
    foreach ($revs as $rv) {
        $oid = rev_parse_object($repo, $rv);
        if ($oid === null) die_git("ambiguous argument '$rv': unknown revision or path not in the working tree.");
        $o = $repo->odb->read($oid);
        if ($o === null) die_git("bad object $rv");
        if ($o[0] === 'blob') { out($o[1]); continue; }
        if ($o[0] === 'tree') { out("tree $rv\n\n"); out(format_tree_names($repo, $o[1])); continue; }
        if ($o[0] === 'tag') {
            $t = TagObj::parse($o[1]);
            if ($pretty === 'oneline') {
                out("tag {$t->tag}\n\n" . rtrim($t->message, "\n") . "\n");
            } else {
                out("tag {$t->tag}\n");
                if ($t->tagger) {
                    out('Tagger: ' . $t->tagger->name . ' <' . $t->tagger->email . ">\n");
                    out('Date:   ' . git_date_default($t->tagger->ts, $t->tagger->tz) . "\n");
                }
                out("\n" . rtrim($t->message, "\n") . "\n\n");
            }
            $oid = $t->object;
            $o = $repo->odb->read($oid);
            if ($o === null || $o[0] !== 'commit') continue;
        }
        $c = CommitObj::parse($o[1], $oid);
        out(format_commit($repo, $c, $pretty));
        $parentTree = isset($c->parents[0]) ? peel_to_tree($repo, $c->parents[0]) : null;
        $entries = diff_tree_tree($repo, $parentTree, $c->tree, null);
        $opts['patch'] = $opts['patch'] ?? true;
        out(emit_diff($repo, $entries, $opts));
    }
    return 0;
}

function format_tree_names(Repo $repo, string $data): string {
    $t = TreeObj::parse($data);
    $s = '';
    foreach ($t->entries as $e) $s .= $e->name . ($e->mode === '40000' ? "/\n" : "\n");
    return $s;
}

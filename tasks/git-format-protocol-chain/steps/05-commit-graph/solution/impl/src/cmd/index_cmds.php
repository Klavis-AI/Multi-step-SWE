<?php
// git-ls-files(1), git-update-index(1), git-write-tree(1), git-read-tree(1),
// git-checkout-index(1), git-add(1), git-rm(1), git-mv(1)

function repo_prefix(Repo $repo): string {
    if ($repo->worktree === null) return '';
    $cwd = path_real(getcwd());
    $wt = path_real($repo->worktree);
    if ($cwd === $wt) return '';
    if (strncmp($cwd, $wt . '/', strlen($wt) + 1) === 0) return substr($cwd, strlen($wt) + 1) . '/';
    return '';
}

function display_path(Repo $repo, string $path, bool $fullName = false): string {
    if ($fullName) return $path;
    $prefix = repo_prefix($repo);
    if ($prefix !== '' && strncmp($path, $prefix, strlen($prefix)) === 0) return substr($path, strlen($prefix));
    if ($prefix !== '') {
        // path outside the current directory: git shows it relative
        return str_repeat('../', substr_count(rtrim($prefix, '/'), '/') + 1) . $path;
    }
    return $path;
}

function cmd_ls_files(array $argv): int {
    $cached = false; $deleted = false; $modified = false; $others = false; $ignored = false;
    $stage = false; $unmerged = false; $z = false; $excludeStd = false; $fullName = false;
    $errorUnmatch = false; $tags = false; $abbrev = null; $directory = false; $killed = false;
    $debug = false; $deduplicate = false; $format = null; $sparse = false;
    $excludes = []; $paths = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-c' || $a === '--cached': $cached = true; break;
            case $a === '-d' || $a === '--deleted': $deleted = true; break;
            case $a === '-m' || $a === '--modified': $modified = true; break;
            case $a === '-o' || $a === '--others': $others = true; break;
            case $a === '-i' || $a === '--ignored': $ignored = true; break;
            case $a === '-s' || $a === '--stage': $stage = true; break;
            case $a === '-u' || $a === '--unmerged': $unmerged = true; break;
            case $a === '-k' || $a === '--killed': $killed = true; break;
            case $a === '-z': $z = true; break;
            case $a === '-t': $tags = true; break;
            case $a === '--debug': $debug = true; break;
            case $a === '--deduplicate': $deduplicate = true; break;
            case $a === '--sparse': $sparse = true; break;
            case $a === '--directory': $directory = true; break;
            case $a === '--no-empty-directory': break;
            case $a === '--exclude-standard': $excludeStd = true; break;
            case $a === '--full-name': $fullName = true; break;
            case $a === '--error-unmatch': $errorUnmatch = true; break;
            case $a === '--abbrev': $abbrev = 7; break;
            case strncmp($a, '--abbrev=', 9) === 0: $abbrev = (int)substr($a, 9); break;
            case strncmp($a, '--exclude=', 10) === 0: $excludes[] = substr($a, 10); break;
            case $a === '-x': $excludes[] = $argv[++$i] ?? ''; break;
            case strncmp($a, '--exclude-from=', 15) === 0:
                $c = @file_get_contents(substr($a, 15));
                if ($c !== false) foreach (explode("\n", $c) as $l) if ($l !== '') $excludes[] = $l;
                break;
            case strncmp($a, '--format=', 9) === 0: $format = substr($a, 9); break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; $i = count($argv); break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git ls-files [<options>] [<file>...]", 129);
            default: $paths[] = $a;
        }
    }
    $repo = need_repo(true);
    if (!$cached && !$deleted && !$modified && !$others && !$unmerged && !$killed) $cached = true;
    $idx = $repo->index();
    // A sparse index lists its collapsed directory entries only under --sparse;
    // otherwise it expands to full paths first, and git prints a hint when it
    // does (git-sparse-checkout(1), "The sparse index").
    if (!$sparse && function_exists('sparse_index_expand')) {
        $hasSparse = false;
        foreach ($idx->entries as $e) if ($e->isSparseDir()) { $hasSparse = true; break; }
        if ($hasSparse) {
            fwrite(STDERR, "hint: The sparse index is expanding to a full index, a slow operation.\n");
            fwrite(STDERR, "hint: Your working directory likely has contents that are outside of\n");
            fwrite(STDERR, "hint: your sparse-checkout patterns. Use \x27git sparse-checkout list\x27 to\n");
            fwrite(STDERR, "hint: see your sparse-checkout definition and compare it to your working\n");
            fwrite(STDERR, "hint: directory contents. Running \x27git clean\x27 may assist in this cleanup.\n");
            fwrite(STDERR, "hint: Disable this message with \x22git config set advice.sparseIndexExpanded false\x22\n");
            sparse_index_expand($repo, $idx);
        }
    }
    $prefix = repo_prefix($repo);
    global $GIT_OPTS;
    $ps = Pathspec::parse($paths, $prefix, $GIT_OPTS['literal_pathspecs']);
    $term = $z ? "\0" : "\n";
    $out = [];
    $seen = [];
    $filemode = $repo->config->getBool('core.filemode', true);
    if ($cached || $stage || $unmerged) {
        foreach ($idx->sorted() as $e) {
            if (!$ps->matches($e->path)) continue;
            if ($unmerged && $e->stage() === 0) continue;
            if (!$unmerged && !$cached && !$stage) continue;
            if ($deduplicate && isset($seen[$e->path])) continue;
            $seen[$e->path] = true;
            if ($stage || $unmerged) {
                $oid = $abbrev ? substr($e->oid, 0, $abbrev) : $e->oid;
                $out[] = sprintf("%06o %s %d\t%s", $e->mode, $oid, $e->stage(), quote_path_maybe(display_path($repo, $e->path, $fullName), $term));
            } elseif ($tags) {
                $tag = $e->stage() === 0 ? 'H' : 'M';
                if ($e->skipWorktree()) $tag = 'S';
                if ($e->assumeValid()) $tag = 'h';
                $out[] = "$tag " . quote_path_maybe(display_path($repo, $e->path, $fullName), $term);
            } else {
                $out[] = quote_path_maybe(display_path($repo, $e->path, $fullName), $term);
            }
        }
    }
    if ($deleted || $modified) {
        foreach (diff_index_worktree($repo, $idx, $ps) as $d) {
            if ($d->status === 'D' && $deleted) $out[] = quote_path_maybe(display_path($repo, $d->path, $fullName), $term);
            elseif ($d->status !== 'D' && $modified) $out[] = quote_path_maybe(display_path($repo, $d->path, $fullName), $term);
        }
    }
    if ($others || ($ignored && $excludeStd)) {
        $scan = wt_scan($repo, $ps, true);
        $tracked = [];
        foreach ($idx->sorted() as $e) $tracked[$e->path] = true;
        $list = $ignored ? array_keys($scan->ignored) : array_keys($scan->files);
        if ($ignored && $directory) foreach (array_keys($scan->ignoredDirs) as $d) $list[] = $d;
        sort($list, SORT_STRING);
        foreach ($list as $p) {
            if (isset($tracked[$p])) continue;
            if (!$ps->matches(rtrim($p, '/'))) continue;
            if (!$ignored && $excludes) {
                $ex = new ExcludeStack($repo);
                $ex->addText(implode("\n", $excludes), '');
                if ($ex->check($p, false) === true) continue;
            }
            $out[] = quote_path_maybe(display_path($repo, $p, $fullName), $term);
        }
    }
    if ($errorUnmatch) {
        foreach ($paths as $p) {
            $found = false;
            $full = $prefix . $p;
            foreach ($idx->sorted() as $e) if ($e->path === $full || $e->path === $p) { $found = true; break; }
            if (!$found) { fwrite(STDERR, "error: pathspec '$p' did not match any file(s) known to git\n"); return 1; }
        }
    }
    foreach ($out as $line) out($line . $term);
    return 0;
}

function cmd_write_tree(array $argv): int {
    $prefix = null; $missingOk = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($a === '--missing-ok') { $missingOk = true; continue; }
        if (strncmp($a, '--prefix=', 9) === 0) { $prefix = rtrim(substr($a, 9), '/'); continue; }
    }
    $repo = need_repo();
    $idx = $repo->index();
    if ($idx->hasConflicts()) {
        foreach ($idx->sorted() as $e) {
            if ($e->stage() !== 0) { fwrite(STDERR, "error: Entry '{$e->path}' has not been staged.\n"); }
        }
        die_git('git write-tree: error building trees');
    }
    $oid = index_write_tree($repo, $idx, $missingOk);
    if ($prefix !== null) {
        $e = tree_lookup_path($repo, $oid, $prefix);
        if ($e === null) die_git("prefix $prefix not found");
        $oid = $e->oid;
    }
    out("$oid\n");
    return 0;
}

function cmd_update_index(array $argv): int {
    $repo = need_repo();
    $idx = $repo->index();
    $add = false; $remove = false; $forceRemove = false; $refresh = false; $really = false;
    $quiet = false; $stdin = false; $z = false; $indexInfo = false; $infoOnly = false;
    $chmod = null; $paths = []; $cacheinfo = []; $assume = null; $skipWorktree = null;
    $ignoreMissing = false; $unmerged = false; $version = null; $again = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--add': $add = true; break;
            case $a === '--remove': $remove = true; break;
            case $a === '--force-remove': $forceRemove = true; $remove = true; break;
            case $a === '--refresh': $refresh = true; break;
            case $a === '--really-refresh': $refresh = true; $really = true; break;
            case $a === '-q': $quiet = true; break;
            case $a === '--unmerged': $unmerged = true; break;
            case $a === '--ignore-missing': $ignoreMissing = true; break;
            case $a === '--stdin': $stdin = true; break;
            case $a === '--index-info': $indexInfo = true; break;
            case $a === '--info-only': $infoOnly = true; break;
            case $a === '-z': $z = true; break;
            case $a === '--again' || $a === '-g': $again = true; break;
            case $a === '--assume-unchanged': $assume = true; break;
            case $a === '--no-assume-unchanged': $assume = false; break;
            case $a === '--skip-worktree': $skipWorktree = true; break;
            case $a === '--no-skip-worktree': $skipWorktree = false; break;
            // The untracked cache and fsmonitor are index-side accelerators:
            // git accepts these flags, and `status` must give the same answer
            // with them as without.  This build keeps no cache, so honouring
            // the flags and leaving observable behaviour unchanged is correct.
            case $a === '--untracked-cache' || $a === '--no-untracked-cache': break;
            case $a === '--test-untracked-cache': out(""); return 0;
            case $a === '--force-untracked-cache': break;
            case $a === '--fsmonitor' || $a === '--no-fsmonitor': break;
            case $a === '--fsmonitor-valid' || $a === '--no-fsmonitor-valid': break;
            case strncmp($a, '--chmod=', 8) === 0: $chmod = substr($a, 8); break;
            case strncmp($a, '--index-version', 15) === 0:
                $version = (int)(strpos($a, '=') !== false ? substr($a, strpos($a, '=') + 1) : ($argv[++$i] ?? '2'));
                break;
            case $a === '--cacheinfo':
                $next = $argv[$i + 1] ?? '';
                if (strpos($next, ',') !== false) { $cacheinfo[] = explode(',', $next, 3); $i++; }
                else { $cacheinfo[] = [$argv[$i + 1] ?? '', $argv[$i + 2] ?? '', $argv[$i + 3] ?? '']; $i += 3; }
                break;
            case $a === '--verbose' || $a === '-v': break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; $i = count($argv); break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git update-index [<options>] [--] [<file>...]", 129);
            default: $paths[] = $a;
        }
    }
    $prefix = repo_prefix($repo);
    if ($version !== null) { $idx->version = $version; $idx->versionExplicit = true; $idx->dirty = true; }
    foreach ($cacheinfo as $ci) {
        [$mode, $oid, $p] = $ci;
        if (!$add && $idx->find($p, 0) === null) {
            fwrite(STDERR, "error: $p: cannot add to the index - missing --add option?\n");
            fwrite(STDERR, "fatal: git update-index: --cacheinfo cannot add $p\n");
            return 128;
        }
        $e = new IndexEntry();
        $e->mode = octdec($mode);
        $e->oid = $oid;
        $e->path = $p;
        $stage = 0;
        $idx->remove($p, 0);
        $idx->put($e);
    }
    if ($indexInfo) {
        $data = stream_get_contents(STDIN);
        $lines = $z ? explode("\0", $data) : explode("\n", $data);
        foreach ($lines as $line) {
            if ($line === '') continue;
            if (preg_match('/^(\d+) ([0-9a-f]+)\s+(\d)\t(.*)$/s', $line, $m)) {
                $e = new IndexEntry();
                $e->mode = octdec($m[1]);
                $e->oid = $m[2];
                $e->path = $z ? $m[4] : unquote_c_style($m[4]);
                $e->setStage((int)$m[3]);
                if ($e->mode === 0) { $idx->remove($e->path, (int)$m[3]); continue; }
                $idx->put($e);
            } elseif (preg_match('/^0{6} 0+ ?(\d)?\t(.*)$/s', $line, $m)) {
                $idx->remove($m[2], null);
            }
        }
        $idx->write();
        return 0;
    }
    if ($stdin) {
        $data = stream_get_contents(STDIN);
        foreach (($z ? explode("\0", $data) : explode("\n", $data)) as $l) {
            if ($l !== '') $paths[] = $z ? $l : unquote_c_style($l);
        }
    }
    $ret = 0;
    if ($refresh) {
        $filemode = $repo->config->getBool('core.filemode', true);
        foreach ($idx->sorted() as $e) {
            if ($paths) {
                $ps = Pathspec::parse($paths, $prefix);
                if (!$ps->matches($e->path)) continue;
            }
            if ($e->stage() !== 0) {
                if (!$quiet) { fwrite(STDERR, "{$e->path}: needs merge\n"); }
                $ret = 1;
                continue;
            }
            $full = $repo->worktree . '/' . $e->path;
            $st = lstat_info($full);
            if ($st === null) {
                if (!$quiet && !$ignoreMissing) { out("{$e->path}: needs update\n"); $ret = 1; }
                continue;
            }
            if (entry_stat_matches($e, $st, $filemode) && !$really) continue;
            $content = wt_read_blob($full, $st);
            $oid = $repo->hashData('blob', $content);
            $mode = wt_mode_for($st, $filemode);
            if ($oid === $e->oid && ($mode === $e->mode || !$filemode)) {
                $e->fillStat($st);
                $idx->dirty = true;
            } else {
                if (!$quiet) out("{$e->path}: needs update\n");
                $ret = 1;
            }
        }
        $idx->write();
        return $ret;
    }
    foreach ($paths as $p) {
        $rel = ltrim(path_normalize($prefix . $p), '/');
        $full = $repo->worktree . '/' . $rel;
        $existing = $idx->find($rel, 0);
        if ($forceRemove) { $idx->remove($rel, null); continue; }
        $st = lstat_info($full);
        if ($st === null) {
            if ($remove) { $idx->remove($rel, null); continue; }
            if ($existing === null) {
                fwrite(STDERR, "error: $p: does not exist and --remove not passed\n");
                fwrite(STDERR, "fatal: Unable to process path $p\n");
                return 128;
            }
            fwrite(STDERR, "error: $p: does not exist and --remove not passed\n");
            fwrite(STDERR, "fatal: Unable to process path $p\n");
            return 128;
        }
        if ($existing === null && !$add) {
            fwrite(STDERR, "error: $p: cannot add to the index - missing --add option?\n");
            fwrite(STDERR, "fatal: Unable to process path $p\n");
            return 128;
        }
        if ($assume !== null || $skipWorktree !== null || $chmod !== null) {
            $e = $existing;
            if ($e === null) continue;
            if ($assume !== null) $e->flags = $assume ? ($e->flags | 0x8000) : ($e->flags & ~0x8000);
            if ($skipWorktree !== null) $e->flags2 = $skipWorktree ? ($e->flags2 | 0x4000) : ($e->flags2 & ~0x4000);
            if ($chmod !== null) {
                if ($chmod === '+x') $e->mode = 0100755;
                elseif ($chmod === '-x') $e->mode = 0100644;
                else die_git("git update-index: --chmod param '$chmod' must be either -x or +x");
            }
            $idx->dirty = true;
            continue;
        }
        index_add_path($repo, $idx, $rel);
    }
    if ($assume !== null || $skipWorktree !== null) {
        // applied above per path
    }
    $idx->write();
    return $ret;
}

function cmd_read_tree(array $argv): int {
    $merge = false; $update = false; $reset = false; $prefix = null; $empty = false;
    $aggressive = false; $trees = []; $indexOutput = null; $dryRun = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-m': $merge = true; break;
            case $a === '-u': $update = true; break;
            case $a === '-i': break;
            case $a === '-v': break;
            case $a === '-n' || $a === '--dry-run': $dryRun = true; break;
            case $a === '--reset': $reset = true; $merge = true; break;
            case $a === '--empty': $empty = true; break;
            case $a === '--aggressive': $aggressive = true; break;
            case strncmp($a, '--prefix=', 9) === 0: $prefix = rtrim(substr($a, 9), '/'); break;
            case strncmp($a, '--index-output=', 15) === 0: $indexOutput = substr($a, 15); break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git read-tree [<options>] <tree-ish> [<tree-ish> [<tree-ish>]]", 129);
            default: $trees[] = $a;
        }
    }
    $repo = need_repo();
    $idx = $repo->index();
    if ($empty) {
        $idx->entries = [];
        $idx->cacheTree = null;
        $idx->write($indexOutput);
        return 0;
    }
    if (!$trees) usage_die('usage: git read-tree [<options>] <tree-ish> [<tree-ish> [<tree-ish>]]', 129);
    $oids = [];
    foreach ($trees as $t) {
        $o = rev_parse_object($repo, $t);
        if ($o === null) die_git("not a valid object name $t");
        $tr = peel_to_tree($repo, $o);
        if ($tr === null) die_git("not a tree object");
        $oids[] = $tr;
    }
    if (!$merge && count($oids) > 1) {
        // without -m the trees are simply read in order; the last one wins
        if ($prefix === null) { $idx->entries = []; $idx->reindex(); $idx->cacheTree = null; }
        foreach ($oids as $t) {
            $flat = [];
            tree_flatten($repo, $t, '', $flat);
            foreach ($flat as $p => $v) {
                $e = new IndexEntry();
                $e->path = $prefix === null ? $p : $prefix . '/' . $p;
                $e->mode = octdec($v[0]);
                $e->oid = $v[1];
                $idx->put($e);
            }
        }
        if ($update) checkout_index_entries($repo, $idx, true);
        $idx->write($indexOutput);
        return 0;
    }
    if (count($oids) === 1 && !$merge) {
        $flat = [];
        tree_flatten($repo, $oids[0], '', $flat);
        if ($prefix === null) { $idx->entries = []; $idx->cacheTree = null; }
        foreach ($flat as $p => $v) {
            $e = new IndexEntry();
            $e->path = $prefix === null ? $p : $prefix . '/' . $p;
            $e->mode = octdec($v[0]);
            $e->oid = $v[1];
            $idx->put($e);
        }
        if ($update) checkout_index_entries($repo, $idx, true);
        $idx->write($indexOutput);
        return 0;
    }
    if (count($oids) === 1 && $merge) {
        // one-tree merge: --reset style switch
        $flat = [];
        tree_flatten($repo, $oids[0], '', $flat);
        $old = [];
        foreach ($idx->sorted() as $e) if ($e->stage() === 0) $old[$e->path] = $e;
        $idx->entries = [];
        foreach ($flat as $p => $v) {
            $e = new IndexEntry();
            $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
            if (isset($old[$p]) && $old[$p]->oid === $v[1] && $old[$p]->mode === $e->mode) {
                $e->fillStat(['ctime' => $old[$p]->ctime_s, 'mtime' => $old[$p]->mtime_s, 'dev' => $old[$p]->dev,
                              'ino' => $old[$p]->ino, 'uid' => $old[$p]->uid, 'gid' => $old[$p]->gid, 'size' => $old[$p]->size]);
            }
            $idx->entries[] = $e;
        }
        $idx->reindex();
        $idx->sort();
        $idx->cacheTree = null;
        if ($update) {
            foreach ($old as $p => $e) if (!isset($flat[$p])) wt_remove_path($repo, $p);
            checkout_index_entries($repo, $idx, $reset);
        }
        $idx->write($indexOutput);
        return 0;
    }
    // two- and three-way merges
    $flats = [];
    foreach ($oids as $o) { $f = []; tree_flatten($repo, $o, '', $f); $flats[] = $f; }
    if (count($oids) === 2) {
        [$old, $new] = $flats;
        $oldIdx = [];
        foreach ($idx->sorted() as $e) if ($e->stage() === 0) $oldIdx[$e->path] = $e;
        // a two-way merge refuses to touch paths whose work-tree file is not
        // represented by the index entry's cached stat information
        $filemode = $repo->config->getBool('core.filemode', true);
        foreach ($oldIdx as $p => $e) {
            $inOld = isset($old[$p]); $inNew = isset($new[$p]);
            $changes = (!$inNew) || ($inNew && $new[$p][1] !== $e->oid);
            if (!$changes) continue;
            if ($repo->worktree === null) continue;
            $st = lstat_info($repo->worktree . '/' . $p);
            if ($st === null) continue;
            if (!entry_stat_matches($e, $st, $filemode)) {
                fwrite(STDERR, "error: Entry '$p' not uptodate. Cannot merge.\n");
                return 128;
            }
        }
        $result = [];
        foreach ($new as $p => $v) $result[$p] = $v;
        foreach ($old as $p => $v) {
            if (!isset($new[$p])) continue;
        }
        // local modifications must not be lost
        foreach ($oldIdx as $p => $e) {
            $inOld = isset($old[$p]); $inNew = isset($new[$p]);
            $localMod = !$inOld || $old[$p][1] !== $e->oid;
            if ($localMod && $inNew && $new[$p][1] !== $e->oid) {
                die_git("Entry '$p' would be overwritten by merge. Cannot merge.");
            }
            if ($localMod && !$inNew && $inOld) {
                die_git("Entry '$p' not uptodate. Cannot merge.");
            }
            if ($localMod && !$inOld && !$inNew) $result[$p] = [sprintf('%o', $e->mode), $e->oid];
        }
        $idx->entries = [];
        foreach ($result as $p => $v) {
            $e = new IndexEntry();
            $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
            if (isset($oldIdx[$p]) && $oldIdx[$p]->oid === $v[1]) {
                $e->ctime_s = $oldIdx[$p]->ctime_s; $e->mtime_s = $oldIdx[$p]->mtime_s;
                $e->dev = $oldIdx[$p]->dev; $e->ino = $oldIdx[$p]->ino;
                $e->uid = $oldIdx[$p]->uid; $e->gid = $oldIdx[$p]->gid; $e->size = $oldIdx[$p]->size;
            }
            $idx->entries[] = $e;
        }
        $idx->reindex();
        $idx->sort();
        $idx->cacheTree = null;
        if ($update) {
            foreach ($oldIdx as $p => $e) if (!isset($result[$p])) wt_remove_path($repo, $p);
            checkout_index_entries($repo, $idx, true);
        }
        $idx->write($indexOutput);
        return 0;
    }
    // three trees: base, ours, theirs
    [$base, $ours, $theirs] = $flats;
    $res = merge_trees_flat($repo, $base, $ours, $theirs);
    $idx->entries = [];
    foreach ($res->entries as $p => $stages) {
        foreach ($stages as $st => $v) {
            $e = new IndexEntry();
            $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
            $e->setStage($st);
            $idx->entries[] = $e;
        }
    }
    $idx->reindex();
        $idx->sort();
    $idx->cacheTree = null;
    if ($update) checkout_index_entries($repo, $idx, true);
    $idx->write($indexOutput);
    return $res->conflicts ? 1 : 0;
}

function checkout_index_entries(Repo $repo, Index $idx, bool $force) {
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0 && $e->stage() !== 2) continue;
        if ($e->skipWorktree()) continue;
        $path = $repo->worktree . '/' . $e->path;
        if (!$force && file_exists($path)) continue;
        wt_checkout_entry($repo, $e);
    }
    $idx->dirty = true;
}

function cmd_checkout_index(array $argv): int {
    $all = false; $force = false; $quiet = false; $prefix = ''; $stage = null; $paths = []; $z = false; $stdin = false;
    $tempOut = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-a' || $a === '--all': $all = true; break;
            case $a === '-f' || $a === '--force': $force = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '-u' || $a === '--index': break;
            case $a === '-n' || $a === '--no-create': break;
            case $a === '-z': $z = true; break;
            case $a === '--stdin': $stdin = true; break;
            case $a === '--temp': $tempOut = true; break;
            case strncmp($a, '--prefix=', 9) === 0: $prefix = substr($a, 9); break;
            case strncmp($a, '--stage=', 8) === 0: $stage = substr($a, 8); break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; $i = count($argv); break;
            default: $paths[] = $a;
        }
    }
    $repo = need_repo(true);
    $idx = $repo->index();
    if ($stdin) {
        $data = stream_get_contents(STDIN);
        foreach (($z ? explode("\0", $data) : explode("\n", $data)) as $l) if ($l !== '') $paths[] = $l;
    }
    $rel = repo_prefix($repo);
    $ps = $all ? null : Pathspec::parse($paths, $rel);
    $ret = 0;
    foreach ($idx->sorted() as $e) {
        if ($ps !== null && !$ps->matches($e->path)) continue;
        $wantStage = $stage === null ? 0 : (int)$stage;
        if ($stage === 'all') { /* write temp files for each stage */ }
        elseif ($e->stage() !== $wantStage) continue;
        $target = $repo->worktree . '/' . $prefix . $e->path;
        if (!$force && file_exists($target)) continue;
        mkdir_p(dirname($target));
        $o = $repo->odb->read($e->oid);
        if ($o === null) { $ret = 1; continue; }
        if ($e->mode === 0120000) { @unlink($target); @symlink($o[1], $target); }
        else {
            @file_put_contents($target, $o[1]);
            @chmod($target, ($e->mode & 0111) ? (0777 & ~umask_value()) : (0666 & ~umask_value()));
        }
    }
    return $ret;
}

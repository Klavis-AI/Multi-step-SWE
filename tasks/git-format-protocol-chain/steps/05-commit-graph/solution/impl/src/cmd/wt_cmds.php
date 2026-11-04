<?php
// git-add(1), git-rm(1), git-mv(1), git-status(1), git-commit(1)

function cmd_add(array $argv): int {
    $all = false; $update = false; $dryRun = false; $force = false; $intent = false;
    $verbose = false; $refresh = false; $paths = []; $ignoreMissing = false; $ignoreErrors = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-A' || $a === '--all' || $a === '--no-ignore-removal': $all = true; break;
            case $a === '-u' || $a === '--update': $update = true; break;
            case $a === '-n' || $a === '--dry-run': $dryRun = true; break;
            case $a === '-f' || $a === '--force': $force = true; break;
            case $a === '-N' || $a === '--intent-to-add': $intent = true; break;
            case $a === '-v' || $a === '--verbose': $verbose = true; break;
            case $a === '--refresh': $refresh = true; break;
            case $a === '--ignore-missing': $ignoreMissing = true; break;
            case $a === '--ignore-errors': $ignoreErrors = true; break;
            case $a === '--renormalize': break;
            case $a === '--sparse': break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; $i = count($argv); break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git add [<options>] [--] <pathspec>...", 129);
            default: $paths[] = $a;
        }
    }
    $repo = need_repo(true);
    $idx = $repo->index();
    $prefix = repo_prefix($repo);
    global $GIT_OPTS;
    if (!$paths && !$all && !$update) {
        fwrite(STDERR, "Nothing specified, nothing added.\nhint: Maybe you wanted to say 'git add .'?\nhint: Disable this message with \"git config set advice.addEmptyPathspec false\"\n");
        return 0;
    }
    $ps = Pathspec::parse($paths, $prefix, $GIT_OPTS['literal_pathspecs']);
    $scan = wt_scan($repo, $ps, false);
    $tracked = [];
    foreach ($idx->sorted() as $e) if ($e->stage() === 0) $tracked[$e->path] = $e;
    $ex = build_exclude_stack($repo);
    $added = 0;
    if (!$update) {
        foreach ($scan->files as $p => $st) {
            if (!$ps->matches($p)) continue;
            if (isset($tracked[$p])) continue;
            if ($dryRun) { out("add '$p'\n"); $added++; continue; }
            index_add_path($repo, $idx, $p, $intent);
            if ($verbose) out("add '$p'\n");
            $added++;
        }
        // a directory with its own .git becomes a gitlink; git says so, because
        // the outer repository will not carry its contents (git-add(1))
        foreach ($scan->gitlinks as $p => $abs) {
            if (!$ps->matches($p) && !$ps->matches($p . '/')) continue;
            if (isset($tracked[$p])) continue;
            $oid = nested_repo_head($abs);
            if ($oid === null) continue;
            if ($dryRun) { out("add '$p'\n"); $added++; continue; }
            warn_embedded_repository($p);
            $e = new IndexEntry();
            $e->path = $p;
            $e->mode = 0160000;
            $e->oid = $oid;
            $idx->put($e);
            if ($verbose) out("add '$p'\n");
            $added++;
        }
    }
    // modified and deleted tracked files
    foreach ($tracked as $p => $e) {
        if (!$ps->matches($p)) continue;
        $full = $repo->worktree . '/' . $p;
        $st = lstat_info($full);
        if ($st === null) {
            if ($update || $all || $paths) {
                if ($dryRun) { out("remove '$p'\n"); continue; }
                $idx->remove($p, null);
            }
            continue;
        }
        if ($dryRun) continue;
        index_add_path($repo, $idx, $p);
    }
    if (!$dryRun) $idx->write();
    // report pathspecs that matched nothing
    foreach ($paths as $p) {
        if ($p === '.' || $p === '') continue;
        $full = ltrim(path_normalize($prefix . $p), '/');
        $hit = false;
        foreach ($scan->files as $f => $_) if (Pathspec::matchOne($full, $f, false, false)) { $hit = true; break; }
        if (!$hit) foreach ($tracked as $f => $_) if (Pathspec::matchOne($full, $f, false, false)) { $hit = true; break; }
        if (!$hit && !$ignoreMissing) {
            fwrite(STDERR, "fatal: pathspec '$p' did not match any files\n");
            return 128;
        }
    }
    return 0;
}

function cmd_rm(array $argv): int {
    $cached = false; $force = false; $recursive = false; $dryRun = false; $quiet = false;
    $ignoreUnmatch = false; $paths = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--cached': $cached = true; break;
            case $a === '-f' || $a === '--force': $force = true; break;
            case $a === '-r': $recursive = true; break;
            case $a === '-n' || $a === '--dry-run': $dryRun = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--ignore-unmatch': $ignoreUnmatch = true; break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; $i = count($argv); break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git rm [<options>] [--] <file>...", 129);
            default: $paths[] = $a;
        }
    }
    $repo = need_repo(true);
    $idx = $repo->index();
    $prefix = repo_prefix($repo);
    $ps = Pathspec::parse($paths, $prefix);
    $matched = [];
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0 && $e->stage() !== 1) { if ($e->stage() > 1) continue; }
        if (!$ps->matches($e->path)) continue;
        $matched[$e->path] = $e;
    }
    if (!$matched) {
        if ($ignoreUnmatch) return 0;
        fwrite(STDERR, "fatal: pathspec '" . ($paths[0] ?? '') . "' did not match any files\n");
        return 128;
    }
    if (!$recursive) {
        foreach ($matched as $p => $e) {
            foreach ($paths as $spec) {
                $full = ltrim(path_normalize($prefix . $spec), '/');
                if ($full !== '' && $p !== $full && strncmp($p, $full . '/', strlen($full) + 1) === 0) {
                    fwrite(STDERR, "fatal: not removing '$spec' recursively without -r\n");
                    return 128;
                }
            }
        }
    }
    $head = $repo->refs->resolve('HEAD');
    $headTree = $head === null ? null : peel_to_tree($repo, $head);
    $flat = [];
    if ($headTree !== null) tree_flatten($repo, $headTree, '', $flat);
    if (!$force && !$cached) {
        foreach ($matched as $p => $e) {
            $full = $repo->worktree . '/' . $p;
            $st = lstat_info($full);
            if ($st === null) continue;
            $content = wt_read_blob($full, $st);
            $oid = $repo->hashData('blob', $content);
            $staged = !isset($flat[$p]) || $flat[$p][1] !== $e->oid;
            if ($oid !== $e->oid) {
                fwrite(STDERR, "error: the following file has local modifications:\n    $p\n(use --cached to keep the file, or -f to force removal)\n");
                return 1;
            }
            if ($staged) {
                fwrite(STDERR, "error: the following file has changes staged in the index:\n    $p\n(use --cached to keep the file, or -f to force removal)\n");
                return 1;
            }
        }
    }
    foreach ($matched as $p => $e) {
        if (!$quiet) out("rm '$p'\n");
        if ($dryRun) continue;
        $idx->remove($p, null);
        if (!$cached) wt_remove_path($repo, $p);
    }
    if (!$dryRun) $idx->write();
    return 0;
}

function cmd_mv(array $argv): int {
    $force = false; $dryRun = false; $args = []; $k = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($a === '-f' || $a === '--force') { $force = true; continue; }
        if ($a === '-n' || $a === '--dry-run') { $dryRun = true; continue; }
        if ($a === '-k') { $k = true; continue; }
        if ($a === '-v') continue;
        if ($a === '--') continue;
        $args[] = $a;
    }
    if (count($args) < 2) usage_die('usage: git mv [<options>] <source>... <destination>', 129);
    $repo = need_repo(true);
    $idx = $repo->index();
    $prefix = repo_prefix($repo);
    $dest = array_pop($args);
    $destFull = ltrim(path_normalize($prefix . $dest), '/');
    $destIsDir = is_dir($repo->worktree . '/' . $destFull);
    foreach ($args as $src) {
        $srcFull = ltrim(path_normalize($prefix . $src), '/');
        $target = $destIsDir ? $destFull . '/' . basename($srcFull) : $destFull;
        $e = $idx->find($srcFull, 0);
        $srcPath = $repo->worktree . '/' . $srcFull;
        if ($e === null && !is_dir($srcPath)) {
            if ($k) continue;
            fwrite(STDERR, "fatal: not under version control, source=$src, destination=$dest\n");
            return 128;
        }
        if (file_exists($repo->worktree . '/' . $target) && !$force && !$destIsDir) {
            if ($k) continue;
            fwrite(STDERR, "fatal: destination exists, source=$src, destination=$dest\n");
            return 128;
        }
        if ($dryRun) { out("Checking rename of '$srcFull' to '$target'\n"); continue; }
        if (is_dir($srcPath)) {
            mkdir_p(dirname($repo->worktree . '/' . $target));
            @rename($srcPath, $repo->worktree . '/' . $target);
            foreach ($idx->sorted() as $ie) {
                if (strncmp($ie->path, $srcFull . '/', strlen($srcFull) + 1) === 0) {
                    $ie->path = $target . '/' . substr($ie->path, strlen($srcFull) + 1);
                }
            }
            $idx->reindex();
        $idx->sort();
            $idx->dirty = true;
            continue;
        }
        mkdir_p(dirname($repo->worktree . '/' . $target));
        @rename($srcPath, $repo->worktree . '/' . $target);
        $idx->remove($srcFull, null);
        index_add_path($repo, $idx, $target);
    }
    if (!$dryRun) $idx->write();
    return 0;
}

/**
 * git's default "normal" untracked mode reports a directory that contains no
 * tracked file as a single "dir/" entry.
 */
function collapse_untracked(WorktreeScan $scan, array $tracked, ?Pathspec $ps, bool $all): array {
    $untracked = [];
    $dirHasTracked = [];
    foreach (array_keys($tracked) as $t) {
        $d = '';
        foreach (explode('/', $t) as $seg) {
            $rest = strpos($t, '/', strlen($d));
            if ($rest === false) break;
            $d .= substr($t, strlen($d), $rest - strlen($d) + 1);
            $dirHasTracked[$d] = true;
        }
    }
    $collapsed = [];
    foreach ($scan->files as $p => $st) {
        if (isset($tracked[$p])) continue;
        if (!$all) {
            // find the topmost parent directory with no tracked content
            $parts = explode('/', $p);
            array_pop($parts);
            $dir = '';
            $pick = null;
            foreach ($parts as $seg) {
                $dir .= $seg . '/';
                if (!isset($dirHasTracked[$dir])) { $pick = $dir; break; }
            }
            if ($pick !== null) {
                if ($ps !== null && !$ps->matchesDir(rtrim($pick, '/')) && !$ps->matches(rtrim($pick, '/'))) continue;
                $collapsed[$pick] = true;
                continue;
            }
        }
        if ($ps !== null && !$ps->matches($p)) continue;
        $untracked[] = $p;
    }
    foreach (array_keys($collapsed) as $d) $untracked[] = $d;
    return $untracked;
}

function status_collect(Repo $repo, ?Pathspec $ps = null, bool $untrackedAll = false): array {
    $idx = $repo->index();
    $head = $repo->refs->resolve('HEAD');
    $headTree = $head === null ? null : peel_to_tree($repo, $head);
    $staged = diff_tree_index($repo, $headTree, $idx, $ps);
    if ($repo->config->getBool('diff.renames', true)) detect_renames($repo, $staged);
    $unstaged = diff_index_worktree($repo, $idx, $ps);
    $scan = wt_scan($repo, $ps, true);
    $tracked = [];
    foreach ($idx->sorted() as $e) $tracked[$e->path] = true;
    $untracked = collapse_untracked($scan, $tracked, $ps, $untrackedAll);
    sort($untracked, SORT_STRING);
    $unmerged = [];
    foreach ($idx->sorted() as $e) if ($e->stage() !== 0) $unmerged[$e->path] = true;
    return ['staged' => $staged, 'unstaged' => $unstaged, 'untracked' => $untracked,
            'unmerged' => array_keys($unmerged), 'head' => $head, 'ignored' => array_keys($scan->ignored)];
}

/** git quotes a path containing a space in status output, unlike ls-files. */
function status_quote_path(string $p, string $term): string {
    if ($term === "\0") return $p;
    $q = quote_c_style_if_needed($p);
    if ($q === $p && strpos($p, ' ') !== false) return '"' . $p . '"';
    return $q;
}

function status_branch_name(Repo $repo): array {
    $raw = $repo->refs->readRaw('HEAD');
    if ($raw !== null && $raw[1] !== null) {
        return [preg_replace('#^refs/heads/#', '', $raw[1]), true];
    }
    $oid = $repo->refs->resolve('HEAD');
    return [$oid === null ? 'HEAD' : substr($oid, 0, 7), false];
}

function cmd_status(array $argv): int {
    $porcelain = null; $short = false; $long = true; $branchOpt = false; $z = false;
    $untrackedMode = 'normal'; $ignored = false; $paths = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--porcelain': $porcelain = 1; $long = false; break;
            case strncmp($a, '--porcelain=', 12) === 0: $porcelain = (int)substr($a, 12); $long = false; break;
            case $a === '-s' || $a === '--short': $short = true; $long = false; break;
            case $a === '-b' || $a === '--branch': $branchOpt = true; break;
            case $a === '-z': $z = true; break;
            case $a === '--long': $long = true; break;
            case strncmp($a, '--untracked-files=', 18) === 0: $untrackedMode = substr($a, 18); break;
            case $a === '-u' || $a === '--untracked-files': $untrackedMode = 'all'; break;
            case $a === '-uno': $untrackedMode = 'no'; break;
            case $a === '-uall': $untrackedMode = 'all'; break;
            case $a === '--ignored' || strncmp($a, '--ignored=', 10) === 0: $ignored = true; break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; $i = count($argv); break;
            case $a !== '' && $a[0] === '-': break;
            default: $paths[] = $a;
        }
    }
    $repo = need_repo(true);
    // status answers over the full index; it expands a sparse index in memory
    // (silently -- unlike a plain `ls-files`, git prints no hint here).
    if (function_exists('sparse_index_expand')) sparse_index_expand($repo, $repo->index());
    $ps = Pathspec::parse($paths, repo_prefix($repo));
    $st = status_collect($repo, $ps->isEmpty() ? null : $ps);
    [$branch, $onBranch] = status_branch_name($repo);
    $term = $z ? "\0" : "\n";
    if ($porcelain !== null || $short) {
        $rows = [];
        $stagedMap = [];
        foreach ($st['staged'] as $d) $stagedMap[$d->path] = $d->status;
        $unstagedMap = [];
        foreach ($st['unstaged'] as $d) $unstagedMap[$d->path] = $d->status;
        $paths2 = array_unique(array_merge(array_keys($stagedMap), array_keys($unstagedMap)));
        sort($paths2, SORT_STRING);
        if ($branchOpt) {
            if ($porcelain === 2) {
                out('# branch.oid ' . ($st['head'] ?? '(initial)') . $term);
                out('# branch.head ' . $branch . $term);
            } else {
                out('## ' . ($st['head'] === null ? 'No commits yet on ' . $branch : $branch) . $term);
            }
        }
        $renameFrom = [];
        foreach ($st['staged'] as $d) if ($d->status === 'R') $renameFrom[$d->path] = $d->oldPath;
        foreach ($paths2 as $p) {
            $x = $stagedMap[$p] ?? ' ';
            $y = $unstagedMap[$p] ?? ' ';
            if (in_array($p, $st['unmerged'], true)) { $x = 'U'; $y = 'U'; }
            $shown = status_quote_path($p, $term);
            if (isset($renameFrom[$p])) {
                $shown = $term === "\0"
                    ? $shown . "\0" . $renameFrom[$p]
                    : status_quote_path($renameFrom[$p], $term) . ' -> ' . $shown;
            }
            out($x . $y . ' ' . $shown . $term);
        }
        if ($untrackedMode !== 'no') {
            foreach ($st['untracked'] as $p) out('?? ' . status_quote_path($p, $term) . $term);
        }
        if ($ignored) foreach ($st['ignored'] as $p) out('!! ' . status_quote_path($p, $term) . $term);
        return 0;
    }
    // long format
    $head = $st['head'];
    if ($onBranch) out("On branch $branch\n");
    else out("HEAD detached at $branch\n");
    if ($head === null) out("\nNo commits yet\n\n");
    $label = function (string $s): string {
        switch ($s) {
            case 'A': return 'new file:';
            case 'D': return 'deleted:';
            case 'M': return 'modified:';
            case 'T': return 'typechange:';
            case 'R': return 'renamed:';
            case 'C': return 'copied:';
            default: return 'modified:';
        }
    };
    $unmergedSet = [];
    foreach ($st['unmerged'] as $p) $unmergedSet[$p] = true;
    $stagedReal = array_values(array_filter($st['staged'], function ($d) use ($unmergedSet) {
        return !isset($unmergedSet[$d->path]);
    }));
    $unstagedReal = array_values(array_filter($st['unstaged'], function ($d) use ($unmergedSet) {
        return !isset($unmergedSet[$d->path]);
    }));
    if ($stagedReal) {
        out("Changes to be committed:\n");
        out("  (use \"git restore --staged <file>...\" to unstage)\n");
        foreach ($stagedReal as $d) {
            $shown = $d->status === 'R'
                ? status_quote_path($d->oldPath, "\n") . ' -> ' . status_quote_path($d->path, "\n")
                : status_quote_path($d->path, "\n");
            out("\t" . str_pad($label($d->status), 12) . $shown . "\n");
        }
        out("\n");
    }
    if ($st['unmerged']) {
        out("Unmerged paths:\n");
        out("  (use \"git restore --staged <file>...\" to unstage)\n");
        out("  (use \"git add <file>...\" to mark resolution)\n");
        foreach ($st['unmerged'] as $p) {
            out("\t" . str_pad('both modified:', 12) . quote_path_maybe($p, "\n") . "\n");
        }
        out("\n");
    }
    if ($unstagedReal) {
        $hasDeleted = false;
        foreach ($unstagedReal as $d) if ($d->status === 'D') $hasDeleted = true;
        out("Changes not staged for commit:\n");
        out("  (use \"git add" . ($hasDeleted ? '/rm' : '') . " <file>...\" to update what will be committed)\n");
        out("  (use \"git restore <file>...\" to discard changes in working directory)\n");
        foreach ($unstagedReal as $d) {
            out("\t" . str_pad($label($d->status), 12) . status_quote_path($d->path, "\n") . "\n");
        }
        out("\n");
    }
    if ($st['untracked'] && $untrackedMode !== 'no') {
        out("Untracked files:\n");
        out("  (use \"git add <file>...\" to include in what will be committed)\n");
        foreach ($st['untracked'] as $p) out("\t" . status_quote_path($p, "\n") . "\n");
        out("\n");
    }
    if (!$stagedReal && !$unstagedReal && !$st['unmerged']) {
        if ($st['untracked'] && $untrackedMode !== 'no') {
            out("nothing added to commit but untracked files present (use \"git add\" to track)\n");
        } elseif ($head === null) {
            out("nothing to commit (create/copy files and use \"git add\" to track)\n");
        } else {
            out("nothing to commit, working tree clean\n");
        }
        return 0;
    }
    if (!$stagedReal && ($unstagedReal || $st['unmerged'])) {
        out("no changes added to commit (use \"git add\" and/or \"git commit -a\")\n");
    }
    return 0;
}

function cmd_commit(array $argv): int {
    $message = null; $file = null; $all = false; $amend = false; $allowEmpty = false;
    $allowEmptyMessage = false; $quiet = false; $paths = []; $author = null; $date = null;
    $reuse = null; $noVerify = false; $porcelainOut = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-m': $message = ($message === null ? '' : $message . "\n\n") . ($argv[++$i] ?? ''); break;
            case strncmp($a, '--message=', 10) === 0: $message = ($message === null ? '' : $message . "\n\n") . substr($a, 10); break;
            case $a === '-F' || $a === '--file': $file = $argv[++$i] ?? ''; break;
            case strncmp($a, '--file=', 7) === 0: $file = substr($a, 7); break;
            case $a === '-a' || $a === '--all': $all = true; break;
            case $a === '--amend': $amend = true; break;
            case $a === '--allow-empty': $allowEmpty = true; break;
            case $a === '--allow-empty-message': $allowEmptyMessage = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--no-verify' || $a === '-n': $noVerify = true; break;
            case strncmp($a, '--author=', 9) === 0: $author = substr($a, 9); break;
            case strncmp($a, '--date=', 7) === 0: $date = substr($a, 7); break;
            case $a === '-C' || $a === '--reuse-message': $reuse = $argv[++$i] ?? ''; break;
            case $a === '-c': $reuse = $argv[++$i] ?? ''; break;
            case $a === '--reset-author': break;
            case $a === '--no-edit' || $a === '-e' || $a === '--edit': break;
            case $a === '-s' || $a === '--signoff': break;
            case $a === '-v' || $a === '--verbose': break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; $i = count($argv); break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git commit [<options>] [--] <pathspec>...", 129);
            default: $paths[] = $a;
        }
    }
    $repo = need_repo(true);
    $idx = $repo->index();
    if ($idx->hasConflicts()) {
        fwrite(STDERR, "error: Committing is not possible because you have unmerged files.\nhint: Fix them up in the work tree, and then use 'git add/rm <file>'\nhint: as appropriate to mark resolution and make a commit.\nfatal: Exiting because of an unresolved conflict.\n");
        return 1;
    }
    $prefix = repo_prefix($repo);
    if ($all || $paths) {
        $ps = Pathspec::parse($paths, $prefix);
        foreach ($idx->sorted() as $e) {
            if ($paths && !$ps->matches($e->path)) continue;
            $full = $repo->worktree . '/' . $e->path;
            if (!file_exists($full) && !is_link($full)) { $idx->remove($e->path, null); continue; }
            index_add_path($repo, $idx, $e->path);
        }
        if ($paths) {
            $scan = wt_scan($repo, $ps, false);
            foreach ($scan->files as $p => $st) {
                if (!$ps->matches($p)) continue;
                if ($idx->find($p, 0) === null) continue;   // only tracked paths for `commit <path>`
                index_add_path($repo, $idx, $p);
            }
        }
        $idx->write();
    }
    $head = $repo->refs->resolve('HEAD');
    $headRaw = $repo->refs->readRaw('HEAD');
    $branchRef = ($headRaw !== null && $headRaw[1] !== null) ? $headRaw[1] : null;
    $parents = [];
    if ($amend) {
        if ($head === null) die_git('You have nothing to amend.');
        $prev = CommitObj::parse($repo->odb->read($head)[1], $head);
        $parents = $prev->parents;
    } elseif ($head !== null) {
        $parents = [$head];
    }
    $mergeHead = @file_get_contents($repo->path('MERGE_HEAD'));
    if ($mergeHead !== false) {
        foreach (explode("\n", trim($mergeHead)) as $mh) if ($mh !== '') $parents[] = $mh;
    }
    $tree = index_write_tree($repo, $idx);
    if (!$allowEmpty && !$amend && $mergeHead === false) {
        $parentTree = null;
        if (isset($parents[0])) $parentTree = peel_to_tree($repo, $parents[0]);
        if ($parentTree !== null && $parentTree === $tree) {
            $stx = status_collect($repo, null);
            [$branch, $onBranch] = status_branch_name($repo);
            out("On branch $branch\n");
            out("nothing to commit, working tree clean\n");
            return 1;
        }
        if ($parentTree === null && $tree === $repo->emptyTreeOid()) {
            [$branch, $onBranch] = status_branch_name($repo);
            out("On branch $branch\n\nInitial commit\n\nnothing to commit\n");
            return 1;
        }
    }
    $msg = $message;
    if ($file !== null) {
        $c = $file === '-' ? stream_get_contents(STDIN) : @file_get_contents($file);
        if ($c === false) die_git("could not read '$file'");
        $msg = $c;
    }
    if ($msg === null && $reuse !== null) {
        $o = rev_parse_object($repo, $reuse);
        if ($o !== null) $msg = CommitObj::parse($repo->odb->read($o)[1])->message;
    }
    if ($msg === null && $amend && $head !== null) {
        $msg = CommitObj::parse($repo->odb->read($head)[1])->message;
    }
    if ($msg === null) {
        $mf = @file_get_contents($repo->path('MERGE_MSG'));
        if ($mf !== false) $msg = $mf;
    }
    if ($msg === null || trim($msg) === '') {
        if (!$allowEmptyMessage) {
            fwrite(STDERR, "Aborting commit due to empty commit message.\n");
            return 1;
        }
        $msg = $msg ?? '';
    }
    // strip comments and trailing whitespace like git's cleanup=default
    $lines = explode("\n", $msg);
    $keep = [];
    foreach ($lines as $l) {
        if (strlen($l) && $l[0] === '#') continue;
        $keep[] = rtrim($l);
    }
    $msg = rtrim(implode("\n", $keep), "\n") . "\n";
    $c = new CommitObj();
    $c->tree = $tree;
    $c->parents = $parents;
    [$an, $ae, $ats, $atz] = author_ident($repo);
    if ($author !== null && preg_match('/^(.*?)\s*<([^>]*)>\s*$/', $author, $m)) { $an = $m[1]; $ae = $m[2]; }
    if ($date !== null) { $d = parse_date($date); if ($d !== false) { $ats = $d[0]; $atz = $d[1]; } }
    if ($amend && $head !== null) {
        $prev = CommitObj::parse($repo->odb->read($head)[1]);
        if ($prev->author && getenv('GIT_AUTHOR_NAME') === false && $author === null) {
            $an = $prev->author->name; $ae = $prev->author->email;
            if ($date === null) { $ats = $prev->author->ts; $atz = $prev->author->tz; }
        }
    }
    [$cn, $ce, $cts, $ctz] = committer_ident($repo);
    $c->author = Ident::parse("$an <$ae> $ats $atz");
    $c->committer = Ident::parse("$cn <$ce> $cts $ctz");
    $c->message = $msg;
    $oid = $repo->odb->write('commit', $c->format());
    $summary = $c->subject();
    $reason = ($amend ? 'commit (amend): ' : ($parents === [] ? 'commit (initial): ' : ($mergeHead !== false ? 'commit (merge): ' : 'commit: '))) . $summary;
    $target = $branchRef ?? 'HEAD';
    $repo->refs->writeRef($target, $oid, null, null, $reason);
    if ($branchRef !== null) $repo->refs->reflogAppend('HEAD', $head, $oid, $reason);
    @unlink($repo->path('MERGE_HEAD'));
    @unlink($repo->path('MERGE_MSG'));
    @unlink($repo->path('MERGE_MODE'));
    if (!$quiet) {
        $branch = $branchRef === null ? 'HEAD detached' : preg_replace('#^refs/heads/#', '', $branchRef);
        $rootTag = ($parents === []) ? ' (root-commit)' : '';
        out("[$branch$rootTag " . substr($oid, 0, 7) . "] $summary\n");
        $parentTree = isset($parents[0]) ? peel_to_tree($repo, $parents[0]) : null;
        $entries = diff_tree_tree($repo, $amend && isset($parents[0]) ? $parentTree : $parentTree, $tree, null);
        $files = count($entries);
        $ins = 0; $del = 0;
        foreach ($entries as $d) {
            $oldData = preg_match('/^0+$/', $d->oldOid) ? '' : ($repo->odb->read($d->oldOid)[1] ?? '');
            $newData = preg_match('/^0+$/', $d->newOid) ? '' : ($repo->odb->read($d->newOid)[1] ?? '');
            [$a, $r] = count_line_changes($oldData, $newData);
            $ins += $a; $del += $r;
        }
        $line = ' ' . $files . ' file' . ($files === 1 ? '' : 's') . ' changed';
        if ($ins) $line .= ', ' . $ins . ' insertion' . ($ins === 1 ? '' : 's') . '(+)';
        if ($del) $line .= ', ' . $del . ' deletion' . ($del === 1 ? '' : 's') . '(-)';
        out($line . "\n");
        foreach ($entries as $d) {
            if ($d->status === 'A') out(' create mode ' . $d->newMode . ' ' . $d->path . "\n");
            elseif ($d->status === 'D') out(' delete mode ' . $d->oldMode . ' ' . $d->path . "\n");
        }
    }
    return 0;
}

/** HEAD of a repository nested inside the work tree. */
function nested_repo_head(string $path): ?string {
    $dot = $path . '/.git';
    if (is_file($dot)) {
        $c = trim((string)@file_get_contents($dot));
        if (strncmp($c, 'gitdir:', 7) !== 0) return null;
        $dot = trim(substr($c, 7));
        if ($dot !== '' && $dot[0] !== '/') $dot = $path . '/' . $dot;
    }
    if (!is_dir($dot)) return null;
    try {
        $sub = Repo::open($dot, null);
    } catch (Throwable $e) {
        return null;
    }
    return $sub->refs->resolve('HEAD');
}

function warn_embedded_repository(string $path) {
    fwrite(STDERR, "warning: adding embedded git repository: $path\n");
    fwrite(STDERR, "hint: You've added another git repository inside your current repository.\n");
    fwrite(STDERR, "hint: Clones of the outer repository will not contain the contents of\n");
    fwrite(STDERR, "hint: the embedded repository and will not know how to obtain it.\n");
    fwrite(STDERR, "hint: If you meant to add a submodule, use:\n");
    fwrite(STDERR, "hint:\n");
    fwrite(STDERR, "hint: \tgit submodule add <url> $path\n");
    fwrite(STDERR, "hint:\n");
    fwrite(STDERR, "hint: If you added this path by mistake, you can remove it from the\n");
    fwrite(STDERR, "hint: index with:\n");
    fwrite(STDERR, "hint:\n");
    fwrite(STDERR, "hint: \tgit rm --cached $path\n");
    fwrite(STDERR, "hint:\n");
    fwrite(STDERR, "hint: See \"git help submodule\" for more information.\n");
    fwrite(STDERR, "hint: Disable this message with \"git config set advice.addEmbeddedRepo false\"\n");
}

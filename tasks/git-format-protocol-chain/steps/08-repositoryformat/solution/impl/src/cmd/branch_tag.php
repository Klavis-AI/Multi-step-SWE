<?php
// git-branch(1), git-tag(1), git-checkout(1), git-switch(1), git-restore(1),
// git-reset(1), git-merge(1)

function cmd_branch(array $argv): int {
    $repo = need_repo();
    $delete = false; $forceDelete = false; $move = false; $copy = false; $force = false;
    $verbose = 0; $all = false; $remotes = false; $list = false; $showCurrent = false;
    $format = null; $args = []; $setUpstream = null; $unsetUpstream = false; $quiet = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-d' || $a === '--delete': $delete = true; break;
            case $a === '-D': $delete = true; $forceDelete = true; break;
            case $a === '-m' || $a === '--move': $move = true; break;
            case $a === '-M': $move = true; $force = true; break;
            case $a === '-c' || $a === '--copy': $copy = true; break;
            case $a === '-C': $copy = true; $force = true; break;
            case $a === '-f' || $a === '--force': $force = true; break;
            case $a === '-v' || $a === '--verbose': $verbose++; break;
            case $a === '-a' || $a === '--all': $all = true; $list = true; break;
            case $a === '-r' || $a === '--remotes': $remotes = true; $list = true; break;
            case $a === '-l' || $a === '--list': $list = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--show-current': $showCurrent = true; break;
            case strncmp($a, '--format=', 9) === 0: $format = substr($a, 9); break;
            case $a === '-u' || $a === '--set-upstream-to': $setUpstream = $argv[++$i] ?? ''; break;
            case strncmp($a, '--set-upstream-to=', 18) === 0: $setUpstream = substr($a, 18); break;
            case $a === '--unset-upstream': $unsetUpstream = true; break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git branch [<options>] [-r | -a] [--merged] [--no-merged]", 129);
            default: $args[] = $a;
        }
    }
    $headRaw = $repo->refs->readRaw('HEAD');
    $current = ($headRaw !== null && $headRaw[1] !== null) ? preg_replace('#^refs/heads/#', '', $headRaw[1]) : null;
    if ($showCurrent) {
        if ($current !== null) out($current . "\n");
        return 0;
    }
    if ($delete) {
        $ret = 0;
        foreach ($args as $name) {
            $ref = strncmp($name, 'refs/', 5) === 0 ? $name : ($remotes ? "refs/remotes/$name" : "refs/heads/$name");
            $oid = $repo->refs->resolve($ref);
            if ($oid === null) {
                fwrite(STDERR, "error: branch '$name' not found.\n");
                $ret = 1;
                continue;
            }
            if ($current !== null && $ref === "refs/heads/$current") {
                fwrite(STDERR, "error: cannot delete branch '$name' used by worktree at '" . $repo->worktree . "'\n");
                $ret = 1;
                continue;
            }
            if (!$forceDelete) {
                $head = $repo->refs->resolve('HEAD');
                if ($head !== null && !is_ancestor($repo, $oid, $head)) {
                    fwrite(STDERR, "error: the branch '$name' is not fully merged.\nhint: If you are sure you want to delete it, run 'git branch -D $name'.\n");
                    $ret = 1;
                    continue;
                }
            }
            $repo->refs->deleteRef($ref, null, '');
            if (!$quiet) out("Deleted branch $name (was " . substr($oid, 0, 7) . ").\n");
        }
        return $ret;
    }
    if ($move || $copy) {
        $from = count($args) >= 2 ? $args[0] : ($current ?? '');
        $to = count($args) >= 2 ? $args[1] : ($args[0] ?? '');
        if ($to === '') usage_die('usage: git branch [<options>] (-m | -M) [<old-branch>] <new-branch>', 129);
        if (!check_ref_format("refs/heads/$to")) die_git("'$to' is not a valid branch name");
        $oid = $repo->refs->resolve("refs/heads/$from");
        if ($oid === null) die_git("branch '$from' does not exist");
        if (!$force && $repo->refs->resolve("refs/heads/$to") !== null) die_git("a branch named '$to' already exists");
        $repo->refs->writeRef("refs/heads/$to", $oid, null, null, ($copy ? 'branch: Copied from ' : 'Branch: renamed refs/heads/') . $from);
        if ($move) {
            $logFrom = $repo->refs instanceof FilesRefStore ? $repo->refs->reflogPath("refs/heads/$from") : null;
            $logTo = $repo->refs instanceof FilesRefStore ? $repo->refs->reflogPath("refs/heads/$to") : null;
            if ($logFrom !== null && is_file($logFrom)) { mkdir_p(dirname($logTo)); @copy($logFrom, $logTo); }
            $repo->refs->deleteRef("refs/heads/$from", null, '');
            if ($current === $from) $repo->refs->writeRef('HEAD', null, "refs/heads/$to", null, '', false);
        }
        return 0;
    }
    if ($setUpstream !== null) {
        $branch = $args[0] ?? $current;
        $up = $setUpstream;
        $upRef = resolve_ref_dwim($repo, $up);
        if ($upRef === null) die_git("the requested upstream branch '$up' does not exist");
        $name = $upRef[0];
        if (preg_match('#^refs/remotes/([^/]+)/(.*)$#', $name, $m)) {
            $cf = ConfigFile::load($repo->commonPath('config'));
            $cf->setValue('branch', $branch, 'remote', $m[1]);
            $cf->setValue('branch', $branch, 'merge', 'refs/heads/' . $m[2]);
            $cf->save();
            out("branch '$branch' set up to track '{$m[1]}/{$m[2]}'.\n");
        } else {
            $cf = ConfigFile::load($repo->commonPath('config'));
            $cf->setValue('branch', $branch, 'remote', '.');
            $cf->setValue('branch', $branch, 'merge', $name);
            $cf->save();
            out("branch '$branch' set up to track '$up'.\n");
        }
        return 0;
    }
    if ($args && !$list) {
        $name = $args[0];
        $start = $args[1] ?? 'HEAD';
        if (!check_ref_format("refs/heads/$name")) die_git("'$name' is not a valid branch name");
        $oid = rev_parse_object($repo, $start);
        if ($oid === null) die_git("not a valid object name: '$start'");
        $oid = peel_to_commit($repo, $oid);
        if ($oid === null) die_git("not a valid object name: '$start'");
        if (!$force && $repo->refs->resolve("refs/heads/$name") !== null) {
            fwrite(STDERR, "fatal: a branch named '$name' already exists\n");
            return 128;
        }
        $repo->refs->writeRef("refs/heads/$name", $oid, null, null, 'branch: Created from ' . $start);
        return 0;
    }
    // listing
    $prefixes = ['refs/heads/'];
    if ($remotes) $prefixes = ['refs/remotes/'];
    if ($all) $prefixes = ['refs/heads/', 'refs/remotes/'];
    $rows = [];
    foreach ($prefixes as $pfx) {
        foreach ($repo->refs->listRefs($pfx) as $name => $oid) {
            $short = substr($name, strlen($pfx));
            if ($pfx === 'refs/remotes/' && $all) $short = substr($name, strlen('refs/'));
            if ($args) {
                $hit = false;
                foreach ($args as $p) if (glob_match($p, $short, false) || $p === $short) { $hit = true; break; }
                if (!$hit) continue;
            }
            $rows[] = [$short, $oid, $name];
        }
    }
    usort($rows, function ($a, $b) { return strcmp($a[0], $b[0]); });
    if ($current === null && !$remotes && $format === null) {
        $h = $repo->refs->resolve('HEAD');
        if ($h !== null) {
            $detached = '(HEAD detached at ' . substr($h, 0, 7) . ')';
            array_unshift($rows, [$detached, $h, 'HEAD']);
        }
    }
    $maxName = 0;
    foreach ($rows as $r) $maxName = max($maxName, strlen($r[0]));
    foreach ($rows as [$short, $oid, $full]) {
        if ($format !== null) { out(expand_ref_format($repo, $format, $full, $oid) . "\n"); continue; }
        $mark = (($current !== null && $full === "refs/heads/$current") || ($current === null && $full === 'HEAD')) ? '* ' : '  ';
        if ($verbose) {
            $o = $repo->odb->read($oid);
            $subject = ($o !== null && $o[0] === 'commit') ? CommitObj::parse($o[1])->subject() : '';
            out($mark . str_pad($short, $maxName) . ' ' . substr($oid, 0, 7) . ' ' . $subject . "\n");
        } else {
            out($mark . $short . "\n");
        }
    }
    return 0;
}

function cmd_tag(array $argv): int {
    $repo = need_repo();
    $annotate = false; $message = null; $file = null; $force = false; $delete = false;
    $list = false; $args = []; $format = null; $pointsAt = null; $sign = false; $verify = false;
    $numLines = null;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-a' || $a === '--annotate': $annotate = true; break;
            case $a === '-s' || $a === '--sign': $annotate = true; $sign = true; break;
            case $a === '-m': $message = ($message === null ? '' : $message . "\n\n") . ($argv[++$i] ?? ''); $annotate = true; break;
            case strncmp($a, '--message=', 10) === 0: $message = substr($a, 10); $annotate = true; break;
            case $a === '-F' || $a === '--file': $file = $argv[++$i] ?? ''; $annotate = true; break;
            case $a === '-f' || $a === '--force': $force = true; break;
            case $a === '-d' || $a === '--delete': $delete = true; break;
            case $a === '-l' || $a === '--list': $list = true; break;
            case $a === '-v' || $a === '--verify': $verify = true; break;
            case preg_match('/^-n(\d*)$/', $a, $m) === 1: $numLines = $m[1] === '' ? 1 : (int)$m[1]; $list = true; break;
            case strncmp($a, '--format=', 9) === 0: $format = substr($a, 9); break;
            case $a === '--points-at': $pointsAt = $argv[++$i] ?? ''; break;
            case strncmp($a, '--points-at=', 12) === 0: $pointsAt = substr($a, 12); break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $args[] = $a;
        }
    }
    if ($delete) {
        $ret = 0;
        foreach ($args as $name) {
            $ref = "refs/tags/$name";
            $oid = $repo->refs->resolve($ref);
            if ($oid === null) { fwrite(STDERR, "error: tag '$name' not found.\n"); $ret = 1; continue; }
            $repo->refs->deleteRef($ref, null, '');
            out("Deleted tag '$name' (was " . substr($oid, 0, 7) . ")\n");
        }
        return $ret;
    }
    if (!$args || $list) {
        $rows = [];
        foreach ($repo->refs->listRefs('refs/tags/') as $name => $oid) {
            $short = substr($name, strlen('refs/tags/'));
            if ($args) {
                $hit = false;
                foreach ($args as $p) if (glob_match($p, $short, false) || $p === $short) { $hit = true; break; }
                if (!$hit) continue;
            }
            if ($pointsAt !== null) {
                $want = rev_parse_object($repo, $pointsAt);
                $peeled = peel_object($repo, $oid);
                if ($oid !== $want && $peeled !== $want) continue;
            }
            $rows[] = [$short, $oid, $name];
        }
        usort($rows, function ($a, $b) { return strcmp($a[0], $b[0]); });
        foreach ($rows as [$short, $oid, $full]) {
            if ($format !== null) { out(expand_ref_format($repo, $format, $full, $oid) . "\n"); continue; }
            out($short . "\n");
        }
        return 0;
    }
    $name = $args[0];
    $target = $args[1] ?? 'HEAD';
    if (!check_ref_format("refs/tags/$name")) die_git("'$name' is not a valid tag name.");
    $oid = rev_parse_object($repo, $target);
    if ($oid === null) die_git("Failed to resolve '$target' as a valid ref.");
    if (!$force && $repo->refs->resolve("refs/tags/$name") !== null) {
        fwrite(STDERR, "fatal: tag '$name' already exists\n");
        return 128;
    }
    if ($annotate) {
        $body = $message;
        if ($file !== null) {
            $c = $file === '-' ? stream_get_contents(STDIN) : @file_get_contents($file);
            if ($c === false) die_git("could not open '$file'");
            $body = $c;
        }
        if ($body === null) die_git('no tag message?');
        $o = $repo->odb->read($oid);
        $t = new TagObj();
        $t->object = $oid;
        $t->type = $o[0];
        $t->tag = $name;
        [$cn, $ce, $cts, $ctz] = committer_ident($repo);
        $t->tagger = Ident::parse("$cn <$ce> $cts $ctz");
        $t->message = rtrim($body, "\n") . "\n";
        $oid = $repo->odb->write('tag', $t->format());
    }
    $repo->refs->writeRef("refs/tags/$name", $oid, null, null, '');
    return 0;
}

function checkout_switch_branch(Repo $repo, string $refname, ?string $oid, bool $detach, bool $force, bool $quiet): int {
    $idx = $repo->index();
    $head = $repo->refs->resolve('HEAD');
    $oldTree = $head === null ? null : peel_to_tree($repo, $head);
    $newTree = $oid === null ? null : peel_to_tree($repo, $oid);
    $oldFlat = []; $newFlat = [];
    if ($oldTree !== null) tree_flatten($repo, $oldTree, '', $oldFlat);
    if ($newTree !== null) tree_flatten($repo, $newTree, '', $newFlat);
    // refuse to lose local modifications
    if (!$force) {
        $dirty = [];
        foreach (diff_index_worktree($repo, $idx, null) as $d) {
            $p = $d->path;
            $inOld = isset($oldFlat[$p]); $inNew = isset($newFlat[$p]);
            if ($inOld && $inNew && $oldFlat[$p][1] === $newFlat[$p][1]) continue;
            if (!$inNew && !$inOld) continue;
            $dirty[] = $p;
        }
        foreach (diff_tree_index($repo, $oldTree, $idx, null) as $d) {
            $p = $d->path;
            if (isset($newFlat[$p]) && isset($oldFlat[$p]) && $oldFlat[$p][1] === $newFlat[$p][1]) continue;
            $dirty[] = $p;
        }
        $dirty = array_values(array_unique($dirty));
        if ($dirty) {
            fwrite(STDERR, "error: Your local changes to the following files would be overwritten by checkout:\n");
            foreach ($dirty as $p) fwrite(STDERR, "\t$p\n");
            fwrite(STDERR, "Please commit your changes or stash them before you switch branches.\nAborting\n");
            return 1;
        }
    }
    foreach ($oldFlat as $p => $v) {
        if (!isset($newFlat[$p])) wt_remove_path($repo, $p);
    }
    $idx->entries = [];
    foreach ($newFlat as $p => $v) {
        $e = new IndexEntry();
        $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
        $idx->entries[] = $e;
    }
    $idx->reindex();
        $idx->sort();
    $idx->cacheTree = null;
    foreach ($idx->sorted() as $e) wt_checkout_entry($repo, $e);
    $idx->write();
    return 0;
}

function cmd_checkout(array $argv): int {
    $repo = need_repo(true);
    $newBranch = null; $forceNew = false; $detach = false; $force = false; $quiet = false;
    $track = null; $orphan = null; $paths = []; $args = []; $afterDD = false; $merge = false;
    $ours = false; $theirs = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($afterDD) { $paths[] = $a; continue; }
        switch (true) {
            case $a === '-b': $newBranch = $argv[++$i] ?? ''; break;
            case $a === '-B': $newBranch = $argv[++$i] ?? ''; $forceNew = true; break;
            case $a === '--orphan': $orphan = $argv[++$i] ?? ''; break;
            case $a === '--detach': $detach = true; break;
            case $a === '-f' || $a === '--force': $force = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '-m' || $a === '--merge': $merge = true; break;
            case $a === '--ours': $ours = true; break;
            case $a === '--theirs': $theirs = true; break;
            case $a === '-t' || $a === '--track': $track = true; break;
            case $a === '--': $afterDD = true; break;
            case $a !== '' && $a[0] === '-': break;
            default: $args[] = $a;
        }
    }
    $idx = $repo->index();
    if ($orphan !== null) {
        $repo->refs->writeRef('HEAD', null, "refs/heads/$orphan", null, '', false);
        if (!$quiet) fwrite(STDERR, "Switched to a new branch '$orphan'\n");
        return 0;
    }
    if ($paths || (count($args) && $repo->index()->find($args[0], 0) !== null && rev_parse_object($repo, $args[0]) === null)) {
        // restore paths from the index or a tree
        $source = null;
        if ($args && rev_parse_object($repo, $args[0]) !== null && ($paths || count($args) > 1)) $source = array_shift($args);
        foreach ($args as $a) $paths[] = $a;
        $ps = Pathspec::parse($paths, repo_prefix($repo));
        if ($source !== null) {
            $tree = peel_to_tree($repo, rev_parse_object($repo, $source));
            $flat = [];
            tree_flatten($repo, $tree, '', $flat);
            foreach ($flat as $p => $v) {
                if (!$ps->matches($p)) continue;
                $e = new IndexEntry();
                $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
                $idx->remove($p, null);
                $idx->put($e);
                wt_checkout_entry($repo, $e);
            }
        } else {
            foreach ($idx->sorted() as $e) {
                if ($e->stage() !== 0 && $e->stage() !== 2) continue;
                if (!$ps->matches($e->path)) continue;
                if ($e->stage() === 2) { $stage = $ours ? 2 : ($theirs ? 3 : 2); $x = $idx->find($e->path, $stage); if ($x) wt_checkout_entry($repo, $x); continue; }
                wt_checkout_entry($repo, $e);
            }
        }
        $idx->write();
        return 0;
    }
    if ($newBranch !== null) {
        $start = $args[0] ?? 'HEAD';
        $oid = rev_parse_object($repo, $start);
        if ($oid === null) die_git("invalid reference: $start");
        $oid = peel_to_commit($repo, $oid);
        if (!$forceNew && $repo->refs->resolve("refs/heads/$newBranch") !== null) {
            fwrite(STDERR, "fatal: a branch named '$newBranch' already exists\n");
            return 128;
        }
        if (!check_ref_format("refs/heads/$newBranch")) die_git("'$newBranch' is not a valid branch name");
        $r = checkout_switch_branch($repo, "refs/heads/$newBranch", $oid, false, $force, $quiet);
        if ($r !== 0) return $r;
        $repo->refs->writeRef("refs/heads/$newBranch", $oid, null, null, 'branch: Created from ' . $start);
        $repo->refs->writeRef('HEAD', null, "refs/heads/$newBranch", null, '', false);
        $repo->refs->reflogAppend('HEAD', $repo->refs->resolve('HEAD'), $oid, "checkout: moving from " . ($args[0] ?? 'HEAD') . " to $newBranch");
        if (!$quiet) fwrite(STDERR, "Switched to a new branch '$newBranch'\n");
        return 0;
    }
    if (!$args) usage_die('usage: git checkout [<options>] <branch>', 129);
    $name = $args[0];
    $ref = resolve_ref_dwim($repo, $name);
    $oid = rev_parse_object($repo, $name);
    if ($oid === null) {
        fwrite(STDERR, "error: pathspec '$name' did not match any file(s) known to git\n");
        return 1;
    }
    $commit = peel_to_commit($repo, $oid);
    if ($commit === null) die_git("reference is not a tree: $name");
    $oldHeadRaw = $repo->refs->readRaw('HEAD');
    $oldName = ($oldHeadRaw !== null && $oldHeadRaw[1] !== null) ? preg_replace('#^refs/heads/#', '', $oldHeadRaw[1]) : substr((string)$repo->refs->resolve('HEAD'), 0, 7);
    $oldOid = $repo->refs->resolve('HEAD');
    $r = checkout_switch_branch($repo, $name, $commit, $detach, $force, $quiet);
    if ($r !== 0) return $r;
    if (!$detach && $ref !== null && strncmp($ref[0], 'refs/heads/', 11) === 0) {
        $repo->refs->writeRef('HEAD', null, $ref[0], null, '', false);
        $short = substr($ref[0], 11);
        $repo->refs->reflogAppend('HEAD', $oldOid, $commit, "checkout: moving from $oldName to $short");
        if (!$quiet) fwrite(STDERR, "Switched to branch '$short'\n");
    } else {
        $repo->refs->writeRef('HEAD', $commit, null, null, "checkout: moving from $oldName to $name", false);
        if (!$quiet) {
            fwrite(STDERR, "Note: switching to '$name'.\n\nYou are in 'detached HEAD' state.\n");
            fwrite(STDERR, "HEAD is now at " . substr($commit, 0, 7) . ' ' . commit_subject_of($repo, $commit) . "\n");
        }
    }
    return 0;
}

/** The diffstat and create/delete summary git prints after a merge. */
function merge_summary(Repo $repo, ?string $oldTree, ?string $newTree): string {
    $entries = diff_tree_tree($repo, $oldTree, $newTree, null);
    $opts = ['stat' => true, 'patch' => false, 'abbrev' => 7];
    $text = emit_diff($repo, $entries, $opts);
    foreach ($entries as $d) {
        if ($d->status === 'A') $text .= ' create mode ' . $d->newMode . ' ' . $d->path . "\n";
        elseif ($d->status === 'D') $text .= ' delete mode ' . $d->oldMode . ' ' . $d->path . "\n";
    }
    return $text;
}

function commit_subject_of(Repo $repo, string $oid): string {
    $o = $repo->odb->read($oid);
    if ($o === null || $o[0] !== 'commit') return '';
    return CommitObj::parse($o[1])->subject();
}

function cmd_reset(array $argv): int {
    $repo = need_repo();
    $mode = null; $quiet = false; $args = []; $paths = []; $afterDD = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($afterDD) { $paths[] = $a; continue; }
        switch (true) {
            case $a === '--soft': $mode = 'soft'; break;
            case $a === '--mixed': $mode = 'mixed'; break;
            case $a === '--hard': $mode = 'hard'; break;
            case $a === '--merge': $mode = 'merge'; break;
            case $a === '--keep': $mode = 'keep'; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--': $afterDD = true; break;
            case $a !== '' && $a[0] === '-': break;
            default: $args[] = $a;
        }
    }
    $idx = $repo->index();
    $rev = 'HEAD';
    if ($args) {
        $maybe = $args[0];
        if (rev_parse_object($repo, $maybe) !== null) { $rev = $maybe; array_shift($args); }
    }
    foreach ($args as $a) $paths[] = $a;
    $oid = rev_parse_object($repo, $rev);
    if ($oid === null) die_git("ambiguous argument '$rev': unknown revision or path not in the working tree.");
    $commit = peel_to_commit($repo, $oid);
    $tree = peel_to_tree($repo, $oid);
    if ($paths) {
        $ps = Pathspec::parse($paths, repo_prefix($repo));
        $flat = [];
        if ($tree !== null) tree_flatten($repo, $tree, '', $flat);
        foreach ($idx->sorted() as $e) {
            if (!$ps->matches($e->path)) continue;
            if (!isset($flat[$e->path])) $idx->remove($e->path, null);
        }
        foreach ($flat as $p => $v) {
            if (!$ps->matches($p)) continue;
            $e = new IndexEntry();
            $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
            $idx->remove($p, null);
            $idx->put($e);
        }
        $idx->write();
        return 0;
    }
    if ($mode === null) $mode = 'mixed';
    $head = $repo->refs->resolve('HEAD');
    $headRaw = $repo->refs->readRaw('HEAD');
    $target = ($headRaw !== null && $headRaw[1] !== null) ? $headRaw[1] : 'HEAD';
    $repo->refs->writeRef($target, $commit, null, null, 'reset: moving to ' . $rev);
    if ($target !== 'HEAD') $repo->refs->reflogAppend('HEAD', $head, $commit, 'reset: moving to ' . $rev);
    if ($mode === 'soft') return 0;
    $flat = [];
    if ($tree !== null) tree_flatten($repo, $tree, '', $flat);
    $old = [];
    foreach ($idx->sorted() as $e) if ($e->stage() === 0) $old[$e->path] = $e;
    $idx->entries = [];
    foreach ($flat as $p => $v) {
        $e = new IndexEntry();
        $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
        if (isset($old[$p]) && $old[$p]->oid === $v[1] && $old[$p]->mode === $e->mode) {
            $e->ctime_s = $old[$p]->ctime_s; $e->mtime_s = $old[$p]->mtime_s; $e->dev = $old[$p]->dev;
            $e->ino = $old[$p]->ino; $e->uid = $old[$p]->uid; $e->gid = $old[$p]->gid; $e->size = $old[$p]->size;
        }
        $idx->entries[] = $e;
    }
    $idx->reindex();
        $idx->sort();
    $idx->cacheTree = null;
    if ($mode === 'hard') {
        foreach ($old as $p => $e) if (!isset($flat[$p])) wt_remove_path($repo, $p);
        foreach ($idx->sorted() as $e) wt_checkout_entry($repo, $e);
    }
    $idx->write();
    if ($mode === 'hard' && !$quiet) {
        out('HEAD is now at ' . substr($commit, 0, 7) . ' ' . commit_subject_of($repo, $commit) . "\n");
    }
    if ($mode === 'mixed' && !$quiet) {
        $ch = diff_index_worktree($repo, $idx, null);
        if ($ch) {
            out("Unstaged changes after reset:\n");
            foreach ($ch as $d) out($d->status . "\t" . $d->path . "\n");
        }
    }
    return 0;
}

function cmd_merge(array $argv): int {
    $repo = need_repo(true);
    $message = null; $noFF = false; $ffOnly = false; $squash = false; $abort = false;
    $quiet = false; $args = []; $noCommit = false; $strategy = null;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-m': $message = $argv[++$i] ?? ''; break;
            case $a === '--no-ff': $noFF = true; break;
            case $a === '--ff-only': $ffOnly = true; break;
            case $a === '--ff': break;
            case $a === '--squash': $squash = true; break;
            case $a === '--abort': $abort = true; break;
            case $a === '--continue': break;
            case $a === '--no-commit': $noCommit = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--no-edit' || $a === '-e' || $a === '--edit': break;
            case $a === '-s' || $a === '--strategy': $strategy = $argv[++$i] ?? ''; break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $args[] = $a;
        }
    }
    $idx = $repo->index();
    if ($abort) {
        $head = $repo->refs->resolve('HEAD');
        @unlink($repo->path('MERGE_HEAD'));
        @unlink($repo->path('MERGE_MSG'));
        @unlink($repo->path('MERGE_MODE'));
        if ($head !== null) {
            $tree = peel_to_tree($repo, $head);
            $flat = [];
            tree_flatten($repo, $tree, '', $flat);
            $old = [];
            foreach ($idx->sorted() as $e) $old[$e->path] = true;
            $idx->entries = [];
            foreach ($flat as $p => $v) {
                $e = new IndexEntry(); $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
                $idx->entries[] = $e;
            }
            $idx->reindex();
        $idx->sort();
            foreach (array_keys($old) as $p) if (!isset($flat[$p])) wt_remove_path($repo, $p);
            foreach ($idx->sorted() as $e) wt_checkout_entry($repo, $e);
            $idx->write();
        }
        return 0;
    }
    if (!$args) usage_die('usage: git merge [<options>] [<commit>...]', 129);
    $head = $repo->refs->resolve('HEAD');
    if ($head === null) die_git('No current branch.');
    $otherName = $args[0];
    $other = rev_parse_object($repo, $otherName);
    if ($other === null) die_git("$otherName - not something we can merge");
    $other = peel_to_commit($repo, $other);
    if (is_ancestor($repo, $other, $head)) {
        if (!$quiet) out("Already up to date.\n");
        return 0;
    }
    $bases = merge_bases($repo, $head, $other);
    $base = $bases[0] ?? null;
    if ($base === $head && !$noFF) {
        // fast-forward
        if (!$quiet) out('Updating ' . substr($head, 0, 7) . '..' . substr($other, 0, 7) . "\nFast-forward\n");
        $headRaw = $repo->refs->readRaw('HEAD');
        $target = ($headRaw !== null && $headRaw[1] !== null) ? $headRaw[1] : 'HEAD';
        $newTree = peel_to_tree($repo, $other);
        $oldTree = peel_to_tree($repo, $head);
        $oldFlat = []; $newFlat = [];
        tree_flatten($repo, $oldTree, '', $oldFlat);
        tree_flatten($repo, $newTree, '', $newFlat);
        foreach ($oldFlat as $p => $v) if (!isset($newFlat[$p])) wt_remove_path($repo, $p);
        $idx->entries = [];
        foreach ($newFlat as $p => $v) {
            $e = new IndexEntry(); $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
            $idx->entries[] = $e;
            wt_checkout_entry($repo, $e);
        }
        $idx->reindex();
        $idx->sort();
        $idx->cacheTree = null;
        $idx->write();
        $repo->refs->writeRef($target, $other, null, null, "merge $otherName: Fast-forward");
        if ($target !== 'HEAD') $repo->refs->reflogAppend('HEAD', $head, $other, "merge $otherName: Fast-forward");
        if (!$quiet) out(merge_summary($repo, $oldTree, $newTree));
        return 0;
    }
    if ($ffOnly) {
        fwrite(STDERR, "fatal: Not possible to fast-forward, aborting.\n");
        return 128;
    }
    $baseFlat = []; $ourFlat = []; $theirFlat = [];
    if ($base !== null) tree_flatten($repo, peel_to_tree($repo, $base), '', $baseFlat);
    tree_flatten($repo, peel_to_tree($repo, $head), '', $ourFlat);
    tree_flatten($repo, peel_to_tree($repo, $other), '', $theirFlat);
    $res = merge_trees_flat($repo, $baseFlat, $ourFlat, $theirFlat, 'HEAD', $otherName);
    $idx->entries = [];
    foreach ($res->entries as $p => $stages) {
        foreach ($stages as $st => $v) {
            $e = new IndexEntry();
            $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1]; $e->setStage($st);
            $idx->entries[] = $e;
        }
    }
    $idx->reindex();
        $idx->sort();
    $idx->cacheTree = null;
    foreach ($res->removed as $p => $_) wt_remove_path($repo, $p);
    foreach ($res->entries as $p => $stages) {
        if (isset($res->files[$p])) {
            $full = $repo->worktree . '/' . $p;
            mkdir_p(dirname($full));
            @file_put_contents($full, $res->files[$p][1]);
            if (octdec($res->files[$p][0]) & 0111) @chmod($full, 0777 & ~umask_value());
            continue;
        }
        $e = $idx->find($p, 0);
        if ($e !== null) wt_checkout_entry($repo, $e);
    }
    $idx->write();
    foreach (array_keys($res->files) as $p) {
        if (!$quiet) out("Auto-merging $p\n");
    }
    if ($res->conflicts) {
        foreach (array_keys($res->conflicts) as $p) {
            out("CONFLICT (content): Merge conflict in $p\n");
        }
        write_file_atomic($repo->path('MERGE_HEAD'), $other . "\n");
        write_file_atomic($repo->path('MERGE_MSG'), ($message ?? "Merge commit '$otherName'") . "\n");
        out("Automatic merge failed; fix conflicts and then commit the result.\n");
        return 1;
    }
    if ($noCommit || $squash) {
        write_file_atomic($repo->path('MERGE_HEAD'), $other . "\n");
        write_file_atomic($repo->path('MERGE_MSG'), ($message ?? "Merge commit '$otherName'") . "\n");
        return 0;
    }
    $tree = index_write_tree($repo, $idx);
    $c = new CommitObj();
    $c->tree = $tree;
    $c->parents = [$head, $other];
    [$an, $ae, $ats, $atz] = author_ident($repo);
    [$cn, $ce, $cts, $ctz] = committer_ident($repo);
    $c->author = Ident::parse("$an <$ae> $ats $atz");
    $c->committer = Ident::parse("$cn <$ce> $cts $ctz");
    $c->message = ($message ?? "Merge commit '$otherName'") . "\n";
    $oid = $repo->odb->write('commit', $c->format());
    $headRaw = $repo->refs->readRaw('HEAD');
    $target = ($headRaw !== null && $headRaw[1] !== null) ? $headRaw[1] : 'HEAD';
    $repo->refs->writeRef($target, $oid, null, null, "merge $otherName: Merge made by the 'ort' strategy.");
    if ($target !== 'HEAD') $repo->refs->reflogAppend('HEAD', $head, $oid, "merge $otherName: Merge made by the 'ort' strategy.");
    if (!$quiet) out("Merge made by the 'ort' strategy.\n");
    return 0;
}

<?php
// git-stripspace(1), git-pack-refs(1), git-switch(1), git-restore(1) and the
// shared `-h` usage handling.

function git_usage_text(string $cmd): string {
    $u = [
        'add' => 'git add [<options>] [--] <pathspec>...',
        'branch' => 'git branch [<options>] [-r | -a] [--merged] [--no-merged]',
        'cat-file' => 'git cat-file <type> <object>',
        'check-ignore' => 'git check-ignore [<options>] <pathname>...',
        'check-ref-format' => 'git check-ref-format [--normalize] [<options>] <refname>',
        'checkout' => 'git checkout [<options>] <branch>',
        'checkout-index' => 'git checkout-index [<options>] [--] [<file>...]',
        'commit' => 'git commit [<options>] [--] <pathspec>...',
        'commit-tree' => 'git commit-tree [(-p <parent>)...] [(-m <message>)...] [(-F <file>)...] <tree>',
        'config' => 'git config [<options>]',
        'count-objects' => 'git count-objects [-v] [-H | --human-readable]',
        'diff' => 'git diff [<options>] [<commit>] [--] [<path>...]',
        'diff-files' => 'git diff-files [-q] [-0 | -1 | -2 | -3 | -c | --cc] [<common-diff-options>] [<path>...]',
        'diff-index' => 'git diff-index [<options>] <tree-ish> [<path>...]',
        'diff-tree' => 'git diff-tree [<options>] <tree-ish> [<tree-ish>] [<path>...]',
        'fsck' => 'git fsck [<options>] [<object>...]',
        'for-each-ref' => 'git for-each-ref [<options>] [<pattern>...]',
        'hash-object' => 'git hash-object [-t <type>] [-w] [--path=<file> | --no-filters] [--stdin [--literally]] [--] <file>...',
        'init' => 'git init [-q | --quiet] [--bare] [--template=<template-directory>] [--shared[=<permissions>]] [<directory>]',
        'log' => 'git log [<options>] [<revision-range>] [[--] <path>...]',
        'ls-files' => 'git ls-files [<options>] [<file>...]',
        'ls-tree' => 'git ls-tree [<options>] <tree-ish> [<path>...]',
        'merge' => 'git merge [<options>] [<commit>...]',
        'merge-base' => 'git merge-base [-a | --all] <commit> <commit>...',
        'mktree' => 'git mktree [-z] [--missing] [--batch]',
        'mv' => 'git mv [<options>] <source>... <destination>',
        'pack-refs' => 'git pack-refs [--all] [--no-prune] [--include <pattern>] [--exclude <pattern>]',
        'read-tree' => 'git read-tree [<options>] <tree-ish> [<tree-ish> [<tree-ish>]]',
        'reflog' => 'git reflog [show] [<log-options>] [<ref>]',
        'reset' => 'git reset [--mixed | --soft | --hard | --merge | --keep] [-q] [<commit>]',
        'restore' => 'git restore [<options>] [--source=<branch>] <file>...',
        'rev-list' => 'git rev-list [<options>] <commit>... [--] [<path>...]',
        'rev-parse' => 'git rev-parse --parseopt [<options>] -- [<args>...]',
        'rm' => 'git rm [<options>] [--] <file>...',
        'show' => 'git show [<options>] [<object>...]',
        'show-ref' => 'git show-ref [-q | --quiet] [--verify] [--head] [-d | --dereference] [-s | --hash[=<n>]] [--abbrev[=<n>]] [--tags] [--heads] [--] [<pattern>...]',
        'status' => 'git status [<options>] [--] <pathspec>...',
        'stripspace' => 'git stripspace [-s | --strip-comments]',
        'switch' => 'git switch [<options>] [<branch>]',
        'symbolic-ref' => 'git symbolic-ref [-m <reason>] <name> <ref>',
        'tag' => 'git tag [-a | -s | -u <key-id>] [-f] [-m <msg> | -F <file>] <tagname> [<commit> | <object>]',
        'update-index' => 'git update-index [<options>] [--] [<file>...]',
        'update-ref' => 'git update-ref [<options>] -d <refname> [<old-oid>]',
        'var' => 'git var (-l | <variable>)',
        'write-tree' => 'git write-tree [--missing-ok] [--prefix=<prefix>/]',
        'version' => 'git version [--build-options]',
    ];
    return $u[$cmd] ?? "git $cmd";
}

function cmd_stripspace(array $argv): int {
    $strip = false; $comment = false; $commentChar = '#';
    foreach ($argv as $a) {
        if ($a === '-s' || $a === '--strip-comments') { $strip = true; continue; }
        if ($a === '-c' || $a === '--comment-lines') { $comment = true; continue; }
    }
    $d = Repo::discover();
    if ($d !== null) {
        $repo = Repo::open($d[0], $d[1]);
        $cc = $repo->config->get('core.commentchar', null);
        if ($cc !== null && !is_bool($cc) && $cc !== '') $commentChar = $cc;
    }
    $in = stream_get_contents(STDIN);
    if ($comment) {
        $out = '';
        foreach (explode("\n", $in) as $i => $line) {
            if ($i === substr_count($in, "\n") && $line === '') break;
            $out .= ($line === '' ? $commentChar : $commentChar . ' ' . $line) . "\n";
        }
        out($out);
        return 0;
    }
    $lines = explode("\n", $in);
    $result = [];
    $pendingBlank = 0;
    $seenText = false;
    foreach ($lines as $i => $line) {
        if ($i === count($lines) - 1 && $line === '') continue;
        $line = rtrim($line, " \t\r");
        if ($strip && strlen($line) && $line[0] === $commentChar) continue;
        if ($line === '') {
            if ($seenText) $pendingBlank++;
            continue;
        }
        if ($seenText) $result[] = str_repeat("\n", min($pendingBlank, 1));
        $pendingBlank = 0;
        $seenText = true;
        $result[] = $line . "\n";
    }
    out(implode('', $result));
    return 0;
}

function cmd_pack_refs(array $argv): int {
    $repo = need_repo();
    $all = false; $prune = true; $includes = []; $excludes = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--all': $all = true; break;
            case $a === '--no-prune': $prune = false; break;
            case $a === '--prune': $prune = true; break;
            case $a === '--include': $includes[] = $argv[++$i] ?? ''; break;
            case strncmp($a, '--include=', 10) === 0: $includes[] = substr($a, 10); break;
            case $a === '--exclude': $excludes[] = $argv[++$i] ?? ''; break;
            case strncmp($a, '--exclude=', 10) === 0: $excludes[] = substr($a, 10); break;
            case strncmp($a, '--auto', 6) === 0: break;
        }
    }
    if ($repo->refs instanceof ReftableRefStore) {
        // pack-refs on a reftable repo compacts the stack into a single table.
        $repo->refs->stack->compact();
        return 0;
    }
    if (!($repo->refs instanceof FilesRefStore)) return 0;
    $store = $repo->refs;
    $packed = $store->packedRefs();
    $loose = [];
    foreach ($repo->refs->listRefs('refs/') as $name => $oid) {
        $p = $store->refPath($name);
        if (!is_file($p)) continue;    // already packed
        $loose[$name] = $oid;
    }
    foreach ($loose as $name => $oid) {
        $take = $all || strncmp($name, 'refs/tags/', 10) === 0;
        foreach ($includes as $pat) if (glob_match($pat, $name, false)) $take = true;
        foreach ($excludes as $pat) if (glob_match($pat, $name, false)) $take = false;
        if (!$take) continue;
        $packed[$name] = $oid;
        if ($prune) {
            @unlink($store->refPath($name));
        }
    }
    $store->writePackedRefs($packed);
    return 0;
}

function cmd_switch(array $argv): int {
    $args = [];
    $create = null; $forceCreate = false; $detach = false; $orphan = null; $force = false; $quiet = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-c' || $a === '--create': $create = $argv[++$i] ?? ''; break;
            case $a === '-C' || $a === '--force-create': $create = $argv[++$i] ?? ''; $forceCreate = true; break;
            case $a === '-d' || $a === '--detach': $detach = true; break;
            case $a === '--orphan': $orphan = $argv[++$i] ?? ''; break;
            case $a === '-f' || $a === '--force' || $a === '--discard-changes': $force = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $args[] = $a;
        }
    }
    $fwd = [];
    if ($create !== null) { $fwd[] = $forceCreate ? '-B' : '-b'; $fwd[] = $create; }
    if ($orphan !== null) { $fwd[] = '--orphan'; $fwd[] = $orphan; }
    if ($detach) $fwd[] = '--detach';
    if ($force) $fwd[] = '-f';
    if ($quiet) $fwd[] = '-q';
    foreach ($args as $a) $fwd[] = $a;
    if (!$args && $create === null && $orphan === null) {
        usage_die('usage: ' . git_usage_text('switch'), 129);
    }
    if ($args && $create === null && $orphan === null) {
        $repo = need_repo(true);
        if (rev_parse_object($repo, $args[0]) === null) {
            die_git('invalid reference: ' . $args[0]);
        }
    }
    return cmd_checkout($fwd);
}

function cmd_restore(array $argv): int {
    $repo = need_repo(true);
    $staged = false; $worktree = false; $source = null; $paths = []; $afterDD = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($afterDD) { $paths[] = $a; continue; }
        switch (true) {
            case $a === '-S' || $a === '--staged': $staged = true; break;
            case $a === '-W' || $a === '--worktree': $worktree = true; break;
            case $a === '-s' || $a === '--source': $source = $argv[++$i] ?? ''; break;
            case strncmp($a, '--source=', 9) === 0: $source = substr($a, 9); break;
            case $a === '--': $afterDD = true; break;
            case $a !== '' && $a[0] === '-': break;
            default: $paths[] = $a;
        }
    }
    if (!$staged && !$worktree) $worktree = true;
    $idx = $repo->index();
    $ps = Pathspec::parse($paths, repo_prefix($repo));
    $head = $repo->refs->resolve('HEAD');
    $srcTree = null;
    if ($source !== null) {
        $o = rev_parse_object($repo, $source);
        if ($o === null) die_git("invalid reference: $source");
        $srcTree = peel_to_tree($repo, $o);
    } elseif ($staged) {
        $srcTree = $head === null ? null : peel_to_tree($repo, $head);
    }
    if ($staged) {
        $flat = [];
        if ($srcTree !== null) tree_flatten($repo, $srcTree, '', $flat);
        foreach ($idx->sorted() as $e) {
            if (!$ps->matches($e->path)) continue;
            if (!isset($flat[$e->path])) { $idx->remove($e->path, null); continue; }
        }
        foreach ($flat as $p => $v) {
            if (!$ps->matches($p)) continue;
            $e = new IndexEntry();
            $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
            $idx->remove($p, null);
            $idx->put($e);
        }
    }
    if ($worktree) {
        if ($srcTree !== null && $source !== null) {
            $flat = [];
            tree_flatten($repo, $srcTree, '', $flat);
            foreach ($flat as $p => $v) {
                if (!$ps->matches($p)) continue;
                $e = new IndexEntry();
                $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
                wt_checkout_entry($repo, $e);
                if (!$staged) { $idx->remove($p, null); $idx->put($e); }
            }
        } else {
            foreach ($idx->sorted() as $e) {
                if ($e->stage() !== 0) continue;
                if (!$ps->matches($e->path)) continue;
                wt_checkout_entry($repo, $e);
            }
        }
    }
    $idx->write();
    return 0;
}

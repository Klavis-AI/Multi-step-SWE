<?php
// git-fsck(1), git-count-objects(1), git-check-ignore(1)

function cmd_fsck(array $argv): int {
    $repo = need_repo();
    $strict = false; $dangling = true; $unreachable = false; $connectivityOnly = false;
    $nameObjects = false; $progress = false; $lostFound = false;
    foreach ($argv as $a) {
        switch (true) {
            case $a === '--strict': $strict = true; break;
            case $a === '--no-dangling': $dangling = false; break;
            case $a === '--dangling': $dangling = true; break;
            case $a === '--unreachable': $unreachable = true; break;
            case $a === '--connectivity-only': $connectivityOnly = true; break;
            case $a === '--name-objects': $nameObjects = true; break;
            case $a === '--lost-found': $lostFound = true; break;
            case $a === '--full' || $a === '--no-full': break;
            case $a === '--cache' || $a === '--no-reflogs' || $a === '--root' || $a === '--tags': break;
            case $a === '--progress' || $a === '--no-progress': break;
            case $a === '-v' || $a === '--verbose': break;
        }
    }
    $errors = 0;   // git's bitmask: 1 object, 2 reachable, 4 pack, 8 refs
    $objects = [];
    $repo->odb->forEachLoose(function ($oid) use (&$objects) { $objects[$oid] = true; });
    $packedOnly = [];
    foreach ($repo->odb->packs() as $pk) {
        foreach ($pk->allOids() as $oid) {
            if (!isset($objects[$oid])) $packedOnly[$oid] = true;
        }
    }
    $types = [];
    $links = [];
    $corruptObjects = [];
    foreach (array_keys($packedOnly) as $oid) {
        // objects that only exist in a pack: parse them for links and types
        $o = $repo->odb->read($oid);
        if ($o === null) { fwrite(STDERR, "error: $oid: object corrupt or missing\n"); $errors |= 1; continue; }
        $types[$oid] = $o[0];
        if ($o[0] === 'tree') {
            foreach (TreeObj::parse($o[1])->entries as $e) {
                $links[] = [$oid, 'tree', $e->oid, $e->mode === '40000' ? 'tree' : ($e->mode === '160000' ? 'commit' : 'blob'), $e->name];
            }
        } elseif ($o[0] === 'commit') {
            $c = CommitObj::parse($o[1], $oid);
            $links[] = [$oid, 'commit', $c->tree, 'tree', ''];
            foreach ($c->parents as $p) $links[] = [$oid, 'commit', $p, 'commit', ''];
        } elseif ($o[0] === 'tag') {
            $t = TagObj::parse($o[1]);
            $links[] = [$oid, 'tag', $t->object, $t->type, ''];
        }
    }
    foreach (array_keys($objects) as $oid) {
        $path = $repo->odb->loosePath($oid);
        $rel = $repo->odb->relativePath($path);
        $raw = @file_get_contents($path);
        if ($raw === false) { fwrite(STDERR, "error: $oid: object corrupt or missing: $rel\n"); $errors |= 1; continue; }
        $ctx = inflate_init(ZLIB_ENCODING_DEFLATE);
        $data = $ctx === false ? false : @inflate_add($ctx, $raw, ZLIB_FINISH);
        if ($data === false) {
            fwrite(STDERR, "error: inflate: data stream error (incorrect data check)\n");
            fwrite(STDERR, "error: $oid: object corrupt or missing: $rel\n");
            fwrite(STDERR, "error: corrupt loose object '$oid'\n");
            fwrite(STDERR, "error: unable to unpack contents of $rel\n");
            $corruptObjects[$oid] = $rel;
            $errors |= 1;
            continue;
        }
        $nul = strpos($data, "\0");
        if ($nul === false) {
            fwrite(STDERR, "error: $oid: object corrupt or missing: $rel\n");
            $corruptObjects[$oid] = $rel;
            $errors |= 1;
            continue;
        }
        $hdr = substr($data, 0, $nul);
        $sp = strpos($hdr, ' ');
        $type = $sp === false ? '' : substr($hdr, 0, $sp);
        $size = $sp === false ? -1 : (int)substr($hdr, $sp + 1);
        $content = substr($data, $nul + 1);
        if (!in_array($type, ['blob', 'tree', 'commit', 'tag'], true)) {
            fwrite(STDERR, "error: $oid: object is of unknown type '$type'\n");
            $errors |= 1;
            continue;
        }
        if ($size !== strlen($content)) {
            fwrite(STDERR, "error: $oid: object corrupt or missing: $rel\n");
            fwrite(STDERR, "error: corrupt loose object '$oid'\n");
            fwrite(STDERR, "error: unable to unpack contents of $rel\n");
            $corruptObjects[$oid] = $rel;
            $errors |= 1;
            continue;
        }
        $actual = $repo->hashData($type, $content);
        if ($actual !== $oid) {
            fwrite(STDERR, "error: $oid: hash-path mismatch, found at: $rel\n");
            $errors |= 1;
            continue;
        }
        $types[$oid] = $type;
        if ($type === 'tree') {
            $t = TreeObj::parse($content);
            $prev = null;
            foreach ($t->entries as $e) {
                if (!preg_match('/^(100644|100755|120000|160000|40000|40000)$/', $e->mode)) {
                    fwrite(STDERR, "error in tree $oid: badFilemode: contains bad file modes\n");
                    $errors |= 1;
                }
                if ($e->name === '' || strpos($e->name, '/') !== false) {
                    fwrite(STDERR, "error in tree $oid: malformedName: contains invalid name\n");
                    $errors |= 1;
                }
                if ($e->name === '.' || $e->name === '..' || $e->name === '.git') {
                    fwrite(STDERR, "error in tree $oid: hasDotgit: contains '.git'\n");
                    $errors |= 1;
                }
                $links[] = [$oid, 'tree', $e->oid, $e->mode === '40000' ? 'tree' : ($e->mode === '160000' ? 'commit' : 'blob'), $e->name];
            }
        } elseif ($type === 'commit') {
            $c = CommitObj::parse($content, $oid);
            if ($c->tree === '') { fwrite(STDERR, "error in commit $oid: missingTree: invalid format - expected 'tree' line\n"); $errors |= 1; }
            else $links[] = [$oid, 'commit', $c->tree, 'tree', ''];
            foreach ($c->parents as $p) $links[] = [$oid, 'commit', $p, 'commit', ''];
            if ($c->author === null) { fwrite(STDERR, "error in commit $oid: missingAuthor: invalid format - expected 'author' line\n"); $errors |= 1; }
        } elseif ($type === 'tag') {
            $t = TagObj::parse($content);
            if ($t->object === '') { fwrite(STDERR, "error in tag $oid: missingObject: invalid format - expected 'object' line\n"); $errors |= 1; }
            else $links[] = [$oid, 'tag', $t->object, $t->type, ''];
        }
    }
    $missing = [];
    foreach ($links as [$from, $fromType, $to, $wantType, $name]) {
        if (isset($types[$to])) continue;
        if ($wantType === 'commit' && $fromType === 'tree') continue;   // gitlink
        $hdr = $repo->odb->readHeader($to);
        if ($hdr !== null) continue;
        $desc = $wantType === 'tree' ? 'tree' : ($wantType === 'commit' ? 'commit' : 'blob');
        $missing[$to] = $desc;
    }
    // reachability.  A repository with a promisor remote is allowed to be
    // missing objects: they are the ones the filter left behind, and git does
    // not report them (technical/partial-clone).
    $promisorRepo = function_exists('promisor_remotes') && promisor_remotes($repo) !== [];
    $reachable = [];
    $refs = $repo->refs->listRefs('refs/');
    $tips = array_values($refs);
    $h = $repo->refs->resolve('HEAD');
    if ($h !== null) $tips[] = $h;
    foreach ($tips as $tip) {
        $stack = [$tip];
        while ($stack) {
            $o = array_pop($stack);
            if (isset($reachable[$o])) continue;
            $reachable[$o] = true;
            if ($repo->odb->readHeader($o) === null) {
                if (isset($missing[$o]) || $promisorRepo) continue;
                fwrite(STDERR, "error: refs: invalid sha1 pointer $o\n");
                $errors |= 1;
                continue;
            }
            $obj = $repo->odb->read($o);
            if ($obj === null) continue;
            if ($obj[0] === 'commit') {
                $c = CommitObj::parse($obj[1], $o);
                $stack[] = $c->tree;
                foreach ($c->parents as $p) $stack[] = $p;
            } elseif ($obj[0] === 'tree') {
                foreach (TreeObj::parse($obj[1])->entries as $e) {
                    if ($e->mode === '160000') continue;
                    $stack[] = $e->oid;
                }
            } elseif ($obj[0] === 'tag') {
                $stack[] = TagObj::parse($obj[1])->object;
            }
        }
    }
    foreach ($repo->index()->sorted() as $e) $reachable[$e->oid] = true;
    foreach ($missing as $oid => $desc) {
        if (!isset($reachable[$oid])) continue;
        if ($promisorRepo) continue;   // the filter left it behind, by design
        out("missing $desc $oid\n");
        $errors |= 2;
    }
    if ($repo->refs->resolve('HEAD') === null) {
        $raw = $repo->refs->readRaw('HEAD');
        if ($raw !== null && $raw[1] !== null) {
            fwrite(STDERR, "notice: HEAD points to an unborn branch (" . preg_replace('#^refs/heads/#', '', $raw[1]) . ")\n");
        }
    }
    if (!$refs && $repo->refs->resolve('HEAD') === null) {
        fwrite(STDERR, "notice: No default references\n");
    }
    if ($dangling) {
        $names = [];
        foreach ($types as $oid => $type) {
            if (isset($reachable[$oid])) continue;
            // an object referenced by another unreachable object is not "dangling"
            $names[$oid] = $type;
        }
        $referenced = [];
        foreach ($links as [$from, $ft, $to, $wt, $n]) {
            if (isset($names[$from])) $referenced[$to] = true;
        }
        foreach ($names as $oid => $type) {
            if (isset($referenced[$oid])) continue;
            out("dangling $type $oid\n");
        }
    }
    if ($corruptObjects) {
        foreach ($corruptObjects as $oid => $rel) {
            fwrite(STDERR, "error: corrupt loose object '$oid'\n");
            fwrite(STDERR, "fatal: loose object $oid (stored in $rel) is corrupt\n");
            return 128;
        }
    }
    return $errors;   // git's bitmask exit code
}

function cmd_count_objects(array $argv): int {
    $repo = need_repo();
    $verbose = in_array('-v', $argv, true) || in_array('--verbose', $argv, true);
    $human = in_array('-H', $argv, true) || in_array('--human-readable', $argv, true);
    $count = 0; $size = 0;
    $repo->odb->forEachLoose(function ($oid) use (&$count, &$size, $repo) {
        $count++;
        // git reports disk usage, in KiB, from the allocated blocks
        $st = @stat($repo->odb->loosePath($oid));
        if ($st !== false) $size += (int)($st['blocks'] / 2);
    });
    if (!$verbose) { out("$count objects, {$size} kilobytes\n"); return 0; }
    $packs = glob($repo->objectsDir() . '/pack/*.pack') ?: [];
    $inPack = 0; $packSize = 0;
    foreach ($packs as $p) {
        $st = @stat($p);
        $packSize += $st === false ? 0 : (int)($st['blocks'] / 2);
        $idx = preg_replace('/\.pack$/', '.idx', $p);
        if (is_file($idx)) {
            $raw = @file_get_contents($idx);
            if ($raw !== false && strlen($raw) > 1032 && substr($raw, 0, 4) === "\xfftOc") {
                $inPack += rd32($raw, 8 + 255 * 4);
            }
        }
    }
    out("count: $count\nsize: $size\nin-pack: $inPack\npacks: " . count($packs) . "\nsize-pack: $packSize\nprune-packable: 0\ngarbage: 0\nsize-garbage: 0\n");
    return 0;
}

function cmd_check_ignore(array $argv): int {
    $repo = need_repo(true);
    $verbose = false; $nonMatching = false; $stdin = false; $z = false; $noIndex = false; $quiet = false;
    $paths = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-v' || $a === '--verbose': $verbose = true; break;
            case $a === '-n' || $a === '--non-matching': $nonMatching = true; break;
            case $a === '--stdin': $stdin = true; break;
            case $a === '-z': $z = true; break;
            case $a === '--no-index': $noIndex = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--': break;
            default: $paths[] = $a;
        }
    }
    if ($nonMatching && !$verbose) {
        fwrite(STDERR, "fatal: --non-matching is only valid with --verbose\n");
        return 128;
    }
    if (!$stdin && !$paths) usage_die('usage: git check-ignore [<options>] <pathname>...', 129);
    $prefix = repo_prefix($repo);
    $idx = $repo->index();
    $tracked = [];
    foreach ($idx->sorted() as $e) $tracked[$e->path] = true;
    $anyMatch = false;
    $term = $z ? "\0" : "\n";
    $check = function (string $p) use (&$anyMatch, $repo, $prefix, $tracked, $noIndex, $verbose, $nonMatching, $quiet, $term) {
        $rel = ltrim(path_normalize($prefix . $p), '/');
        if (!$noIndex && isset($tracked[$rel])) {
            if ($nonMatching && $verbose) out("::\t" . quote_path_maybe($p, $term) . $term);
            return;
        }
        $ex = build_exclude_stack($repo);
        $dirs = explode('/', $rel);
        array_pop($dirs);
        $acc = '';
        $ex->addFile($repo->worktree . '/.gitignore', '');
        foreach ($dirs as $d) {
            $acc .= $d . '/';
            $ex->addFile($repo->worktree . '/' . $acc . '.gitignore', $acc);
        }
        $isDir = is_dir($repo->worktree . '/' . $rel);
        // One rule pass: checkVerbose returns the last matching rule, and the
        // ignored/not-ignored verdict is exactly whether that rule is negative.
        $m = $ex->checkVerbose($rel, $isDir);
        $ignored = $m !== null && !$m[0]->negative;
        if ($ignored) {
            $anyMatch = true;
            if ($quiet) return;
            if ($verbose) {
                $r = $m[0];
                $src = $r->base === '' ? '.gitignore' : $r->base . '.gitignore';
                if ($r->base === '' && !is_file($repo->worktree . '/.gitignore')) $src = $repo->commonPath('info/exclude');
                if ($r->base === '' && is_file($repo->worktree . '/.gitignore')) {
                    $src = exclude_rule_source($repo, $r) ?? '.gitignore';
                } else {
                    $src = exclude_rule_source($repo, $r) ?? $src;
                }
                out($src . ':' . $r->lineNo . ':' . ($r->negative ? '!' : '') . $r->pattern . ($r->dirOnly ? '/' : '') . "\t" . quote_path_maybe($p, $term) . $term);
            } else {
                out(quote_path_maybe($p, $term) . $term);
            }
        } elseif ($nonMatching) {
            if ($verbose) out("::\t" . quote_path_maybe($p, $term) . $term);
            else out(quote_path_maybe($p, $term) . $term);
        }
    };
    foreach ($paths as $p) $check($p);
    if ($stdin) {
        if ($z) {
            $buf = '';
            while (!feof(STDIN)) {
                $chunk = fread(STDIN, 4096);
                if ($chunk === false || $chunk === '') break;
                $buf .= $chunk;
                while (($nul = strpos($buf, "\0")) !== false) {
                    $line = substr($buf, 0, $nul);
                    $buf = substr($buf, $nul + 1);
                    if ($line !== '') { $check($line); flush(); }
                }
            }
            if (trim($buf) !== '') { $check($buf); flush(); }
        } else {
            while (($line = fgets(STDIN)) !== false) {
                $line = rtrim($line, "\n");
                if ($line === '') continue;
                if ($line[0] === '"') $line = unquote_c_style($line);
                $check($line);
                flush();
            }
        }
    }
    return $anyMatch ? 0 : 1;
}

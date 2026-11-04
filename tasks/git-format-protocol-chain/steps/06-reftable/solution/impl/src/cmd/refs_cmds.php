<?php
// git-update-ref(1), git-symbolic-ref(1), git-show-ref(1), git-for-each-ref(1),
// git-check-ref-format(1), git-branch(1), git-tag(1), git-reflog(1)

function cmd_update_ref(array $argv): int {
    $repo = need_repo();
    $delete = false; $noDeref = false; $stdin = false; $z = false; $create = false;
    $reason = ''; $args = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-d': $delete = true; break;
            case $a === '--no-deref': $noDeref = true; break;
            case $a === '--stdin': $stdin = true; break;
            case $a === '-z': $z = true; break;
            case $a === '--create-reflog': $create = true; break;
            case $a === '-m': $reason = $argv[++$i] ?? ''; break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $args[] = $argv[$j]; $i = count($argv); break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git update-ref [<options>] -d <refname> [<old-oid>]", 129);
            default: $args[] = $a;
        }
    }
    if ($stdin) return update_ref_stdin($repo, $z, $reason);
    if (!$args) usage_die('usage: git update-ref [<options>] <refname> <new-oid> [<old-oid>]', 129);
    $ref = $args[0];
    if ($delete) {
        $old = $args[1] ?? null;
        if ($old !== null) {
            $o = rev_parse_object($repo, $old);
            if ($o === null) die_git("invalid old value: $old");
            $old = $o;
        }
        if (!$repo->refs->exists($ref)) {
            // deleting a ref that is already gone is a no-op unless an old
            // value other than the null oid was required
            if ($old !== null && $old !== str_repeat('0', $repo->hexsz)) {
                fwrite(STDERR, "error: cannot lock ref '$ref': unable to resolve reference '$ref'\n");
                return 1;
            }
            return 0;
        }
        return $repo->refs->deleteRef($ref, $old, $reason, !$noDeref) ? 0 : 1;
    }
    if (count($args) < 2) usage_die('usage: git update-ref [<options>] <refname> <new-oid> [<old-oid>]', 129);
    $newRaw = $args[1];
    $new = rev_parse_object($repo, $newRaw);
    if ($new === null) die_git("$newRaw: not a valid SHA1");
    $old = null;
    if (isset($args[2])) {
        if ($args[2] === '') $old = str_repeat('0', $repo->hexsz);
        else {
            $old = rev_parse_object($repo, $args[2]);
            if ($old === null) die_git("{$args[2]}: not a valid SHA1");
        }
    }
    if ($create) $repo->refs->reflogCreate($ref);
    return $repo->refs->writeRef($ref, $new, null, $old, $reason, !$noDeref) ? 0 : 1;
}

function update_ref_stdin(Repo $repo, bool $z, string $reason): int {
    $data = stream_get_contents(STDIN);
    $tokens = $z ? explode("\0", $data) : preg_split('/\n/', $data);
    $i = 0;
    $ops = [];
    $nextTok = function () use (&$i, $tokens, $z) {
        while ($i < count($tokens)) {
            $t = $tokens[$i++];
            if (!$z) { $t = trim($t); if ($t === '') continue; }
            elseif ($t === '' && $i >= count($tokens)) continue;
            return $t;
        }
        return null;
    };
    if (!$z) {
        foreach ($tokens as $line) {
            $line = trim($line);
            if ($line === '') continue;
            $parts = preg_split('/\s+/', $line);
            $cmd = array_shift($parts);
            $args = [];
            foreach ($parts as $p) $args[] = ($p !== '' && $p[0] === '"') ? unquote_c_style($p) : $p;
            $ops[] = [$cmd, $args];
        }
    } else {
        while (($t = $nextTok()) !== null) {
            $sp = strpos($t, ' ');
            $cmd = $sp === false ? $t : substr($t, 0, $sp);
            $first = $sp === false ? null : substr($t, $sp + 1);
            $args = $first === null ? [] : [$first];
            $need = ['update' => 2, 'create' => 1, 'delete' => 1, 'verify' => 1, 'option' => 0, 'symref-update' => 2];
            $n = $need[$cmd] ?? 0;
            for ($k = count($args); $k < $n + 1 - 1; $k++) {
                $v = $nextTok();
                if ($v === null) break;
                $args[] = $v;
            }
            $ops[] = [$cmd, $args];
        }
    }
    $failed = false;
    foreach ($ops as [$cmd, $args]) {
        switch ($cmd) {
            case 'update':
                $ref = $args[0] ?? '';
                $new = $args[1] ?? '';
                $old = $args[2] ?? null;
                $nullOid = str_repeat('0', $repo->hexsz);
                $newOid = ($new === '' || $new === $nullOid) ? $nullOid : rev_parse_object($repo, $new);
                if ($newOid === null) { fwrite(STDERR, "fatal: update $ref: invalid new value provided: '$new'\n"); return 128; }
                $oldOid = null;
                if ($old !== null && $old !== '') {
                    $oldOid = rev_parse_object($repo, $old);
                    if ($oldOid === null) { fwrite(STDERR, "fatal: update $ref: invalid <old-oid>: $old\n"); return 128; }
                } elseif ($old === '') $oldOid = str_repeat('0', $repo->hexsz);
                if ($newOid === $nullOid) {
                    if ($oldOid !== null && $oldOid !== $nullOid && $repo->refs->resolve($ref) === null) {
                        fwrite(STDERR, "fatal: cannot lock ref '$ref': unable to resolve reference '$ref'\n");
                        return 128;
                    }
                    if (!$repo->refs->deleteRef($ref, $oldOid, $reason)) $failed = true;
                } elseif (!$repo->refs->writeRef($ref, $newOid, null, $oldOid, $reason)) $failed = true;
                break;
            case 'create':
                $ref = $args[0] ?? ''; $new = $args[1] ?? '';
                $newOid = rev_parse_object($repo, $new);
                if ($newOid === null) { fwrite(STDERR, "fatal: create $ref: invalid new value provided: '$new'\n"); return 128; }
                if (!$repo->refs->writeRef($ref, $newOid, null, str_repeat('0', $repo->hexsz), $reason)) $failed = true;
                break;
            case 'delete':
                $ref = $args[0] ?? ''; $old = $args[1] ?? null;
                $oldOid = ($old !== null && $old !== '') ? rev_parse_object($repo, $old) : null;
                if (!$repo->refs->deleteRef($ref, $oldOid, $reason)) $failed = true;
                break;
            case 'verify':
                $ref = $args[0] ?? ''; $old = $args[1] ?? null;
                $cur = $repo->refs->resolve($ref);
                $want = ($old === null || $old === '') ? null : rev_parse_object($repo, $old);
                if ($want !== null && $cur !== $want) {
                    fwrite(STDERR, "fatal: cannot lock ref '$ref': is at " . ($cur ?? 'unborn') . " but expected $want\n");
                    return 128;
                }
                break;
            case 'symref-update':
                $ref = $args[0] ?? ''; $target = $args[1] ?? '';
                if (!$repo->refs->writeRef($ref, null, $target, null, $reason, false)) $failed = true;
                break;
            case 'option':
                break;
            case 'start': case 'prepare': case 'commit': case 'abort':
                break;
            default:
                fwrite(STDERR, "fatal: unknown command: $cmd\n");
                return 128;
        }
    }
    return $failed ? 1 : 0;
}

function cmd_symbolic_ref(array $argv): int {
    $repo = need_repo();
    $delete = false; $quiet = false; $short = false; $reason = ''; $args = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-d' || $a === '--delete': $delete = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--short': $short = true; break;
            case $a === '-m': $reason = $argv[++$i] ?? ''; break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git symbolic-ref [-m <reason>] <name> <ref>", 129);
            default: $args[] = $a;
        }
    }
    if (!$args) usage_die('usage: git symbolic-ref [-m <reason>] <name> <ref>', 129);
    $name = $args[0];
    if ($delete) {
        if ($name === 'HEAD') die_git("deleting 'HEAD' is not allowed");
        $raw = $repo->refs->readRaw($name);
        if ($raw === null || $raw[1] === null) {
            fwrite(STDERR, "error: Cannot delete $name, not a symbolic ref\n");
            return 1;
        }
        @unlink($repo->refs instanceof FilesRefStore ? $repo->refs->refPath($name) : $repo->path($name));
        return 0;
    }
    if (count($args) === 1) {
        $raw = $repo->refs->readRaw($name);
        if ($raw === null || $raw[1] === null) {
            if (!$quiet) fwrite(STDERR, "fatal: ref $name is not a symbolic ref\n");
            return 1;
        }
        $t = $raw[1];
        if ($short) $t = preg_replace('#^refs/(heads|tags|remotes)/#', '', $t);
        out("$t\n");
        return 0;
    }
    $target = $args[1];
    if (!check_ref_format($target, false) && strncmp($target, 'refs/', 5) !== 0) {
        die_git("Refusing to point $name outside of refs/");
    }
    $repo->refs->writeRef($name, null, $target, null, $reason, false);
    return 0;
}

function cmd_show_ref(array $argv): int {
    $repo = need_repo();
    $head = false; $tags = false; $heads = false; $dereference = false; $hashOnly = false;
    $verify = false; $quiet = false; $abbrev = null; $exists = false; $patterns = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--head': $head = true; break;
            case $a === '--tags': $tags = true; break;
            case $a === '--heads' || $a === '--branches': $heads = true; break;
            case $a === '-d' || $a === '--dereference': $dereference = true; break;
            case $a === '-s' || $a === '--hash': $hashOnly = true; break;
            case strncmp($a, '--hash=', 7) === 0: $hashOnly = true; $abbrev = (int)substr($a, 7); break;
            case $a === '--verify': $verify = true; break;
            case $a === '--exists': $exists = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '--abbrev': $abbrev = 7; break;
            case strncmp($a, '--abbrev=', 9) === 0: $abbrev = (int)substr($a, 9); break;
            case $a === '--': break;
            default: $patterns[] = $a;
        }
    }
    if ($exists) {
        $ref = $patterns[0] ?? '';
        $raw = $repo->refs->readRaw($ref);
        if ($raw === null) { if (!$quiet) fwrite(STDERR, "error: reference does not exist\n"); return 2; }
        return 0;
    }
    if ($verify) {
        $found = false;
        foreach ($patterns as $p) {
            $oid = $repo->refs->resolve($p);
            if ($oid === null) {
                if ($quiet) return 1;
                fwrite(STDERR, "fatal: '$p' - not a valid ref\n");
                return 128;
            }
            $found = true;
            if (!$quiet) {
                $o = $abbrev ? substr($oid, 0, $abbrev) : $oid;
                out($hashOnly ? "$o\n" : "$o $p\n");
            }
        }
        return $found ? 0 : 1;
    }
    $refs = $repo->refs->listRefs('refs/');
    $lines = [];
    if ($head) {
        $h = $repo->refs->resolve('HEAD');
        if ($h !== null) $lines[] = [$h, 'HEAD'];
    }
    foreach ($refs as $name => $oid) {
        if ($tags && strncmp($name, 'refs/tags/', 10) !== 0) continue;
        if ($heads && strncmp($name, 'refs/heads/', 11) !== 0) continue;
        if ($patterns) {
            $hit = false;
            foreach ($patterns as $p) {
                if ($name === $p || substr($name, -strlen("/$p")) === "/$p") { $hit = true; break; }
            }
            if (!$hit) continue;
        }
        $lines[] = [$oid, $name];
        if ($dereference) {
            $o = $repo->odb->read($oid);
            if ($o !== null && $o[0] === 'tag') {
                $peeled = peel_object($repo, $oid);
                if ($peeled !== null && $peeled !== $oid) $lines[] = [$peeled, $name . '^{}'];
            }
        }
    }
    if (!$lines) return 1;
    foreach ($lines as [$oid, $name]) {
        // git-show-ref(1) lists the references "along with the associated
        // commit IDs": a ref whose object is not in the object database is a
        // fatal error, not a line of output.
        if (substr($name, -3) !== '^{}' && !$repo->odb->has($oid)) {
            fwrite(STDERR, "fatal: git show-ref: bad ref $name ($oid)\n");
            return 128;
        }
        $o = $abbrev ? substr($oid, 0, $abbrev) : $oid;
        if ($quiet) continue;
        out($hashOnly ? "$o\n" : "$o $name\n");
    }
    return 0;
}

function cmd_check_ref_format(array $argv): int {
    $allowOnelevel = false; $refspecPattern = false; $normalize = false; $branch = false;
    $args = [];
    foreach ($argv as $a) {
        if ($a === '--allow-onelevel') { $allowOnelevel = true; continue; }
        if ($a === '--no-allow-onelevel') { $allowOnelevel = false; continue; }
        if ($a === '--refspec-pattern') { $refspecPattern = true; continue; }
        if ($a === '--normalize' || $a === '--print') { $normalize = true; continue; }
        if ($a === '--branch') { $branch = true; continue; }
        $args[] = $a;
    }
    if (!$args) usage_die('usage: git check-ref-format [--normalize] [<options>] <refname>', 129);
    $ref = $args[0];
    if ($branch) {
        if (!check_ref_format("refs/heads/$ref", false)) { fwrite(STDERR, "fatal: '$ref' is not a valid branch name\n"); return 1; }
        out("$ref\n");
        return 0;
    }
    $norm = $ref;
    if ($normalize) {
        $norm = preg_replace('#/+#', '/', $ref);
        $norm = ltrim($norm, '/');
    }
    if (!check_ref_format($norm, $allowOnelevel, $refspecPattern)) return 1;
    if ($normalize) out("$norm\n");
    return 0;
}

function format_ref_atom(Repo $repo, string $atom, string $refname, string $oid): string {
    // header-only first: a corrupt object body must not stop ref listing
    $hdr = $repo->odb->readHeader($oid);
    $type = $hdr === null ? '' : $hdr[0];
    $needBody = !in_array($atom, ['refname', 'refname:short', 'objectname', 'objectname:short',
                                  'objecttype', 'objectsize', 'upstream', 'HEAD'], true);
    $o = $needBody ? $repo->odb->read($oid) : null;
    if ($o !== null) $type = $o[0];
    if ($atom === 'objectsize' && $hdr !== null) return (string)$hdr[1];
    switch (true) {
        case $atom === 'refname': return $refname;
        case $atom === 'refname:short': return preg_replace('#^refs/(heads|tags|remotes)/#', '', $refname);
        case $atom === 'objectname': return $oid;
        case $atom === 'objectname:short': return substr($oid, 0, 7);
        case $atom === 'objecttype': return $type;
        case $atom === 'objectsize': return $o === null ? '0' : (string)strlen($o[1]);
        case $atom === 'upstream': return '';
        case $atom === 'HEAD':
            $h = $repo->refs->resolveRefName('HEAD');
            return $h === $refname ? '*' : ' ';
        case strncmp($atom, 'subject', 7) === 0:
            if ($type === 'commit') return CommitObj::parse($o[1])->subject();
            if ($type === 'tag') { $t = TagObj::parse($o[1]); return trim(explode("\n", $t->message)[0]); }
            return '';
        case strncmp($atom, 'contents', 8) === 0:
            if ($type === 'commit') return CommitObj::parse($o[1])->message;
            if ($type === 'tag') return TagObj::parse($o[1])->message;
            return '';
        case strncmp($atom, 'authorname', 10) === 0:
            return ($type === 'commit' && CommitObj::parse($o[1])->author) ? CommitObj::parse($o[1])->author->name : '';
        case strncmp($atom, 'authoremail', 11) === 0:
            return ($type === 'commit' && CommitObj::parse($o[1])->author) ? '<' . CommitObj::parse($o[1])->author->email . '>' : '';
        case strncmp($atom, 'committername', 13) === 0:
            return ($type === 'commit' && CommitObj::parse($o[1])->committer) ? CommitObj::parse($o[1])->committer->name : '';
        case strncmp($atom, 'objecttype', 10) === 0: return $type;
        case $atom === '*objectname':
            $p = peel_object($repo, $oid); return $p ?? '';
        case $atom === '*objecttype':
            $p = peel_object($repo, $oid);
            $po = $p === null ? null : $repo->odb->read($p);
            return $po === null ? '' : $po[0];
    }
    return '';
}

function cmd_for_each_ref(array $argv): int {
    $repo = need_repo();
    $format = "%(objectname) %(objecttype)\t%(refname)";
    $count = null; $sort = []; $patterns = []; $pointsAt = null; $merged = null; $contains = null;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case strncmp($a, '--format=', 9) === 0: $format = substr($a, 9); break;
            case $a === '--format': $format = $argv[++$i] ?? ''; break;
            case strncmp($a, '--count=', 8) === 0: $count = (int)substr($a, 8); break;
            case $a === '--count': $count = (int)($argv[++$i] ?? '0'); break;
            case strncmp($a, '--sort=', 7) === 0: $sort[] = substr($a, 7); break;
            case strncmp($a, '--points-at=', 12) === 0: $pointsAt = substr($a, 12); break;
            case $a === '--points-at': $pointsAt = $argv[++$i] ?? ''; break;
            case $a === '--shell' || $a === '--perl' || $a === '--python' || $a === '--tcl': break;
            case $a === '--': break;
            default: $patterns[] = $a;
        }
    }
    $refs = $repo->refs->listRefs('refs/');
    $rows = [];
    foreach ($refs as $name => $oid) {
        if ($patterns) {
            $hit = false;
            foreach ($patterns as $p) {
                $pp = rtrim($p, '/');
                if ($name === $pp || strncmp($name, $pp . '/', strlen($pp) + 1) === 0) { $hit = true; break; }
                if (strpbrk($p, '*?[') !== false && glob_match($p, $name, false)) { $hit = true; break; }
            }
            if (!$hit) continue;
        }
        if ($pointsAt !== null) {
            $want = rev_parse_object($repo, $pointsAt);
            if ($want === null) continue;
            $peeled = peel_object($repo, $oid);
            if ($oid !== $want && $peeled !== $want) continue;
        }
        $rows[] = [$name, $oid];
    }
    usort($rows, function ($a, $b) { return strcmp($a[0], $b[0]); });
    foreach ($sort as $s) {
        $desc = false;
        if ($s !== '' && $s[0] === '-') { $desc = true; $s = substr($s, 1); }
        usort($rows, function ($x, $y) use ($s, $repo, $desc) {
            $a = format_ref_atom($repo, $s, $x[0], $x[1]);
            $b = format_ref_atom($repo, $s, $y[0], $y[1]);
            $c = strcmp($a, $b);
            return $desc ? -$c : $c;
        });
    }
    if ($count !== null) $rows = array_slice($rows, 0, $count);
    foreach ($rows as [$name, $oid]) {
        out(expand_ref_format($repo, $format, $name, $oid) . "\n");
    }
    return 0;
}

function expand_ref_format(Repo $repo, string $format, string $name, string $oid): string {
    $format = str_replace(['%00', '%0a', '%09'], ["\0", "\n", "\t"], $format);
    return preg_replace_callback('/%\(([^)]*)\)/', function ($m) use ($repo, $name, $oid) {
        return format_ref_atom($repo, $m[1], $name, $oid);
    }, $format);
}

function cmd_reflog(array $argv): int {
    $repo = need_repo();
    $sub = $argv[0] ?? 'show';
    if (in_array($sub, ['show', 'expire', 'delete', 'exists'], true)) array_shift($argv);
    else $sub = 'show';
    if ($sub === 'exists') {
        $ref = $argv[0] ?? '';
        return $repo->refs->reflogExists($ref) ? 0 : 1;
    }
    if ($sub === 'expire' || $sub === 'delete') return 0;
    $ref = 'HEAD';
    foreach ($argv as $a) { if ($a !== '' && $a[0] !== '-') { $ref = $a; break; } }
    $full = $ref;
    $r = resolve_ref_dwim($repo, $ref);
    if ($r !== null) $full = $r[0];
    $log = $repo->refs->reflogRead($full);
    $short = preg_replace('#^refs/(heads|remotes|tags)/#', '', $full);
    if ($full === 'HEAD') $short = 'HEAD';
    $n = count($log);
    for ($i = $n - 1; $i >= 0; $i--) {
        $e = $log[$i];
        $msg = $e['msg'];
        out(substr($e['new'], 0, 7) . " $short@{" . ($n - 1 - $i) . '}: ' . $msg . "\n");
    }
    return 0;
}

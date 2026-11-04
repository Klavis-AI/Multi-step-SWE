<?php
// git-rev-parse(1), git-rev-list(1), git-merge-base(1)

function cmd_rev_parse(array $argv): int {
    global $GIT_OPTS;
    $verify = false; $quiet = false; $short = null; $symbolicFull = false; $abbrevRef = false;
    $revsOnly = false; $noRevs = false; $flagsOnly = false; $notMode = false; $sq = false;
    $default = null; $out = [];
    $repo = null;
    $needRepo = function () use (&$repo) { if ($repo === null) $repo = need_repo(); return $repo; };
    $args = $argv;
    $endOfOptions = false;
    for ($i = 0; $i < count($args); $i++) {
        $a = $args[$i];
        if (!$endOfOptions && $a !== '' && $a[0] === '-') {
            switch (true) {
                case $a === '--end-of-options': $endOfOptions = true; continue 2;
                case $a === '--verify': $verify = true; continue 2;
                case $a === '-q' || $a === '--quiet': $quiet = true; continue 2;
                case $a === '--short': $short = 7; continue 2;
                case strncmp($a, '--short=', 8) === 0: $short = (int)substr($a, 8); continue 2;
                case $a === '--symbolic-full-name': $symbolicFull = true; continue 2;
                case $a === '--symbolic': $symbolicFull = true; continue 2;
                case $a === '--abbrev-ref': case strncmp($a, '--abbrev-ref=', 13) === 0: $abbrevRef = true; continue 2;
                case $a === '--revs-only': $revsOnly = true; continue 2;
                case $a === '--no-revs': $noRevs = true; continue 2;
                case $a === '--flags': $flagsOnly = true; continue 2;
                case $a === '--no-flags': continue 2;
                case $a === '--sq': $sq = true; continue 2;
                case strncmp($a, '--default', 9) === 0:
                    $default = strpos($a, '=') !== false ? substr($a, 10) : ($args[++$i] ?? null);
                    continue 2;
                case $a === '--git-dir':
                    $r = $needRepo();
                    $g = $r->gitdir;
                    $cwd = path_real(getcwd());
                    if (strncmp($g, $cwd . '/', strlen($cwd) + 1) === 0) $g = substr($g, strlen($cwd) + 1);
                    out($g . "\n"); continue 2;
                case $a === '--absolute-git-dir':
                    out($needRepo()->gitdir . "\n"); continue 2;
                case $a === '--git-common-dir':
                    out($needRepo()->commondir . "\n"); continue 2;
                case $a === '--show-toplevel':
                    $r = $needRepo();
                    if ($r->worktree === null) { die_git('this operation must be run in a work tree'); }
                    out($r->worktree . "\n"); continue 2;
                case $a === '--show-prefix':
                    $r = $needRepo();
                    $p = repo_prefix($r);
                    out($p === '' ? "\n" : $p . "\n"); continue 2;
                case $a === '--show-cdup':
                    $r = $needRepo();
                    $p = repo_prefix($r);
                    $up = $p === '' ? '' : str_repeat('../', substr_count(rtrim($p, '/'), '/') + 1);
                    out($up . "\n"); continue 2;
                case $a === '--is-inside-work-tree':
                    $r = $needRepo();
                    out(($r->worktree !== null && strncmp(path_real(getcwd()) . '/', path_real($r->worktree) . '/', strlen(path_real($r->worktree)) + 1) === 0) ? "true\n" : "false\n");
                    continue 2;
                case $a === '--is-inside-git-dir':
                    $r = $needRepo();
                    $cwd = path_real(getcwd());
                    out((strncmp($cwd . '/', path_real($r->gitdir) . '/', strlen(path_real($r->gitdir)) + 1) === 0) ? "true\n" : "false\n");
                    continue 2;
                case $a === '--is-bare-repository':
                    out($needRepo()->isBare() ? "true\n" : "false\n"); continue 2;
                case $a === '--is-shallow-repository':
                    out(is_file($needRepo()->commonPath('shallow')) ? "true\n" : "false\n"); continue 2;
                case $a === '--show-object-format': case strncmp($a, '--show-object-format=', 21) === 0:
                    out($needRepo()->hashAlgo . "\n"); continue 2;
                case $a === '--show-ref-format':
                    out($needRepo()->refFormat . "\n"); continue 2;
                case $a === '--git-path':
                    $p = $args[++$i] ?? '';
                    $r = $needRepo();
                    out(rtrim($r->path($p), '/') . "\n"); continue 2;
                case $a === '--resolve-git-dir':
                    $p = $args[++$i] ?? '';
                    if (is_file($p)) { $g = Repo::resolveGitFile($p); if ($g !== null && Repo::isGitDirectory($g)) { out("$g\n"); continue 2; } }
                    if (Repo::isGitDirectory($p)) { out(path_absolute($p) . "\n"); continue 2; }
                    fwrite(STDERR, "fatal: not a gitdir '$p'\n"); return 128;
                case $a === '--all':
                    $r = $needRepo();
                    foreach ($r->refs->listRefs('refs/') as $n => $o) $out[] = $o;
                    continue 2;
                case $a === '--branches' || $a === '--heads':
                    $r = $needRepo();
                    foreach ($r->refs->listRefs('refs/heads/') as $n => $o) $out[] = $o;
                    continue 2;
                case $a === '--tags':
                    $r = $needRepo();
                    foreach ($r->refs->listRefs('refs/tags/') as $n => $o) $out[] = $o;
                    continue 2;
                case $a === '--remotes':
                    $r = $needRepo();
                    foreach ($r->refs->listRefs('refs/remotes/') as $n => $o) $out[] = $o;
                    continue 2;
                case $a === '--not': $notMode = !$notMode; continue 2;
                case $a === '--parseopt': return rev_parse_parseopt(array_slice($args, $i + 1));
                case $a === '--':
                    if (!$noRevs) for ($j = $i + 1; $j < count($args); $j++) $out[] = $args[$j];
                    return rev_parse_emit($out, $sq);
                default:
                    if ($flagsOnly || !$revsOnly) $out[] = $a;
                    continue 2;
            }
        }
        if ($noRevs || $flagsOnly) { continue; }
        $r = $needRepo();
        $name = $a;
        $neg = false;
        if ($name !== '' && $name[0] === '^') { $neg = true; $name = substr($name, 1); }
        if (strpos($name, '..') !== false) {
            $parts = explode('..', $name, 2);
            $threeDot = strncmp(substr($name, strlen($parts[0])), '...', 3) === 0;
            if ($threeDot) $parts = explode('...', $name, 2);
            $lhs = $parts[0] === '' ? 'HEAD' : $parts[0];
            $rhs = $parts[1] === '' ? 'HEAD' : $parts[1];
            $lo = rev_parse_object($r, $lhs); $ro = rev_parse_object($r, $rhs);
            if ($lo === null || $ro === null) {
                if ($verify) { if (!$quiet) fwrite(STDERR, "fatal: Needed a single revision\n"); return 128; }
                $out[] = $a; continue;
            }
            if ($threeDot) { $out[] = $lo; $out[] = $ro; $out[] = '^' . (merge_bases($r, $lo, $ro)[0] ?? ''); }
            else { $out[] = $ro; $out[] = '^' . $lo; }
            continue;
        }
        $res = rev_parse_full($r, $name);
        if ($res === null) {
            if (!$verify && !$revsOnly && !file_exists($name)) {
                fwrite(STDERR, "fatal: ambiguous argument '$name': unknown revision or path not in the working tree.\nUse '--' to separate paths from revisions, like this:\n'git <command> [<revision>...] -- [<file>...]'\n");
                rev_parse_emit($out, $sq);
                out($a . "\n");
                return 128;
            }
            if ($verify) {
                if (!$quiet) fwrite(STDERR, "fatal: Needed a single revision\n");
                return 128;
            }
            if ($revsOnly) continue;
            if (file_exists($name) || $name === '') { $out[] = $name; continue; }
            fwrite(STDERR, "fatal: ambiguous argument '$name': unknown revision or path not in the working tree.\nUse '--' to separate paths from revisions, like this:\n'git <command> [<revision>...] -- [<file>...]'\n");
            return 128;
        }
        $val = $res[0];
        if ($symbolicFull || $abbrevRef) {
            $refname = $res[1];
            if ($refname === null) {
                $val = $name === 'HEAD' ? 'HEAD' : $val;
                if ($symbolicFull && $refname === null && !$abbrevRef) { $out[] = ''; continue; }
            } else {
                $val = $abbrevRef ? preg_replace('#^refs/(heads|tags|remotes)/#', '', $refname) : $refname;
            }
            if ($abbrevRef && $name === 'HEAD') {
                $full = $repo->refs->readRaw('HEAD');
                if ($full !== null && $full[1] !== null) $val = preg_replace('#^refs/heads/#', '', $full[1]);
            }
        } elseif ($short !== null) {
            $val = substr($val, 0, max(4, $short));
        }
        $out[] = ($neg ? '^' : '') . $val;
        if ($verify && count($out) > 1) { fwrite(STDERR, "fatal: Needed a single revision\n"); return 128; }
    }
    if (!$out && $default !== null) {
        $r = $needRepo();
        $o = rev_parse_object($r, $default);
        if ($o === null) { if ($verify) { if (!$quiet) fwrite(STDERR, "fatal: Needed a single revision\n"); return 128; } }
        else $out[] = $short !== null ? substr($o, 0, $short) : $o;
    }
    if ($verify && count($out) !== 1) {
        if (!$quiet) fwrite(STDERR, "fatal: Needed a single revision\n");
        return 128;
    }
    return rev_parse_emit($out, $sq);
}

function rev_parse_emit(array $out, bool $sq): int {
    if ($sq) { out(implode(' ', array_map(function ($v) { return "'" . str_replace("'", "'\\''", $v) . "'"; }, $out)) . "\n"); return 0; }
    foreach ($out as $o) out($o . "\n");
    return 0;
}

function rev_parse_parseopt(array $args): int {
    $spec = stream_get_contents(STDIN);
    out("set -- --\n");
    return 0;
}

function cmd_rev_list(array $argv): int {
    $repo = need_repo();
    $walk = new RevWalk($repo);
    $objects = false; $count = false; $quiet = false; $noWalk = false; $parents = false;
    $header = false; $format = null; $pretty = null; $all = false; $stdin = false;
    $showMissing = null; $filter = null; $left = false; $boundary = false;
    $printOmitted = false;
    $paths = []; $negate = false; $indexed = false;
    $revs = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--objects': $objects = true; break;
            case $a === '--objects-edge' || $a === '--objects-edge-aggressive': $objects = true; break;
            case $a === '--indexed-objects': $indexed = true; break;
            case $a === '--count': $count = true; break;
            case $a === '--use-bitmap-index': break;  // walk gives the same answer
            case $a === '--test-bitmap': return revlist_test_bitmap($repo);
            case $a === '--quiet': $quiet = true; break;
            case $a === '--no-walk' || strncmp($a, '--no-walk=', 10) === 0: $noWalk = true; break;
            case $a === '--do-walk': $noWalk = false; break;
            case $a === '--parents': $parents = true; break;
            case $a === '--header': $header = true; break;
            case $a === '--all': $all = true; break;
            case $a === '--branches': foreach ($repo->refs->listRefs('refs/heads/') as $o) $walk->push($o); break;
            case $a === '--tags': foreach ($repo->refs->listRefs('refs/tags/') as $o) { $p = peel_to_commit($repo, $o); if ($p) $walk->push($p); } break;
            case $a === '--remotes': foreach ($repo->refs->listRefs('refs/remotes/') as $o) $walk->push($o); break;
            case $a === '--stdin': $stdin = true; break;
            case $a === '--reverse': $walk->reverse = true; break;
            case $a === '--topo-order': $walk->topoOrder = true; break;
            case $a === '--date-order': $walk->dateOrder = true; break;
            case $a === '--first-parent': $walk->firstParent = true; break;
            case $a === '--no-merges': $walk->merges = false; break;
            case $a === '--merges': $walk->merges = true; break;
            case $a === '--left-right': $left = true; break;
            case $a === '--boundary': $boundary = true; break;
            case $a === '-n': $walk->maxCount = (int)($argv[++$i] ?? '0'); break;
            case strncmp($a, '--max-count=', 12) === 0: $walk->maxCount = (int)substr($a, 12); break;
            case preg_match('/^-(\d+)$/', $a, $m) === 1: $walk->maxCount = (int)$m[1]; break;
            case strncmp($a, '--skip=', 7) === 0: $walk->skip = (int)substr($a, 7); break;
            case strncmp($a, '--since=', 8) === 0: $d = parse_date(substr($a, 8)); if ($d) $walk->since = $d[0]; break;
            case strncmp($a, '--until=', 8) === 0: $d = parse_date(substr($a, 8)); if ($d) $walk->until = $d[0]; break;
            case strncmp($a, '--grep=', 7) === 0: $walk->grep[] = substr($a, 7); break;
            case strncmp($a, '--author=', 9) === 0: $walk->author = substr($a, 9); break;
            case strncmp($a, '--committer=', 12) === 0: $walk->committerPat = substr($a, 12); break;
            case strncmp($a, '--pretty', 8) === 0: $pretty = strpos($a, '=') !== false ? substr($a, strpos($a, '=') + 1) : 'medium'; break;
            case strncmp($a, '--format=', 9) === 0: $pretty = substr($a, 9); break;
            case strncmp($a, '--missing=', 10) === 0: $showMissing = substr($a, 10); break;
            case strncmp($a, '--filter=', 9) === 0: $filter = substr($a, 9); break;
            case $a === '--no-filter': $filter = null; break;
            case $a === '--filter-print-omitted': $printOmitted = true; break;
            case $a === '--not': $negate = !$negate; break;
            case $a === '--': for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; $i = count($argv); break;
            case $a !== '' && $a[0] === '-': break;
            default: $revs[] = $a;
        }
    }
    $extraObjects = [];
    if ($all) {
        foreach ($repo->refs->listRefs('refs/') as $n => $o) {
            $hdr = $repo->odb->readHeader($o);
            if ($hdr !== null && $hdr[0] === 'tag') {
                $extraObjects[$o] = preg_replace('#^refs/tags/#', '', $n);
            }
            $c = peel_to_commit($repo, $o);
            if ($c !== null) $walk->push($c);
        }
        $h = $repo->refs->resolve('HEAD');
        if ($h !== null) $walk->push($h);
    }
    if ($stdin) {
        while (($l = fgets(STDIN)) !== false) { $l = trim($l); if ($l !== '') $revs[] = $l; }
    }
    foreach ($revs as $rv) {
        $neg = $negate;
        if ($rv !== '' && $rv[0] === '^') { $neg = !$neg; $rv = substr($rv, 1); }
        if (strpos($rv, '..') !== false) {
            $three = strpos($rv, '...') !== false;
            $parts = $three ? explode('...', $rv, 2) : explode('..', $rv, 2);
            $lo = rev_parse_object($repo, $parts[0] === '' ? 'HEAD' : $parts[0]);
            $ro = rev_parse_object($repo, $parts[1] === '' ? 'HEAD' : $parts[1]);
            if ($lo === null || $ro === null) die_git("bad revision '$rv'");
            $lo = peel_to_commit($repo, $lo); $ro = peel_to_commit($repo, $ro);
            if ($three) {
                $base = merge_bases($repo, $lo, $ro);
                $walk->push($lo); $walk->push($ro);
                foreach ($base as $b) $walk->hide($b);
            } else { $walk->hide($lo); $walk->push($ro); }
            continue;
        }
        $o = rev_parse_object($repo, $rv);
        if ($o === null) die_git("bad revision '$rv'");
        $c = peel_to_commit($repo, $o);
        if ($c === null) {
            if ($objects) { out($o . "\n"); continue; }
            die_git("bad revision '$rv'");
        }
        if ($neg) $walk->hide($c); else $walk->push($c);
    }
    if (!$walk->hasStarts()) {
        if ($count) { out("0\n"); return 0; }
        usage_die('usage: git rev-list [<options>] <commit>... [--] [<path>...]', 128);
    }
    if ($noWalk) {
        $list = [];
        foreach ($revs as $rv) {
            $o = rev_parse_object($repo, $rv);
            if ($o !== null) { $c = peel_to_commit($repo, $o); if ($c) $list[$c] = true; }
        }
        if ($count) { out(count($list) . "\n"); return 0; }
        foreach (array_keys($list) as $o) out("$o\n");
        return 0;
    }
    $commits = $walk->commits();
    if ($count && !$objects) { out(count($commits) . "\n"); return 0; }
    if ($quiet && !$objects) return 0;
    if ($objects) {
        // A repository with a promisor remote is allowed to be missing
        // objects; --missing=<action> says what to do about them elsewhere
        // (git-rev-list(1)).
        $promisor = function_exists('promisor_remotes') && promisor_remotes($repo) !== [];
        $require = ($showMissing === 'error') || ($showMissing === null && !$promisor);
        $objs = $extraObjects;
        foreach ($commits as $oid => $c) {
            $objs[$oid] = '';
            collect_tree_objects($repo, $c->tree, '', $objs, $require);
        }
        // git-rev-list(1): allow-any and print both *omit* a missing object
        // from the listing; print additionally reports it, prefixed with '?'.
        $missing = [];
        if ($showMissing !== 'error' && ($showMissing !== null || $promisor)) {
            foreach ($objs as $oid => $path) {
                if ($repo->odb->has($oid)) continue;
                $missing[$oid] = $path;
                unset($objs[$oid]);
            }
        }
        // an object filter drops objects from the listing; with
        // --filter-print-omitted the ones it dropped are printed afterwards,
        // each prefixed with '~' (git-rev-list(1))
        $omitted = [];
        if ($filter !== null) {
            foreach ($objs as $oid => $path) {
                if (isset($commits[$oid])) continue;
                if (filter_excludes($repo, $filter, $oid)) {
                    $omitted[$oid] = $path;
                    unset($objs[$oid]);
                }
            }
        }
        if ($count) { out(count($objs) . "\n"); return 0; }
        foreach ($objs as $oid => $path) {
            out($oid . (isset($commits[$oid]) ? '' : ' ' . $path) . "\n");
        }
        if ($showMissing === 'print') foreach ($missing as $oid => $path) out('?' . $oid . "\n");
        if ($printOmitted) foreach ($omitted as $oid => $path) out('~' . $oid . "\n");
        return 0;
    }
    foreach ($commits as $oid => $c) {
        $line = $oid;
        if ($parents) foreach ($c->parents as $p) $line .= ' ' . $p;
        out($line . "\n");
        if ($pretty !== null) {
            out(format_commit($repo, $c, $pretty, false));
        }
        if ($header) out($repo->odb->read($oid)[1] . "\0");
    }
    return 0;
}

function cmd_merge_base(array $argv): int {
    $repo = need_repo();
    $all = false; $isAncestor = false; $independent = false; $octopus = false; $forkPoint = false;
    $args = [];
    foreach ($argv as $a) {
        if ($a === '-a' || $a === '--all') { $all = true; continue; }
        if ($a === '--is-ancestor') { $isAncestor = true; continue; }
        if ($a === '--independent') { $independent = true; continue; }
        if ($a === '--octopus') { $octopus = true; continue; }
        if ($a === '--fork-point') { $forkPoint = true; continue; }
        if ($a === '--') continue;
        $args[] = $a;
    }
    $oids = [];
    foreach ($args as $a) {
        $o = rev_parse_object($repo, $a);
        if ($o === null) die_git("Not a valid object name $a");
        $c = peel_to_commit($repo, $o);
        if ($c === null) die_git("Not a valid commit name $a");
        $oids[] = $c;
    }
    if ($isAncestor) {
        if (count($oids) !== 2) usage_die('usage: git merge-base --is-ancestor <commit> <commit>', 129);
        return is_ancestor($repo, $oids[0], $oids[1]) ? 0 : 1;
    }
    if ($independent) {
        $out = [];
        foreach ($oids as $a) {
            $red = false;
            foreach ($oids as $b) if ($a !== $b && is_ancestor($repo, $a, $b)) { $red = true; break; }
            if (!$red) $out[$a] = true;
        }
        foreach (array_keys($out) as $o) out("$o\n");
        return 0;
    }
    if (count($oids) < 2) usage_die('usage: git merge-base [-a | --all] <commit> <commit>...', 129);
    $bases = merge_bases($repo, $oids[0], $oids[1]);
    for ($i = 2; $i < count($oids); $i++) {
        $next = [];
        foreach ($bases as $b) foreach (merge_bases($repo, $b, $oids[$i]) as $m) $next[$m] = true;
        $bases = array_keys($next);
    }
    if (!$bases) return 1;
    if ($all) { foreach ($bases as $b) out("$b\n"); return 0; }
    out($bases[0] . "\n");
    return 0;
}

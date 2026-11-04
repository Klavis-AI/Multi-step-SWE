<?php
// Working-tree scanning, stat comparison, index<->worktree and index<->tree
// differences, and checkout of entries.

function lstat_info(string $path) {
    $st = @lstat($path);
    if ($st === false) return null;
    return $st;
}

function wt_mode_for(array $st, bool $filemode): int {
    $m = $st['mode'];
    if (($m & 0170000) === 0120000) return 0120000;
    if (($m & 0170000) === 0040000) return 0040000;
    if ($filemode) return ($m & 0111) ? 0100755 : 0100644;
    return 0100644;
}

function wt_read_blob(string $path, array $st): string {
    if (($st['mode'] & 0170000) === 0120000) {
        $t = @readlink($path);
        return $t === false ? '' : $t;
    }
    $c = @file_get_contents($path);
    return $c === false ? '' : $c;
}

/** Does the index entry's cached stat still describe the file? */
function entry_stat_matches(IndexEntry $e, array $st, bool $filemode): bool {
    if ($e->mtime_s !== ($st['mtime'] & 0xffffffff)) return false;
    if ($e->size !== ($st['size'] & 0xffffffff)) return false;
    if ($e->ctime_s !== ($st['ctime'] & 0xffffffff)) return false;
    if ($e->ino !== 0 && $e->ino !== ($st['ino'] & 0xffffffff)) return false;
    $mode = wt_mode_for($st, $filemode);
    if ($filemode) {
        if ($e->mode !== $mode) return false;
    } else {
        if (($e->mode & 0170000) !== ($mode & 0170000)) return false;
    }
    return true;
}

class WorktreeScan {
    /** directories that contain no tracked file, reported as "dir/" */
    public array $untrackedDirs = [];
    public array $files = [];      // path => stat
    public array $dirs = [];       // path => true
    public array $ignored = [];    // path => true
    public array $ignoredDirs = [];
    /** nested repositories: path => absolute path, recorded as gitlinks */
    public array $gitlinks = [];
}

/** Walk the work tree, honouring .gitignore files at each level. */
function wt_scan(Repo $repo, ?Pathspec $ps = null, bool $collectIgnored = false): WorktreeScan {
    $scan = new WorktreeScan();
    $root = $repo->worktree;
    if ($root === null) return $scan;
    $ex = build_exclude_stack($repo);
    $walk = function (string $dir, string $rel) use (&$walk, $repo, $root, $ex, $scan, $ps, $collectIgnored) {
        $mark = $ex->snapshot();
        $gi = $dir . '/.gitignore';
        if (is_file($gi)) $ex->addFile($gi, $rel);
        $entries = @scandir($dir);
        if ($entries === false) { $ex->restore($mark); return; }
        sort($entries, SORT_STRING);
        foreach ($entries as $e) {
            if ($e === '.' || $e === '..') continue;
            $p = $dir . '/' . $e;
            $r = $rel . $e;
            $st = lstat_info($p);
            if ($st === null) continue;
            $isDir = ($st['mode'] & 0170000) === 0040000;
            if ($isDir && $rel === '' && $e === '.git') continue;
            if ($isDir && $e === '.git') continue;
            $ign = $ex->check($r, $isDir);
            if ($isDir && ($rel !== '' || $e !== '.git') && wt_is_nested_repo($p)) {
                // a directory with its own .git is a gitlink, not a subtree
                if ($ign !== true) $scan->gitlinks[$r] = $p;
                continue;
            }
            if ($isDir) {
                if ($ign === true) {
                    if ($collectIgnored) {
                        $scan->ignoredDirs[$r . '/'] = true;
                        collect_ignored_files($p, $r . '/', $scan);
                    }
                    continue;
                }
                $scan->dirs[$r . '/'] = true;
                $walk($p, $r . '/');
                continue;
            }
            if ($ign === true) {
                if ($collectIgnored) $scan->ignored[$r] = true;
                continue;
            }
            $scan->files[$r] = $st;
        }
        $ex->restore($mark);
    };
    $walk($root, '');
    return $scan;
}

/** A directory that holds its own .git is a repository, not a subtree. */
function wt_is_nested_repo(string $path): bool {
    return is_dir($path . '/.git') || is_file($path . '/.git');
}

/** Every file below an ignored directory is itself ignored. */
function collect_ignored_files(string $dir, string $rel, WorktreeScan $scan) {
    $entries = @scandir($dir);
    if ($entries === false) return;
    sort($entries, SORT_STRING);
    foreach ($entries as $e) {
        if ($e === '.' || $e === '..') continue;
        $p = $dir . '/' . $e;
        $r = $rel . $e;
        if (is_dir($p) && !is_link($p)) { collect_ignored_files($p, $r . '/', $scan); continue; }
        $scan->ignored[$r] = true;
    }
}

/** Flatten a tree object into path => [mode, oid]. */
function tree_flatten(Repo $repo, string $treeOid, string $prefix = '', array &$out = []): array {
    $o = $repo->odb->read($treeOid);
    if ($o === null || $o[0] !== 'tree') return $out;
    $t = TreeObj::parse($o[1]);
    foreach ($t->entries as $e) {
        $path = $prefix . $e->name;
        if ($e->mode === '40000') { tree_flatten($repo, $e->oid, $path . '/', $out); continue; }
        $out[$path] = [$e->mode, $e->oid];
    }
    return $out;
}

/** Build tree objects from the index; returns the root tree oid. */
function index_write_tree(Repo $repo, Index $idx, bool $missingOk = false): string {
    $byDir = [];
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0) die_git('git write-tree: error building trees');
        if ($e->intentToAdd()) continue;
        $dir = dirname($e->path);
        if ($dir === '.') $dir = '';
        $byDir[$dir][] = $e;
    }
    $build = function (string $dir) use (&$build, &$byDir, $repo, $idx): string {
        $t = new TreeObj();
        $children = [];
        foreach ($byDir as $d => $_) {
            if ($d === $dir) continue;
            $parent = $dir === '' ? '' : $dir . '/';
            if ($dir !== '' && strncmp($d, $parent, strlen($parent)) !== 0) continue;
            $rest = $dir === '' ? $d : substr($d, strlen($parent));
            if ($rest === '') continue;
            $first = explode('/', $rest)[0];
            $children[$first] = true;
        }
        // subdirectories that only exist through deeper entries
        foreach ($idx->sorted() as $e) {
            $p = $e->path;
            if ($dir !== '' && strncmp($p, $dir . '/', strlen($dir) + 1) !== 0) continue;
            $rest = $dir === '' ? $p : substr($p, strlen($dir) + 1);
            if (strpos($rest, '/') !== false) $children[explode('/', $rest)[0]] = true;
        }
        foreach (($byDir[$dir] ?? []) as $e) {
            $te = new TreeEntry();
            $te->mode = ltrim(sprintf('%o', $e->mode), '0');
            $te->name = basename($e->path);
            $te->oid = $e->oid;
            $t->entries[] = $te;
        }
        foreach (array_keys($children) as $c) {
            $sub = $dir === '' ? $c : $dir . '/' . $c;
            $oid = $build($sub);
            if ($oid === '') continue;
            $te = new TreeEntry();
            $te->mode = '40000';
            $te->name = $c;
            $te->oid = $oid;
            $t->entries[] = $te;
        }
        if (!$t->entries) return '';
        return $repo->odb->write('tree', $t->format());
    };
    $root = $build('');
    if ($root === '') $root = $repo->odb->write('tree', '');
    return $root;
}

/** Write one index entry out to the work tree. */
function wt_checkout_entry(Repo $repo, IndexEntry $e, bool $force = true): bool {
    $path = $repo->worktree . '/' . $e->path;
    $o = $repo->odb->read($e->oid);
    if ($o === null) { err_git("unable to read object {$e->oid}"); return false; }
    mkdir_p(dirname($path));
    if ($e->mode === 0120000) {
        @unlink($path);
        if (!@symlink($o[1], $path)) return false;
    } elseif ($e->mode === 0160000) {
        mkdir_p($path);
    } else {
        if (is_link($path)) @unlink($path);
        if (@file_put_contents($path, $o[1]) === false) return false;
        @chmod($path, ($e->mode & 0111) ? (0777 & ~umask_value()) : (0666 & ~umask_value()));
    }
    $st = lstat_info($path);
    if ($st !== null) {
        $e->fillStat($st);
        $e->mode = wt_mode_for($st, $repo->config->getBool('core.filemode', true));
        if ($o[0] === 'blob' && ($st['mode'] & 0170000) === 0120000) $e->mode = 0120000;
    }
    return true;
}

function wt_remove_path(Repo $repo, string $rel) {
    $p = $repo->worktree . '/' . $rel;
    @unlink($p);
    $dir = dirname($p);
    while (strlen($dir) > strlen($repo->worktree)) {
        if (!@rmdir($dir)) break;
        $dir = dirname($dir);
    }
}

/** Stage a work-tree path into the index. */
function index_add_path(Repo $repo, Index $idx, string $rel, bool $intentToAdd = false): bool {
    $full = $repo->worktree . '/' . $rel;
    $st = lstat_info($full);
    if ($st === null) return false;
    $filemode = $repo->config->getBool('core.filemode', true);
    $mode = wt_mode_for($st, $filemode);
    if ($mode === 0040000) return false;
    $prev = $idx->find($rel, 0);
    if ($prev !== null && !$filemode) {
        // keep the recorded executable bit when the filesystem cannot store it
        if (($prev->mode & 0170000) === 0100000 && ($mode & 0170000) === 0100000) $mode = $prev->mode;
    }
    $content = wt_read_blob($full, $st);
    if ($mode !== 0120000) {
        $converted = convert_to_git($repo, $rel, $content);
        if ($converted !== $content) {
            // git warns only when the round trip would not restore the file
            if (convert_to_worktree($repo, $rel, $converted) !== $content) {
                fwrite(STDERR, "warning: in the working copy of '$rel', CRLF will be replaced by LF the next time Git touches it\n");
            }
            $content = $converted;
        }
    }
    $oid = $repo->odb->write('blob', $content);
    $e = new IndexEntry();
    $e->path = $rel;
    $e->mode = $mode;
    $e->oid = $oid;
    $e->fillStat($st);
    if ($intentToAdd) $e->flags2 |= 0x2000;
    $idx->remove($rel, null);
    $idx->put($e);
    return true;
}

/** Compare index entries against the work tree. */
function diff_index_worktree(Repo $repo, Index $idx, ?Pathspec $ps = null): array {
    $out = [];
    $filemode = $repo->config->getBool('core.filemode', true);
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0) continue;
        if ($ps !== null && !$ps->matches($e->path)) continue;
        if ($e->skipWorktree()) continue;
        $full = $repo->worktree . '/' . $e->path;
        $st = lstat_info($full);
        $d = new DiffEntry();
        $d->path = $e->path;
        $d->oldMode = sprintf('%06o', $e->mode);
        $d->oldOid = $e->oid;
        if ($st === null) {
            if ($e->intentToAdd()) continue;
            $d->status = 'D';
            $d->newMode = '000000';
            $d->newOid = str_repeat('0', $repo->hexsz);
            $out[] = $d;
            continue;
        }
        if ($e->mode === 0160000) {
            // a gitlink is compared by the nested repository's HEAD, never by
            // the contents of its directory
            $head = function_exists('nested_repo_head') ? nested_repo_head($full) : null;
            if ($head === null || $head === $e->oid) continue;
            $d->status = 'M';
            $d->newMode = '160000';
            $d->newOid = $head;
            $out[] = $d;
            continue;
        }
        $mode = wt_mode_for($st, $filemode);
        if (!$filemode && ($mode & 0170000) === 0100000 && ($e->mode & 0170000) === 0100000) $mode = $e->mode;
        if ($e->assumeValid()) continue;
        if (entry_stat_matches($e, $st, $filemode) && !$e->intentToAdd()) continue;
        $content = wt_read_blob($full, $st);
        $oid = $repo->hashData('blob', $content);
        if ($oid === $e->oid && $mode === $e->mode && !$e->intentToAdd()) continue;
        $d->status = $e->intentToAdd() ? 'A' : 'M';
        if ($e->intentToAdd()) { $d->oldMode = '000000'; $d->oldOid = str_repeat('0', $repo->hexsz); }
        $d->newMode = sprintf('%06o', $mode);
        $d->newOid = $oid;
        $out[] = $d;
    }
    return $out;
}

/** Compare a tree against the index. */
function diff_tree_index(Repo $repo, ?string $treeOid, Index $idx, ?Pathspec $ps = null): array {
    $tree = [];
    if ($treeOid !== null) tree_flatten($repo, $treeOid, '', $tree);
    $out = [];
    $seen = [];
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0) continue;
        $p = $e->path;
        $seen[$p] = true;
        if ($ps !== null && !$ps->matches($p)) continue;
        $mode = sprintf('%06o', $e->mode);
        if (!isset($tree[$p])) {
            if ($e->intentToAdd()) continue;
            $d = new DiffEntry();
            $d->status = 'A'; $d->path = $p;
            $d->oldMode = '000000'; $d->oldOid = str_repeat('0', $repo->hexsz);
            $d->newMode = $mode; $d->newOid = $e->oid;
            $out[] = $d;
            continue;
        }
        [$tm, $to] = $tree[$p];
        $tmode = sprintf('%06o', octdec($tm));
        if ($to === $e->oid && $tmode === $mode) continue;
        $d = new DiffEntry();
        $d->status = ($tmode !== $mode && $to === $e->oid) ? 'T' : 'M';
        if (substr($tmode, 0, 3) !== substr($mode, 0, 3)) $d->status = 'T';
        $d->path = $p;
        $d->oldMode = $tmode; $d->oldOid = $to;
        $d->newMode = $mode; $d->newOid = $e->oid;
        $out[] = $d;
    }
    foreach ($tree as $p => $v) {
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

function diff_tree_tree(Repo $repo, ?string $a, ?string $b, ?Pathspec $ps = null): array {
    $ta = []; $tb = [];
    if ($a !== null) tree_flatten($repo, $a, '', $ta);
    if ($b !== null) tree_flatten($repo, $b, '', $tb);
    $out = [];
    foreach ($tb as $p => $v) {
        if ($ps !== null && !$ps->matches($p)) continue;
        if (!isset($ta[$p])) {
            $d = new DiffEntry();
            $d->status = 'A'; $d->path = $p;
            $d->oldMode = '000000'; $d->oldOid = str_repeat('0', $repo->hexsz);
            $d->newMode = sprintf('%06o', octdec($v[0])); $d->newOid = $v[1];
            $out[] = $d;
            continue;
        }
        if ($ta[$p][1] === $v[1] && $ta[$p][0] === $v[0]) continue;
        $d = new DiffEntry();
        $d->path = $p;
        $d->oldMode = sprintf('%06o', octdec($ta[$p][0])); $d->oldOid = $ta[$p][1];
        $d->newMode = sprintf('%06o', octdec($v[0])); $d->newOid = $v[1];
        $d->status = (substr($d->oldMode, 0, 3) !== substr($d->newMode, 0, 3)) ? 'T' : 'M';
        $out[] = $d;
    }
    foreach ($ta as $p => $v) {
        if (isset($tb[$p])) continue;
        if ($ps !== null && !$ps->matches($p)) continue;
        $d = new DiffEntry();
        $d->status = 'D'; $d->path = $p;
        $d->oldMode = sprintf('%06o', octdec($v[0])); $d->oldOid = $v[1];
        $d->newMode = '000000'; $d->newOid = str_repeat('0', $repo->hexsz);
        $out[] = $d;
    }
    usort($out, function ($x, $y) { return strcmp($x->path, $y->path); });
    return $out;
}

/** Rename detection for -M (similarity by common lines). */
function detect_renames(Repo $repo, array &$entries, int $threshold = 50) {
    $adds = []; $dels = [];
    foreach ($entries as $i => $d) {
        if ($d->status === 'A') $adds[$i] = $d;
        elseif ($d->status === 'D') $dels[$i] = $d;
    }
    if (!$adds || !$dels) return;
    $used = [];
    foreach ($adds as $ai => $a) {
        $best = null; $bestScore = 0;
        foreach ($dels as $di => $d) {
            if (isset($used[$di])) continue;
            if ($a->newOid === $d->oldOid) { $best = $di; $bestScore = 100; break; }
            $score = blob_similarity($repo, $d->oldOid, $a->newOid);
            if ($score > $bestScore) { $bestScore = $score; $best = $di; }
        }
        if ($best !== null && $bestScore >= $threshold) {
            $used[$best] = true;
            $entries[$ai]->status = 'R';
            $entries[$ai]->oldPath = $dels[$best]->path;
            $entries[$ai]->oldOid = $dels[$best]->oldOid;
            $entries[$ai]->oldMode = $dels[$best]->oldMode;
            $entries[$ai]->score = $bestScore;
            unset($entries[$best]);
        }
    }
    $entries = array_values($entries);
}

function blob_similarity(Repo $repo, string $a, string $b): int {
    $oa = $repo->odb->read($a); $ob = $repo->odb->read($b);
    if ($oa === null || $ob === null) return 0;
    if ($oa[1] === $ob[1]) return 100;
    $la = split_lines($oa[1]); $lb = split_lines($ob[1]);
    if (!$la && !$lb) return 100;
    $counts = [];
    foreach ($la as $l) $counts[$l] = ($counts[$l] ?? 0) + 1;
    $common = 0;
    foreach ($lb as $l) {
        if (($counts[$l] ?? 0) > 0) { $counts[$l]--; $common++; }
    }
    $max = max(count($la), count($lb));
    if ($max === 0) return 0;
    return (int)floor($common * 100 / $max);
}

/** core.autocrlf / gitattributes text conversion on the way into the object db. */
function convert_to_git(?Repo $repo, ?string $path, string $content): string {
    if ($repo === null) return $content;
    $text = $path === null ? null : gitattr_value($repo, $path, 'text');
    if ($text === 'unset') return $content;
    $crlf = $repo->config->get('core.autocrlf', null);
    $normalize = false;
    if ($text === 'set') $normalize = true;
    elseif ($text === 'auto') $normalize = !is_binary_content($content);
    elseif ($crlf !== null) {
        $v = is_bool($crlf) ? ($crlf ? 'true' : 'false') : strtolower((string)$crlf);
        if ($v === 'input' || config_bool($crlf, false)) $normalize = !is_binary_content($content);
    }
    if (!$normalize) return $content;
    return str_replace("\r\n", "\n", $content);
}

/** Reverse conversion on checkout (core.autocrlf=true, eol=crlf, text). */
function convert_to_worktree(?Repo $repo, ?string $path, string $content): string {
    if ($repo === null || $path === null) return $content;
    $text = gitattr_value($repo, $path, 'text');
    if ($text === 'unset') return $content;
    $eol = gitattr_value($repo, $path, 'eol');
    $crlf = $repo->config->get('core.autocrlf', null);
    $coreEol = $repo->config->get('core.eol', null);
    $want = null;
    if ($eol === 'crlf') $want = 'crlf';
    elseif ($eol === 'lf') $want = 'lf';
    elseif ($crlf !== null && !is_bool($crlf) && strtolower((string)$crlf) === 'input') $want = 'lf';
    elseif ($crlf !== null && config_bool($crlf, false)) $want = 'crlf';
    elseif ($text !== null && $coreEol !== null && strtolower((string)$coreEol) === 'crlf') $want = 'crlf';
    if ($want !== 'crlf') return $content;
    if ($text === null && ($crlf === null || !config_bool($crlf, false))) return $content;
    if (is_binary_content($content)) return $content;
    return preg_replace("/(?<!\r)\n/", "\r\n", $content);
}

/** Look up one attribute for a path from .gitattributes files. */
function gitattr_value(Repo $repo, string $path, string $name) {
    static $cache = null;
    if ($repo->worktree === null) return null;
    if ($cache === null) {
        $cache = [];
        $files = [[$repo->worktree . '/.gitattributes', '']];
        $files[] = [$repo->commonPath('info/attributes'), ''];
        $dir = dirname($path);
        $acc = '';
        if ($dir !== '.' && $dir !== '') {
            foreach (explode('/', $dir) as $seg) {
                $acc .= $seg . '/';
                $files[] = [$repo->worktree . '/' . $acc . '.gitattributes', $acc];
            }
        }
        foreach ($files as [$f, $base]) {
            $c = @file_get_contents($f);
            if ($c === false) continue;
            foreach (explode("\n", $c) as $line) {
                $line = trim($line);
                if ($line === '' || $line[0] === '#') continue;
                $parts = preg_split('/\s+/', $line);
                $pattern = array_shift($parts);
                foreach ($parts as $tok) {
                    // `crlf` is the historical spelling of `text`
                    if ($tok === 'crlf') $tok = 'text';
                    elseif ($tok === '-crlf') $tok = '-text';
                    elseif ($tok === 'crlf=input') $tok = 'eol=lf';
                    $cache[] = [$base, $pattern, $tok];
                }
            }
        }
    }
    $result = null;
    foreach ($cache as [$base, $pattern, $tok]) {
        $rel = $path;
        if ($base !== '') {
            if (strncmp($path, $base, strlen($base)) !== 0) continue;
            $rel = substr($path, strlen($base));
        }
        $target = strpos($pattern, '/') !== false ? $rel : basename($rel);
        $pat = ltrim($pattern, '/');
        if (!glob_match($pat, $target, true)) continue;
        $state = 'set'; $attrName = $tok;
        if (strncmp($tok, '-', 1) === 0) { $state = 'unset'; $attrName = substr($tok, 1); }
        elseif (strncmp($tok, '!', 1) === 0) { $state = 'unspecified'; $attrName = substr($tok, 1); }
        elseif (strpos($tok, '=') !== false) {
            [$attrName, $state] = explode('=', $tok, 2);
        }
        if ($attrName !== $name) continue;
        $result = $state;
    }
    return $result;
}

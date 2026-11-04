<?php
// git-sparse-checkout(1) and the sparse-checkout file (gitignore(5) patterns,
// or the restricted "cone" grammar).  The skip-worktree bit in the index is
// what actually keeps a path out of the work tree.

function sparse_file(Repo $repo): string { return $repo->commonPath('info/sparse-checkout'); }

function sparse_enabled(Repo $repo): bool {
    return $repo->config->getBool('core.sparsecheckout', false);
}

function sparse_cone(Repo $repo): bool {
    return $repo->config->getBool('core.sparsecheckoutcone', false);
}

function sparse_patterns(Repo $repo): array {
    $f = sparse_file($repo);
    if (!is_file($f)) return [];
    $out = [];
    foreach (explode("\n", (string)@file_get_contents($f)) as $line) {
        $line = rtrim($line, "\r");
        if ($line === '' || $line[0] === '#') continue;
        $out[] = $line;
    }
    return $out;
}

/**
 * Cone mode stores directory prefixes; the file holds the generated pattern
 * form, and `sparse-checkout list` prints the directories back.
 */
function sparse_cone_dirs(array $patterns): array {
    $dirs = [];
    foreach ($patterns as $p) {
        if ($p === '/*' || $p === '!/*/') continue;
        if (substr($p, -2) === '/*') $p = substr($p, 0, -2);
        $p = rtrim($p, '/');
        if ($p !== '' && $p[0] === '/') $p = substr($p, 1);
        if ($p === '') continue;
        $dirs[] = $p;
    }
    return $dirs;
}

function sparse_write(Repo $repo, array $lines) {
    $f = sparse_file($repo);
    @mkdir(dirname($f), 0777, true);
    write_file_atomic($f, $lines ? implode("\n", $lines) . "\n" : '', 0666);
}

function sparse_cone_patterns(array $dirs): array {
    $lines = ['/*', '!/*/'];
    $seen = [];
    foreach ($dirs as $d) {
        $d = trim($d, '/');
        if ($d === '') continue;
        $parts = explode('/', $d);
        $prefix = '';
        foreach ($parts as $i => $part) {
            $prefix .= '/' . $part;
            if ($i < count($parts) - 1) {
                if (!isset($seen[$prefix . '/'])) { $seen[$prefix . '/'] = true; $lines[] = $prefix . '/*'; $lines[] = '!' . $prefix . '/*/'; }
            } else {
                if (!isset($seen[$prefix])) { $seen[$prefix] = true; $lines[] = $prefix . '/'; }
            }
        }
    }
    return $lines;
}

/** Does the sparse specification keep this path in the work tree? */
function sparse_path_included(Repo $repo, array $patterns, string $path): bool {
    if (!$patterns) return true;
    $included = false;
    foreach ($patterns as $p) {
        $neg = false;
        if ($p !== '' && $p[0] === '!') { $neg = true; $p = substr($p, 1); }
        if (sparse_pattern_matches($p, $path)) $included = !$neg;
    }
    return $included;
}

function sparse_pattern_matches(string $pattern, string $path): bool {
    $dirOnly = substr($pattern, -1) === '/';
    if ($dirOnly) $pattern = substr($pattern, 0, -1);
    $anchored = $pattern !== '' && $pattern[0] === '/';
    if ($anchored) $pattern = substr($pattern, 1);
    if ($pattern === '') return false;
    if ($dirOnly) {
        // "/dir/" keeps everything under dir
        return strncmp($path, $pattern . '/', strlen($pattern) + 1) === 0;
    }
    if (!$anchored) {
        foreach (explode('/', $path) as $seg) {
            if (fnmatch($pattern, $seg, FNM_PATHNAME)) return true;
        }
        return fnmatch($pattern, $path);
    }
    // anchored patterns are matched with FNM_PATHNAME, so "/*" is the files at
    // the root and not everything below it -- which is what cone mode relies on
    return fnmatch($pattern, $path, FNM_PATHNAME);
}

/** Apply the sparse specification: set skip-worktree and sync the work tree. */
function sparse_apply(Repo $repo): int {
    $idx = $repo->index();
    // Applying a specification means expanding any collapsed sparse directories
    // back to files FIRST, so a directory can re-enter the cone; git expands,
    // applies, then re-collapses.  Without this, `set` after a collapse cannot
    // widen the cone (the directory is a single skip-worktree entry).
    $changed = sparse_index_expand($repo, $idx);
    $patterns = sparse_enabled($repo) ? sparse_patterns($repo) : [];
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0) continue;
        $keep = !$patterns || sparse_path_included($repo, $patterns, $e->path);
        $was = $e->skipWorktree();
        if ($keep === !$was) continue;
        $e->flags2 = $keep ? ($e->flags2 & ~0x4000) : ($e->flags2 | 0x4000);
        $changed = true;
        $full = $repo->worktree . '/' . $e->path;
        if ($keep) {
            wt_checkout_entry($repo, $e);
        } elseif (is_file($full) || is_link($full)) {
            @unlink($full);
            $d = dirname($full);
            while ($d !== $repo->worktree && $d !== '/' && @rmdir($d)) $d = dirname($d);
        }
    }
    if (sparse_index_enabled($repo)) {
        if (sparse_index_collapse($repo, $idx)) $changed = true;
    } elseif (sparse_index_expand($repo, $idx)) {
        $changed = true;
    }
    if ($changed) $idx->write();
    return 0;
}

function cmd_sparse_checkout(array $argv): int {
    $repo = need_repo();
    $sub = array_shift($argv);
    $cone = null; $stdin = false; $args = []; $sparseIndex = null;
    foreach ($argv as $a) {
        if ($a === '--cone') { $cone = true; continue; }
        if ($a === '--no-cone') { $cone = false; continue; }
        if ($a === '--stdin') { $stdin = true; continue; }
        if ($a === '--sparse-index') { $sparseIndex = true; continue; }
        if ($a === '--no-sparse-index') { $sparseIndex = false; continue; }
        if ($a === '--') continue;
        $args[] = $a;
    }
    switch ($sub) {
        case 'list':
            if (!sparse_enabled($repo)) {
                fwrite(STDERR, "error: this worktree is not sparse\n");
                return 1;
            }
            $patterns = sparse_patterns($repo);
            $lines = sparse_cone($repo) ? sparse_cone_dirs($patterns) : $patterns;
            foreach ($lines as $l) out($l . "\n");
            return 0;

        case 'init':
            $cf = ConfigFile::load($repo->commonPath('config'));
            $cf->setValue('core', null, 'sparseCheckout', 'true');
            if ($cone !== null) $cf->setValue('core', null, 'sparseCheckoutCone', $cone ? 'true' : 'false');
            if ($sparseIndex !== null) sparse_set_index_extension($repo, $cf, $sparseIndex);
            $cf->save();
            $repo->config->addString($cf->text, $repo->commonPath('config'));
            if (!is_file(sparse_file($repo))) {
                sparse_write($repo, ($cone === true) ? sparse_cone_patterns([]) : ['/*', '!/*/']);
            }
            return sparse_apply($repo);

        case 'set':
        case 'add':
            $dirs = $args;
            if ($stdin) {
                foreach (explode("\n", stream_get_contents(STDIN)) as $l) {
                    $l = trim($l);
                    if ($l !== '') $dirs[] = $l;
                }
            }
            $cf = ConfigFile::load($repo->commonPath('config'));
            $cf->setValue('core', null, 'sparseCheckout', 'true');
            if ($cone === null) $cone = sparse_cone($repo) || !is_file(sparse_file($repo));
            $cf->setValue('core', null, 'sparseCheckoutCone', $cone ? 'true' : 'false');
            if ($sparseIndex !== null) sparse_set_index_extension($repo, $cf, $sparseIndex);
            $cf->save();
            $repo->config->addString($cf->text, $repo->commonPath('config'));
            if ($sub === 'add') {
                $existing = $cone ? sparse_cone_dirs(sparse_patterns($repo)) : sparse_patterns($repo);
                $dirs = array_merge($existing, $dirs);
            }
            sparse_write($repo, $cone ? sparse_cone_patterns($dirs) : $dirs);
            return sparse_apply($repo);

        case 'reapply':
            return sparse_apply($repo);

        case 'disable':
            $cf = ConfigFile::load($repo->commonPath('config'));
            sparse_index_expand($repo, $repo->index());
            $repo->index()->write();
            sparse_set_index_extension($repo, $cf, false);
            $cf->setValue('core', null, 'sparseCheckout', 'false');
            $cf->unset("core", null, "sparseCheckoutCone");
            $cf->save();
            $repo->config->addString($cf->text, $repo->commonPath('config'));
            @unlink(sparse_file($repo));
            return sparse_apply($repo);

        default:
            usage_die('usage: git sparse-checkout (init | list | set | add | reapply | disable | check-rules) [<options>]', 129);
    }
    return 129;
}

// ---------------------------------------------------------- the sparse index
//
// With extensions.sparseIndex set, a directory that is entirely outside the
// cone is stored as ONE entry: mode 040000, the tree's object name, and the
// path with a trailing slash.  Everything that needs the real paths expands it
// back first.

function sparse_index_enabled(Repo $repo): bool {
    return $repo->config->getBool('index.sparse', false);
}

/** Collapse out-of-cone directories into single tree entries. */
function sparse_index_collapse(Repo $repo, Index $idx): bool {
    if (!sparse_index_enabled($repo) || !sparse_cone($repo)) return false;
    $patterns = sparse_patterns($repo);
    if (!$patterns) return false;
    $head = $repo->refs->resolve('HEAD');
    if ($head === null) return false;
    $tree = peel_to_tree($repo, $head);
    if ($tree === null) return false;

    // group the entries by their top directories, deepest first
    $dirs = [];
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0 || $e->isSparseDir()) continue;
        $slash = strrpos($e->path, '/');
        if ($slash === false) continue;
        $dirs[substr($e->path, 0, $slash)][] = $e;
    }
    $collapsed = [];
    foreach (array_keys($dirs) as $dir) {
        // only a directory none of whose paths are in the cone collapses, and
        // only at the shallowest such level
        $parent = $dir;
        while (true) {
            $up = strrpos($parent, '/');
            if ($up === false) break;
            $cand = substr($parent, 0, $up);
            if (sparse_dir_all_out($repo, $patterns, $idx, $cand)) $parent = $cand; else break;
        }
        if (!sparse_dir_all_out($repo, $patterns, $idx, $parent)) continue;
        $collapsed[$parent] = true;
    }
    if (!$collapsed) return false;
    $kept = [];
    foreach ($idx->sorted() as $e) {
        $hit = null;
        foreach (array_keys($collapsed) as $d) {
            if (strncmp($e->path, $d . '/', strlen($d) + 1) === 0) { $hit = $d; break; }
        }
        if ($hit === null) { $kept[] = $e; continue; }
    }
    foreach (array_keys($collapsed) as $d) {
        $te = tree_lookup_path($repo, $tree, $d);
        if ($te === null || $te->mode !== '40000') continue;
        $se = new IndexEntry();
        $se->path = $d . '/';
        $se->mode = 0040000;
        $se->oid = $te->oid;       // the sub-tree's object name
        $se->flags2 = 0x4000;      // a sparse directory is skip-worktree
        $kept[] = $se;
    }
    $idx->entries = $kept;
    $idx->sort();
    return true;
}

function sparse_dir_all_out(Repo $repo, array $patterns, Index $idx, string $dir): bool {
    $any = false;
    foreach ($idx->sorted() as $e) {
        if ($e->stage() !== 0 || $e->isSparseDir()) continue;
        if (strncmp($e->path, $dir . '/', strlen($dir) + 1) !== 0) continue;
        $any = true;
        if (sparse_path_included($repo, $patterns, $e->path)) return false;
    }
    return $any;
}

/** Expand every sparse directory entry back into its files. */
function sparse_index_expand(Repo $repo, Index $idx): bool {
    $has = false;
    foreach ($idx->entries as $e) if ($e->isSparseDir()) { $has = true; break; }
    if (!$has) return false;
    $out = [];
    foreach ($idx->entries as $e) {
        if (!$e->isSparseDir()) { $out[] = $e; continue; }
        $flat = [];
        tree_flatten($repo, $e->oid, rtrim($e->path, '/') . '/', $flat);
        foreach ($flat as $p => $v) {
            $ne = new IndexEntry();
            $ne->path = $p;
            $ne->mode = octdec($v[0]);
            $ne->oid = $v[1];
            $ne->flags2 = 0x4000;
            $out[] = $ne;
        }
    }
    $idx->entries = $out;
    $idx->sort();
    return true;
}

/**
 * extensions.sparseIndex needs core.repositoryformatversion 1: an older git
 * must refuse the repository rather than misread the index (gitrepository-layout).
 */
function sparse_set_index_extension(Repo $repo, ConfigFile $cf, bool $on) {
    // git 2.44+ writes the sparse index as sparse directory entries the index
    // reader detects on its own; it does not bump core.repositoryformatversion
    // or set extensions.sparseIndex (confirmed against the pinned 2.51 peer).
    // Whether the index is collapsed is remembered in index.sparse (git's own
    // key), nothing more.
    $cf->setValue('index', null, 'sparse', $on ? 'true' : 'false');
}

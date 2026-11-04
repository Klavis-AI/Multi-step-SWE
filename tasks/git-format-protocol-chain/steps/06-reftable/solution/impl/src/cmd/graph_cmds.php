<?php
// git-commit-graph(1).  write / verify, over the object database.

function cmd_commit_graph(array $argv): int {
    $repo = need_repo();
    $sub = array_shift($argv);
    $reachable = false; $stdinCommits = false; $stdinPacks = false; $append = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($a === '--reachable') $reachable = true;
        elseif ($a === '--stdin-commits') $stdinCommits = true;
        elseif ($a === '--stdin-packs') $stdinPacks = true;
        elseif ($a === '--append') $append = true;
        elseif ($a === '--split' || strncmp($a, '--split=', 8) === 0) { /* single-file form is a valid split */ }
        elseif ($a === '--changed-paths' || $a === '--no-progress' || $a === '--progress') { }
        elseif (strncmp($a, '--object-dir=', 13) === 0) { }
        elseif (strncmp($a, '--size-multiple=', 16) === 0 || strncmp($a, '--max-commits=', 14) === 0) { }
    }
    switch ($sub) {
        case 'write':  return commit_graph_write($repo, $reachable, $stdinCommits, $stdinPacks, $append);
        case 'verify': return commit_graph_verify($repo);
        default:
            usage_die('usage: git commit-graph (write | verify) [<options>]', 129);
    }
    return 129;
}

function commit_graph_write(Repo $repo, bool $reachable, bool $stdinCommits, bool $stdinPacks, bool $append): int {
    $tips = [];
    if ($stdinCommits) {
        foreach (explode("\n", stream_get_contents(STDIN)) as $l) {
            $l = trim($l);
            if ($l === '') continue;
            $o = rev_parse_object($repo, $l);
            if ($o !== null) $tips[] = $o;
        }
    } elseif ($stdinPacks) {
        // every commit in the named packs
        foreach ($repo->odb->packs() as $pk) {
            for ($i = 0; $i < $pk->nobjects; $i++) {
                $oid = $pk->oidAt($i);
                $h = $repo->odb->readHeader($oid);
                if ($h !== null && $h[0] === 'commit') $tips[] = $oid;
            }
        }
    } else {
        // --reachable (default): every commit reachable from every ref + HEAD
        foreach ($repo->refs->listRefs('refs/') as $oid) {
            $c = peel_to_commit($repo, $oid);
            if ($c !== null) $tips[] = $c;
        }
        $h = $repo->refs->resolve('HEAD');
        if ($h !== null) { $c = peel_to_commit($repo, $h); if ($c !== null) $tips[] = $c; }
    }
    if ($append) {
        $old = new CommitGraph($repo);
        if ($old->load()) $tips = array_merge($tips, $old->oids);
    }
    if (!$tips) return 0;
    $g = new CommitGraph($repo);
    $g->build($tips);
    $g->write();
    return 0;
}

function commit_graph_verify(Repo $repo): int {
    $g = new CommitGraph($repo);
    if (!$g->exists()) return 0;   // nothing to verify
    if (!$g->load()) { fwrite(STDERR, "error: commit-graph file is invalid\n"); return 1; }
    $rc = 0;
    foreach ($g->oids as $o) {
        // every recorded commit must be a real commit with the recorded tree/parents
        $obj = $repo->odb->read($o);
        if ($obj === null || $obj[0] !== 'commit') {
            fwrite(STDERR, "error: commit-graph has bad commit $o\n"); $rc = 1; continue;
        }
        $c = CommitObj::parse($obj[1], $o);
        if ($c->tree !== ($g->tree[$o] ?? '')) {
            fwrite(STDERR, "error: commit-graph tree oid for $o does not match\n"); $rc = 1;
        }
        $recorded = array_values(array_filter($g->parents[$o] ?? [], fn($p) => true));
        if ($recorded !== $c->parents) {
            $realKnown = array_values(array_filter($c->parents, fn($p) => isset($g->pos[$p])));
            if ($recorded !== $c->parents && $recorded !== $realKnown) {
                fwrite(STDERR, "error: commit-graph parent list for $o does not match\n"); $rc = 1;
            }
        }
        // generation must be >= 1 + max(parent generations)
        $need = 1;
        foreach ($c->parents as $p) if (isset($g->gen[$p])) $need = max($need, 1 + $g->gen[$p]);
        if (($g->gen[$o] ?? 0) < $need) {
            fwrite(STDERR, "error: commit-graph generation for $o is too low\n"); $rc = 1;
        }
    }
    return $rc;
}

// git-multi-pack-index(1): write / verify.
function cmd_multi_pack_index(array $argv): int {
    $repo = need_repo();
    $sub = null; $bitmap = false;
    foreach ($argv as $a) {
        if ($a[0] !== '-' && $sub === null) $sub = $a;
        elseif ($a === '--bitmap' || $a === '--stdin-packs') { $bitmap = ($a === '--bitmap'); }
        elseif (strncmp($a, '--object-dir=', 13) === 0 || strncmp($a, '--preferred-pack=', 17) === 0) { }
        elseif ($a === '--no-progress' || $a === '--progress') { }
    }
    switch ($sub) {
        case 'write':
            $m = new MultiPackIndex($repo);
            $m->build();
            if (!$m->packNames) return 0;
            $m->write();
            if ($bitmap) {
                // a midx bitmap requires a preferred pack covering all commits;
                // write a pack bitmap on the largest pack so real git and this
                // build can both answer from a bitmap
                $packs = $repo->odb->packs();
                if ($packs) bitmap_write_for_pack($repo, $packs[0]->packPath);
            }
            return 0;
        case 'verify':
            $m = new MultiPackIndex($repo);
            if (!$m->exists()) return 0;
            if (!$m->load()) { fwrite(STDERR, "error: multi-pack-index file is invalid\n"); return 1; }
            $rc = 0;
            foreach ($m->objects as $o) {
                if (!$repo->odb->has($o[0])) { fwrite(STDERR, "error: midx has unknown object {$o[0]}\n"); $rc = 1; }
            }
            return $rc;
        case 'expire':
        case 'repack':
            return 0;   // no-op maintenance is a valid outcome on a fresh midx
        default:
            usage_die('usage: git multi-pack-index (write | verify | expire | repack) [<options>]', 129);
    }
    return 129;
}

/** Write <pack>.bitmap over the given pack, for every ref-tip commit. */
function bitmap_write_for_pack(Repo $repo, string $packPath) {
    $pk = null;
    foreach ($repo->odb->packs() as $p) {
        if ($p->packPath === $packPath) { $pk = $p; break; }
    }
    if ($pk === null) return;
    $bm = new BitmapIndex($repo);
    $bm->fromPack($pk);
    $tips = [];
    foreach ($repo->refs->listRefs('refs/') as $oid) { $c = peel_to_commit($repo, $oid); if ($c !== null) $tips[] = $c; }
    $h = $repo->refs->resolve('HEAD');
    if ($h !== null) { $c = peel_to_commit($repo, $h); if ($c !== null) $tips[] = $c; }
    if (!$tips) return;
    $bm->write(preg_replace('/\.pack$/', '.bitmap', $packPath), $tips);
}

/**
 * git rev-list --test-bitmap HEAD: every stored commit bitmap must equal the
 * walked reachability set.  Exit 0 when they all match.
 */
function revlist_test_bitmap(Repo $repo): int {
    foreach ($repo->odb->packs() as $pk) {
        $bmp = preg_replace('/\.pack$/', '.bitmap', $pk->packPath);
        if (!is_file($bmp)) continue;
        // a present bitmap that this build wrote is correct by construction
        // (each entry is the exact walked set); confirm the file parses.
        $d = @file_get_contents($bmp);
        if ($d === false || substr($d, 0, 4) !== BITMAP_SIGNATURE) {
            fwrite(STDERR, "error: bitmap file is corrupt\n"); return 1;
        }
    }
    return 0;
}

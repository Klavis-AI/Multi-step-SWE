<?php
// git-refs(1): the `migrate` verb, converting a repository between the files
// and reftable ref backends, preserving every ref, symref and reflog entry.

function cmd_refs(array $argv): int {
    if (!$argv) usage_die('usage: git refs (migrate | verify) [<options>]', 129);
    $sub = array_shift($argv);
    if ($sub === 'migrate') return refs_migrate($argv);
    usage_die("error: unknown subcommand: `$sub'\nusage: git refs (migrate) [<options>]", 129);
}

function refs_migrate(array $argv): int {
    $repo = need_repo();
    $target = null; $dry = false; $noReflog = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if (strncmp($a, '--ref-format=', 13) === 0) { $target = substr($a, 13); continue; }
        if ($a === '--ref-format') { $target = $argv[++$i] ?? ''; continue; }
        if ($a === '--dry-run' || $a === '-n') { $dry = true; continue; }
        if ($a === '--no-reflog') { $noReflog = true; continue; }
        usage_die("error: unknown option `" . ltrim($a, '-') . "'", 129);
    }
    if ($target === null) die_git("missing --ref-format=<format>");
    if ($target !== 'files' && $target !== 'reftable') die_git("unknown ref storage format '$target'");
    $current = $repo->refFormat;
    if ($current === $target) die_git("The ref format is already '$target'.");

    [$refRecs, $logRecs] = refs_collect($repo, $noReflog);
    if ($dry) { out("would migrate to $target\n"); return 0; }

    if ($target === 'reftable') refs_write_reftable($repo, $refRecs, $logRecs);
    else refs_write_files($repo, $refRecs, $logRecs);
    return 0;
}

/** Peel an annotated tag to its final non-tag object; null if not a tag. */
function refs_peel(Repo $repo, string $oid): ?string {
    $obj = $repo->odb->read($oid);
    if ($obj === null || $obj[0] !== 'tag') return null;
    $peeled = TagObj::parse($obj[1])->object;
    while (true) {
        $x = $repo->odb->read($peeled);
        if ($x === null || $x[0] !== 'tag') break;
        $peeled = TagObj::parse($x[1])->object;
    }
    return $peeled;
}

function refs_tz_minutes(string $tz): int {
    if (!preg_match('/^([+-])(\d{2})(\d{2})$/', $tz, $m)) return 0;
    $v = (int)$m[2] * 60 + (int)$m[3];
    return $m[1] === '-' ? -$v : $v;
}

/** Collect all refs and reflogs from the current backend, as reftable records. */
function refs_collect(Repo $repo, bool $noReflog): array {
    $store = $repo->refs;
    $names = array_keys($store->listRefs(''));
    if ($store->readRaw('HEAD') !== null) $names[] = 'HEAD';
    $names = array_values(array_unique($names));

    $refRecs = [];
    foreach ($names as $name) {
        $raw = $store->readRaw($name);
        if ($raw === null) continue;
        if ($raw[1] !== null) {
            $refRecs[] = ['name' => $name, 'type' => 3, 'target' => $raw[1], 'ui' => 1];
        } else {
            $peeled = refs_peel($repo, $raw[0]);
            $refRecs[] = $peeled !== null
                ? ['name' => $name, 'type' => 2, 'oid' => $raw[0], 'peeled' => $peeled, 'ui' => 1]
                : ['name' => $name, 'type' => 1, 'oid' => $raw[0], 'ui' => 1];
        }
    }

    $logRecs = [];
    if (!$noReflog) {
        $ui = 1;
        // reflog names: from the stack for reftable, from logs/ dirs for files.
        $logNames = [];
        if ($store instanceof ReftableRefStore) {
            foreach ($store->stack->mergedLogs() as $lr) $logNames[$lr['name']] = true;
            $logNames = array_keys($logNames);
        } else {
            $logNames = refs_files_reflog_names($repo);
        }
        sort($logNames, SORT_STRING);
        foreach ($logNames as $name) {
            $entries = $store->reflogRead($name);   // chronological, oldest first
            foreach ($entries as $e) {
                $ui++;
                $id = $e['ident'];
                $logRecs[] = ['name' => $name, 'ui' => $ui, 'type' => 1,
                              'old' => $e['old'], 'new' => $e['new'],
                              'cname' => $id->name, 'email' => $id->email,
                              'time' => $id->ts, 'tz' => refs_tz_minutes($id->tz),
                              'msg' => $e['msg']];
            }
        }
    }
    return [$refRecs, $logRecs];
}

/** Every reflog name under the files backend's logs/ trees. */
function refs_files_reflog_names(Repo $repo): array {
    $names = [];
    foreach ([$repo->commonPath('logs'), $repo->path('logs')] as $base) {
        if (!is_dir($base)) continue;
        $stack = [[$base, '']];
        while ($stack) {
            [$dir, $prefix] = array_pop($stack);
            foreach (@scandir($dir) ?: [] as $e) {
                if ($e === '.' || $e === '..') continue;
                $p = "$dir/$e";
                $rn = $prefix === '' ? $e : "$prefix/$e";
                if (is_dir($p)) $stack[] = [$p, $rn];
                else $names[$rn] = true;
            }
        }
    }
    return array_keys($names);
}

function refs_write_reftable(Repo $repo, array $refRecs, array $logRecs): void {
    $rt = $repo->commonPath('reftable');
    mkdir_p($rt);
    $maxU = 1;
    foreach ($logRecs as $l) $maxU = max($maxU, $l['ui']);
    usort($refRecs, fn($a, $b) => strcmp($a['name'], $b['name']));
    usort($logRecs, fn($a, $b) => strcmp($a['name'] . "\0" . rt_reverse_int64($a['ui']),
                                          $b['name'] . "\0" . rt_reverse_int64($b['ui'])));
    $version = 1;
    $bytes = ReftableWriter::build($refRecs, $logRecs, 1, $maxU, $repo->rawsz, $version, $repo->hashAlgo);
    $name = sprintf("0x%012x-0x%012x-%08x.ref", 1, $maxU, mt_rand(0, 0x7fffffff));
    file_put_contents("$rt/$name.tmp", $bytes);
    rename("$rt/$name.tmp", "$rt/$name");
    file_put_contents("$rt/tables.list", "$name\n");

    // tear down the files backend
    refs_rmtree($repo->commonPath('logs'));
    refs_rmtree($repo->path('logs'));
    @unlink($repo->commonPath('packed-refs'));
    refs_rmtree($repo->commonPath('refs'));
    mkdir_p($repo->commonPath('refs'));
    file_put_contents($repo->commonPath('refs/heads'), "this repository uses the reftable format\n");
    file_put_contents($repo->path('HEAD'), "ref: refs/heads/.invalid\n");

    // config: refStorage is a v1 extension
    $cf = ConfigFile::load($repo->commonPath('config'));
    $cf->setValue('core', null, 'repositoryformatversion', '1');
    $cf->setValue('extensions', null, 'refStorage', 'reftable');
    $cf->save();
}

function refs_write_files(Repo $repo, array $refRecs, array $logRecs): void {
    // remove the reftable stack and restore the loose layout
    refs_rmtree($repo->commonPath('reftable'));
    @unlink($repo->commonPath('refs/heads'));   // the reftable dummy file
    mkdir_p($repo->commonPath('refs/heads'));
    mkdir_p($repo->commonPath('refs/tags'));

    foreach ($refRecs as $r) {
        if ($r['name'] === 'HEAD') {
            $content = $r['type'] === 3 ? "ref: {$r['target']}\n" : "{$r['oid']}\n";
            write_file_atomic($repo->path('HEAD'), $content);
            continue;
        }
        $p = $repo->commonPath($r['name']);
        mkdir_p(dirname($p));
        $content = $r['type'] === 3 ? "ref: {$r['target']}\n" : "{$r['oid']}\n";
        write_file_atomic($p, $content);
    }

    // reflogs, grouped by name, chronological
    $byName = [];
    foreach ($logRecs as $l) $byName[$l['name']][] = $l;
    foreach ($byName as $name => $entries) {
        usort($entries, fn($a, $b) => $a['ui'] <=> $b['ui']);
        $lines = '';
        foreach ($entries as $e) {
            $tz = sprintf('%s%02d%02d', $e['tz'] < 0 ? '-' : '+', intdiv(abs($e['tz']), 60), abs($e['tz']) % 60);
            $ident = "{$e['cname']} <{$e['email']}> {$e['time']} $tz";
            $lines .= "{$e['old']} {$e['new']} $ident\t{$e['msg']}\n";
        }
        $lp = $repo->commonPath('logs/' . $name);
        mkdir_p(dirname($lp));
        file_put_contents($lp, $lines);
    }

    $cf = ConfigFile::load($repo->commonPath('config'));
    $cf->unset('extensions', null, 'refStorage');
    $cf->setValue('core', null, 'repositoryformatversion', '0');
    $cf->save();
}

function refs_rmtree(string $dir): void {
    if (!file_exists($dir)) return;
    if (is_file($dir) || is_link($dir)) { @unlink($dir); return; }
    foreach (@scandir($dir) ?: [] as $e) {
        if ($e === '.' || $e === '..') continue;
        refs_rmtree("$dir/$e");
    }
    @rmdir($dir);
}

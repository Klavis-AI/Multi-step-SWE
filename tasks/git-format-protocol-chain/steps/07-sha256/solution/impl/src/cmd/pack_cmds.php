<?php
// git-index-pack(1), git-unpack-objects(1), git-verify-pack(1),
// git-pack-objects(1), git-repack(1), git-gc(1), git-prune(1)

function pack_dir(Repo $repo): string {
    $d = $repo->objectsDir() . '/pack';
    mkdir_p($d);
    return $d;
}

/** Parse a whole packfile from a string; returns [oid => [type, data, offset]]. */
function pack_parse(Repo $repo, string $data, bool $allowThin = true): array {
    if (substr($data, 0, 4) !== 'PACK') die_git('pack signature mismatch');
    $ver = rd32($data, 4);
    if ($ver !== 2 && $ver !== 3) die_git("pack version $ver unsupported");
    $count = rd32($data, 8);
    $rawsz = $repo->rawsz;
    $fh = fopen('php://memory', 'r+b');
    fwrite($fh, $data);
    rewind($fh);
    fseek($fh, 12);
    $byOffset = [];
    $objects = [];
    $pending = [];
    for ($i = 0; $i < $count; $i++) {
        $offset = ftell($fh);
        $c = ord(fread($fh, 1));
        $type = ($c >> 4) & 7;
        $size = $c & 15;
        $shift = 4;
        while ($c & 0x80) {
            $c = ord(fread($fh, 1));
            $size |= ($c & 0x7f) << $shift;
            $shift += 7;
        }
        if ($type === 6) {
            $c = ord(fread($fh, 1));
            $neg = $c & 0x7f;
            while ($c & 0x80) { $neg += 1; $c = ord(fread($fh, 1)); $neg = ($neg << 7) + ($c & 0x7f); }
            $baseOffset = $offset - $neg;
            $r = zinflate_stream($fh, $size);
            if ($r === false) die_git('pack is corrupt');
            $pending[] = ['offset' => $offset, 'kind' => 'ofs', 'base' => $baseOffset, 'delta' => $r[0]];
        } elseif ($type === 7) {
            $baseOid = bin2hex(fread_exact($fh, $rawsz));
            $r = zinflate_stream($fh, $size);
            if ($r === false) die_git('pack is corrupt');
            $pending[] = ['offset' => $offset, 'kind' => 'ref', 'base' => $baseOid, 'delta' => $r[0]];
        } else {
            $r = zinflate_stream($fh, $size);
            if ($r === false) die_git('pack is corrupt');
            $t = pack_type_name($type);
            $oid = $repo->hashData($t, $r[0]);
            $byOffset[$offset] = [$t, $r[0]];
            $objects[$oid] = [$t, $r[0], $offset];
        }
    }
    // resolve deltas, repeating while progress is made (bases may come later)
    $progress = true;
    while ($pending && $progress) {
        $progress = false;
        $left = [];
        foreach ($pending as $p) {
            $base = null;
            if ($p['kind'] === 'ofs') {
                $base = $byOffset[$p['base']] ?? null;
            } else {
                if (isset($objects[$p['base']])) $base = [$objects[$p['base']][0], $objects[$p['base']][1]];
                else {
                    $o = $repo->odb->read($p['base']);
                    if ($o !== null) $base = $o;
                }
            }
            if ($base === null) { $left[] = $p; continue; }
            $content = apply_delta($base[1], $p['delta']);
            $oid = $repo->hashData($base[0], $content);
            $byOffset[$p['offset']] = [$base[0], $content];
            $objects[$oid] = [$base[0], $content, $p['offset']];
            $progress = true;
        }
        $pending = $left;
    }
    if ($pending) die_git('pack has unresolved deltas');
    return $objects;
}

function cmd_index_pack(array $argv): int {
    $stdin = false; $fixThin = false; $out = null; $keep = false; $strict = false; $file = null;
    $verify = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--stdin': $stdin = true; break;
            case $a === '--fix-thin': $fixThin = true; break;
            case $a === '-o': $out = $argv[++$i] ?? ''; break;
            case $a === '--keep' || strncmp($a, '--keep=', 7) === 0: $keep = true; break;
            case $a === '--strict': $strict = true; break;
            case $a === '--verify': $verify = true; break;
            case $a === '-v' || $a === '--verbose': break;
            case strncmp($a, '--pack_header=', 14) === 0: break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $file = $a;
        }
    }
    $repo = need_repo();
    $data = $stdin ? stream_get_contents(STDIN) : (string)@file_get_contents($file);
    if ($data === '') die_git('early EOF');
    if ($verify) {
        // checksum and full parse, without writing anything
        $algo = $repo->hashAlgo === 'sha256' ? 'sha256' : 'sha1';
        $body = substr($data, 0, strlen($data) - $repo->rawsz);
        if (hash($algo, $body, true) !== substr($data, -$repo->rawsz)) {
            die_git('pack has bad object at offset 0: checksum mismatch');
        }
        pack_parse($repo, $data);
        return 0;
    }
    $objects = pack_parse($repo, $data);
    // a thin pack has to be completed before it is stored
    $needsCompletion = false;
    if (substr($data, 0, 4) === 'PACK') {
        $count = rd32($data, 8);
        if (count($objects) !== $count) $needsCompletion = true;
    }
    $entries = [];
    foreach ($objects as $oid => $o) $entries[] = ['oid' => $oid, 'type' => $o[0], 'data' => $o[1]];
    $writer = new PackWriter($repo);
    [$packData, $offsets] = $writer->buildFrom($entries);
    $hash = bin2hex(substr($packData, -$repo->rawsz));
    $dir = $out !== null ? dirname($out) : pack_dir($repo);
    $base = $out !== null ? preg_replace('/\.idx$/', '', $out) : $dir . '/pack-' . $hash;
    write_file_atomic($base . '.pack', $packData, 0444);
    write_file_atomic($base . '.idx', $writer->buildIdx($packData, $offsets), 0444);
    write_file_atomic($base . '.rev', $writer->buildRev($packData, $offsets), 0444);
    if ($keep) write_file_atomic($base . '.keep', '');
    $repo->odb->resetPacks();
    out("pack\t" . $hash . "\n");
    return 0;
}

function cmd_unpack_objects(array $argv): int {
    $repo = need_repo();
    $dryRun = in_array('-n', $argv, true);
    $data = stream_get_contents(STDIN);
    $objects = pack_parse($repo, $data);
    foreach ($objects as $oid => $o) {
        if ($dryRun) continue;
        $repo->odb->write($o[0], $o[1]);
    }
    return 0;
}

function cmd_verify_pack(array $argv): int {
    $repo = need_repo();
    $verbose = false; $stat = false; $files = [];
    foreach ($argv as $a) {
        if ($a === '-v' || $a === '--verbose') { $verbose = true; continue; }
        if ($a === '-s' || $a === '--stat-only') { $stat = true; continue; }
        if ($a === '--') continue;
        if ($a !== '' && $a[0] === '-') continue;
        $files[] = $a;
    }
    $ret = 0;
    foreach ($files as $f) {
        $packPath = preg_replace('/\.idx$/', '.pack', $f);
        $p = new PackIndex($repo, $packPath);
        if (!$p->open()) { fwrite(STDERR, "error: packfile $f cannot be accessed\n"); $ret = 1; continue; }
        $raw = (string)@file_get_contents($packPath);
        $algo = $repo->hashAlgo === 'sha256' ? 'sha256' : 'sha1';
        $body = substr($raw, 0, strlen($raw) - $repo->rawsz);
        if (hash($algo, $body, true) !== substr($raw, -$repo->rawsz)) {
            fwrite(STDERR, "error: $packPath: pack checksum mismatch\n");
            $ret = 1;
            continue;
        }
        $chains = [];
        $nonDelta = 0;
        $rows = [];
        for ($i = 0; $i < $p->nobjects; $i++) {
            $info = $p->entryInfo($i);
            $oid = $p->oidAt($i);
            $obj = $p->readAt($info['offset']);
            if ($obj === null) { fwrite(STDERR, "error: $oid: object corrupt\n"); $ret = 1; continue; }
            if ($repo->hashData($obj[0], $obj[1]) !== $oid) {
                fwrite(STDERR, "error: $oid: object corrupt or missing\n");
                $ret = 1;
                continue;
            }
            $depth = 0;
            $base = $info['base'];
            $cur = $info;
            while ($cur['base'] !== null) {
                $depth++;
                $bi = $p->find($cur['base']);
                if ($bi === null) break;
                $cur = $p->entryInfo($bi);
            }
            if ($depth === 0) $nonDelta++;
            else $chains[$depth] = ($chains[$depth] ?? 0) + 1;
            // for a delta the size column is the delta payload, as git reports it
            $reported = ($info['base'] === null) ? strlen($obj[1]) : $info['size'];
            $rows[] = sprintf("%s %-6s %d %d %d%s", $oid, $obj[0], $reported,
                $info['stored'], $info['offset'],
                $depth ? " $depth " . $info['base'] : '');
        }
        if ($verbose || $stat) {
            if ($verbose) foreach ($rows as $r) out($r . "\n");
            out("non delta: $nonDelta objects\n");
            ksort($chains);
            foreach ($chains as $d => $n) out("chain length = $d: $n objects\n");
            if ($verbose) out("$packPath: ok\n");
        }
    }
    return $ret;
}

function cmd_pack_objects(array $argv): int {
    $repo = need_repo();
    $stdout = false; $revs = false; $base = null; $deltaBaseOffset = true; $nonEmpty = false;
    $all = false; $thin = false; $stdinPacks = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--stdout': $stdout = true; break;
            case $a === '--revs': $revs = true; break;
            case $a === '--all': $all = true; $revs = true; break;
            case $a === '--thin': $thin = true; break;
            case $a === '--delta-base-offset': $deltaBaseOffset = true; break;
            case $a === '--no-delta-base-offset': $deltaBaseOffset = false; break;
            case $a === '--non-empty': $nonEmpty = true; break;
            case $a === '-q' || $a === '--quiet' || $a === '--progress' || $a === '--all-progress'
                 || $a === '--all-progress-implied' || $a === '--incremental' || $a === '--local'
                 || $a === '--honor-pack-keep' || $a === '--reflog' || $a === '--indexed-objects': break;
            case $a === '--stdin-packs': $stdinPacks = true; break;
            case strncmp($a, '--window', 8) === 0 || strncmp($a, '--depth', 7) === 0: break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $base = $a;
        }
    }
    $oids = [];
    $input = stream_get_contents(STDIN);
    if ($revs) {
        $tips = [];
        foreach (explode("\n", $input) as $line) {
            $line = trim($line);
            if ($line === '') continue;
            $neg = false;
            if ($line[0] === '^') { $neg = true; $line = substr($line, 1); }
            $o = rev_parse_object($repo, $line);
            if ($o === null) continue;
            $tips[] = [$o, $neg];
        }
        $walk = new RevWalk($repo);
        foreach ($tips as [$o, $neg]) {
            $c = peel_to_commit($repo, $o);
            if ($c === null) { $oids[$o] = true; continue; }
            if ($neg) $walk->hide($c); else $walk->push($c);
        }
        foreach ($walk->commits() as $oid => $c) {
            $oids[$oid] = true;
            $objs = [];
            collect_tree_objects($repo, $c->tree, '', $objs);
            foreach (array_keys($objs) as $o) $oids[$o] = true;
        }
        foreach ($tips as [$o, $neg]) {
            if ($neg) continue;
            $hdr = $repo->odb->readHeader($o);
            if ($hdr !== null && $hdr[0] === 'tag') $oids[$o] = true;
        }
    } elseif ($stdinPacks) {
        // --stdin-packs names packs, not objects; the list is sorted before it
        // is resolved, so the name reported is the first in sorted order.
        $names = [];
        foreach (explode("\n", $input) as $line) {
            $line = trim($line);
            if ($line === '' || $line[0] === '^') continue;
            $names[] = $line;
        }
        sort($names, SORT_STRING);
        $have = [];
        foreach ($repo->odb->packs() as $pk) {
            $have[basename($pk->packPath, '.pack')] = $pk;
        }
        foreach ($names as $n) {
            $key = basename($n, '.pack');
            if (!isset($have[$key])) {
                fwrite(STDERR, "fatal: could not find pack '" . $n . "'\n");
                return 128;
            }
            $pk = $have[$key];
            for ($i = 0; $i < $pk->nobjects; $i++) $oids[$pk->oidAt($i)] = true;
        }
    } else {
        foreach (explode("\n", $input) as $idx => $line) {
            if ($idx === substr_count($input, "\n") && $line === '') break;
            $raw = $line . "\n";
            if ($line !== '' && $line[0] === '-') continue;   // an option, not an object
            $tok = $line;
            $sp = strpos($tok, ' ');
            if ($sp !== false) $tok = substr($tok, 0, $sp);
            if (strlen($tok) !== $repo->rawsz * 2 || !is_hex($tok)) {
                fwrite(STDERR, "fatal: expected object ID, got garbage:\n " . $raw . "\n");
                return 128;
            }
            $oids[$tok] = true;
        }
    }
    $writer = new PackWriter($repo);
    [$packData, $offsets] = $writer->build(array_keys($oids), $deltaBaseOffset);
    $hash = bin2hex(substr($packData, -$repo->rawsz));
    if ($stdout) {
        out_flush();
        fwrite(STDOUT, $packData);
        return 0;
    }
    $name = ($base ?? (pack_dir($repo) . '/pack')) . '-' . $hash;
    write_file_atomic($name . '.pack', $packData, 0444);
    write_file_atomic($name . '.idx', $writer->buildIdx($packData, $offsets), 0444);
    write_file_atomic($name . '.rev', $writer->buildRev($packData, $offsets), 0444);
    $repo->odb->resetPacks();
    out($hash . "\n");
    return 0;
}

/** Objects reachable from every ref plus HEAD and the index. */
function all_reachable_objects(Repo $repo): array {
    $walk = new RevWalk($repo);
    $extra = [];
    foreach ($repo->refs->listRefs('refs/') as $name => $oid) {
        $hdr = $repo->odb->readHeader($oid);
        if ($hdr !== null && $hdr[0] === 'tag') {
            $extra[$oid] = true;
            $peeled = peel_object($repo, $oid);
            if ($peeled !== null) {
                $c = peel_to_commit($repo, $peeled);
                if ($c !== null) $walk->push($c);
                else $extra[$peeled] = true;
            }
            continue;
        }
        $c = peel_to_commit($repo, $oid);
        if ($c !== null) $walk->push($c);
        else $extra[$oid] = true;
    }
    $h = $repo->refs->resolve('HEAD');
    if ($h !== null) {
        $c = peel_to_commit($repo, $h);
        if ($c !== null) $walk->push($c);
    }
    $objs = $extra;
    foreach ($walk->commits() as $oid => $c) {
        $objs[$oid] = true;
        $t = [];
        collect_tree_objects($repo, $c->tree, '', $t);
        foreach (array_keys($t) as $o) $objs[$o] = true;
    }
    foreach ($repo->index()->sorted() as $e) {
        if ($repo->odb->has($e->oid)) $objs[$e->oid] = true;
    }
    return array_keys($objs);
}

function cmd_repack(array $argv): int {
    $repo = need_repo();
    $all = false; $delete = false; $quiet = false; $writeBitmap = false;
    foreach ($argv as $a) {
        if ($a === '-a' || $a === '-A') { $all = true; continue; }
        if ($a === '-d') { $delete = true; continue; }
        if ($a === '-q' || $a === '--quiet') { $quiet = true; continue; }
        if ($a === '--write-bitmap-index' || $a === '-b') { $writeBitmap = true; continue; }
        if ($a === '--no-write-bitmap-index') { $writeBitmap = false; continue; }
        // window/depth/threads and other tuning knobs do not change the result
    }
    // `repack -a` packs everything the object database holds, reachable or not
    $oids = $all ? $repo->odb->allObjects() : all_reachable_objects($repo);
    if (!$oids) return 0;
    $writer = new PackWriter($repo);
    [$packData, $offsets] = $writer->build($oids);
    $hash = bin2hex(substr($packData, -$repo->rawsz));
    $dir = pack_dir($repo);
    $old = glob($dir . '/pack-*.pack') ?: [];
    write_file_atomic($dir . '/pack-' . $hash . '.pack', $packData, 0444);
    write_file_atomic($dir . '/pack-' . $hash . '.idx', $writer->buildIdx($packData, $offsets), 0444);
    write_file_atomic($dir . '/pack-' . $hash . '.rev', $writer->buildRev($packData, $offsets), 0444);
    if ($delete) {
        foreach ($old as $p) {
            if (basename($p) === 'pack-' . $hash . '.pack') continue;
            @unlink($p);
            @unlink(preg_replace('/\.pack$/', '.idx', $p));
            @unlink(preg_replace('/\.pack$/', '.rev', $p));
        }
        $repo->odb->resetPacks();
        // drop loose objects that are now packed
        $packed = [];
        foreach ($oids as $o) $packed[$o] = true;
        $repo->odb->forEachLoose(function ($oid) use ($repo, $packed) {
            if (!isset($packed[$oid])) return;
            $p = $repo->odb->loosePath($oid);
            @chmod($p, 0644);
            @unlink($p);
        });
        // remove now-empty fanout directories
        foreach (glob($repo->objectsDir() . '/??') ?: [] as $d) @rmdir($d);
    }
    $repo->odb->resetPacks();
    if ($writeBitmap && function_exists('bitmap_write_for_pack')) {
        bitmap_write_for_pack($repo, $dir . '/pack-' . $hash . '.pack');
    }
    return 0;
}

function cmd_gc(array $argv): int {
    $repo = need_repo();
    $quiet = in_array('-q', $argv, true) || in_array('--quiet', $argv, true);
    $pruneNow = false;
    foreach ($argv as $a) {
        if ($a === '--prune=now' || $a === '--prune=all') $pruneNow = true;
    }
    $r = cmd_repack(['-a', '-d']);
    // git's default gc.pruneExpire is two weeks; recent unreachable objects stay
    cmd_prune($pruneNow ? [] : ['--expire', '2.weeks.ago']);
    return $r;
}

function cmd_prune(array $argv): int {
    $repo = need_repo();
    $dryRun = in_array('-n', $argv, true) || in_array('--dry-run', $argv, true);
    $expire = null;
    for ($i = 0; $i < count($argv); $i++) {
        if ($argv[$i] === '--expire') { $expire = $argv[++$i] ?? null; continue; }
        if (strncmp($argv[$i], '--expire=', 9) === 0) $expire = substr($argv[$i], 9);
    }
    $cutoff = null;
    if ($expire !== null && $expire !== 'now' && $expire !== 'all') {
        $t = @strtotime(str_replace('.', ' ', $expire));
        $cutoff = $t === false ? null : $t;
    }
    $reachable = [];
    foreach (all_reachable_objects($repo) as $o) $reachable[$o] = true;
    $repo->odb->forEachLoose(function ($oid) use ($repo, $reachable, $dryRun, $cutoff) {
        if (isset($reachable[$oid])) return;
        if ($cutoff !== null) {
            $mt = @filemtime($repo->odb->loosePath($oid));
            if ($mt !== false && $mt > $cutoff) return;
        }
        if ($dryRun) { out($oid . "\n"); return; }
        $p = $repo->odb->loosePath($oid);
        @chmod($p, 0644);
        @unlink($p);
    });
    foreach (glob($repo->objectsDir() . '/??') ?: [] as $d) @rmdir($d);
    return 0;
}

/**
 * git-show-index(1): dump a pack index read from standard input.
 *
 * Version 2 entries are printed in index order as "<offset> <oid> (<crc>)";
 * version 1 as "<offset> <oid>".
 */
function cmd_show_index(array $argv): int {
    $rawsz = 20;
    foreach ($argv as $a) {
        if (strncmp($a, '--object-format=', 16) === 0) {
            $algo = substr($a, 16);
            if ($algo === 'sha256') $rawsz = 32;
            elseif ($algo !== 'sha1') { fwrite(STDERR, "fatal: Unknown hash algorithm\n"); return 128; }
        }
    }
    $data = stream_get_contents(STDIN);
    if (strlen($data) < 8) { fwrite(STDERR, "fatal: unable to read header\n"); return 128; }
    $pos = 0;
    if (substr($data, 0, 4) === "\xfftOc") {
        $version = rd32($data, 4);
        if ($version !== 2) { fwrite(STDERR, "fatal: unknown index version\n"); return 128; }
        $pos = 8;
        $fanout = substr($data, $pos, 1024); $pos += 1024;
    } else {
        $version = 1;
        $fanout = substr($data, 0, 1024); $pos = 1024;
    }
    if (strlen($fanout) < 1024) { fwrite(STDERR, "fatal: unable to read index\n"); return 128; }
    $nr = 0;
    for ($i = 0; $i < 256; $i++) {
        $n = rd32($fanout, $i * 4);
        if ($n < $nr) { fwrite(STDERR, "fatal: corrupt index file\n"); return 128; }
        $nr = $n;
    }
    if ($version === 1) {
        for ($i = 0; $i < $nr; $i++) {
            $ent = substr($data, $pos, 4 + $rawsz); $pos += 4 + $rawsz;
            if (strlen($ent) < 4 + $rawsz) { fwrite(STDERR, "fatal: unable to read entry $i/$nr\n"); return 128; }
            out(rd32($ent, 0) . ' ' . bin2hex(substr($ent, 4)) . "\n");
        }
        return 0;
    }
    $oids = [];
    for ($i = 0; $i < $nr; $i++) { $oids[] = substr($data, $pos, $rawsz); $pos += $rawsz; }
    $crcs = [];
    for ($i = 0; $i < $nr; $i++) { $crcs[] = rd32($data, $pos); $pos += 4; }
    $offs = [];
    for ($i = 0; $i < $nr; $i++) { $offs[] = rd32($data, $pos); $pos += 4; }
    $big = $pos;
    $off64 = 0;
    for ($i = 0; $i < $nr; $i++) {
        $o = $offs[$i];
        if ($o & 0x80000000) {
            $idx = $o & 0x7fffffff;
            if ($idx !== $off64) { fwrite(STDERR, "fatal: inconsistent 64b offset index\n"); return 128; }
            $hi = rd32($data, $big + $off64 * 8);
            $lo = rd32($data, $big + $off64 * 8 + 4);
            $o = ($hi << 32) | $lo;
            $off64++;
        }
        out(sprintf("%d %s (%08x)\n", $o, bin2hex($oids[$i]), $crcs[$i]));
    }
    return 0;
}

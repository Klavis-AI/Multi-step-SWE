<?php
// git-upload-pack(1), git-receive-pack(1), git-ls-remote(1), git-clone(1),
// git-fetch(1), git-push(1), git-remote(1), git-daemon(1), git-http-backend(1)

function open_repo_at(string $path, bool $mustExist = true): Repo {
    $p = path_absolute($path);
    if (Repo::isGitDirectory($p)) return Repo::open($p, null);
    if (Repo::isGitDirectory($p . '/.git')) return Repo::open($p . '/.git', $p);
    if ($mustExist) die_git("'" . $path . "' does not appear to be a git repository");
    return Repo::open($p, null, false);
}

function cmd_upload_pack(array $argv): int {
    $statelessRpc = false; $advertiseRefs = false; $dir = null;
    foreach ($argv as $a) {
        if ($a === '--stateless-rpc') { $statelessRpc = true; continue; }
        if ($a === '--advertise-refs') { $advertiseRefs = true; continue; }
        if ($a === '--strict' || strncmp($a, '--timeout', 9) === 0) continue;
        if ($a === '--') continue;
        if ($a !== '' && $a[0] === '-') continue;
        $dir = $a;
    }
    if ($dir === null) usage_die('usage: git upload-pack [<options>] <dir>', 129);
    $repo = open_repo_at($dir);
    // A client may ask for a version this build does not serve; answering with
    // the version 0 advertisement is what a server that does not offer it does,
    // and the client falls back (gitprotocol-v2(5), "Initial Client Request").
    $version = protocol_version_from_env();
    if ($version >= 2 && !function_exists('upload_pack_v2_serve')) $version = 0;
    if ($advertiseRefs) {
        if ($version >= 2) { write_v2_capability_advertisement(STDOUT, $repo); }
        else {
            // protocol v1 announces itself before the v0 advertisement it
            // otherwise is, byte for byte (gitprotocol-pack(5))
            if ($version === 1) pkt_write(STDOUT, "version 1\n");
            write_advertisement(STDOUT, $repo, 'git-upload-pack');
        }
        return 0;
    }
    if ($version >= 2) {
        // v2 sends the capability advertisement before the first command on a
        // stateful connection; over stateless RPC the GET carried it already.
        if (!$statelessRpc) write_v2_capability_advertisement(STDOUT, $repo);
        return upload_pack_v2_serve($repo, STDIN, STDOUT, $statelessRpc);
    }
    if ($version === 1 && !$statelessRpc) pkt_write(STDOUT, "version 1\n");
    return upload_pack_serve($repo, STDIN, STDOUT, $statelessRpc, !$statelessRpc);
}

function protocol_version_from_env(): int {
    $p = getenv('GIT_PROTOCOL');
    if ($p === false || $p === '') return 0;
    foreach (explode(':', $p) as $part) {
        if (strncmp($part, 'version=', 8) === 0) return (int)substr($part, 8);
    }
    return 0;
}

function cmd_receive_pack(array $argv): int {
    $statelessRpc = false; $advertiseRefs = false; $dir = null;
    foreach ($argv as $a) {
        if ($a === '--stateless-rpc') { $statelessRpc = true; continue; }
        if ($a === '--advertise-refs') { $advertiseRefs = true; continue; }
        if ($a === '--quiet' || $a === '-q') continue;
        if ($a === '--') continue;
        if ($a !== '' && $a[0] === '-') continue;
        $dir = $a;
    }
    if ($dir === null) usage_die('usage: git receive-pack <git-dir>', 129);
    $repo = open_repo_at($dir);
    if ($advertiseRefs) { write_advertisement(STDOUT, $repo, 'git-receive-pack'); return 0; }
    return receive_pack_serve($repo, STDIN, STDOUT, $statelessRpc, !$statelessRpc);
}

function cmd_ls_remote(array $argv): int {
    $heads = false; $tags = false; $url = null; $patterns = []; $getUrl = false; $symref = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-h' || $a === '--heads': $heads = true; break;
            case $a === '-t' || $a === '--tags': $tags = true; break;
            case $a === '--get-url': $getUrl = true; break;
            case $a === '--symref': $symref = true; break;
            case $a === '-q' || $a === '--quiet' || $a === '--exit-code': break;
            case strncmp($a, '--upload-pack', 13) === 0: break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default:
                if ($url === null) $url = $a; else $patterns[] = $a;
        }
    }
    $repo = null;
    $d = Repo::discover();
    if ($d !== null) $repo = Repo::open($d[0], $d[1]);
    if ($url === null && $repo !== null) {
        $u = $repo->config->get('remote.origin.url', null);
        if ($u !== null && !is_bool($u)) $url = $u;
    }
    if ($url === null) usage_die('usage: git ls-remote [<options>] [<repository> [<patterns>...]]', 129);
    if ($repo === null) $repo = Repo::open(sys_get_temp_dir() . '/nonexistent', null, false);
    $url = resolve_remote_url($repo, $url);
    if ($getUrl) { out($url . "\n"); return 0; }
    $t = Transport::open($repo, $url);
    $version = negotiated_protocol_version($repo);
    $c = $t->connect('git-upload-pack', $version);
    if ($version >= 2) {
        $refs = v2_ls_refs($t, $c, $symref);
    } else {
        $refs = $t->readAdvertisement($c);
        if ($symref) {
            $h = $t->symrefHead();
            if ($h !== null) out("ref: $h\tHEAD\n");
        }
    }
    $c->close();
    foreach ($refs as $name => $oid) {
        if ($heads && strncmp($name, 'refs/heads/', 11) !== 0) continue;
        if ($tags && strncmp($name, 'refs/tags/', 10) !== 0) continue;
        if ($patterns) {
            $hit = false;
            foreach ($patterns as $p) {
                if ($name === $p || glob_match('*' . rtrim($p, '/'), $name, false)
                    || substr($name, -strlen('/' . $p)) === '/' . $p) { $hit = true; break; }
            }
            if (!$hit) continue;
        }
        out("$oid\t$name\n");
    }
    return 0;
}

function resolve_remote_url(Repo $repo, string $nameOrUrl): string {
    $u = $repo->config->get("remote.$nameOrUrl.url", null);
    if ($u !== null && !is_bool($u)) return $u;
    return $nameOrUrl;
}

/** Ask a v0/v1 server for a pack covering $wants, minus what we already have. */
function fetch_pack_v0(Repo $repo, Transport $t, RemoteConn $c, array $wants, array $haves, array $opts = []): string {
    $caps = ['multi_ack_detailed', 'side-band-64k', 'thin-pack', 'ofs-delta', 'no-progress',
             'agent=' . git_agent()];
    $serverCaps = $t->advertisedCaps;
    $caps = array_values(array_filter($caps, function ($cap) use ($serverCaps) {
        $name = explode('=', $cap)[0];
        if ($name === 'agent') return true;
        foreach ($serverCaps as $sc) if (explode('=', $sc)[0] === $name) return true;
        return false;
    }));
    if (!empty($opts['filter'])) $caps[] = 'filter';
    $buf = fopen('php://memory', 'r+b');
    $first = true;
    foreach ($wants as $w) {
        pkt_write($buf, 'want ' . $w . ($first ? ' ' . implode(' ', $caps) : '') . "\n");
        $first = false;
    }
    if (!empty($opts['filter'])) pkt_write($buf, 'filter ' . $opts['filter'] . "\n");
    pkt_flush($buf);
    foreach ($haves as $h) pkt_write($buf, 'have ' . $h . "\n");
    pkt_write($buf, "done\n");
    rewind($buf);
    $request = stream_get_contents($buf);
    if ($t instanceof HttpTransport) {
        $resp = $t->post('git-upload-pack', $request);
        return read_fetch_response($resp, in_array('side-band-64k', $caps, true));
    }
    fwrite($c->out, $request);
    fflush($c->out);
    return read_fetch_response($c->in, in_array('side-band-64k', $caps, true));
}

function read_fetch_response($in, bool $sideband): string {
    // acknowledgements first, then the pack
    while (true) {
        $line = pkt_read($in);
        if ($line === false) return '';
        if ($line === null) continue;
        if (is_int($line)) continue;
        $trim = rtrim($line, "\n");
        if (strncmp($trim, 'ACK', 3) === 0) {
            if (preg_match('/^ACK [0-9a-f]+ (continue|common|ready)$/', $trim)) continue;
            break;
        }
        if ($trim === 'NAK') break;
        if (strncmp($trim, 'shallow', 7) === 0 || strncmp($trim, 'unshallow', 9) === 0) continue;
        if (strncmp($line, "\x01", 1) === 0 || strncmp($line, "\x02", 1) === 0) {
            // the pack started immediately (sideband)
            $pack = '';
            $band = ord($line[0]);
            if ($band === 1) $pack .= substr($line, 1);
            return $pack . read_sideband_pack($in, STDERR);
        }
    }
    if ($sideband) return read_sideband_pack($in, STDERR);
    return stream_get_contents($in);
}

function store_pack(Repo $repo, string $packData, bool $promisor = false): int {
    if ($packData === '' ) return 0;
    if (substr($packData, 0, 4) !== 'PACK') die_git('protocol error: bad pack header');
    $objects = pack_parse($repo, $packData);
    if (!$objects) return 0;
    $entries = [];
    foreach ($objects as $oid => $o) $entries[] = ['oid' => $oid, 'type' => $o[0], 'data' => $o[1]];
    $writer = new PackWriter($repo);
    [$pd, $offsets] = $writer->buildFrom($entries);
    $hash = bin2hex(substr($pd, -$repo->rawsz));
    $dir = pack_dir($repo);
    write_file_atomic($dir . '/pack-' . $hash . '.pack', $pd, 0444);
    write_file_atomic($dir . '/pack-' . $hash . '.idx', $writer->buildIdx($pd, $offsets), 0444);
    write_file_atomic($dir . '/pack-' . $hash . '.rev', $writer->buildRev($pd, $offsets), 0444);
    // a pack fetched from a promisor remote is marked, so fsck knows the
    // objects it does not contain are allowed to be missing
    if ($promisor) write_file_atomic($dir . '/pack-' . $hash . '.promisor', '', 0444);
    $repo->odb->resetPacks();
    return count($objects);
}

function cmd_clone(array $argv): int {
    global $GIT_OPTS;
    $bare = false; $mirror = false; $origin = 'origin'; $branch = null; $noCheckout = false;
    $quiet = false; $depth = null; $filter = null; $separate = null; $args = []; $local = null;
    $bundleUri = null; $refFormat = null;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--bare': $bare = true; break;
            case $a === '--mirror': $mirror = true; $bare = true; break;
            case $a === '-o' || $a === '--origin': $origin = $argv[++$i] ?? 'origin'; break;
            case strncmp($a, '--origin=', 9) === 0: $origin = substr($a, 9); break;
            case $a === '-b' || $a === '--branch': $branch = $argv[++$i] ?? null; break;
            case strncmp($a, '--branch=', 9) === 0: $branch = substr($a, 9); break;
            case $a === '-n' || $a === '--no-checkout': $noCheckout = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '-v' || $a === '--verbose' || $a === '--progress': break;
            case $a === '--local' || $a === '-l' || $a === '--no-hardlinks' || $a === '-s' || $a === '--shared': break;
            case $a === '--no-local': $local = false; break;
            case strncmp($a, '--depth', 7) === 0: $depth = (int)(strpos($a, '=') !== false ? substr($a, 8) : ($argv[++$i] ?? '0')); break;
            case strncmp($a, '--filter=', 9) === 0: $filter = substr($a, 9); break;
            case strncmp($a, '--bundle-uri=', 13) === 0: $bundleUri = substr($a, 13); break;
            case strncmp($a, '--separate-git-dir=', 19) === 0: $separate = substr($a, 19); break;
            case strncmp($a, '--ref-format=', 13) === 0: $refFormat = substr($a, 13); break;
            case strncmp($a, '--upload-pack', 13) === 0 || strncmp($a, '-u', 2) === 0: break;
            case strncmp($a, '--config=', 9) === 0 || $a === '-c': break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $args[] = $a;
        }
    }
    if (!$args) usage_die('usage: git clone [<options>] [--] <repo> [<dir>]', 129);
    $url = $args[0];
    $configUrl = $url;
    // `git init` moves us into the new repository, so anchor a local source now.
    // git records <cwd>/<url as given> for a relative local path, unnormalised.
    $bundleSource = null;
    if (!preg_match('#^[a-z][a-z0-9+.-]*://#', $url) && !preg_match('#^[^/]+@[^/:]+:#', $url)) {
        $abs = path_absolute($url);
        if (is_dir($abs) || is_dir($abs . '/.git') || is_dir($abs . '.git')) {
            if ($url[0] !== '/') $configUrl = rtrim(getcwd(), '/') . '/' . $url;
            $url = $abs;
        } elseif (is_file($abs) && function_exists('bundle_read_header')
                  && bundle_read_header($abs) !== null) {
            // git-clone(1): <repository> may be a bundle file
            $bundleSource = $abs;
            if ($url[0] !== '/') $configUrl = rtrim(getcwd(), '/') . '/' . $url;
            $url = $abs;
        } elseif (!file_exists($abs)) {
            fwrite(STDERR, "fatal: repository '" . $args[0] . "' does not exist\n");
            return 128;
        }
    }
    $dir = $args[1] ?? preg_replace('/\.git$/', '', basename(rtrim($url, '/')));
    if ($dir === '' || $dir === '.') $dir = basename(rtrim($url, '/'));
    if (is_dir($dir) && (count(scandir($dir)) > 2)) {
        die_git("destination path '$dir' already exists and is not an empty directory.");
    }
    if (!$quiet) fwrite(STDERR, "Cloning into '" . ($bare ? rtrim($dir, '/') : $dir) . "'...\n");
    mkdir_p($dir);
    $gitdir = $bare ? path_absolute($dir) : path_absolute($dir) . '/.git';
    $worktree = $bare ? null : path_absolute($dir);
    $initArgs = ['-q'];
    if ($bare) $initArgs[] = '--bare';
    if ($refFormat !== null) $initArgs[] = '--ref-format=' . $refFormat;
    $initArgs[] = $dir;
    $prevGitDir = $GIT_OPTS['git_dir'];
    $GIT_OPTS['git_dir'] = null;
    cmd_init($initArgs);
    $repo = Repo::open($gitdir, $worktree);
    $cf = ConfigFile::load($repo->commonPath('config'));
    $cf->setValue('remote', $origin, 'url', $configUrl);
    if ($mirror) {
        $cf->setValue('remote', $origin, 'fetch', '+refs/*:refs/*');
        $cf->setValue('remote', $origin, 'mirror', 'true');
    } elseif ($bare) {
        // a bare clone keeps branches where they are, with no remote-tracking refs
        $cf->setValue('remote', $origin, 'fetch', '+refs/heads/*:refs/heads/*');
    } else {
        $cf->setValue('remote', $origin, 'fetch', "+refs/heads/*:refs/remotes/$origin/*");
    }
    if ($bare) $cf->setValue('core', null, 'bare', 'true');
    $cf->save();
    $repo->config->addString($cf->text, $repo->commonPath('config'));

    if ($bundleSource !== null) {
        return clone_from_bundle($repo, $bundleSource, $origin, $branch, $bare,
                                 $noCheckout, $quiet, $mirror, $url);
    }
    $t = Transport::open($repo, resolve_remote_url($repo, $url));
    $version = negotiated_protocol_version($repo);
    $c = $t->connect('git-upload-pack', $version);
    $refs = ($version >= 2) ? v2_ls_refs($t, $c, true) : $t->readAdvertisement($c);
    $wants = [];
    foreach ($refs as $name => $oid) {
        if (strncmp($name, 'refs/heads/', 11) === 0 || strncmp($name, 'refs/tags/', 10) === 0
            || ($mirror && strncmp($name, 'refs/', 5) === 0)) {
            if (substr($name, -3) === '^{}') continue;
            $wants[$oid] = true;
        }
    }
    if (!$wants) {
        if (!$quiet) fwrite(STDERR, "warning: You appear to have cloned an empty repository.\n");
        $c->close();
        clone_finish_head($repo, $refs, $t, $origin, $branch, $bare, $noCheckout, $quiet);
        return 0;
    }
    $opts = [];
    if ($filter !== null) $opts['filter'] = $filter;
    $packData = ($version >= 2)
        ? fetch_pack_v2($repo, $t, $c, array_keys($wants), [], $opts)
        : fetch_pack_v0($repo, $t, $c, array_keys($wants), [], $opts);
    $c->close();
    store_pack($repo, $packData, $filter !== null);
    if ($filter !== null) {
        $cf = ConfigFile::load($repo->commonPath('config'));
        $cf->setValue('core', null, 'repositoryformatversion', '1');
        $cf->setValue('remote', $origin, 'promisor', 'true');
        $cf->setValue('remote', $origin, 'partialclonefilter', $filter);
        $cf->save();
        // the checkout below reads blobs the filter left out, so the promisor
        // configuration has to be live before it runs
        $repo->config->addString($cf->text, $repo->commonPath('config'));
    }
    // remote-tracking refs
    foreach ($refs as $name => $oid) {
        if (substr($name, -3) === '^{}') continue;
        if (strncmp($name, 'refs/heads/', 11) === 0) {
            $target = ($mirror || $bare) ? $name : 'refs/remotes/' . $origin . '/' . substr($name, 11);
            $repo->refs->writeRef($target, $oid, null, null, "clone: from $url", false);
        } elseif (strncmp($name, 'refs/tags/', 10) === 0) {
            $repo->refs->writeRef($name, $oid, null, null, "clone: from $url", false);
        } elseif ($mirror && strncmp($name, 'refs/', 5) === 0) {
            $repo->refs->writeRef($name, $oid, null, null, "clone: from $url", false);
        }
    }
    clone_finish_head($repo, $refs, $t, $origin, $branch, $bare, $noCheckout, $quiet);
    return 0;
}

function clone_finish_head(Repo $repo, array $refs, Transport $t, string $origin, ?string $branch, bool $bare, bool $noCheckout, bool $quiet) {
    $headRef = null;
    if ($branch !== null) {
        $headRef = 'refs/heads/' . $branch;
        if (!isset($refs[$headRef])) die_git("Remote branch $branch not found in upstream $origin");
    } else {
        $sym = $t->symrefHead();
        if ($sym !== null && isset($refs[$sym])) $headRef = $sym;
        elseif (isset($refs['HEAD'])) {
            foreach ($refs as $name => $oid) {
                if (strncmp($name, 'refs/heads/', 11) === 0 && $oid === $refs['HEAD']) { $headRef = $name; break; }
            }
        }
    }
    if ($headRef === null) {
        foreach (['refs/heads/main', 'refs/heads/master'] as $cand) {
            if (isset($refs[$cand])) { $headRef = $cand; break; }
        }
    }
    if ($headRef === null) {
        foreach ($refs as $name => $oid) {
            if (strncmp($name, 'refs/heads/', 11) === 0) { $headRef = $name; break; }
        }
    }
    if ($headRef === null) return;
    $short = substr($headRef, strlen('refs/heads/'));
    $oid = $refs[$headRef] ?? null;
    if ($oid === null) return;
    if ($bare) {
        $repo->refs->writeRef($headRef, $oid, null, null, '', false);
        $repo->refs->writeRef('HEAD', null, $headRef, null, '', false);
        return;
    }
    $repo->refs->writeRef($headRef, $oid, null, null, "clone: from origin", false);
    $repo->refs->writeRef('HEAD', null, $headRef, null, '', false);
    // git records where the remote's own HEAD pointed, as a symbolic
    // remote-tracking ref (git-clone(1))
    $short0 = substr($headRef, strlen('refs/heads/'));
    $repo->refs->writeRef('refs/remotes/' . $origin . '/HEAD', null,
                          'refs/remotes/' . $origin . '/' . $short0, null, '', false);
    $cf = ConfigFile::load($repo->commonPath('config'));
    $cf->setValue('branch', $short, 'remote', $origin);
    $cf->setValue('branch', $short, 'merge', $headRef);
    $cf->save();
    if ($noCheckout) return;
    $tree = peel_to_tree($repo, $oid);
    if ($tree === null) return;
    $flat = [];
    tree_flatten($repo, $tree, '', $flat);
    $idx = $repo->index();
    $idx->entries = [];
    $idx->reindex();
    foreach ($flat as $p => $v) {
        $e = new IndexEntry();
        $e->path = $p; $e->mode = octdec($v[0]); $e->oid = $v[1];
        $idx->entries[] = $e;
    }
    $idx->sort();
    foreach ($idx->sorted() as $e) wt_checkout_entry($repo, $e);
    $idx->write();
}

function cmd_fetch(array $argv): int {
    $repo = need_repo();
    $all = false; $prune = false; $tags = false; $quiet = false; $args = []; $depth = null;
    $filter = null; $force = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--all': $all = true; break;
            case $a === '-p' || $a === '--prune': $prune = true; break;
            case $a === '-t' || $a === '--tags': $tags = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '-f' || $a === '--force': $force = true; break;
            case $a === '-v' || $a === '--verbose' || $a === '--progress' || $a === '--no-progress': break;
            case strncmp($a, '--filter=', 9) === 0: $filter = substr($a, 9); break;
            case strncmp($a, '--depth', 7) === 0: break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $args[] = $a;
        }
    }
    $remote = $args[0] ?? 'origin';
    $refspecs = array_slice($args, 1);
    $url = resolve_remote_url($repo, $remote);
    $t = Transport::open($repo, $url);
    $version = negotiated_protocol_version($repo);
    $c = $t->connect('git-upload-pack', $version);
    $refs = ($version >= 2) ? v2_ls_refs($t, $c, true) : $t->readAdvertisement($c);
    $specs = $refspecs;
    if (!$specs) {
        $configured = $repo->config->getAll("remote.$remote.fetch");
        foreach ($configured as $s) if (!is_bool($s)) $specs[] = $s;
        if (!$specs) $specs[] = '+refs/heads/*:refs/remotes/' . $remote . '/*';
    }
    $updates = [];
    $wants = [];
    foreach ($specs as $spec) {
        $forceSpec = false;
        if ($spec !== '' && $spec[0] === '+') { $forceSpec = true; $spec = substr($spec, 1); }
        $colon = strrpos($spec, ':');
        $src = $colon === false ? $spec : substr($spec, 0, $colon);
        $dst = $colon === false ? '' : substr($spec, $colon + 1);
        foreach ($refs as $name => $oid) {
            if (substr($name, -3) === '^{}') continue;
            $target = null;
            if (strpos($src, '*') !== false) {
                $prefix = substr($src, 0, strpos($src, '*'));
                if (strncmp($name, $prefix, strlen($prefix)) !== 0) continue;
                $rest = substr($name, strlen($prefix));
                $target = $dst === '' ? '' : str_replace('*', $rest, $dst);
            } else {
                $cands = [$src, 'refs/heads/' . $src, 'refs/tags/' . $src, 'refs/' . $src];
                if (!in_array($name, $cands, true)) continue;
                $target = $dst;
                if ($target !== '' && strpos($target, 'refs/') !== 0) $target = 'refs/heads/' . $target;
            }
            $wants[$oid] = true;
            if ($target !== null && $target !== '') $updates[$target] = $oid;
        }
    }
    if ($tags) {
        foreach ($refs as $name => $oid) {
            if (strncmp($name, 'refs/tags/', 10) !== 0 || substr($name, -3) === '^{}') continue;
            $wants[$oid] = true;
            $updates[$name] = $oid;
        }
    }
    $needed = [];
    foreach (array_keys($wants) as $w) if (!$repo->odb->has($w)) $needed[] = $w;
    $haves = [];
    foreach ($repo->refs->listRefs('refs/') as $n => $o) $haves[$o] = true;
    $h = $repo->refs->resolve('HEAD');
    if ($h !== null) $haves[$h] = true;
    $fetched = 0;
    if ($needed) {
        $opts = [];
        if ($filter !== null) $opts['filter'] = $filter;
        $packData = ($version >= 2)
            ? fetch_pack_v2($repo, $t, $c, $needed, array_keys($haves), $opts)
            : fetch_pack_v0($repo, $t, $c, $needed, array_keys($haves), $opts);
        $fetched = store_pack($repo, $packData);
    }
    $c->close();
    $fetchHead = '';
    foreach ($updates as $target => $oid) {
        $old = $repo->refs->resolve($target);
        if ($old !== $oid) {
            $repo->refs->writeRef($target, $oid, null, null, "fetch $remote", false);
        }
    }
    foreach ($refs as $name => $oid) {
        if (substr($name, -3) === '^{}' || $name === 'HEAD') continue;
        $branch = strncmp($name, 'refs/heads/', 11) === 0 ? substr($name, 11) : $name;
        $fetchHead .= $oid . "\t\tbranch '" . $branch . "' of " . $url . "\n";
    }
    write_file_atomic($repo->path('FETCH_HEAD'), $fetchHead);
    return 0;
}

function cmd_push(array $argv): int {
    $repo = need_repo();
    $args = []; $force = false; $all = false; $tags = false; $delete = false;
    $setUpstream = false; $quiet = false; $mirror = false; $dryRun = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-f' || $a === '--force': $force = true; break;
            case $a === '--all': $all = true; break;
            case $a === '--tags': $tags = true; break;
            case $a === '--mirror': $mirror = true; break;
            case $a === '-d' || $a === '--delete': $delete = true; break;
            case $a === '-u' || $a === '--set-upstream': $setUpstream = true; break;
            case $a === '-q' || $a === '--quiet': $quiet = true; break;
            case $a === '-n' || $a === '--dry-run': $dryRun = true; break;
            case $a === '-v' || $a === '--verbose' || $a === '--porcelain' || $a === '--progress': break;
            case strncmp($a, '--receive-pack', 14) === 0: break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-': break;
            default: $args[] = $a;
        }
    }
    $remote = $args[0] ?? 'origin';
    $refspecs = array_slice($args, 1);
    $url = resolve_remote_url($repo, $remote);
    $t = Transport::open($repo, $url);
    $c = $t->connect('git-receive-pack', 0);
    $remoteRefs = $t->readAdvertisement($c);
    $null = str_repeat('0', $repo->hexsz);
    if (!$refspecs) {
        if ($all || $mirror) {
            foreach ($repo->refs->listRefs('refs/heads/') as $n => $o) $refspecs[] = $n . ':' . $n;
        } else {
            $head = $repo->refs->readRaw('HEAD');
            if ($head === null || $head[1] === null) die_git('You are not currently on a branch.');
            $short = substr($head[1], strlen('refs/heads/'));
            $merge = $repo->config->get("branch.$short.merge", null);
            $dst = ($merge !== null && !is_bool($merge)) ? $merge : $head[1];
            $refspecs[] = $head[1] . ':' . $dst;
        }
    }
    if ($tags) foreach ($repo->refs->listRefs('refs/tags/') as $n => $o) $refspecs[] = $n . ':' . $n;
    $commands = [];
    $pushSources = [];
    $wants = [];
    foreach ($refspecs as $spec) {
        $f = $force;
        if ($spec !== '' && $spec[0] === '+') { $f = true; $spec = substr($spec, 1); }
        $colon = strpos($spec, ':');
        $src = $colon === false ? $spec : substr($spec, 0, $colon);
        $dst = $colon === false ? $spec : substr($spec, $colon + 1);
        if ($delete || $src === '') {
            $dstFull = strncmp($dst, 'refs/', 5) === 0 ? $dst : 'refs/heads/' . $dst;
            $old = $remoteRefs[$dstFull] ?? $null;
            $commands[] = [$old, $null, $dstFull];
            $pushSources[] = $dst;
            continue;
        }
        $srcOid = rev_parse_object($repo, $src);
        if ($srcOid === null) die_git("src refspec $src does not match any");
        $dstFull = $dst;
        if (strncmp($dstFull, 'refs/', 5) !== 0) {
            $r = resolve_ref_dwim($repo, $src);
            $dstFull = (strncmp($dst, 'refs/', 5) === 0) ? $dst : 'refs/heads/' . preg_replace('#^refs/heads/#', '', $dst);
        }
        $old = $remoteRefs[$dstFull] ?? $null;
        if ($old === $srcOid) continue;
        if ($old !== $null && !$f) {
            if (!is_ancestor($repo, $old, $srcOid)) {
                fwrite(STDERR, " ! [rejected]        " . $src . " -> " . preg_replace('#^refs/heads/#', '', $dstFull) . " (non-fast-forward)\n");
                fwrite(STDERR, "error: failed to push some refs to '" . $url . "'\n");
                return 1;
            }
        }
        $commands[] = [$old, $srcOid, $dstFull];
        $pushSources[] = $src;
        $wants[$srcOid] = true;
    }
    if (!$commands) {
        if (!$quiet) fwrite(STDERR, "Everything up-to-date\n");
        $c->close();
        return 0;
    }
    if ($dryRun) { $c->close(); return 0; }
    $haves = [];
    foreach ($remoteRefs as $n => $o) if ($repo->odb->has($o)) $haves[] = $o;
    $objects = fetch_object_set($repo, array_keys($wants), $haves);
    $caps = ['report-status', 'side-band-64k', 'ofs-delta', 'agent=' . git_agent()];
    $buf = fopen('php://memory', 'r+b');
    $first = true;
    foreach ($commands as [$old, $new, $ref]) {
        pkt_write($buf, "$old $new $ref" . ($first ? "\0" . implode(' ', $caps) : '') . "\n");
        $first = false;
    }
    pkt_flush($buf);
    if ($objects) {
        $writer = new PackWriter($repo);
        [$packData, $offsets] = $writer->build($objects);
        fwrite($buf, $packData);
    }
    rewind($buf);
    $request = stream_get_contents($buf);
    if ($t instanceof HttpTransport) {
        $resp = $t->post('git-receive-pack', $request);
        $in = $resp;
    } else {
        fwrite($c->out, $request);
        $c->shutdownWrite();
        $in = $c->in;
    }
    $status = read_push_report($in);
    $c->close();
    $failed = false;
    foreach ($status['refs'] as $ref => $res) {
        if ($res !== 'ok') {
            fwrite(STDERR, " ! [remote rejected] " . $ref . " (" . $res . ")\n");
            $failed = true;
        }
    }
    if (!$quiet && !$failed) {
        fwrite(STDERR, "To " . $url . "\n");
        foreach ($commands as $i => [$old, $new, $ref]) {
            $short = preg_replace('#^refs/(heads|tags)/#', '', $ref);
            $srcShort = preg_replace('#^refs/(heads|tags)/#', '', $pushSources[$i] ?? $short);
            if ($new === $null) {
                fwrite(STDERR, " - [deleted]         " . $short . "\n");
            } elseif ($old === $null) {
                $kind = strncmp($ref, 'refs/tags/', 10) === 0 ? 'new tag' : 'new branch';
                fwrite(STDERR, ' * ' . str_pad('[' . $kind . ']', 18) . $srcShort . ' -> ' . $short . "\n");
            } elseif (is_ancestor($repo, $old, $new)) {
                fwrite(STDERR, "   " . substr($old, 0, 7) . ".." . substr($new, 0, 7) . "  " . $srcShort . " -> " . $short . "\n");
            } else {
                fwrite(STDERR, " + " . substr($old, 0, 7) . "..." . substr($new, 0, 7) . " " . $srcShort . " -> " . $short . " (forced update)\n");
            }
        }
    }
    return $failed ? 1 : 0;
}

function read_push_report($in): array {
    $res = ['unpack' => '', 'refs' => []];
    $payload = '';
    while (true) {
        $line = pkt_read($in);
        if ($line === false || $line === null) break;
        if (is_int($line)) continue;
        if ($line !== '' && (ord($line[0]) === 1 || ord($line[0]) === 2 || ord($line[0]) === 3)) {
            $band = ord($line[0]);
            if ($band === 1) $payload .= substr($line, 1);
            elseif ($band === 2) fwrite(STDERR, substr($line, 1));
            elseif ($band === 3) fwrite(STDERR, substr($line, 1));
            continue;
        }
        $payload .= pkt_line($line);
    }
    if ($payload !== '') {
        $mem = fopen('php://memory', 'r+b');
        fwrite($mem, $payload);
        rewind($mem);
        while (true) {
            $line = pkt_read($mem);
            if ($line === false || $line === null) break;
            if (is_int($line)) continue;
            $line = rtrim($line, "\n");
            if (strncmp($line, 'unpack ', 7) === 0) { $res['unpack'] = substr($line, 7); continue; }
            if (strncmp($line, 'ok ', 3) === 0) { $res['refs'][substr($line, 3)] = 'ok'; continue; }
            if (strncmp($line, 'ng ', 3) === 0) {
                $rest = substr($line, 3);
                $sp = strpos($rest, ' ');
                $res['refs'][$sp === false ? $rest : substr($rest, 0, $sp)] = $sp === false ? 'rejected' : substr($rest, $sp + 1);
            }
        }
    }
    return $res;
}

function cmd_remote(array $argv): int {
    $repo = need_repo();
    $verbose = in_array('-v', $argv, true) || in_array('--verbose', $argv, true);
    $args = array_values(array_filter($argv, function ($a) { return $a !== '-v' && $a !== '--verbose'; }));
    $sub = $args[0] ?? 'show';
    if ($sub === 'add') {
        $name = $args[1] ?? ''; $url = $args[2] ?? '';
        $cf = ConfigFile::load($repo->commonPath('config'));
        $cf->setValue('remote', $name, 'url', $url);
        $cf->setValue('remote', $name, 'fetch', "+refs/heads/*:refs/remotes/$name/*");
        $cf->save();
        return 0;
    }
    if ($sub === 'remove' || $sub === 'rm') {
        $name = $args[1] ?? '';
        $cf = ConfigFile::load($repo->commonPath('config'));
        $cf->removeSection('remote', $name);
        $cf->save();
        return 0;
    }
    if ($sub === 'set-url') {
        $name = $args[1] ?? ''; $url = $args[2] ?? '';
        $cf = ConfigFile::load($repo->commonPath('config'));
        $cf->setValue('remote', $name, 'url', $url);
        $cf->save();
        return 0;
    }
    if ($sub === 'get-url') {
        $name = $args[1] ?? '';
        $u = $repo->config->get("remote.$name.url", null);
        if ($u === null || is_bool($u)) { fwrite(STDERR, "error: No such remote '$name'\n"); return 2; }
        out($u . "\n");
        return 0;
    }
    foreach ($repo->config->subsections('remote') as $name) {
        if ($verbose) {
            $u = $repo->config->get("remote.$name.url", '');
            out("$name\t$u (fetch)\n$name\t$u (push)\n");
        } else {
            out($name . "\n");
        }
    }
    return 0;
}

<?php
// Wire protocol version 2 (gitprotocol-v2(5)): the capability advertisement,
// the `ls-refs` and `fetch` commands on the server side, and the client side
// that drives them.  Version 0/1 is untouched and still used whenever the peer
// does not ask for 2.

function v2_agent(): string { return git_agent(); }

/**
 * The capability advertisement a v2 server sends before its first command.
 * `filter` appears inside the `fetch` value only when the repository allows it,
 * exactly as uploadpack.allowFilter controls it in git.
 */
function v2_capabilities(Repo $repo): array {
    $fetch = ['shallow', 'wait-for-done'];
    if ($repo->config->getBool('uploadpack.allowfilter', false)) $fetch[] = 'filter';
    $caps = [
        'version 2',
        'agent=' . v2_agent(),
        'ls-refs=unborn',
        'fetch=' . implode(' ', $fetch),
        'server-option',
        'object-format=sha1',
    ];
    // bundle_uri_advertise() in the pinned git defaults to 0 and consults exactly
    // one key, uploadpack.advertisebundleuris; the bundle.* values are what the
    // bundle-uri COMMAND reports, not what triggers the advertisement.
    // getBool(), not config_bool(get(...)): get() returns null for an absent key
    // and config_bool() would then hand null to trim(), which PHP 8.2 deprecates
    // -- the notice lands on stderr and corrupts every compared v2 session that
    // has no bundle config at all.
    if ($repo->config->getBool('uploadpack.advertisebundleuris', false)) $caps[] = 'bundle-uri';
    return $caps;
}

function write_v2_capability_advertisement($out, Repo $repo) {
    foreach (v2_capabilities($repo) as $c) pkt_write($out, $c . "\n");
    pkt_flush($out);
}

/** bundle.* configuration, as the bundle-uri command reports it. */
function v2_bundle_uris(Repo $repo): array {
    $rows = [];
    foreach ($repo->config->allKeys() as $key) {
        if (strncmp($key, 'bundle.', 7) !== 0) continue;
        $value = $repo->config->get($key, null);
        if (is_bool($value)) $value = $value ? 'true' : 'false';
        if ($value === null) continue;
        $rows[$key] = $value;
    }
    return $rows;
}

/**
 * Read one v2 command request: the command name, the client capabilities before
 * the delimiter, and the argument lines after it.  Returns null at end of input.
 */
function v2_read_request($in): ?array {
    $command = null; $caps = []; $args = [];
    $inArgs = false;
    while (true) {
        $line = pkt_read($in);
        if ($line === false) return null;
        if ($line === null) break;                       // flush: request complete
        if ($line === 1) { $inArgs = true; continue; }   // delim
        if ($line === 2) continue;
        $line = rtrim($line, "\n");
        if ($inArgs) { $args[] = $line; continue; }
        if (strncmp($line, 'command=', 8) === 0) { $command = substr($line, 8); continue; }
        if ($line !== '') $caps[] = $line;
    }
    if ($command === null && !$args && !$caps) return null;
    return [$command, $caps, $args];
}

function upload_pack_v2_serve(Repo $repo, $in, $out, bool $statelessRpc = false) {
    while (true) {
        $req = v2_read_request($in);
        if ($req === null) return 0;
        [$command, $caps, $args] = $req;
        if ($command === 'ls-refs') {
            v2_serve_ls_refs($repo, $out, $args);
        } elseif ($command === 'fetch') {
            v2_serve_fetch($repo, $in, $out, $args);
        } elseif ($command === 'bundle-uri') {
            v2_serve_bundle_uri($repo, $out);
        } elseif ($command === 'object-info') {
            v2_serve_object_info($repo, $out, $args);
        } else {
            fwrite(STDERR, "fatal: invalid command '" . (string)$command . "'\n");
            return 128;
        }
        fflush($out);
        if ($statelessRpc) return 0;
    }
}

function v2_serve_ls_refs(Repo $repo, $out, array $args) {
    $peel = false; $symrefs = false; $unborn = false; $prefixes = [];
    foreach ($args as $a) {
        if ($a === 'peel') { $peel = true; continue; }
        if ($a === 'symrefs') { $symrefs = true; continue; }
        if ($a === 'unborn') { $unborn = true; continue; }
        if (strncmp($a, 'ref-prefix ', 11) === 0) { $prefixes[] = substr($a, 11); continue; }
    }
    $match = function (string $name) use ($prefixes): bool {
        if (!$prefixes) return true;
        foreach ($prefixes as $p) if (strncmp($name, $p, strlen($p)) === 0) return true;
        return false;
    };
    $emit = function (string $oid, string $name) use ($out, $peel, $symrefs, $repo) {
        $line = $oid . ' ' . $name;
        if ($symrefs) {
            $raw = $repo->refs->readRaw($name);
            $target = $raw === null ? null : $raw[1];
            if ($target !== null) $line .= ' symref-target:' . $target;
        }
        if ($peel) {
            $hdr = $repo->odb->readHeader($oid);
            if ($hdr !== null && $hdr[0] === 'tag') {
                $p = peel_object($repo, $oid);
                if ($p !== null && $p !== $oid) $line .= ' peeled:' . $p;
            }
        }
        pkt_write($out, $line . "\n");
    };
    $head = $repo->refs->resolve('HEAD');
    if ($match('HEAD')) {
        if ($head !== null) {
            $emit($head, 'HEAD');
        } elseif ($unborn) {
            $raw = $repo->refs->readRaw('HEAD');
            $target = $raw === null ? null : $raw[1];
            pkt_write($out, 'unborn HEAD' . ($target !== null ? ' symref-target:' . $target : '') . "\n");
        }
    }
    foreach ($repo->refs->listRefs('refs/') as $name => $oid) {
        if (!$match($name)) continue;
        $emit($oid, $name);
    }
    pkt_flush($out);
}

function v2_serve_bundle_uri(Repo $repo, $out) {
    foreach (v2_bundle_uris($repo) as $key => $value) {
        pkt_write($out, $key . '=' . $value . "\n");
    }
    pkt_flush($out);
}

function v2_serve_object_info(Repo $repo, $out, array $args) {
    $wantSize = false; $oids = [];
    foreach ($args as $a) {
        if ($a === 'size') { $wantSize = true; continue; }
        if (strncmp($a, 'oid ', 4) === 0) { $oids[] = substr($a, 4); continue; }
    }
    if ($wantSize) pkt_write($out, "size\n");
    foreach ($oids as $oid) {
        $hdr = $repo->odb->readHeader($oid);
        pkt_write($out, $oid . ' ' . ($hdr === null ? '' : $hdr[1]) . "\n");
    }
    pkt_flush($out);
}

/**
 * The v2 `fetch` command.  One request may be answered either with an
 * `acknowledgments` section alone (the client then sends another round) or with
 * `acknowledgments` + `packfile`, and a request carrying `done` goes straight
 * to the pack.
 */
function v2_serve_fetch(Repo $repo, $in, $out, array $args) {
    $wants = []; $haves = []; $wantRefs = []; $done = false;
    $ofsDelta = false; $includeTag = false; $filter = null;
    $deepen = null; $waitForDone = false;
    foreach ($args as $a) {
        if (strncmp($a, 'want ', 5) === 0) { $wants[] = trim(substr($a, 5)); continue; }
        if (strncmp($a, 'have ', 5) === 0) { $haves[] = trim(substr($a, 5)); continue; }
        if (strncmp($a, 'want-ref ', 9) === 0) { $wantRefs[] = trim(substr($a, 9)); continue; }
        if (strncmp($a, 'filter ', 7) === 0) { $filter = substr($a, 7); continue; }
        if (strncmp($a, 'deepen', 6) === 0) { $deepen = $a; continue; }
        if ($a === 'done') { $done = true; continue; }
        if ($a === 'ofs-delta') { $ofsDelta = true; continue; }
        if ($a === 'include-tag') { $includeTag = true; continue; }
        if ($a === 'wait-for-done') { $waitForDone = true; continue; }
    }
    if ($deepen !== null) {
        pkt_write($out, "ERR shallow fetch is not supported by this server\n");
        return;
    }
    foreach ($wantRefs as $ref) {
        $oid = $repo->refs->resolve($ref);
        if ($oid === null) { pkt_write($out, "ERR unknown ref " . $ref . "\n"); return; }
        $wants[] = $oid;
    }
    $common = [];
    foreach ($haves as $h) if ($repo->odb->has($h)) $common[] = $h;
    $ready = $done || (!$waitForDone && $common !== []);
    if (!$done || $common) {
        pkt_write($out, "acknowledgments\n");
        if (!$common) {
            pkt_write($out, "NAK\n");
        } else {
            foreach ($common as $oid) pkt_write($out, 'ACK ' . $oid . "\n");
            if ($ready) pkt_write($out, "ready\n");
        }
        if (!$ready) { pkt_flush($out); return; }
        fwrite($out, PKT_DELIM);
    }
    if ($wantRefs) {
        pkt_write($out, "wanted-refs\n");
        foreach ($wantRefs as $ref) {
            pkt_write($out, $repo->refs->resolve($ref) . ' ' . $ref . "\n");
        }
        fwrite($out, PKT_DELIM);
    }
    pkt_write($out, "packfile\n");
    $objects = fetch_object_set($repo, $wants, $common, $filter);
    $writer = new PackWriter($repo);
    [$packData, ] = $writer->build($objects, $ofsDelta);
    $sb = new SideBand($out, true);
    $sb->data($packData);
    pkt_flush($out);
}

// ---------------------------------------------------------------- client side

/** Read the server's v2 capability advertisement into name => value. */
function v2_read_capabilities($in): array {
    $caps = [];
    while (true) {
        $line = pkt_read($in);
        if ($line === false || $line === null) break;
        if (is_int($line)) continue;
        $line = rtrim($line, "\n");
        $eq = strpos($line, '=');
        if ($eq === false) $caps[$line] = true;
        else $caps[substr($line, 0, $eq)] = substr($line, $eq + 1);
    }
    return $caps;
}

function v2_write_command($out, string $command, array $args) {
    pkt_write($out, 'command=' . $command . "\n");
    pkt_write($out, 'agent=' . v2_agent() . "\n");
    pkt_write($out, "object-format=sha1\n");
    fwrite($out, PKT_DELIM);
    foreach ($args as $a) pkt_write($out, $a . "\n");
    pkt_flush($out);
    fflush($out);
}

/** Build one v2 command request and return a stream holding the response. */
function v2_request(Transport $t, RemoteConn $c, string $command, array $args) {
    if (!$c->v2CapsRead) { $c->v2Caps = v2_read_capabilities($c->in); $c->v2CapsRead = true; }
    $buf = fopen('php://memory', 'r+b');
    pkt_write($buf, 'command=' . $command . "\n");
    pkt_write($buf, 'agent=' . v2_agent() . "\n");
    pkt_write($buf, "object-format=sha1\n");
    fwrite($buf, PKT_DELIM);
    foreach ($args as $a) pkt_write($buf, $a . "\n");
    pkt_flush($buf);
    rewind($buf);
    $body = stream_get_contents($buf);
    fclose($buf);
    return $t->sendV2($c, $body);
}

/**
 * `ls-refs` as a client.  Returns refname => oid in the order the server listed
 * them, with peeled tags as `<ref>^{}` entries; symref targets land in
 * $c->symrefs.
 */
function v2_ls_refs(Transport $t, RemoteConn $c, bool $symrefs = true, array $prefixes = []): array {
    $args = ['peel'];
    if ($symrefs) $args[] = 'symrefs';
    $args[] = 'unborn';
    foreach ($prefixes as $p) $args[] = 'ref-prefix ' . $p;
    $in = v2_request($t, $c, 'ls-refs', $args);
    $refs = []; $sym = []; $peeled = [];
    while (true) {
        $line = pkt_read($in);
        if ($line === false || $line === null) break;
        if (is_int($line)) continue;
        $line = rtrim($line, "\n");
        if ($line === '') continue;
        if (strncmp($line, 'unborn ', 7) === 0) {
            if (preg_match('/^(\S+).*symref-target:(\S+)/', substr($line, 7), $m)) $sym[$m[1]] = $m[2];
            continue;
        }
        $parts = explode(' ', $line);
        $oid = array_shift($parts);
        $name = array_shift($parts);
        if ($name === null) continue;
        $refs[$name] = $oid;
        foreach ($parts as $attr) {
            if (strncmp($attr, 'symref-target:', 14) === 0) $sym[$name] = substr($attr, 14);
            if (strncmp($attr, 'peeled:', 7) === 0) $peeled[$name] = substr($attr, 7);
        }
    }
    foreach ($peeled as $name => $oid) $refs[$name . '^{}'] = $oid;
    $c->symrefs = $sym;
    $t->advertisedRefs = $refs;
    return $refs;
}

/** `fetch` as a client: returns the raw packfile bytes. */
function v2_fetch(Transport $t, RemoteConn $c, array $wants, array $haves,
                  ?string $filter = null, bool $includeTag = false): string {
    $args = [];
    foreach ($wants as $w) $args[] = 'want ' . $w;
    foreach (array_slice($haves, 0, 256) as $h) $args[] = 'have ' . $h;
    $args[] = 'thin-pack';
    $args[] = 'ofs-delta';
    $args[] = 'no-progress';
    if ($includeTag) $args[] = 'include-tag';
    if ($filter !== null) $args[] = 'filter ' . $filter;
    $args[] = 'done';
    $in = v2_request($t, $c, 'fetch', $args);
    $pack = '';
    $section = null;
    while (true) {
        $line = pkt_read($in);
        if ($line === false || $line === null) break;
        if (is_int($line)) { $section = null; continue; }
        if ($section === null) { $section = rtrim($line, "\n"); continue; }
        if (strncmp($line, 'ERR ', 4) === 0) die_git(rtrim(substr($line, 4), "\n"));
        if ($section === 'packfile') {
            $band = ord($line[0]);
            $payload = substr($line, 1);
            if ($band === 1) $pack .= $payload;
            elseif ($band === 3) die_git(rtrim($payload, "\n"));
            elseif ($band === 2) fwrite(STDERR, $payload);
        }
    }
    return $pack;
}

/**
 * The client half of `fetch`, with the signature the porcelain already uses:
 * returns the raw packfile bytes.
 */
function fetch_pack_v2(Repo $repo, Transport $t, RemoteConn $c, array $wants,
                       array $haves, array $opts = []): string {
    $args = [];
    foreach ($wants as $w) $args[] = 'want ' . $w;
    foreach (array_slice($haves, 0, 256) as $h) $args[] = 'have ' . $h;
    $args[] = 'thin-pack';
    $args[] = 'ofs-delta';
    $args[] = 'no-progress';
    if (!empty($opts['include_tag'])) $args[] = 'include-tag';
    if (!empty($opts['filter'])) $args[] = 'filter ' . $opts['filter'];
    foreach (($opts['want_refs'] ?? []) as $ref) $args[] = 'want-ref ' . $ref;
    $args[] = 'done';
    $in = v2_request($t, $c, 'fetch', $args);
    $pack = '';
    $section = null;
    while (true) {
        $line = pkt_read($in);
        if ($line === false || $line === null) break;
        if (is_int($line)) { $section = null; continue; }
        if ($section === null) {
            $name = rtrim($line, "\n");
            if (strncmp($name, 'ERR ', 4) === 0) die_git(substr($name, 4));
            $section = $name;
            continue;
        }
        if ($section !== 'packfile') continue;
        $band = ord($line[0]);
        $payload = substr($line, 1);
        if ($band === 1) $pack .= $payload;
        elseif ($band === 3) die_git(rtrim($payload, "\n"));
        elseif ($band === 2) fwrite(STDERR, $payload);
    }
    return $pack;
}

// ------------------------------------------------------------ partial clone

/** Remotes marked promisor, in the order git consults them. */
function promisor_remotes(Repo $repo): array {
    $out = [];
    $ext = $repo->config->get('extensions.partialclone', null);
    if (is_string($ext) && $ext !== '') $out[] = $ext;
    foreach ($repo->config->allKeys() as $key) {
        if (!preg_match('/^remote\.(.*)\.promisor$/', $key, $m)) continue;
        if (!$repo->config->getBool($key, false)) continue;
        if (!in_array($m[1], $out, true)) $out[] = $m[1];
    }
    return $out;
}

/**
 * Fetch one object from a promisor remote.  Returns true when something was
 * stored, so the caller can look the object up again.  Recursion is guarded:
 * the fetch itself reads objects.
 */
function promisor_fetch(Repo $repo, string $oid): bool {
    static $busy = false;
    static $failed = [];
    if ($busy || isset($failed[$oid])) return false;
    if (!preg_match('/^[0-9a-f]{40}$/', $oid)) return false;
    $remotes = promisor_remotes($repo);
    if (!$remotes) return false;
    $busy = true;
    $stored = false;
    // a promisor fetch runs a server that may die under us; a broken pipe must
    // not take this process with it
    $prevPipe = null;
    if (function_exists('pcntl_signal')) {
        $prevPipe = true;
        pcntl_signal(SIGPIPE, SIG_IGN);
    }
    foreach ($remotes as $remote) {
        $url = $repo->config->get('remote.' . $remote . '.url', null);
        if (!is_string($url) || $url === '') continue;
        if (!promisor_url_usable($repo, $url)) continue;
        $pack = git_guard(function () use ($repo, $url, $oid) {
            $t = Transport::open($repo, resolve_remote_url($repo, $url));
            $version = negotiated_protocol_version($repo);
            if ($version < 2) $version = 2;
            $c = $t->connect('git-upload-pack', $version);
            $p = fetch_pack_v2($repo, $t, $c, [$oid], [], []);
            $c->close();
            return $p;
        });
        if (is_string($pack) && $pack !== '') {
            store_pack($repo, $pack, true);
            $stored = true;
            break;
        }
    }
    if (!$stored) $failed[$oid] = true;
    if ($prevPipe !== null) pcntl_signal(SIGPIPE, SIG_DFL);
    $busy = false;
    return $stored;
}

/**
 * Is this promisor remote worth contacting?  A partial clone whose source has
 * gone is a normal state -- the objects stay missing and nothing should die.
 */
function promisor_url_usable(Repo $repo, string $url): bool {
    $resolved = resolve_remote_url($repo, $url);
    if (preg_match('#^[a-z][a-z0-9+.-]*://#', $resolved)) return true;
    if (preg_match('#^[^/]+@[^/:]+:#', $resolved)) return true;
    $p = path_absolute($resolved);
    return Repo::isGitDirectory($p) || Repo::isGitDirectory($p . '/.git');
}

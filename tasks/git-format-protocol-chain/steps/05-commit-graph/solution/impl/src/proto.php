<?php
// The pack protocol: pkt-line framing, ref advertisement, upload-pack and
// receive-pack (gitprotocol-common(5), gitprotocol-pack(5),
// gitprotocol-capabilities(5), gitprotocol-http(5)).

const PKT_FLUSH = '0000';
const PKT_DELIM = '0001';
const PKT_RESPONSE_END = '0002';

function pkt_line(string $payload): string {
    return sprintf('%04x', strlen($payload) + 4) . $payload;
}

/** @return string|null|false payload, null on flush, false on EOF */
function pkt_read($fh) {
    $hdr = fread_exact($fh, 4);
    if (strlen($hdr) < 4) return false;
    if ($hdr === PKT_FLUSH) return null;
    if ($hdr === PKT_DELIM) return 1;
    if ($hdr === PKT_RESPONSE_END) return 2;
    if (!preg_match('/^[0-9a-fA-F]{4}$/', $hdr)) {
        fwrite(STDERR, "fatal: protocol error: bad line length character: $hdr\n");
        return false;
    }
    $len = hexdec($hdr);
    if ($len < 4) return false;
    return fread_exact($fh, $len - 4);
}

function pkt_write($fh, string $payload) { fwrite($fh, pkt_line($payload)); }
function pkt_flush($fh) { fwrite($fh, PKT_FLUSH); }

/** Split "value\0caps" as the first advertisement line carries it. */
function split_caps(string $line): array {
    $nul = strpos($line, "\0");
    if ($nul === false) return [$line, []];
    $caps = trim(substr($line, $nul + 1));
    return [substr($line, 0, $nul), $caps === '' ? [] : explode(' ', $caps)];
}

function upload_pack_capabilities(Repo $repo): array {
    $caps = ['multi_ack', 'thin-pack', 'side-band', 'side-band-64k', 'ofs-delta',
             'shallow', 'deepen-since', 'deepen-not', 'deepen-relative',
             'no-progress', 'include-tag', 'multi_ack_detailed', 'no-done'];
    $head = $repo->refs->readRaw('HEAD');
    if ($head !== null && $head[1] !== null && $repo->refs->resolve('HEAD') !== null) {
        $caps[] = 'symref=HEAD:' . $head[1];
    }
    // uploadpack.allowFilter is what makes a server say it accepts filters
    if ($repo->config->getBool('uploadpack.allowfilter', false)) $caps[] = 'filter';
    $caps[] = 'object-format=' . $repo->hashAlgo;
    $caps[] = 'agent=' . git_agent();
    return $caps;
}

function receive_pack_capabilities(Repo $repo): array {
    return ['report-status', 'report-status-v2', 'delete-refs', 'side-band-64k',
            'quiet', 'atomic', 'ofs-delta', 'object-format=' . $repo->hashAlgo,
            'agent=' . git_agent()];
}

/** git's agent string carries the platform, as gitprotocol-capabilities(5) allows. */
function git_agent_full(): string { return git_agent(); }

function git_agent(): string { return 'git/' . GIT_VERSION . '-' . php_uname('s'); }

/** The refs a server advertises, in git's order, with peeled tags. */
function advertised_refs(Repo $repo, bool $withHead = true): array {
    $out = [];
    $head = $repo->refs->resolve('HEAD');
    if ($withHead && $head !== null) $out['HEAD'] = $head;
    foreach ($repo->refs->listRefs('refs/') as $name => $oid) {
        $out[$name] = $oid;
    }
    return $out;
}

function write_advertisement($fh, Repo $repo, string $service) {
    $caps = $service === 'git-upload-pack'
        ? upload_pack_capabilities($repo)
        : receive_pack_capabilities($repo);
    // receive-pack does not advertise HEAD
    $refs = advertised_refs($repo, $service === 'git-upload-pack');
    $first = true;
    foreach ($refs as $name => $oid) {
        $line = "$oid $name";
        if ($first) {
            $line .= "\0" . implode(' ', $caps);
            $first = false;
        }
        pkt_write($fh, $line . "\n");
        if ($service === 'git-upload-pack' && strncmp($name, 'refs/tags/', 10) === 0) {
            $hdr = $repo->odb->readHeader($oid);
            if ($hdr !== null && $hdr[0] === 'tag') {
                $peeled = peel_object($repo, $oid);
                if ($peeled !== null && $peeled !== $oid) pkt_write($fh, "$peeled $name^{}\n");
            }
        }
    }
    if ($first) {
        // no refs at all: advertise the capabilities on a dummy line
        pkt_write($fh, str_repeat('0', $repo->hexsz) . " capabilities^{}\0" . implode(' ', $caps) . "\n");
    }
    pkt_flush($fh);
}

/** Multiplex payload onto the side bands when the client asked for them. */
class SideBand {
    private $fh;
    private bool $enabled;
    private int $max;
    public function __construct($fh, bool $enabled, int $max = 65515) {
        $this->fh = $fh; $this->enabled = $enabled; $this->max = $max;
    }
    public function data(string $s) {
        if (!$this->enabled) { fwrite($this->fh, $s); return; }
        $off = 0;
        while ($off < strlen($s)) {
            $chunk = substr($s, $off, $this->max);
            pkt_write($this->fh, "\x01" . $chunk);
            $off += strlen($chunk);
        }
    }
    public function progress(string $s) {
        if ($this->enabled) pkt_write($this->fh, "\x02" . $s);
    }
    public function error(string $s) {
        if ($this->enabled) pkt_write($this->fh, "\x03" . $s);
        else fwrite($this->fh, $s);
    }
}

/** Server side of a fetch (gitprotocol-pack "Packfile Negotiation"). */
function upload_pack_serve(Repo $repo, $in, $out, bool $statelessRpc = false, bool $advertise = true) {
    if ($advertise && !$statelessRpc) write_advertisement($out, $repo, 'git-upload-pack');
    $wants = [];
    $haves = [];
    $caps = [];
    $done = false;
    $filter = null;
    $deepen = null;
    while (true) {
        $line = pkt_read($in);
        if ($line === false || $line === null) break;
        if (is_int($line)) continue;
        $line = rtrim($line, "\n");
        if (strncmp($line, 'want ', 5) === 0) {
            // "want <oid>[ <capabilities>]": the first want line carries the
            // client's capability list after a space (gitprotocol-pack(5)).
            $parts = preg_split('/\s+/', trim(substr($line, 5)));
            $wants[] = array_shift($parts);
            if ($parts) $caps = array_merge($caps, $parts);
            continue;
        }
        if (strncmp($line, 'filter ', 7) === 0) { $filter = substr($line, 7); continue; }
        if (strncmp($line, 'deepen', 6) === 0) { $deepen = $line; continue; }
        if (strncmp($line, 'shallow ', 8) === 0) { continue; }
        if ($line === 'done') { $done = true; break; }
    }
    if (!$wants) { return 0; }
    $multiAck = in_array('multi_ack_detailed', $caps, true) || in_array('multi_ack', $caps, true);
    $sidebandOn = in_array('side-band-64k', $caps, true) || in_array('side-band', $caps, true);
    $common = [];
    if (!$done) {
        while (true) {
            $line = pkt_read($in);
            if ($line === false) break;
            if ($line === null) {
                // end of a have batch
                if ($common) {
                    if ($multiAck) pkt_write($out, 'ACK ' . $common[count($common) - 1] . " continue\n");
                } else {
                    pkt_write($out, "NAK\n");
                }
                continue;
            }
            if (is_int($line)) continue;
            $line = rtrim($line, "\n");
            if (strncmp($line, 'have ', 5) === 0) {
                $oid = trim(substr($line, 5));
                if ($repo->odb->has($oid)) {
                    $common[] = $oid;
                    $haves[] = $oid;
                    if ($multiAck) pkt_write($out, "ACK $oid common\n");
                }
                continue;
            }
            if ($line === 'done') { $done = true; break; }
        }
    }
    if ($common) {
        pkt_write($out, 'ACK ' . $common[count($common) - 1] . "\n");
    } else {
        pkt_write($out, "NAK\n");
    }
    $sb = new SideBand($out, $sidebandOn);
    $objects = fetch_object_set($repo, $wants, $haves, $filter);
    $writer = new PackWriter($repo);
    [$packData, $offsets] = $writer->build($objects, in_array('ofs-delta', $caps, true));
    $sb->data($packData);
    if ($sidebandOn) pkt_flush($out);
    fflush($out);
    return 0;
}

/** Objects the client needs: reachable from wants, minus everything it has. */
function fetch_object_set(Repo $repo, array $wants, array $haves, ?string $filter = null): array {
    $walk = new RevWalk($repo);
    $extra = [];
    foreach ($wants as $w) {
        if (!$repo->odb->has($w)) continue;
        $hdr = $repo->odb->readHeader($w);
        if ($hdr !== null && $hdr[0] === 'tag') {
            $extra[$w] = true;
            $c = peel_to_commit($repo, $w);
            if ($c !== null) $walk->push($c);
            continue;
        }
        $c = peel_to_commit($repo, $w);
        if ($c !== null) $walk->push($c);
        else $extra[$w] = true;
    }
    foreach ($haves as $h) {
        if ($repo->odb->has($h)) {
            $c = peel_to_commit($repo, $h);
            if ($c !== null) $walk->hide($c);
        }
    }
    $objs = $extra;
    $haveObjs = [];
    if ($haves) {
        $hw = new RevWalk($repo);
        foreach ($haves as $h) {
            $c = peel_to_commit($repo, $h);
            if ($c !== null) $hw->push($c);
        }
        foreach ($hw->commits() as $oid => $c) {
            $haveObjs[$oid] = true;
            $t = [];
            collect_tree_objects($repo, $c->tree, '', $t);
            foreach (array_keys($t) as $o) $haveObjs[$o] = true;
        }
    }
    foreach ($walk->commits() as $oid => $c) {
        if (isset($haveObjs[$oid])) continue;
        $objs[$oid] = true;
        $t = [];
        collect_tree_objects($repo, $c->tree, '', $t);
        foreach (array_keys($t) as $o) {
            if (isset($haveObjs[$o])) continue;
            if ($filter !== null && filter_excludes($repo, $filter, $o)) continue;
            $objs[$o] = true;
        }
    }
    return array_keys($objs);
}


/** Server side of a push. */
function receive_pack_serve(Repo $repo, $in, $out, bool $statelessRpc = false, bool $advertise = true) {
    if ($advertise && !$statelessRpc) write_advertisement($out, $repo, 'git-receive-pack');
    $commands = [];
    $caps = [];
    while (true) {
        $line = pkt_read($in);
        if ($line === false || $line === null) break;
        if (is_int($line)) continue;
        $line = rtrim($line, "\n");
        [$cmd, $c] = split_caps($line);
        if ($c) $caps = array_merge($caps, $c);
        $parts = explode(' ', trim($cmd));
        if (count($parts) >= 3) {
            $commands[] = ['old' => $parts[0], 'new' => $parts[1], 'ref' => $parts[2]];
        }
    }
    if (!$commands) { return 0; }
    $sideband = in_array('side-band-64k', $caps, true);
    $report = in_array('report-status', $caps, true) || in_array('report-status-v2', $caps, true);
    $null = str_repeat('0', $repo->hexsz);
    $needPack = false;
    foreach ($commands as $c) if ($c['new'] !== $null) $needPack = true;
    $unpackStatus = 'ok';
    if ($needPack) {
        $packData = stream_get_contents($in);
        if ($packData === '' || substr($packData, 0, 4) !== 'PACK') {
            $unpackStatus = 'unpack-objects abnormal exit';
        } else {
            $objects = pack_parse($repo, $packData);
            foreach ($objects as $oid => $o) $repo->odb->write($o[0], $o[1]);
            $repo->odb->resetPacks();
        }
    }
    $results = [];
    foreach ($commands as $c) {
        if ($unpackStatus !== 'ok') { $results[$c['ref']] = 'unpacker error'; continue; }
        if (!check_ref_format($c['ref'])) { $results[$c['ref']] = 'funny refname'; continue; }
        $cur = $repo->refs->resolve($c['ref']);
        if ($c['old'] !== $null && $cur !== $c['old']) { $results[$c['ref']] = 'fetch first'; continue; }
        if ($c['old'] === $null && $cur !== null) { $results[$c['ref']] = 'already exists'; continue; }
        if ($c['new'] === $null) {
            $ok = $repo->refs->deleteRef($c['ref'], null, 'push', false);
            $results[$c['ref']] = $ok ? 'ok' : 'failed to delete';
            continue;
        }
        if (!$repo->odb->has($c['new'])) { $results[$c['ref']] = 'missing necessary objects'; continue; }
        $ok = $repo->refs->writeRef($c['ref'], $c['new'], null, null, 'push', false);
        $results[$c['ref']] = $ok ? 'ok' : 'failed to update ref';
    }
    if ($report) {
        $buf = fopen('php://memory', 'r+b');
        pkt_write($buf, ($unpackStatus === 'ok' ? "unpack ok\n" : "unpack $unpackStatus\n"));
        foreach ($results as $ref => $status) {
            pkt_write($buf, $status === 'ok' ? "ok $ref\n" : "ng $ref $status\n");
        }
        pkt_flush($buf);
        rewind($buf);
        $payload = stream_get_contents($buf);
        if ($sideband) {
            $sb = new SideBand($out, true);
            $sb->data($payload);
            pkt_flush($out);
        } else {
            fwrite($out, $payload);
        }
    }
    fflush($out);
    return 0;
}

/** Read a packfile from a side-band multiplexed stream. */
function read_sideband_pack($in, $progressTo = null): string {
    $pack = '';
    while (true) {
        $line = pkt_read($in);
        if ($line === false || $line === null) break;
        if (is_int($line)) continue;
        if ($line === '') continue;
        $band = ord($line[0]);
        $payload = substr($line, 1);
        if ($band === 1) $pack .= $payload;
        elseif ($band === 2) { if ($progressTo) fwrite($progressTo, $payload); }
        elseif ($band === 3) { fwrite(STDERR, $payload); return $pack; }
    }
    return $pack;
}

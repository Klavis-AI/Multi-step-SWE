<?php
// git-bundle(1) and gitformat-bundle(5): the v2 and v3 container formats, and
// the four modes create / verify / list-heads / unbundle.

const BUNDLE_V2_SIGNATURE = "# v2 git bundle\n";
const BUNDLE_V3_SIGNATURE = "# v3 git bundle\n";

/**
 * Parse a bundle header.  Returns [version, capabilities, prerequisites,
 * references, offset-of-packfile] or null when the file is not a bundle.
 */
function bundle_read_header(string $path): ?array {
    $fh = @fopen($path, 'rb');
    if ($fh === false) return null;   // caller distinguishes absent from malformed
    $sig = fgets($fh);
    if ($sig === BUNDLE_V2_SIGNATURE) $version = 2;
    elseif ($sig === BUNDLE_V3_SIGNATURE) $version = 3;
    else { fclose($fh); return null; }
    $caps = []; $pre = []; $refs = [];
    while (($line = fgets($fh)) !== false) {
        if ($line === "\n") break;
        $line = rtrim($line, "\n");
        if ($line === '') break;
        if ($line[0] === '@') {
            $eq = strpos($line, '=');
            if ($eq === false) $caps[substr($line, 1)] = true;
            else $caps[substr($line, 1, $eq - 1)] = substr($line, $eq + 1);
            continue;
        }
        if ($line[0] === '-') {
            $rest = substr($line, 1);
            $sp = strpos($rest, ' ');
            $pre[] = $sp === false ? [$rest, ''] : [substr($rest, 0, $sp), substr($rest, $sp + 1)];
            continue;
        }
        $sp = strpos($line, ' ');
        if ($sp === false) continue;
        $refs[] = [substr($line, 0, $sp), substr($line, $sp + 1)];
    }
    $offset = ftell($fh);
    fclose($fh);
    return [$version, $caps, $pre, $refs, $offset];
}

function bundle_pack_bytes(string $path, int $offset): string {
    $fh = fopen($path, 'rb');
    fseek($fh, $offset);
    $data = stream_get_contents($fh);
    fclose($fh);
    return $data;
}

function cmd_bundle(array $argv): int {
    $sub = array_shift($argv);
    if ($sub === null) usage_die('usage: git bundle create [-q | --quiet | --progress] [--version=<version>] <file> <git-rev-list-args>', 129);
    $quiet = false; $version = null; $args = [];
    // git's bundle parser carries PARSE_OPT_STOP_AT_NON_OPTION (builtin/bundle.c
    // :56-67): the first non-option ends option parsing, so everything after the
    // <file> argument -- including things that look like flags -- is passed
    // through as rev-list arguments. Accepting --version= anywhere would diverge
    // from git.
    $stopped = false;
    foreach ($argv as $a) {
        if (!$stopped) {
            if ($a === '-q' || $a === '--quiet' || $a === '--progress' || $a === '--all-progress'
                || $a === '--all-progress-implied') { $quiet = true; continue; }
            if (strncmp($a, '--version=', 10) === 0) { $version = (int)substr($a, 10); continue; }
            if ($a === '--') { $stopped = true; continue; }
            if ($a === '' || $a[0] !== '-') $stopped = true;
        }
        $args[] = $a;
    }
    switch ($sub) {
        case 'create':      return bundle_create($args, $version, $quiet);
        case 'verify':      return bundle_verify($args, $quiet);
        case 'list-heads':  return bundle_list_heads($args);
        case 'unbundle':    return bundle_unbundle($args);
        default:
            usage_die('usage: git bundle create [-q | --quiet | --progress] [--version=<version>] <file> <git-rev-list-args>', 129);
    }
    return 129;
}

function bundle_create(array $args, ?int $version, bool $quiet): int {
    $repo = need_repo();
    $file = array_shift($args);
    if ($file === null) usage_die('usage: git bundle create [-q | --quiet | --progress] [--version=<version>] <file> <git-rev-list-args>', 129);
    // git raises min_version to 3 when the repository is not SHA-1
    // (bundle.c:504-530), because a v2 bundle cannot carry an object-format line.
    if ($version === null) $version = $repo->hashAlgo === 'sha256' ? 3 : 2;
    if ($version !== 2 && $version !== 3) die_git('unknown bundle version ' . $version);
    // git permits a v3 bundle over a SHA-1 repository: bundle.c:526-530 writes
    // the v3 signature plus an @object-format capability line whatever the
    // algorithm, and t6020-bundle-misc.sh:372 / t5607-clone-bundle.sh:194
    // exercise exactly that.

    // Which refs go in, and which commits bound the history.
    $refs = [];
    $tips = [];
    $excludes = [];
    foreach ($args as $a) {
        if ($a === '--all') {
            foreach ($repo->refs->listRefs('refs/') as $name => $oid) { $refs[$name] = $oid; $tips[] = $oid; }
            $h = $repo->refs->resolve('HEAD');
            if ($h !== null) { $refs['HEAD'] = $h; $tips[] = $h; }
            continue;
        }
        if ($a !== '' && $a[0] === '^') {
            $o = rev_parse_object($repo, substr($a, 1));
            if ($o !== null) $excludes[] = $o;
            continue;
        }
        if (strpos($a, '..') !== false) {
            [$lo, $hi] = explode('..', $a, 2);
            if ($lo !== '') { $o = rev_parse_object($repo, $lo); if ($o !== null) $excludes[] = $o; }
            if ($hi !== '') {
                $o = rev_parse_object($repo, $hi);
                if ($o !== null) {
                    $tips[] = $o;
                    $full = $repo->refs->expandRefName($hi);
                    if ($full !== null) $refs[$full] = $o;
                }
            }
            continue;
        }
        $full = $repo->refs->expandRefName($a);
        $oid = rev_parse_object($repo, $a);
        if ($oid === null) die_git("'" . $a . "' does not appear to be a valid ref");
        $tips[] = $oid;
        if ($full !== null) $refs[$full] = $oid;
    }
    if (!$refs) die_git('Refusing to create empty bundle.');

    $walk = new RevWalk($repo);
    foreach ($tips as $t) { $c = peel_to_commit($repo, $t); if ($c !== null) $walk->push($c); }
    foreach ($excludes as $e) { $c = peel_to_commit($repo, $e); if ($c !== null) $walk->hide($c); }
    $commits = $walk->commits();

    // Prerequisites: the parents of the boundary that stay outside the bundle.
    $prereq = [];
    foreach ($excludes as $e) {
        $c = peel_to_commit($repo, $e);
        if ($c === null) continue;
        $prereq[$c] = '';
    }

    $objs = [];
    foreach ($tips as $t) {
        $hdr = $repo->odb->readHeader($t);
        if ($hdr !== null && $hdr[0] === 'tag') $objs[$t] = true;
    }
    foreach ($commits as $oid => $c) {
        $objs[$oid] = true;
        $t = [];
        collect_tree_objects($repo, $c->tree, '', $t);
        foreach (array_keys($t) as $o) $objs[$o] = true;
    }

    $head = BUNDLE_V2_SIGNATURE;
    if ($version === 3) $head = BUNDLE_V3_SIGNATURE . "@object-format=" . $repo->hashAlgo . "\n";
    foreach ($prereq as $oid => $subject) $head .= '-' . $oid . ($subject === '' ? '' : ' ' . $subject) . "\n";
    foreach ($refs as $name => $oid) $head .= $oid . ' ' . $name . "\n";
    $head .= "\n";

    $writer = new PackWriter($repo);
    [$packData, ] = $writer->build(array_keys($objs), true);
    write_file_atomic($file, $head . $packData, 0666);
    if (!$quiet) fwrite(STDERR, "\n");
    return 0;
}

function bundle_verify(array $args, bool $quiet): int {
    $file = $args[0] ?? null;
    if ($file === null) usage_die('usage: git bundle verify [-q | --quiet] <file>', 129);
    $h = bundle_read_header($file);
    if ($h === null) {
        if (!is_readable($file)) fwrite(STDERR, "error: could not open '" . $file . "'\n");
        else fwrite(STDERR, "error: '" . $file . "' does not look like a v2 or v3 bundle file\n");
        return 1;
    }
    [$version, $caps, $pre, $refs, $offset] = $h;
    $repo = need_repo();
    $missing = [];
    foreach ($pre as [$oid, $name]) {
        if (!$repo->odb->has($oid)) $missing[] = [$oid, $name];
    }
    if ($missing) {
        fwrite(STDERR, "error: Repository lacks these prerequisite commits:\n");
        foreach ($missing as [$oid, $name]) fwrite(STDERR, "error: $oid $name\n");
        return 1;
    }
    if (!$quiet) {
        $n = count($refs);
        out($n === 1 ? "The bundle contains this ref:\n"
                     : "The bundle contains these $n refs:\n");
        foreach ($refs as [$oid, $name]) out($oid . ' ' . $name . "\n");
        if (!$pre) {
            out("The bundle records a complete history.\n");
        } else {
            $m = count($pre);
            out($m === 1 ? "The bundle requires this ref:\n"
                         : "The bundle requires these $m refs:\n");
            foreach ($pre as [$oid, $name]) out($oid . ' ' . $name . "\n");
        }
        out("The bundle uses this hash algorithm: sha1\n");
    }
    fwrite(STDERR, $file . " is okay\n");
    return 0;
}

function bundle_list_heads(array $args): int {
    $file = array_shift($args);
    if ($file === null) usage_die('usage: git bundle list-heads <file>', 129);
    $h = bundle_read_header($file);
    if ($h === null) {
        if (!is_readable($file)) fwrite(STDERR, "error: could not open '" . $file . "'\n");
        else fwrite(STDERR, "error: '" . $file . "' does not look like a v2 or v3 bundle file\n");
        return 1;
    }
    foreach ($h[3] as [$oid, $name]) {
        if ($args) {
            $hit = false;
            foreach ($args as $p) {
                if ($name === $p || substr($name, -strlen('/' . $p)) === '/' . $p) { $hit = true; break; }
            }
            if (!$hit) continue;
        }
        out($oid . ' ' . $name . "\n");
    }
    return 0;
}

function bundle_unbundle(array $args): int {
    $repo = need_repo();
    $file = array_shift($args);
    if ($file === null) usage_die('usage: git bundle unbundle [--progress] <file> [<refname>...]', 129);
    $h = bundle_read_header($file);
    if ($h === null) {
        if (!is_readable($file)) fwrite(STDERR, "error: could not open '" . $file . "'\n");
        else fwrite(STDERR, "error: '" . $file . "' does not look like a v2 or v3 bundle file\n");
        return 1;
    }
    [$version, $caps, $pre, $refs, $offset] = $h;
    foreach ($pre as [$oid, $name]) {
        if (!$repo->odb->has($oid)) {
            fwrite(STDERR, "error: Repository lacks these prerequisite commits:\n");
            fwrite(STDERR, "error: $oid $name\n");
            return 1;
        }
    }
    store_pack($repo, bundle_pack_bytes($file, $offset));
    foreach ($refs as [$oid, $name]) out($oid . ' ' . $name . "\n");
    return 0;
}

/**
 * `git clone <file>.bundle`: the bundle is the transport.  Its refs become the
 * remote's refs and its packfile is the whole fetch.
 */
function clone_from_bundle(Repo $repo, string $path, string $origin, ?string $branch,
                           bool $bare, bool $noCheckout, bool $quiet, bool $mirror,
                           string $url): int {
    $h = bundle_read_header($path);
    if ($h === null) {
        fwrite(STDERR, "fatal: '" . $path . "' does not look like a v2 or v3 bundle file\n");
        return 128;
    }
    [$version, $caps, $pre, $refs, $offset] = $h;
    foreach ($pre as [$oid, $name]) {
        if (!$repo->odb->has($oid)) {
            fwrite(STDERR, "error: Repository lacks these prerequisite commits:\n");
            fwrite(STDERR, "error: " . $oid . ($name === '' ? '' : ' ' . $name) . "\n");
            fwrite(STDERR, "fatal: could not fetch " . $origin . "\n");
            return 128;
        }
    }
    store_pack($repo, bundle_pack_bytes($path, $offset));
    $map = [];
    foreach ($refs as [$oid, $name]) $map[$name] = $oid;
    foreach ($map as $name => $oid) {
        if ($name === 'HEAD') continue;
        if (strncmp($name, 'refs/heads/', 11) === 0) {
            $target = ($mirror || $bare) ? $name
                    : 'refs/remotes/' . $origin . '/' . substr($name, 11);
            $repo->refs->writeRef($target, $oid, null, null, "clone: from $url", false);
        } elseif (strncmp($name, 'refs/', 5) === 0) {
            $repo->refs->writeRef($name, $oid, null, null, "clone: from $url", false);
        }
    }
    $t = new LocalTransport($repo, $path);
    clone_finish_head($repo, $map, $t, $origin, $branch, $bare, $noCheckout, $quiet);
    return 0;
}

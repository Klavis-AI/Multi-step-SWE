<?php
// git-hash-object(1), git-cat-file(1), git-ls-tree(1), git-mktree(1),
// git-commit-tree(1), git-var(1)

const HASH_OBJECT_USAGE = <<<'EOT'
usage: git hash-object [-t <type>] [-w] [--path=<file> | --no-filters]
                       [--stdin [--literally]] [--] <file>...
   or: git hash-object [-t <type>] [-w] --stdin-paths [--no-filters]

    -t <type>             object type
    -w                    write the object into the object database
    --[no-]stdin          read the object from stdin
    --[no-]stdin-paths    read file names from stdin
    --no-filters          store file as is without filters
    --filters             opposite of --no-filters
    --[no-]literally      just hash any random garbage to create corrupt objects for debugging Git
    --[no-]path <file>    process file as it were from this path

EOT;

function hash_object_usage(string $err): int {
    fwrite(STDERR, "error: $err\n" . HASH_OBJECT_USAGE . "\n");
    return 129;
}

function cmd_hash_object(array $argv): int {
    $type = 'blob'; $write = false; $stdin = false; $stdinPaths = false; $literally = false;
    $paths = []; $pathOpt = null; $noFilters = false;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($a === '-t') { $type = $argv[++$i] ?? ''; continue; }
        if ($a === '-w') { $write = true; continue; }
        if ($a === '--stdin') {
            if ($stdin) return hash_object_usage('Multiple --stdin arguments are not supported');
            if ($stdinPaths) return hash_object_usage("Can't use --stdin-paths with --stdin");
            $stdin = true; continue;
        }
        if ($a === '--stdin-paths') {
            if ($stdin) return hash_object_usage("Can't use --stdin-paths with --stdin");
            $stdinPaths = true; continue;
        }
        if ($a === '--literally') { $literally = true; continue; }
        if ($a === '--no-literally') { $literally = false; continue; }
        if ($a === '--no-filters') { $noFilters = true; continue; }
        if ($a === '--filters') { $noFilters = false; continue; }
        if (strncmp($a, '--path=', 7) === 0) { $pathOpt = substr($a, 7); continue; }
        if ($a === '--path') { $pathOpt = $argv[++$i] ?? ''; continue; }
        if ($a === '--') { for ($j = $i + 1; $j < count($argv); $j++) $paths[] = $argv[$j]; break; }
        if ($a !== '' && $a[0] === '-') return hash_object_usage("unknown switch `" . ltrim($a, '-') . "'");
        $paths[] = $a;
    }
    if ($stdinPaths && $paths) return hash_object_usage("Can't specify files with --stdin-paths");
    if ($stdinPaths && $pathOpt !== null) return hash_object_usage("Can't use --stdin-paths with --path");
    if ($pathOpt !== null && $noFilters) return hash_object_usage("Can't use --path with --no-filters");
    if (!in_array($type, ['blob', 'tree', 'commit', 'tag'], true)) {
        die_git("invalid object type \"$type\"");
    }
    $repo = null;
    if ($write) { $repo = need_repo(); }
    else {
        $d = Repo::discover();
        if ($d === null) {
            $repo = new Repo();
            $repo->gitdir = getcwd();
            $repo->commondir = getcwd();
            $repo->config = new Config();
            $repo->worktree = getcwd();
            $repo->odb = new ObjectDB($repo);
            Repo::$rawsz_static = 20;
            Repo::$hexsz_static = 40;
        } else {
            $repo = Repo::open($d[0], $d[1]);
        }
    }
    $emit = function (string $content, ?string $path = null) use ($repo, $type, $write, $literally, $noFilters, $pathOpt) {
        if ($type === 'blob' && !$noFilters) {
            $attrPath = $pathOpt !== null ? $pathOpt : $path;
            if ($attrPath !== null) $content = convert_to_git($repo, $attrPath, $content);
        }
        if ($type !== 'blob' && !$literally) {
            $err = object_check_syntax($repo, $type, $content);
            if ($err !== null) {
                fwrite(STDERR, $err);
                fwrite(STDERR, "fatal: refusing to create malformed object\n");
                exit(128);
            }
        }
        $oid = $write ? $repo->odb->write($type, $content) : $repo->hashData($type, $content);
        out("$oid\n");
    };
    if ($stdinPaths) {
        while (($line = fgets(STDIN)) !== false) {
            $p = rtrim($line, "\n");
            if ($p === '') continue;
            if ($p[0] === '"') $p = unquote_c_style($p);
            $c = @file_get_contents($p);
            if ($c === false) die_git("Cannot open '$p'");
            $emit($c, $p);
        }
        return 0;
    }
    if (!$stdin && !$paths) {
        fwrite(STDERR, HASH_OBJECT_USAGE . "\n");
        return 129;
    }
    if ($stdin) $emit(stream_get_contents(STDIN), $pathOpt);
    foreach ($paths as $p) {
        $c = @file_get_contents($p);
        if ($c === false) die_git("Cannot open '$p'");
        $emit($c, $p);
    }
    return 0;
}

/** Structural validation, as `git hash-object` performs before writing. */
function object_check_syntax(Repo $repo, string $type, string $content): ?string {
    $rawsz = Repo::$rawsz_static;
    if ($type === 'tree') {
        $i = 0; $n = strlen($content);
        $names = [];
        while ($i < $n) {
            $sp = strpos($content, ' ', $i);
            if ($sp === false || $sp - $i < 1) {
                return "error: too-short tree object\nerror: object fails fsck: badTree: cannot be parsed as a tree\n";
            }
            $mode = substr($content, $i, $sp - $i);
            if (!preg_match('/^0?[0-7]{5,6}$/', $mode) || strlen($mode) > 6) {
                return "error: malformed mode in tree entry\nerror: object fails fsck: badTree: cannot be parsed as a tree\n";
            }
            $nul = strpos($content, "\0", $sp + 1);
            if ($nul === false) {
                return "error: too-short tree object\nerror: object fails fsck: badTree: cannot be parsed as a tree\n";
            }
            $name = substr($content, $sp + 1, $nul - $sp - 1);
            if ($name === '') {
                return "error: empty filename in tree entry\nerror: object fails fsck: badTree: cannot be parsed as a tree\n";
            }
            if (isset($names[$name])) {
                return "error: object fails fsck: duplicateEntries: contains duplicate file entries\n";
            }
            $names[$name] = true;
            $i = $nul + 1 + $rawsz;
            if ($i > $n) {
                return "error: too-short tree object\nerror: object fails fsck: badTree: cannot be parsed as a tree\n";
            }
        }
        return null;
    }
    if ($type === 'commit' || $type === 'tag') {
        if (strpos($content, "\n") === false) {
            return "error: object fails fsck: unterminatedHeader: unterminated header\n";
        }
        if ($type === 'commit' && strncmp($content, 'tree ', 5) !== 0) {
            return "error: object fails fsck: missingTree: invalid format - expected 'tree' line\n";
        }
        if ($type === 'tag' && strncmp($content, 'object ', 7) !== 0) {
            return "error: object fails fsck: missingObject: invalid format - expected 'object' line\n";
        }
        return null;
    }
    return null;
}

function cat_file_usage(string $err): int {
    fwrite(STDERR, "$err\n");
    out('usage: ' . git_usage_text('cat-file') . "\n");
    return 129;
}

function cmd_cat_file(array $argv): int {
    $mode = null; $obj = null; $typeArg = null; $batch = null; $batchFmt = null;
    $allObjects = false; $followSymlinks = false; $allowUnknown = false; $useMailmap = false;
    $batchCheck = false; $batchCommand = false; $bufferAll = false; $nulOut = false;
    $pathOpt = null; $textconv = false; $filters = false; $modeFlags = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '-t' || $a === '-s' || $a === '-e' || $a === '-p':
                $modeFlags[] = $a; $mode = $a; break;
            case $a === '--textconv': $textconv = true; $modeFlags[] = $a; break;
            case $a === '--filters': $filters = true; $modeFlags[] = $a; break;
            case $a === '--batch': $batch = 'batch'; break;
            case strncmp($a, '--batch=', 8) === 0: $batch = 'batch'; $batchFmt = substr($a, 8); break;
            case $a === '--batch-check': $batch = 'check'; $batchCheck = true; break;
            case strncmp($a, '--batch-check=', 14) === 0: $batch = 'check'; $batchCheck = true; $batchFmt = substr($a, 14); break;
            case $a === '--batch-command': $batch = 'command'; $batchCommand = true; break;
            case strncmp($a, '--batch-command=', 16) === 0: $batch = 'command'; $batchCommand = true; $batchFmt = substr($a, 16); break;
            case $a === '--batch-all-objects': $allObjects = true; break;
            case $a === '--allow-unknown-type': $allowUnknown = true; break;
            case $a === '--buffer': $bufferAll = true; break;
            case $a === '-z': $nulOut = 'in'; break;
            case $a === '-Z': $nulOut = 'both'; break;
            case $a === '--follow-symlinks': $followSymlinks = true; break;
            case $a === '--use-mailmap' || $a === '--mailmap': $useMailmap = true; break;
            case strncmp($a, '--path=', 7) === 0: $pathOpt = substr($a, 7); break;
            case $a === '--path': $pathOpt = $argv[++$i] ?? ''; break;
            case $a === '--': continue 2;
            case $a !== '' && $a[0] === '-':
                return cat_file_usage("error: unknown switch `" . ltrim($a, '-') . "'");
            default:
                if ($typeArg === null && $mode === null && $batch === null && !$textconv && !$filters) { $typeArg = $a; break; }
                $obj = $a;
        }
    }
    if (count($modeFlags) > 1) {
        return cat_file_usage("error: options '" . $modeFlags[0] . "' and '" . $modeFlags[1] . "' cannot be used together");
    }
    if ($batch !== null && $mode !== null) {
        return cat_file_usage("error: options '" . $mode . "' and '--batch' cannot be used together");
    }
    if ($pathOpt !== null && $batch !== null) {
        return cat_file_usage("error: '--path=<path>' needs '--textconv' or '--filters'");
    }
    if ($pathOpt !== null && !$textconv && !$filters) {
        return cat_file_usage("error: '--path=<path>' needs '--textconv' or '--filters'");
    }
    if (($textconv || $filters) && $batch === null && $obj === null && $typeArg === null) {
        fwrite(STDERR, "fatal: <object> required with '-" . ($textconv ? '-textconv' : '-filters') . "'\n");
        out('usage: ' . git_usage_text('cat-file') . "\n");
        return 129;
    }
    if ($allObjects && $batch === null) {
        return cat_file_usage("error: '--batch-all-objects' requires '--batch' or '--batch-check' or '--batch-command'");
    }
    $repo = need_repo();
    if ($batch !== null) {
        // listing every object in a partial clone cannot list what the
        // promisor remote still holds, and git says so once
        if ($allObjects && function_exists('promisor_remotes') && promisor_remotes($repo) !== []) {
            fwrite(STDERR, "warning: This repository uses promisor remotes. Some objects may not be loaded.\n");
        }
        return cat_file_batch($repo, $batch, $batchFmt, $allObjects, $nulOut, $bufferAll);
    }
    if ($mode === null && $typeArg !== null && $obj === null) {
        return cat_file_usage("fatal: <object> required");
    }
    $name = $obj !== null ? $obj : $typeArg;
    if ($name === null || $name === '') {
        if ($mode !== null) {
            fwrite(STDERR, "fatal: <object> required with '" . $mode . "'\n\n");
        }
        out('usage: ' . git_usage_text('cat-file') . "\n");
        return 129;
    }
    if ($mode !== null && $obj === null && $typeArg !== null) $name = $typeArg;
    $oid = rev_parse_object($repo, $name);
    if ($oid === null || $repo->odb->read($oid) === null) {
        if ($mode === '-e') return 1;
        die_git("Not a valid object name $name");
    }
    $o = $repo->odb->read($oid);
    switch ($mode) {
        case '-t': out($o[0] . "\n"); return 0;
        case '-s': out(strlen($o[1]) . "\n"); return 0;
        case '-e': return 0;
        case '-p':
            if ($o[0] === 'tree') { out(format_tree_for_cat_file($repo, $o[1])); return 0; }
            out($o[1]);
            return 0;
    }
    if ($textconv || $filters) { out($o[1]); return 0; }
    if ($typeArg !== null) {
        if ($typeArg !== $o[0]) die_git("$name: bad file");
        out($o[1]);
        return 0;
    }
    return 0;
}

function cat_file_format_line(Repo $repo, string $oid, string $type, int $size, ?string $fmt, string $rest): string {
    if ($fmt === null) return "$oid $type $size";
    return preg_replace_callback('/%\(([^)]*)\)/', function ($m) use ($repo, $oid, $type, $size, $rest) {
        switch ($m[1]) {
            case 'objectname': return $oid;
            case 'objecttype': return $type;
            case 'objectsize': return (string)$size;
            case 'objectsize:disk':
                $p = $repo->odb->loosePath($oid);
                return (string)(is_file($p) ? filesize($p) : $size);
            case 'deltabase': return str_repeat('0', $repo->hexsz);
            case 'rest': return $rest;
            default: return '';
        }
    }, $fmt);
}

function cat_file_batch(Repo $repo, string $mode, ?string $fmt, bool $allObjects, $nulOut, bool $buffer): int {
    $contents = ($mode === 'batch');
    $inTerm = ($nulOut === 'in' || $nulOut === 'both') ? "\0" : "\n";
    $outTerm = ($nulOut === 'both') ? "\0" : "\n";
    $wantRest = $fmt !== null && strpos($fmt, '%(rest)') !== false;
    $emit = function (string $input) use ($repo, $contents, $fmt, $outTerm, $wantRest) {
        $rest = '';
        $name = $input;
        if ($wantRest) {
            $sp = strpos($input, ' ');
            if ($sp !== false) { $name = substr($input, 0, $sp); $rest = substr($input, $sp + 1); }
        }
        if ($name === '') return;
        if (is_hex($name) && strlen($name) < $repo->hexsz && strlen($name) >= 2) {
            $cands = $repo->odb->expandPrefix($name);
            if (count($cands) > 1) { out("$name ambiguous$outTerm"); return; }
        }
        $oid = rev_parse_object($repo, $name);
        if ($oid === null) { out("$name missing$outTerm"); return; }
        $hdr = $repo->odb->readHeader($oid);
        if ($hdr === null) { out("$name missing$outTerm"); return; }
        out(cat_file_format_line($repo, $oid, $hdr[0], $hdr[1], $fmt, $rest) . $outTerm);
        if ($contents) {
            $o = $repo->odb->read($oid);
            out(($o === null ? '' : $o[1]) . $outTerm);
        }
    };
    if ($allObjects) {
        $oids = $repo->odb->allObjects();
        sort($oids);
        foreach ($oids as $oid) $emit($oid);
        return 0;
    }
    $buf = '';
    $batchCommand = ($mode === 'command');
    while (!feof(STDIN)) {
        $chunk = fread(STDIN, 8192);
        if ($chunk === false || $chunk === '') break;
        $buf .= $chunk;
        while (($pos = strpos($buf, $inTerm)) !== false) {
            $line = substr($buf, 0, $pos);
            $buf = substr($buf, $pos + 1);
            if ($batchCommand) {
                if ($line === 'flush') { flush(); continue; }
                if (strncmp($line, 'contents ', 9) === 0) { $contents = true; $emit(substr($line, 9)); $contents = false; continue; }
                if (strncmp($line, 'info ', 5) === 0) { $emit(substr($line, 5)); continue; }
                if ($line === '') continue;
                fwrite(STDERR, "fatal: unknown command: $line\n");
                exit(128);
            }
            $emit($line);
            flush();
        }
    }
    if (trim($buf) !== '') $emit($buf);
    return 0;
}


function format_tree_for_cat_file(Repo $repo, string $data): string {
    $t = TreeObj::parse($data);
    $s = '';
    foreach ($t->entries as $e) {
        $type = tree_entry_type($e->mode);
        $s .= sprintf("%06o %s %s\t%s\n", octdec($e->mode), $type, $e->oid, quote_c_style_if_needed($e->name));
    }
    return $s;
}

function tree_entry_type(string $mode): string {
    $m = octdec($mode);
    if ($m === 0040000) return 'tree';
    if ($m === 0160000) return 'commit';
    return 'blob';
}

function cmd_ls_tree(array $argv): int {
    $recursive = false; $showTrees = false; $dirsOnly = false; $long = false;
    $nameOnly = false; $z = false; $abbrev = null; $fullTree = false; $fullName = false;
    $objectOnly = false; $format = null;
    $rest = [];
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($a === '-r') { $recursive = true; continue; }
        if ($a === '-t') { $showTrees = true; continue; }
        if ($a === '-d') { $dirsOnly = true; continue; }
        if ($a === '-l' || $a === '--long') { $long = true; continue; }
        if ($a === '-z') { $z = true; continue; }
        if ($a === '--name-only' || $a === '--name-status') { $nameOnly = true; continue; }
        if ($a === '--object-only') { $objectOnly = true; continue; }
        if ($a === '--full-tree') { $fullTree = true; continue; }
        if ($a === '--full-name') { $fullName = true; continue; }
        if ($a === '--abbrev') { $abbrev = 7; continue; }
        if (strncmp($a, '--abbrev=', 9) === 0) { $abbrev = (int)substr($a, 9); continue; }
        if (strncmp($a, '--format=', 9) === 0) { $format = substr($a, 9); continue; }
        if ($a === '--') { for ($j = $i + 1; $j < count($argv); $j++) $rest[] = $argv[$j]; break; }
        $rest[] = $a;
    }
    if (!$rest) usage_die('usage: git ls-tree [<options>] <tree-ish> [<path>...]', 129);
    $repo = need_repo();
    $treeish = array_shift($rest);
    $oid = rev_parse_object($repo, $treeish);
    if ($oid === null) die_git("Not a valid object name $treeish");
    $tree = peel_to_tree($repo, $oid);
    if ($tree === null) die_git("not a tree object");
    $term = $z ? "\0" : "\n";
    $prefixPaths = $rest;
    $walk = function (string $toid, string $prefix) use (&$walk, $repo, $recursive, $showTrees, $dirsOnly, $long, $nameOnly, $objectOnly, $abbrev, $term, $prefixPaths) {
        $o = $repo->odb->read($toid);
        if ($o === null) return;
        $t = TreeObj::parse($o[1]);
        foreach ($t->entries as $e) {
            $path = $prefix . $e->name;
            $isTree = $e->mode === '40000';
            if ($isTree && $recursive && !$dirsOnly) {
                if ($showTrees) ls_tree_emit($repo, $e, $path, $long, $nameOnly, $objectOnly, $abbrev, $term, $prefixPaths);
                $walk($e->oid, $path . '/');
                continue;
            }
            if ($dirsOnly && !$isTree) continue;
            ls_tree_emit($repo, $e, $path, $long, $nameOnly, $objectOnly, $abbrev, $term, $prefixPaths);
        }
    };
    if ($prefixPaths) {
        // restrict to the given paths
        foreach ($prefixPaths as $p) {
            $pp = rtrim($p, '/');
            $entry = tree_lookup_path($repo, $tree, $pp);
            if ($entry === null) continue;
            if ($entry->mode === '40000' && substr($p, -1) === '/') {
                $walk($entry->oid, $pp . '/');
            } elseif ($entry->mode === '40000' && $recursive && !$dirsOnly) {
                $walk($entry->oid, $pp . '/');
            } else {
                ls_tree_emit($repo, $entry, $pp, $long, $nameOnly, $objectOnly, $abbrev, $term, []);
            }
        }
        return 0;
    }
    $walk($tree, '');
    return 0;
}

function ls_tree_emit(Repo $repo, TreeEntry $e, string $path, bool $long, bool $nameOnly, bool $objectOnly, ?int $abbrev, string $term, array $filter) {
    $oid = $abbrev ? substr($e->oid, 0, $abbrev) : $e->oid;
    if ($nameOnly) { out(quote_path_maybe($path, $term) . $term); return; }
    if ($objectOnly) { out($oid . $term); return; }
    $type = tree_entry_type($e->mode);
    $mode = sprintf('%06o', octdec($e->mode));
    if ($long) {
        $size = $type === 'blob' ? (string)($repo->odb->readHeader($e->oid)[1] ?? 0) : '-';
        out(sprintf("%s %s %s %7s\t%s%s", $mode, $type, $oid, $size, quote_path_maybe($path, $term), $term));
        return;
    }
    out("$mode $type $oid\t" . quote_path_maybe($path, $term) . $term);
}

function quote_path_maybe(string $p, string $term): string {
    if ($term === "\0") return $p;
    return quote_c_style_if_needed($p);
}

/** git's C-style quoting for paths with unusual bytes (quote.c). */
function quote_c_style_if_needed(string $p): string {
    $needs = false;
    for ($i = 0; $i < strlen($p); $i++) {
        $c = ord($p[$i]);
        if ($c < 0x20 || $c === 0x7f || $c >= 0x80 || $p[$i] === '"' || $p[$i] === '\\') { $needs = true; break; }
    }
    if (!$needs) return $p;
    $out = '"';
    for ($i = 0; $i < strlen($p); $i++) {
        $ch = $p[$i]; $c = ord($ch);
        switch ($ch) {
            case '"': $out .= '\\"'; break;
            case '\\': $out .= '\\\\'; break;
            case "\n": $out .= '\\n'; break;
            case "\t": $out .= '\\t'; break;
            case "\r": $out .= '\\r'; break;
            case "\x08": $out .= '\\b'; break;
            case "\x0c": $out .= '\\f'; break;
            case "\x0b": $out .= '\\v'; break;
            case "\x07": $out .= '\\a'; break;
            default:
                if ($c < 0x20 || $c >= 0x7f) $out .= sprintf('\\%03o', $c);
                else $out .= $ch;
        }
    }
    return $out . '"';
}

function tree_lookup_path(Repo $repo, string $treeOid, string $path): ?TreeEntry {
    if ($path === '' || $path === '.') {
        $e = new TreeEntry(); $e->mode = '40000'; $e->name = ''; $e->oid = $treeOid;
        return $e;
    }
    $parts = explode('/', $path);
    $cur = $treeOid;
    for ($i = 0; $i < count($parts); $i++) {
        $o = $repo->odb->read($cur);
        if ($o === null || $o[0] !== 'tree') return null;
        $t = TreeObj::parse($o[1]);
        $e = $t->get($parts[$i]);
        if ($e === null) return null;
        if ($i === count($parts) - 1) return $e;
        if ($e->mode !== '40000') return null;
        $cur = $e->oid;
    }
    return null;
}

function peel_to_tree(Repo $repo, string $oid): ?string {
    $seen = 0;
    while ($seen++ < 10) {
        $o = $repo->odb->read($oid);
        if ($o === null) return null;
        if ($o[0] === 'tree') return $oid;
        if ($o[0] === 'commit') { $oid = CommitObj::parse($o[1])->tree; continue; }
        if ($o[0] === 'tag') { $oid = TagObj::parse($o[1])->object; continue; }
        return null;
    }
    return null;
}

function cmd_mktree(array $argv): int {
    $z = in_array('-z', $argv, true);
    $missing = in_array('--missing', $argv, true);
    $repo = need_repo();
    $data = stream_get_contents(STDIN);
    $lines = $z ? explode("\0", $data) : explode("\n", $data);
    $t = new TreeObj();
    foreach ($lines as $line) {
        if ($line === '') continue;
        if (!preg_match('/^(\d+) (blob|tree|commit) ([0-9a-f]+)\t(.*)$/s', $line, $m)) {
            die_git("input format error: $line");
        }
        $e = new TreeEntry();
        $e->mode = ltrim($m[1], '0') === '' ? '0' : ltrim($m[1], '0');
        $e->oid = $m[3];
        $e->name = $z ? $m[4] : unquote_c_style($m[4]);
        $t->entries[] = $e;
    }
    out($repo->odb->write('tree', $t->format()) . "\n");
    return 0;
}

function unquote_c_style(string $s): string {
    if ($s === '' || $s[0] !== '"') return $s;
    $s = substr($s, 1, -1);
    $out = '';
    for ($i = 0; $i < strlen($s); $i++) {
        if ($s[$i] !== '\\') { $out .= $s[$i]; continue; }
        $i++;
        $c = $s[$i] ?? '';
        switch ($c) {
            case 'n': $out .= "\n"; break;
            case 't': $out .= "\t"; break;
            case 'r': $out .= "\r"; break;
            case 'a': $out .= "\x07"; break;
            case 'b': $out .= "\x08"; break;
            case 'f': $out .= "\x0c"; break;
            case 'v': $out .= "\x0b"; break;
            case '"': $out .= '"'; break;
            case '\\': $out .= '\\'; break;
            default:
                if (preg_match('/^[0-7]{1,3}/', substr($s, $i), $m)) {
                    $out .= chr(octdec($m[0]));
                    $i += strlen($m[0]) - 1;
                } else $out .= $c;
        }
    }
    return $out;
}

function cmd_commit_tree(array $argv): int {
    $repo = need_repo();
    $tree = null; $parents = []; $msg = null; $msgFile = null;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($a === '-p') { $parents[] = $argv[++$i] ?? ''; continue; }
        if ($a === '-m') { $msg = ($msg === null ? '' : $msg . "\n") . ($argv[++$i] ?? ''); continue; }
        if ($a === '-F') { $msgFile = $argv[++$i] ?? ''; continue; }
        if ($a === '-S' || strncmp($a, '--gpg-sign', 10) === 0) continue;
        if ($a === '--no-gpg-sign') continue;
        if ($a === '--') continue;
        $tree = $a;
    }
    if ($tree === null) usage_die('usage: git commit-tree [(-p <parent>)...] [-S[<keyid>]] [(-m <message>)...] [(-F <file>)...] <tree>', 129);
    $toid = rev_parse_object($repo, $tree);
    if ($toid === null) die_git("not a valid object name $tree");
    $toid = peel_to_tree($repo, $toid);
    if ($toid === null) die_git("$tree is not a valid 'tree' object");
    $body = $msg;
    if ($msgFile !== null) {
        $c = $msgFile === '-' ? stream_get_contents(STDIN) : @file_get_contents($msgFile);
        if ($c === false) die_git("could not open '$msgFile'");
        $body = ($body === null ? '' : $body . "\n") . $c;
    }
    if ($body === null) $body = stream_get_contents(STDIN);
    if ($msg !== null && $msgFile === null) $body .= "\n";
    $c = new CommitObj();
    $c->tree = $toid;
    foreach ($parents as $p) {
        $po = rev_parse_object($repo, $p);
        if ($po === null) die_git("not a valid object name $p");
        $c->parents[] = $po;
    }
    [$an, $ae, $ats, $atz] = author_ident($repo);
    [$cn, $ce, $cts, $ctz] = committer_ident($repo);
    $c->author = Ident::parse("$an <$ae> $ats $atz");
    $c->committer = Ident::parse("$cn <$ce> $cts $ctz");
    $c->message = $body;
    out($repo->odb->write('commit', $c->format()) . "\n");
    return 0;
}

function cmd_var(array $argv): int {
    if (!$argv) usage_die('usage: git var (-l | <variable>)', 129);
    $repo = null;
    $d = Repo::discover();
    if ($d !== null) $repo = Repo::open($d[0], $d[1]);
    else $repo = Repo::open(sys_get_temp_dir() . '/nonexistent', null, false);
    $name = $argv[0];
    switch ($name) {
        case 'GIT_AUTHOR_IDENT':
            [$n, $e, $t, $z] = author_ident($repo); out("$n <$e> $t $z\n"); return 0;
        case 'GIT_COMMITTER_IDENT':
            [$n, $e, $t, $z] = committer_ident($repo); out("$n <$e> $t $z\n"); return 0;
        case 'GIT_EDITOR':
            out((getenv('GIT_EDITOR') ?: ($repo->config->get('core.editor', null) ?: (getenv('VISUAL') ?: (getenv('EDITOR') ?: 'vi')))) . "\n");
            return 0;
        case 'GIT_PAGER': out((getenv('GIT_PAGER') ?: 'cat') . "\n"); return 0;
        case 'GIT_DEFAULT_BRANCH': out("master\n"); return 0;
        case '-l': {
            $home = getenv('HOME') ?: '';
            $xdg = getenv('XDG_CONFIG_HOME');
            $attrGlobal = ($xdg !== false && $xdg !== '') ? "$xdg/git/attributes" : "$home/.config/git/attributes";
            out("GIT_ATTR_GLOBAL=$attrGlobal\n");
            [$n, $e, $t, $z] = author_ident($repo); out("GIT_AUTHOR_IDENT=$n <$e> $t $z\n");
            [$n, $e, $t, $z] = committer_ident($repo); out("GIT_COMMITTER_IDENT=$n <$e> $t $z\n");
            foreach (config_global_paths() as $p) out("GIT_CONFIG_GLOBAL=$p\n");
            out("GIT_DEFAULT_BRANCH=master\n");
            out('GIT_PAGER=' . (getenv('GIT_PAGER') ?: (getenv('PAGER') ?: 'less')) . "\n");
            out("GIT_SHELL_PATH=/bin/sh\n");
            return 0;
        }
    }
    fwrite(STDERR, 'usage: ' . git_usage_text('var') . "\n");
    return 129;
}

/** git-mktag(1): validate a tag object on stdin and write it. */
function cmd_mktag(array $argv): int {
    $strict = true;
    foreach ($argv as $a) {
        if ($a === '--no-strict') { $strict = false; continue; }
        if ($a === '--strict') { $strict = true; continue; }
    }
    $repo = need_repo();
    $data = stream_get_contents(STDIN);
    $err = mktag_verify($repo, $data, $strict);
    if ($err !== null) {
        fwrite(STDERR, "error: $err\n");
        fwrite(STDERR, "fatal: tag on stdin did not pass our strict fsck check\n");
        return 128;
    }
    out($repo->odb->write('tag', $data) . "\n");
    return 0;
}

/** Structural checks git's fsck applies to a tag object. */
function mktag_verify(Repo $repo, string $data, bool $strict): ?string {
    $hexsz = $repo->hexsz;
    $pos = 0;
    if (strncmp($data, 'object ', 7) !== 0) return 'char0: does not start with "object "';
    $oid = substr($data, 7, $hexsz);
    if (!preg_match('/^[0-9a-f]{' . $hexsz . '}$/', $oid)) return 'char7: could not get object ID';
    $pos = 7 + $hexsz;
    if (($data[$pos] ?? '') !== "\n") return 'char' . $pos . ': did not find expected newline';
    $pos++;
    if (strncmp(substr($data, $pos), 'type ', 5) !== 0) return 'char' . $pos . ': could not find "type "';
    $pos += 5;
    $nl = strpos($data, "\n", $pos);
    if ($nl === false) return 'char' . $pos . ': could not find next "\n"';
    $type = substr($data, $pos, $nl - $pos);
    if (!in_array($type, ['blob', 'tree', 'commit', 'tag'], true)) {
        return 'char' . $pos . ': invalid object type "' . $type . '"';
    }
    $pos = $nl + 1;
    if (strncmp(substr($data, $pos), 'tag ', 4) !== 0) return 'char' . $pos . ': no "tag " found';
    $pos += 4;
    $nl = strpos($data, "\n", $pos);
    if ($nl === false) return 'char' . $pos . ': could not find next "\n"';
    $tagName = substr($data, $pos, $nl - $pos);
    if ($tagName === '') return 'char' . $pos . ': invalid tag name';
    if ($strict && !check_ref_format('refs/tags/' . $tagName, false)) {
        return 'char' . $pos . ': invalid refname format';
    }
    $pos = $nl + 1;
    if (strncmp(substr($data, $pos), 'tagger ', 7) === 0) {
        $pos += 7;
        $nl = strpos($data, "\n", $pos);
        if ($nl === false) return 'char' . $pos . ': could not find next "\n"';
        $ident = substr($data, $pos, $nl - $pos);
        $e = ident_verify($ident);
        if ($e !== null) return 'char' . $pos . ': ' . $e;
        $pos = $nl + 1;
    } elseif ($strict) {
        return 'char' . $pos . ': could not find "tagger "';
    }
    if ($pos < strlen($data) && $data[$pos] !== "\n") {
        return 'char' . $pos . ': trailing garbage in tag header';
    }
    if ($strict) {
        $o = $repo->odb->readHeader($oid);
        if ($o === null) return 'could not read tagged object \'' . $oid . '\'';
        if ($o[0] !== $type) return 'object \'' . $oid . '\' tagged as \'' . $type . '\', but is a \'' . $o[0] . '\' type';
    }
    return null;
}

/** The ident checks fsck applies: "Name <email> timestamp tz". */
function ident_verify(string $ident): ?string {
    $lt = strpos($ident, '<');
    $gt = strpos($ident, '>');
    if ($lt === false) return 'missing "<" in committer/tagger field';
    if ($gt === false || $gt < $lt) return 'missing ">" in committer/tagger field';
    if ($lt > 0 && $ident[$lt - 1] !== ' ') return 'missing space before email';
    $rest = substr($ident, $gt + 1);
    if ($rest === '') return 'missing timestamp';
    if ($rest[0] !== ' ') return 'missing space before date';
    $rest = substr($rest, 1);
    if (!preg_match('/^\d+ [+-]\d{4}$/', $rest)) {
        if (preg_match('/^\d+$/', $rest)) return 'missing timezone';
        return 'invalid date format';
    }
    return null;
}

<?php
// git-config(1): the classic option form plus the 2.46 subcommand form.

function config_target_file(Repo $repo, string $scope, ?string $file): string {
    if ($file !== null) return $file;
    switch ($scope) {
        case 'global':
            $env = getenv('GIT_CONFIG_GLOBAL');
            if ($env !== false && $env !== '') return $env;
            $home = getenv('HOME') ?: '';
            $xdg = getenv('XDG_CONFIG_HOME');
            if ($xdg !== false && $xdg !== '' && is_file("$xdg/git/config")) return "$xdg/git/config";
            if ($home !== '' && is_file("$home/.config/git/config") && !is_file("$home/.gitconfig")) return "$home/.config/git/config";
            return "$home/.gitconfig";
        case 'system': return '/etc/gitconfig';
        case 'worktree':
            if (isset($repo->extensions['worktreeconfig']) && config_bool($repo->extensions['worktreeconfig'], false)) {
                return $repo->path('config.worktree');
            }
            return $repo->commonPath('config');
        default: return $repo->commonPath('config');
    }
}

function cmd_config(array $argv): int {
    $scope = 'local'; $file = null; $action = null; $type = null; $z = false;
    $nameOnly = false; $showOrigin = false; $showScope = false; $default = null;
    $args = []; $all = false; $fixedValue = false;
    $sub = null;
    if ($argv && in_array($argv[0], ['get', 'set', 'unset', 'list', 'rename-section', 'remove-section', 'edit'], true)) {
        $sub = array_shift($argv);
    }
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        switch (true) {
            case $a === '--global': $scope = 'global'; break;
            case $a === '--system': $scope = 'system'; break;
            case $a === '--local': $scope = 'local'; break;
            case $a === '--worktree': $scope = 'worktree'; break;
            case $a === '-f' || $a === '--file': $file = $argv[++$i] ?? ''; break;
            case strncmp($a, '--file=', 7) === 0: $file = substr($a, 7); break;
            case $a === '--get': $action = 'get'; break;
            case $a === '--get-all': $action = 'get-all'; break;
            case $a === '--get-regexp': $action = 'get-regexp'; break;
            case $a === '--add': $action = 'add'; break;
            case $a === '--unset': $action = 'unset'; break;
            case $a === '--unset-all': $action = 'unset-all'; break;
            case $a === '--replace-all': $action = 'replace-all'; break;
            case $a === '-l' || $a === '--list': $action = 'list'; break;
            case $a === '--remove-section': $action = 'remove-section'; break;
            case $a === '--rename-section': $action = 'rename-section'; break;
            case $a === '-e' || $a === '--edit': $action = 'edit'; break;
            case $a === '--bool': $type = 'bool'; break;
            case $a === '--int': $type = 'int'; break;
            case $a === '--bool-or-int': $type = 'bool-or-int'; break;
            case $a === '--path': $type = 'path'; break;
            case strncmp($a, '--type=', 7) === 0: $type = substr($a, 7); break;
            case $a === '--null' || $a === '-z': $z = true; break;
            case $a === '--name-only': $nameOnly = true; break;
            case $a === '--show-origin': $showOrigin = true; break;
            case $a === '--show-scope': $showScope = true; break;
            case $a === '--all': $all = true; break;
            case $a === '--value': $args[] = '--value'; break;
            case strncmp($a, '--default=', 10) === 0: $default = substr($a, 10); break;
            case $a === '--fixed-value': $fixedValue = true; break;
            case $a === '--includes' || $a === '--no-includes': break;
            case $a === '--': break;
            case $a !== '' && $a[0] === '-':
                usage_die("error: unknown option `" . ltrim($a, '-') . "'\nusage: git config [<options>]", 129);
            default: $args[] = $a;
        }
    }
    if ($sub !== null) {
        $map = ['get' => ($all ? 'get-all' : 'get'), 'set' => 'set', 'unset' => ($all ? 'unset-all' : 'unset'),
                'list' => 'list', 'rename-section' => 'rename-section',
                'remove-section' => 'remove-section', 'edit' => 'edit'];
        $action = $map[$sub];
    }
    $repo = null;
    if ($file === null && $scope === 'local') {
        $d = Repo::discover();
        if ($d === null) {
            if ($action === 'list') { $repo = null; }
            else die_git('not in a git directory');
        } else $repo = Repo::open($d[0], $d[1]);
    } else {
        $d = Repo::discover();
        if ($d !== null) $repo = Repo::open($d[0], $d[1]);
    }
    if ($repo === null) {
        $repo = new Repo();
        $repo->gitdir = getcwd(); $repo->commondir = getcwd();
        $repo->config = new Config();
        $repo->extensions = [];
    }
    $target = config_target_file($repo, $scope, $file);

    if ($action === null) {
        // classic positional form
        if (count($args) === 0) usage_die('usage: git config [<options>]', 129);
        $action = count($args) >= 2 ? 'set' : 'get';
    }

    $readAll = function () use ($repo, $scope, $file, $target): array {
        // returns list of [key, value, origin, scope]
        $rows = [];
        $addFile = function (string $p, string $sc) use (&$rows) {
            $cf = ConfigFile::load($p);
            if (!$cf->exists) return;
            foreach ($cf->entries as $e) $rows[] = [$e->key(), is_bool($e->value) ? 'true' : $e->value, $p, $sc];
        };
        if ($file !== null) { $addFile($file, 'command'); return $rows; }
        if ($scope !== 'local' && $scope !== 'all') {
            $addFile($target, $scope);
            return $rows;
        }
        if (!env_bool('GIT_CONFIG_NOSYSTEM')) $addFile('/etc/gitconfig', 'system');
        foreach (config_global_paths() as $p) $addFile($p, 'global');
        $addFile($repo->commonPath('config'), 'local');
        if (isset($repo->extensions['worktreeconfig']) && config_bool($repo->extensions['worktreeconfig'], false)) {
            $addFile($repo->path('config.worktree'), 'worktree');
        }
        return $rows;
    };

    $fmtValue = function ($v) use ($type) {
        if ($type === null) return $v;
        switch ($type) {
            case 'bool': return config_bool($v, false) ? 'true' : 'false';
            case 'int': return (string)config_int($v);
            case 'bool-or-int':
                if (preg_match('/^-?\d+$/', trim((string)$v))) return (string)config_int($v);
                return config_bool($v, false) ? 'true' : 'false';
            case 'path':
                $p = (string)$v;
                if ($p !== '' && $p[0] === '~') $p = (getenv('HOME') ?: '') . substr($p, 1);
                return $p;
            default: return $v;
        }
    };
    $term = $z ? "\0" : "\n";

    switch ($action) {
        case 'list': {
            $rows = $readAll();
            if ($scope === 'local' && $file === null) { /* all scopes */ }
            foreach ($rows as [$k, $v, $origin, $sc]) {
                $line = '';
                if ($showScope) $line .= $sc . ($z ? "\0" : "\t");
                if ($showOrigin) $line .= "file:$origin" . ($z ? "\0" : "\t");
                if ($nameOnly) $line .= $k;
                elseif ($z) $line .= $k . "\n" . $fmtValue($v);
                else $line .= $k . '=' . $fmtValue($v);
                out($line . $term);
            }
            return 0;
        }
        case 'get': case 'get-all': {
            $key = $args[0] ?? '';
            $valuePattern = $args[1] ?? null;
            $p = config_split_key($key);
            if ($p === false) { fwrite(STDERR, "error: key does not contain a section: $key\n"); return 1; }
            $rows = $readAll();
            $hits = [];
            foreach ($rows as [$k, $v, $origin, $sc]) {
                if (strtolower_key($k) !== strtolower_key($key)) continue;
                if ($valuePattern !== null) {
                    $neg = false; $re = $valuePattern;
                    if ($re !== '' && $re[0] === '!') { $neg = true; $re = substr($re, 1); }
                    $m = $fixedValue ? ($v === $re) : (@preg_match('/' . str_replace('/', '\/', $re) . '/', $v) === 1);
                    if ($neg ? $m : !$m) continue;
                }
                $hits[] = [$k, $v, $origin, $sc];
            }
            if (!$hits) {
                if ($default !== null) { out($fmtValue($default) . $term); return 0; }
                return 1;
            }
            if ($action === 'get') $hits = [end($hits)];
            foreach ($hits as [$k, $v, $origin, $sc]) {
                $line = '';
                if ($showScope) $line .= $sc . ($z ? "\0" : "\t");
                if ($showOrigin) $line .= "file:$origin" . ($z ? "\0" : "\t");
                $line .= $nameOnly ? $k : $fmtValue($v);
                out($line . $term);
            }
            return 0;
        }
        case 'get-regexp': {
            $pattern = $args[0] ?? '';
            $rows = $readAll();
            $found = false;
            foreach ($rows as [$k, $v, $origin, $sc]) {
                if (@preg_match('/' . str_replace('/', '\/', $pattern) . '/', $k) !== 1) continue;
                $found = true;
                $line = '';
                if ($showScope) $line .= $sc . ($z ? "\0" : "\t");
                if ($showOrigin) $line .= "file:$origin" . ($z ? "\0" : "\t");
                $line .= $nameOnly ? $k : ($k . ($z ? "\n" : ' ') . $fmtValue($v));
                out($line . $term);
            }
            return $found ? 0 : 1;
        }
        case 'set': case 'add': case 'replace-all': {
            $key = $args[0] ?? '';
            $value = $args[1] ?? null;
            $valuePattern = $args[2] ?? null;
            $p = config_split_key($key);
            if ($p === false) { fwrite(STDERR, "error: key does not contain a section: $key\n"); return 1; }
            if ($value === null) usage_die('usage: git config [<options>]', 129);
            // git records the spelling the user typed; lookups stay case-insensitive
            $spelled = config_split_key_verbatim($key);
            if ($type === 'bool') {
                if (!in_array(strtolower($value), ['true','false','yes','no','on','off','1','0'], true)) {
                    fwrite(STDERR, "fatal: bad boolean config value '$value' for '$key'\n");
                    return 128;
                }
                $value = config_bool($value, false) ? 'true' : 'false';
            }
            if ($type === 'int') {
                if (!preg_match('/^-?\d+[kmgKMG]?$/', $value)) { fwrite(STDERR, "fatal: bad numeric config value '$value' for '$key'\n"); return 128; }
                $value = (string)config_int($value);
            }
            $cf = ConfigFile::load($target);
            $cf->path = $target;
            $ok = $cf->setValue($spelled[0], $spelled[1], $spelled[2], $value, $action === 'add', $valuePattern, $action === 'replace-all');
            if (!$ok) {
                fwrite(STDERR, "warning: $key has multiple values\n");
                fwrite(STDERR, "fatal: cannot overwrite multiple values with a single value\n       Use a regexp, --add or --replace-all to change $key.\n");
                return 5;
            }
            if (!$cf->save()) { fwrite(STDERR, "error: could not write config file $target\n"); return 4; }
            return 0;
        }
        case 'unset': case 'unset-all': {
            $key = $args[0] ?? '';
            $valuePattern = $args[1] ?? null;
            $p = config_split_key($key);
            if ($p === false) { fwrite(STDERR, "error: key does not contain a section: $key\n"); return 1; }
            $cf = ConfigFile::load($target);
            $cf->path = $target;
            $n = $cf->unset($p[0], $p[1], $p[2], $valuePattern, $action === 'unset-all');
            if ($n === 0) return 5;
            if ($n < 0) {
                fwrite(STDERR, "warning: $key has multiple values\n");
                return 5;
            }
            if (!$cf->save()) return 4;
            return 0;
        }
        case 'remove-section': {
            $section = $args[0] ?? '';
            $cf = ConfigFile::load($target);
            $cf->path = $target;
            $sub = null;
            if (strpos($section, '.') !== false) { $parts = explode('.', $section, 2); $section = $parts[0]; $sub = $parts[1]; }
            if (!$cf->removeSection($section, $sub)) {
                fwrite(STDERR, "fatal: no such section: $section\n");
                return 128;
            }
            if (!$cf->save()) return 4;
            return 0;
        }
        case 'rename-section': {
            $from = $args[0] ?? ''; $to = $args[1] ?? '';
            $cf = ConfigFile::load($target);
            $cf->path = $target;
            $split = function (string $s) {
                if (strpos($s, '.') !== false) { $p = explode('.', $s, 2); return [$p[0], $p[1]]; }
                return [$s, null];
            };
            [$fs, $fsub] = $split($from);
            [$ts, $tsub] = $split($to);
            if (!$cf->renameSection($fs, $fsub, $ts, $tsub)) {
                fwrite(STDERR, "fatal: no such section: $from\n");
                return 128;
            }
            if (!$cf->save()) return 4;
            return 0;
        }
        case 'edit':
            return 0;
    }
    return 0;
}

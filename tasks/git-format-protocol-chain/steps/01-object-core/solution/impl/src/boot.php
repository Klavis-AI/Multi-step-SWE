<?php
// Entry point: global options (git(1)), then dispatch to a builtin.

$SRC = __DIR__;
require "$SRC/util.php";
require "$SRC/config.php";
require "$SRC/objects.php";
require "$SRC/repo.php";
require "$SRC/odb.php";
// modules that later milestones add; absent in earlier snapshots
foreach (["pack", "proto", "transport", "protov2", "ewah", "bitmap", "graph", "midx"] as $optional) {
    $f = "$SRC/$optional.php";
    if (is_file($f)) require $f;
}
require "$SRC/refs.php";
if (is_file("$SRC/reftable.php")) require "$SRC/reftable.php";
require "$SRC/index.php";
require "$SRC/pathspec.php";
require "$SRC/diffcore.php";
require "$SRC/revwalk.php";
require "$SRC/worktree.php";
require "$SRC/merge.php";
foreach (glob("$SRC/cmd/*.php") as $f) require $f;

// git dies on SIGPIPE like any well-behaved CLI; PHP ignores it by default.
if (function_exists('pcntl_signal')) {
    @pcntl_signal(SIGPIPE, SIG_DFL);
}

$GIT_OPTS = [
    'git_dir' => null,
    'work_tree' => null,
    'config' => [],
    'bare' => false,
    'literal_pathspecs' => false,
    'namespace' => null,
    'no_replace' => false,
];

const GIT_USAGE = <<<'EOT'
usage: git [-v | --version] [-h | --help] [-C <path>] [-c <name>=<value>]
           [--exec-path[=<path>]] [--html-path] [--man-path] [--info-path]
           [-p | --paginate | -P | --no-pager] [--no-replace-objects] [--no-lazy-fetch]
           [--no-optional-locks] [--no-advice] [--bare] [--git-dir=<path>]
           [--work-tree=<path>] [--namespace=<name>] [--config-env=<name>=<envvar>]
           <command> [<args>]
EOT;

function git_main(array $argv): int {
    global $GIT_OPTS;
    array_shift($argv);
    while ($argv) {
        $a = $argv[0];
        if ($a === '--') { array_shift($argv); break; }
        if (strlen($a) === 0 || $a[0] !== '-') break;
        array_shift($argv);
        if ($a === '-C') {
            $d = array_shift($argv);
            if ($d === null) usage_die("fatal: no directory given for '-C' option", 129);
            if ($d !== '' && !@chdir($d)) die_git("cannot change to '$d': No such file or directory");
            continue;
        }
        if ($a === '-c') {
            $kv = array_shift($argv);
            if ($kv === null) usage_die("fatal: no config key given for -c", 129);
            $GIT_OPTS['config'][] = $kv;
            continue;
        }
        if (strncmp($a, '--git-dir=', 10) === 0) { $GIT_OPTS['git_dir'] = substr($a, 10); continue; }
        if ($a === '--git-dir') { $GIT_OPTS['git_dir'] = array_shift($argv); continue; }
        if (strncmp($a, '--work-tree=', 12) === 0) { $GIT_OPTS['work_tree'] = substr($a, 12); continue; }
        if ($a === '--work-tree') { $GIT_OPTS['work_tree'] = array_shift($argv); continue; }
        if (strncmp($a, '--namespace=', 12) === 0) { $GIT_OPTS['namespace'] = substr($a, 12); continue; }
        if ($a === '--bare') { $GIT_OPTS['bare'] = true; continue; }
        if ($a === '--literal-pathspecs') { $GIT_OPTS['literal_pathspecs'] = true; continue; }
        if ($a === '--no-replace-objects') { $GIT_OPTS['no_replace'] = true; continue; }
        if ($a === '--no-pager' || $a === '-P' || $a === '-p' || $a === '--paginate'
            || $a === '--no-optional-locks' || $a === '--no-advice' || $a === '--no-lazy-fetch'
            || $a === '--glob-pathspecs' || $a === '--noglob-pathspecs' || $a === '--icase-pathspecs') {
            continue;
        }
        if ($a === '--exec-path') { out(git_exec_path() . "\n"); return 0; }
        if (strncmp($a, '--exec-path=', 12) === 0) { putenv('GIT_EXEC_PATH=' . substr($a, 12)); continue; }
        if ($a === '--version' || $a === '-v') { out("git version " . GIT_VERSION . "\n"); return 0; }
        if ($a === '--help' || $a === '-h') { out(GIT_USAGE . "\n"); return 0; }
        if (strncmp($a, '--config-env=', 13) === 0) {
            $spec = substr($a, 13);
            $eq = strpos($spec, '=');
            if ($eq === false) usage_die("fatal: invalid config format: $spec", 129);
            $k = substr($spec, 0, $eq); $envname = substr($spec, $eq + 1);
            $v = getenv($envname);
            if ($v === false) die_git("missing environment variable '$envname' for configuration '$k'");
            $GIT_OPTS['config'][] = "$k=$v";
            continue;
        }
        fwrite(STDERR, "unknown option: $a\n" . GIT_USAGE . "\n");
        return 129;
    }
    if (!$argv) {
        fwrite(STDERR, GIT_USAGE . "\n");
        return 1;
    }
    $cmd = array_shift($argv);
    return git_dispatch($cmd, $argv);
}

function git_exec_path(): string {
    $e = getenv('GIT_EXEC_PATH');
    if ($e !== false && $e !== '') return $e;
    // boot.php lives in <root>/src, and the build puts the shims in <root>/out
    return dirname(__DIR__) . '/out/libexec/git-core';
}

/**
 * Expand bundled short options ("-qm msg" -> "-q" "-m" "msg"), the way git's
 * parse-options does. $withArg lists the short flags that take a value.
 */
function expand_short_opts(array $argv, string $withArg): array {
    $out = [];
    $seenDD = false;
    foreach ($argv as $a) {
        if ($seenDD || strlen($a) < 3 || $a[0] !== '-' || $a[1] === '-') { $out[] = $a; if ($a === '--') $seenDD = true; continue; }
        // a bundle only if every character is a plain short flag
        $chars = substr($a, 1);
        $ok = true;
        for ($i = 0; $i < strlen($chars); $i++) {
            $c = $chars[$i];
            if (!ctype_alpha($c)) { $ok = false; break; }
            if (strpos($withArg, $c) !== false) {
                // the rest of the bundle is this flag's value
                break;
            }
        }
        if (!$ok) { $out[] = $a; continue; }
        for ($i = 0; $i < strlen($chars); $i++) {
            $c = $chars[$i];
            $out[] = '-' . $c;
            if (strpos($withArg, $c) !== false) {
                $rest = substr($chars, $i + 1);
                if ($rest !== '') $out[] = $rest;
                break;
            }
        }
    }
    return $out;
}

function git_dispatch(string $cmd, array $argv): int {
    $table = git_command_table();
    // A command whose module is not part of this build is not a command: the
    // table is shared, the modules are not.
    if (!isset($table[$cmd]) || !function_exists($table[$cmd])) {
        fwrite(STDERR, "git: '$cmd' is not a git command. See 'git --help'.\n");
        return 1;
    }
    $shortWithArg = [
        'commit' => 'mFCc', 'tag' => 'mFn', 'branch' => 'u', 'checkout' => 'bB',
        'switch' => 'c', 'log' => 'n', 'rev-list' => 'n', 'config' => 'f',
        'update-ref' => 'm', 'symbolic-ref' => 'm', 'merge' => 'ms',
        'ls-files' => 'x', 'show' => 'n', 'reset' => '', 'add' => '',
        'ls-tree' => '', 'hash-object' => 't', 'cat-file' => '',
    ];
    if (isset($shortWithArg[$cmd])) $argv = expand_short_opts($argv, $shortWithArg[$cmd]);
    // `git <cmd> -h` prints the synopsis and exits 129, like parse-options
    foreach ($argv as $a) {
        if ($a === '--') break;
        if ($a === '-h' || $a === '--help') {
            out('usage: ' . git_usage_text($cmd) . "\n");
            return 129;
        }
    }
    $fn = $table[$cmd];
    $r = $fn($argv);
    return is_int($r) ? $r : 0;
}

/** Open the repository the global options point at; die like git when there is none. */
function need_repo(bool $needWorktree = false): Repo {
    global $GIT_OPTS;
    static $repo = null;
    if ($repo !== null) return $repo;
    $gd = $GIT_OPTS['git_dir'];
    if ($gd === null) {
        $env = getenv('GIT_DIR');
        if ($env !== false && $env !== '') $gd = $env;
    }
    if ($gd !== null) {
        if (!Repo::isGitDirectory(path_absolute($gd))) {
            die_git("not a git repository: '" . $gd . "'");
        }
        $wt = $GIT_OPTS['work_tree'];
        if ($wt === null) {
            $envwt = getenv('GIT_WORK_TREE');
            if ($envwt !== false && $envwt !== '') $wt = $envwt;
            elseif (!$GIT_OPTS['bare']) $wt = getcwd();
        }
        $repo = Repo::open($gd, $GIT_OPTS['bare'] ? null : $wt);
    } else {
        $d = Repo::discover();
        if ($d === null) {
            die_git('not a git repository (or any of the parent directories): .git');
        }
        $wt = $GIT_OPTS['work_tree'] ?? $d[1];
        $repo = Repo::open($d[0], $GIT_OPTS['bare'] ? null : $wt);
    }
    foreach ($GIT_OPTS['config'] as $kv) {
        $eq = strpos($kv, '=');
        if ($eq === false) { $repo->config->addString("[" . str_replace('.', '] ', $kv) . " = true\n"); continue; }
        $k = substr($kv, 0, $eq); $v = substr($kv, $eq + 1);
        $p = config_split_key($k);
        if ($p === false) die_git("invalid config format: $k");
        $sec = $p[0] . ($p[1] !== null ? ' "' . $p[1] . '"' : '');
        $repo->config->addString("[$sec]\n\t{$p[2]} = " . ConfigFile::quoteValue($v) . "\n");
    }
    if ($needWorktree && $repo->isBare()) {
        die_git('this operation must be run in a work tree');
    }
    return $repo;
}

function committer_ident(Repo $repo): array {
    $name = getenv('GIT_COMMITTER_NAME');
    if ($name === false || $name === '') $name = $repo->config->get('user.name', null);
    $email = getenv('GIT_COMMITTER_EMAIL');
    if ($email === false || $email === '') $email = $repo->config->get('user.email', null);
    if ($name === null || $name === '' || is_bool($name)) $name = default_ident_name();
    if ($email === null || $email === '' || is_bool($email)) $email = default_ident_email();
    $dateEnv = getenv('GIT_COMMITTER_DATE');
    if ($dateEnv !== false && $dateEnv !== '') {
        $d = parse_date($dateEnv);
        if ($d === false) die_git("invalid date format: $dateEnv");
        return [$name, $email, $d[0], $d[1]];
    }
    return [$name, $email, time(), now_tz()];
}

function author_ident(Repo $repo): array {
    $name = getenv('GIT_AUTHOR_NAME');
    if ($name === false || $name === '') $name = $repo->config->get('user.name', null);
    $email = getenv('GIT_AUTHOR_EMAIL');
    if ($email === false || $email === '') $email = $repo->config->get('user.email', null);
    if ($name === null || $name === '' || is_bool($name)) $name = default_ident_name();
    if ($email === null || $email === '' || is_bool($email)) $email = default_ident_email();
    $dateEnv = getenv('GIT_AUTHOR_DATE');
    if ($dateEnv !== false && $dateEnv !== '') {
        $d = parse_date($dateEnv);
        if ($d === false) die_git("invalid date format: $dateEnv");
        return [$name, $email, $d[0], $d[1]];
    }
    return [$name, $email, time(), now_tz()];
}

function default_ident_name(): string {
    $u = @posix_getpwuid(posix_getuid());
    if ($u && !empty($u['gecos'])) return explode(',', $u['gecos'])[0];
    return $u ? $u['name'] : 'unknown';
}
function default_ident_email(): string {
    $u = @posix_getpwuid(posix_getuid());
    $host = @gethostname() ?: 'localhost';
    return ($u ? $u['name'] : 'unknown') . '@' . $host;
}

const GIT_VERSION = '2.51.0';

register_shutdown_function('out_flush');
exit(git_main($argv));

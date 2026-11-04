<?php
// git-init(1)

function cmd_init(array $argv): int {
    global $GIT_OPTS;
    $bare = $GIT_OPTS['bare'];
    $template = null;
    $initialBranch = null;
    $objectFormat = null;
    $refFormat = null;
    $separate = null;
    $shared = null;
    $quiet = false;
    $dir = null;
    for ($i = 0; $i < count($argv); $i++) {
        $a = $argv[$i];
        if ($a === '--bare') { $bare = true; continue; }
        if ($a === '-q' || $a === '--quiet') { $quiet = true; continue; }
        if (strncmp($a, '--template=', 11) === 0) { $template = substr($a, 11); continue; }
        if ($a === '--template') { $template = $argv[++$i] ?? ''; continue; }
        if (strncmp($a, '--initial-branch=', 17) === 0) { $initialBranch = substr($a, 17); continue; }
        if ($a === '-b' || $a === '--initial-branch') { $initialBranch = $argv[++$i] ?? ''; continue; }
        if (strncmp($a, '--object-format=', 16) === 0) {
            $objectFormat = substr($a, 16);
            if (strtolower($objectFormat) !== 'sha1') die_git("unknown hash algorithm '" . $objectFormat . "'");
            continue;
        }
        if (strncmp($a, '--ref-format=', 13) === 0) { $refFormat = substr($a, 13); continue; }
        if (strncmp($a, '--separate-git-dir=', 19) === 0) { $separate = substr($a, 19); continue; }
        if ($a === '--separate-git-dir') { $separate = $argv[++$i] ?? ''; continue; }
        if ($a === '--shared') { $shared = 'group'; continue; }
        if (strncmp($a, '--shared=', 9) === 0) { $shared = substr($a, 9); continue; }
        if ($a === '--') { continue; }
        if ($a !== '' && $a[0] === '-') usage_die("error: unknown switch `" . ltrim($a, '-') . "'\nusage: git init [-q | --quiet] [--bare] [--template=<template-directory>] [--shared[=<permissions>]] [<directory>]", 129);
        $dir = $a;
    }
    if ($objectFormat !== null && $objectFormat !== 'sha1') {
        die_git("unknown hash algorithm '$objectFormat'");
    }
    if ($refFormat !== null && $refFormat !== 'files' && $refFormat !== 'reftable') {
        die_git("unknown ref storage format '$refFormat'");
    }
    if ($refFormat === 'reftable' && !function_exists('reftable_init_repo')) {
        die_git("unknown ref storage format 'reftable'");
    }

    $cwd = getcwd();
    if ($dir !== null && $dir !== '') {
        if (!mkdir_p($dir)) die_git("cannot mkdir $dir: " . (is_dir($dir) ? '' : 'No such file or directory'));
        if (!@chdir($dir)) die_git("cannot chdir to $dir");
    }
    $root = getcwd();

    $envGitDir = $GIT_OPTS['git_dir'] ?? (getenv('GIT_DIR') !== false && getenv('GIT_DIR') !== '' ? getenv('GIT_DIR') : null);
    // an explicit directory argument wins over GIT_DIR
    if ($dir !== null && $dir !== '' && $GIT_OPTS['git_dir'] === null) $envGitDir = null;
    if ($envGitDir !== null) {
        $gitdir = path_absolute($envGitDir);
        $worktree = $bare ? null : (getenv('GIT_WORK_TREE') !== false && getenv('GIT_WORK_TREE') !== '' ? path_absolute(getenv('GIT_WORK_TREE')) : $root);
    } elseif ($bare) {
        $gitdir = $root;
        $worktree = null;
    } else {
        $gitdir = $root . '/.git';
        $worktree = $root;
    }
    $reinit = Repo::isGitDirectory($gitdir);

    if ($separate !== null && $separate !== '' && !$bare) {
        $target = path_absolute($separate);
        mkdir_p($target);
        write_file_atomic($root . '/.git', 'gitdir: ' . $target . "\n");
        $gitdir = $target;
    }

    mkdir_p($gitdir);
    mkdir_p("$gitdir/objects/info");
    mkdir_p("$gitdir/objects/pack");
    mkdir_p("$gitdir/refs/heads");
    mkdir_p("$gitdir/refs/tags");
    mkdir_p("$gitdir/info");

    // templates: --template, init.templateDir, GIT_TEMPLATE_DIR, built-in default
    if ($template === null) {
        $t = getenv('GIT_TEMPLATE_DIR');
        if ($t !== false && $t !== '') $template = $t;
    }
    if ($template !== null && $template !== '' && is_dir($template)) {
        copy_template($template, $gitdir);
    }
    mkdir_p("$gitdir/hooks");
    if (!is_file("$gitdir/info/exclude")) {
        write_file_atomic("$gitdir/info/exclude",
            "# git ls-files --others --exclude-from=.git/info/exclude\n"
            . "# Lines that start with '#' are comments.\n"
            . "# For a project mostly in C, the following would be a good set of\n"
            . "# exclude patterns (uncomment them if you want to use them):\n"
            . "# *.[oa]\n# *~\n");
    }
    if (!is_file("$gitdir/description")) {
        write_file_atomic("$gitdir/description",
            "Unnamed repository; edit this file 'description' to name the repository.\n");
    }

    $branch = $initialBranch;
    if ($branch === null) {
        $cfg = new Config();
        foreach (config_global_paths() as $p) $cfg->addFile($p);
        if (!env_bool('GIT_CONFIG_NOSYSTEM')) $cfg->addFile('/etc/gitconfig');
        $b = $cfg->get('init.defaultbranch', null);
        if ($b !== null && !is_bool($b) && $b !== '') $branch = $b;
        else {
            $env = getenv('GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME');
            if ($env !== false && $env !== '') $branch = $env;
            else {
                $branch = 'master';
                if (!$quiet && !$reinit && $cfg->get('advice.defaultbranchname', null) === null) {
                    fwrite(STDERR, init_default_branch_advice());
                }
            }
        }
    }
    if (!check_ref_format("refs/heads/$branch")) die_git("invalid branch name: $branch");
    if (!is_file("$gitdir/HEAD") || !$reinit) {
        write_file_atomic("$gitdir/HEAD", "ref: refs/heads/$branch\n");
    }

    // config
    $cf = ConfigFile::load("$gitdir/config");
    $cf->setValue('core', null, 'repositoryformatversion', $refFormat === 'reftable' ? '1' : '0');
    $cf->setValue('core', null, 'filemode', filemode_supported($gitdir) ? 'true' : 'false');
    $cf->setValue('core', null, 'bare', $bare ? 'true' : 'false');
    if (!$bare) $cf->setValue('core', null, 'logallrefupdates', 'true');
    if ($worktree !== null && $envGitDir !== null) {
        $cf->setValue('core', null, 'worktree', $worktree);
    }
    
    if ($refFormat === 'reftable') $cf->setValue('extensions', null, 'refStorage', 'reftable');
    $cf->save();

    if ($refFormat === 'reftable') {
        reftable_init_repo($gitdir, "refs/heads/$branch", $objectFormat ?? 'sha1');
    }

    if (!$quiet) {
        $what = $reinit ? 'Reinitialized existing' : 'Initialized empty';
        $shown = $bare ? rtrim($gitdir, '/') . '/' : rtrim($gitdir, '/') . '/';
        out("$what Git repository in $shown\n");
    }
    return 0;
}

function init_default_branch_advice(): string {
    return "hint: Using 'master' as the name for the initial branch. This default branch name\n"
         . "hint: is subject to change. To configure the initial branch name to use in all\n"
         . "hint: of your new repositories, which will suppress this warning, call:\n"
         . "hint:\n"
         . "hint: \tgit config --global init.defaultBranch <name>\n"
         . "hint:\n"
         . "hint: Names commonly chosen instead of 'master' are 'main', 'trunk' and\n"
         . "hint: 'development'. The just-created branch can be renamed via this command:\n"
         . "hint:\n"
         . "hint: \tgit branch -m <name>\n"
         . "hint:\n"
         . "hint: Disable this message with \"git config set advice.defaultBranchName false\"\n";
}

function filemode_supported(string $gitdir): bool {
    $p = "$gitdir/.filemode-probe";
    if (@file_put_contents($p, 'x') === false) return true;
    @chmod($p, 0644);
    clearstatcache(true, $p);
    @chmod($p, 0755);
    clearstatcache(true, $p);
    $st = @stat($p);
    $ok = $st !== false && ($st['mode'] & 0111) !== 0;
    @unlink($p);
    return $ok;
}

function copy_template(string $src, string $dst) {
    $it = @scandir($src);
    if ($it === false) return;
    foreach ($it as $e) {
        if ($e === '.' || $e === '..') continue;
        $s = "$src/$e"; $d = "$dst/$e";
        if (is_dir($s)) { mkdir_p($d); copy_template($s, $d); continue; }
        if (file_exists($d)) continue;
        @copy($s, $d);
        $st = @stat($s);
        if ($st !== false) @chmod($d, $st['mode'] & 0777);
    }
}

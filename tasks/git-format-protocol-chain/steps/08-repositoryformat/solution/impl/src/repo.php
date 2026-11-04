<?php
// Repository discovery, layout and format-version handling
// (gitrepository-layout(5), Documentation/technical/repository-version.adoc).

class Repo {
    public string $gitdir;        // absolute path, no trailing slash
    public ?string $worktree;     // absolute path or null when bare
    public string $commondir;     // $GIT_COMMON_DIR or gitdir
    public Config $config;
    public string $hashAlgo = 'sha1';
    public int $rawsz = 20;
    public int $hexsz = 40;
    public int $formatVersion = 0;
    public array $extensions = [];     // lowercase name => value
    public ObjectDB $odb;
    public RefStore $refs;
    public ?Index $indexObj = null;
    public string $refFormat = 'files';
    public ?string $compatAlgo = null;

    public static ?Repo $current = null;
    public static int $rawsz_static = 20;
    public static int $hexsz_static = 40;

    /** Extensions git accepts, and whether they require repositoryformatversion 1. */
    public const KNOWN_EXTENSIONS = [
        'noop' => false,
        'preciousobjects' => false,
        'partialclone' => false,
        // git reads extensions.worktreeConfig whatever the format version is:
        // it predates the v1 gate and setup.c handles it outside it
        'worktreeconfig' => false,
        'objectformat' => true,
        'compatobjectformat' => true,
        'refstorage' => true,
        'relativeworktrees' => true,
        'noop-v1' => true,
    ];

    public function path(string $rel = ''): string {
        return $rel === '' ? $this->gitdir : $this->gitdir . '/' . $rel;
    }
    /** Paths shared between linked worktrees (objects, refs, packed-refs, config). */
    public function commonPath(string $rel = ''): string {
        return $rel === '' ? $this->commondir : $this->commondir . '/' . $rel;
    }
    public function objectsDir(): string {
        $env = getenv('GIT_OBJECT_DIRECTORY');
        if ($env !== false && $env !== '') return path_absolute($env);
        return $this->commonPath('objects');
    }
    public function indexPath(): string {
        $env = getenv('GIT_INDEX_FILE');
        if ($env !== false && $env !== '') return path_absolute($env);
        return $this->path('index');
    }
    public function isBare(): bool { return $this->worktree === null; }

    public function index(): Index {
        if ($this->indexObj === null) $this->indexObj = Index::read($this);
        return $this->indexObj;
    }

    public static function hashName(string $algo): array {
        return $algo === 'sha256' ? [32, 64] : [20, 40];
    }

    /** Hash the canonical object representation. */
    public function hashData(string $type, string $content): string {
        $store = "$type " . strlen($content) . "\0" . $content;
        return hash($this->hashAlgo === 'sha256' ? 'sha256' : 'sha1', $store);
    }
    public function emptyTreeOid(): string {
        return $this->hashAlgo === 'sha256'
            ? '6ef19b41225c5369f1c104d45d8d85efa9b057b53b14b4b9b939dd74decc5321'
            : '4b825dc642cb6eb9a060e54bf8d69288fbee4904';
    }
    public function nullOid(): string { return str_repeat('0', $this->hexsz); }

    /** Does this directory look like a git directory? (setup.c is_git_directory) */
    public static function isGitDirectory(string $dir): bool {
        if (!is_dir($dir)) return false;
        $objDir = getenv('GIT_OBJECT_DIRECTORY');
        if ($objDir === false || $objDir === '') $objDir = "$dir/objects";
        if (!is_dir($objDir)) return false;
        if (!is_dir("$dir/refs")) return false;
        $head = @file_get_contents("$dir/HEAD");
        if ($head === false) return false;
        $head = rtrim($head, "\n");
        if (strncmp($head, 'ref:', 4) === 0) return true;
        return preg_match('/^[0-9a-f]{40}([0-9a-f]{24})?$/', $head) === 1;
    }

    public static function resolveGitFile(string $path): ?string {
        $c = @file_get_contents($path);
        if ($c === false) return null;
        if (strncmp($c, 'gitdir: ', 8) !== 0) return null;
        $p = rtrim(substr($c, 8), "\r\n");
        if ($p === '') return null;
        if ($p[0] !== '/') $p = dirname($path) . '/' . $p;
        return path_normalize($p);
    }

    /** Walk up from $start looking for a repository. */
    public static function discover(?string $start = null): ?array {
        $dir = $start === null ? getcwd() : path_absolute($start);
        $dir = path_real($dir);
        $ceilings = [];
        $ce = getenv('GIT_CEILING_DIRECTORIES');
        if ($ce !== false && $ce !== '') {
            foreach (explode(':', $ce) as $c) if ($c !== '') $ceilings[] = rtrim(path_normalize($c), '/');
        }
        $cur = $dir;
        while (true) {
            foreach ($ceilings as $c) {
                if ($c !== '' && $cur === $c) return null;
            }
            $dotgit = "$cur/.git";
            if (is_dir($dotgit) && self::isGitDirectory($dotgit)) return [$dotgit, $cur];
            if (is_file($dotgit)) {
                $g = self::resolveGitFile($dotgit);
                if ($g !== null && self::isGitDirectory($g)) return [$g, $cur];
                if ($g !== null) die_git("invalid gitfile format: $dotgit");
            }
            if (self::isGitDirectory($cur)) return [$cur, null];
            $parent = dirname($cur);
            if ($parent === $cur) return null;
            $cur = $parent;
        }
    }

    public static function open(string $gitdir, ?string $worktree, bool $verify = true): Repo {
        $r = new Repo();
        $r->gitdir = rtrim(path_absolute($gitdir), '/');
        $r->commondir = $r->gitdir;
        $envCommon = getenv('GIT_COMMON_DIR');
        if ($envCommon !== false && $envCommon !== '') {
            $r->commondir = rtrim(path_absolute($envCommon), '/');
        } elseif (is_file($r->gitdir . '/commondir')) {
            $cd = trim(@file_get_contents($r->gitdir . '/commondir'));
            if ($cd !== '') $r->commondir = rtrim($cd[0] === '/' ? $cd : path_normalize($r->gitdir . '/' . $cd), '/');
        }
        $r->config = new Config();
        global $GIT_OPTS;
        // precedence: system, global, local, worktree, -c
        if (!env_bool('GIT_CONFIG_NOSYSTEM')) {
            $r->config->addFile('/etc/gitconfig');
        }
        foreach (config_global_paths() as $p) $r->config->addFile($p);
        $r->config->addFile($r->commonPath('config'));
        $r->formatVersion = $r->config->getInt('core.repositoryformatversion', 0);
        foreach ($r->config->allKeys() as $k) {
            if (strncmp($k, 'extensions.', 11) === 0) {
                $name = substr($k, 11);
                $r->extensions[$name] = $r->config->get($k);
            }
        }
        if ($verify) $r->verifyFormat();
        if (isset($r->extensions['worktreeconfig']) && config_bool($r->extensions['worktreeconfig'], false)) {
            $r->config->addFile($r->path('config.worktree'));
        }
        if (isset($r->extensions['objectformat'])) {
            $algo = strtolower((string)$r->extensions['objectformat']);
            if ($algo !== 'sha1' && $algo !== 'sha256') die_git("invalid value for 'extensions.objectformat'");
            $r->hashAlgo = $algo;
        }
        if (isset($r->extensions['compatobjectformat'])) {
            $c = strtolower((string)$r->extensions['compatobjectformat']);
            if ($c !== 'sha1' && $c !== 'sha256') die_git("invalid value for 'extensions.compatobjectformat'");
            if ($c !== $r->hashAlgo) $r->compatAlgo = $c;
        }
        if (isset($r->extensions['refstorage'])) {
            $rf = strtolower((string)$r->extensions['refstorage']);
            if ($rf !== 'files' && $rf !== 'reftable') die_git("invalid value for 'extensions.refstorage': '{$r->extensions['refstorage']}'");
            $r->refFormat = $rf;
        }
        [$r->rawsz, $r->hexsz] = self::hashName($r->hashAlgo);
        self::$rawsz_static = $r->rawsz;
        self::$hexsz_static = $r->hexsz;

        if ($worktree !== null) {
            $r->worktree = rtrim(path_absolute($worktree), '/');
        } else {
            $envWT = getenv('GIT_WORK_TREE');
            if ($envWT !== false && $envWT !== '') $r->worktree = rtrim(path_absolute($envWT), '/');
            else {
                $cw = $r->config->get('core.worktree', null);
                if ($cw !== null && !is_bool($cw)) $r->worktree = rtrim(path_normalize($r->gitdir . '/' . $cw), '/');
                else $r->worktree = null;
            }
        }
        if ($r->config->getBool('core.bare', false) && getenv('GIT_WORK_TREE') === false && $worktree === null) {
            $r->worktree = null;
        }
        $r->odb = new ObjectDB($r);
        $r->refs = RefStore::create($r);
        self::$current = $r;
        return $r;
    }

    public function verifyFormat() {
        $unknown = [];
        $v1only = [];
        foreach ($this->extensions as $name => $val) {
            $known = array_key_exists($name, self::KNOWN_EXTENSIONS);
            if (!$known) { $unknown[] = $name; continue; }
            if (self::KNOWN_EXTENSIONS[$name] && $this->formatVersion < 1) $v1only[] = $name;
        }
        if ($this->formatVersion > 1) {
            die_git("Expected git repo version <= 1, found {$this->formatVersion}");
        }
        if ($this->formatVersion >= 1 && $unknown) {
            $msg = "unknown repository extension" . (count($unknown) > 1 ? "s" : "") . " found:";
            foreach ($unknown as $u) $msg .= "\n\t$u";
            die_git($msg);
        }
        if ($this->formatVersion === 0 && $v1only) {
            $msg = "repo version is 0, but v1-only extension found:";
            foreach ($v1only as $u) $msg .= "\n\t$u";
            die_git($msg);
        }
    }
}

function config_global_paths(): array {
    $paths = [];
    $envGlobal = getenv('GIT_CONFIG_GLOBAL');
    if ($envGlobal !== false && $envGlobal !== '') {
        return $envGlobal === '/dev/null' ? [] : [$envGlobal];
    }
    $xdg = getenv('XDG_CONFIG_HOME');
    $home = getenv('HOME');
    if ($xdg !== false && $xdg !== '') $paths[] = "$xdg/git/config";
    elseif ($home !== false && $home !== '') $paths[] = "$home/.config/git/config";
    if ($home !== false && $home !== '') $paths[] = "$home/.gitconfig";
    return $paths;
}

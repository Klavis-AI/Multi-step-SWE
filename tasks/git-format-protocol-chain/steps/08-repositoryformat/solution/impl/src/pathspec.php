<?php
// Pathspec matching (gitglossary(7) "pathspec") and the exclude machinery
// (gitignore(5)).

class Pathspec {
    /** @var array[] each: [pattern, literal, exclude, icase] */
    public array $items = [];
    public bool $empty = true;

    public static function parse(array $specs, string $prefix = '', bool $literalAll = false): Pathspec {
        $ps = new Pathspec();
        foreach ($specs as $s) {
            $literal = $literalAll; $exclude = false; $icase = false; $fromRoot = false;
            if (strlen($s) > 0 && $s[0] === ':') {
                if (strlen($s) > 1 && $s[1] === '(') {
                    $end = strpos($s, ')');
                    $magic = substr($s, 2, $end - 2);
                    foreach (explode(',', $magic) as $m) {
                        if ($m === 'literal') $literal = true;
                        elseif ($m === 'glob') $literal = false;
                        elseif ($m === 'icase') $icase = true;
                        elseif ($m === 'exclude') $exclude = true;
                        elseif ($m === 'top') $fromRoot = true;
                    }
                    $s = substr($s, $end + 1);
                } else {
                    $i = 1;
                    while ($i < strlen($s)) {
                        if ($s[$i] === '!' || $s[$i] === '^') { $exclude = true; $i++; continue; }
                        if ($s[$i] === '/') { $fromRoot = true; $i++; continue; }
                        break;
                    }
                    $s = substr($s, $i);
                }
            }
            if ($s === '') { $s = ''; }
            $full = ($fromRoot || $prefix === '') ? $s : rtrim($prefix, '/') . '/' . $s;
            $full = ltrim(path_normalize($full), '/');
            if ($full === '.') $full = '';
            $ps->items[] = [$full, $literal, $exclude, $icase];
            $ps->empty = false;
        }
        return $ps;
    }

    public function isEmpty(): bool { return $this->empty; }

    /** Does this pathspec select $path (a file path relative to the work tree root)? */
    public function matches(string $path): bool {
        if ($this->empty) return true;
        $included = false; $anyInclude = false;
        foreach ($this->items as [$pat, $literal, $exclude, $icase]) {
            $m = self::matchOne($pat, $path, $literal, $icase);
            if ($exclude) { if ($m) return false; continue; }
            $anyInclude = true;
            if ($m) $included = true;
        }
        return $anyInclude ? $included : true;
    }

    /** Could this pathspec match something inside directory $dir? */
    public function matchesDir(string $dir): bool {
        if ($this->empty) return true;
        $dir = rtrim($dir, '/');
        foreach ($this->items as [$pat, $literal, $exclude, $icase]) {
            if ($exclude) continue;
            if ($pat === '') return true;
            if ($dir === '') return true;
            if (strncmp($pat, $dir . '/', strlen($dir) + 1) === 0) return true;
            if (strncmp($dir . '/', $pat . '/', strlen($pat) + 1) === 0) return true;
            if (!$literal && (strpos($pat, '*') !== false || strpos($pat, '?') !== false || strpos($pat, '[') !== false)) return true;
        }
        return false;
    }

    public static function matchOne(string $pat, string $path, bool $literal, bool $icase): bool {
        if ($pat === '') return true;
        $p = $icase ? strtolower($path) : $path;
        $q = $icase ? strtolower($pat) : $pat;
        if ($p === $q) return true;
        if (strncmp($p, $q . '/', strlen($q) + 1) === 0) return true;
        if ($literal) return false;
        if (strpbrk($q, '*?[') === false) return false;
        return glob_match($q, $p, false);
    }
}

/**
 * Shell-style glob used by gitignore and pathspecs.
 * $pathname = true makes '*' stop at '/' (gitignore patterns containing '/').
 */
function glob_match(string $pattern, string $subject, bool $pathname = true): bool {
    $re = glob_to_regex($pattern, $pathname);
    return preg_match($re, $subject) === 1;
}

function glob_to_regex(string $pattern, bool $pathname): string {
    $re = '';
    $n = strlen($pattern);
    for ($i = 0; $i < $n; $i++) {
        $c = $pattern[$i];
        if ($c === '*') {
            if ($pathname && $i + 1 < $n && $pattern[$i + 1] === '*') {
                // '**' : match across directory separators
                $j = $i + 2;
                if ($j < $n && $pattern[$j] === '/') {
                    $re .= '(?:.*/)?';
                    $i = $j;
                } else {
                    $re .= '.*';
                    $i = $j - 1;
                }
                continue;
            }
            $re .= $pathname ? '[^/]*' : '.*';
            continue;
        }
        if ($c === '?') { $re .= $pathname ? '[^/]' : '.'; continue; }
        if ($c === '[') {
            $j = $i + 1;
            $neg = false;
            if ($j < $n && ($pattern[$j] === '!' || $pattern[$j] === '^')) { $neg = true; $j++; }
            $cls = '';
            if ($j < $n && $pattern[$j] === ']') { $cls .= '\]'; $j++; }
            while ($j < $n && $pattern[$j] !== ']') {
                if ($pattern[$j] === '\\') { $cls .= '\\\\'; $j++; if ($j < $n) { $cls .= preg_quote($pattern[$j], '#'); $j++; } continue; }
                if ($pattern[$j] === '[' && $j + 1 < $n && $pattern[$j + 1] === ':') {
                    $end = strpos($pattern, ':]', $j);
                    if ($end !== false) {
                        $name = substr($pattern, $j + 2, $end - $j - 2);
                        $map = ['alpha' => 'a-zA-Z', 'digit' => '0-9', 'alnum' => 'a-zA-Z0-9',
                                'space' => '\s', 'upper' => 'A-Z', 'lower' => 'a-z',
                                'punct' => '[:punct:]', 'xdigit' => '0-9a-fA-F', 'blank' => ' \t',
                                'cntrl' => '\x00-\x1f', 'print' => '\x20-\x7e', 'graph' => '\x21-\x7e'];
                        $cls .= $map[$name] ?? '';
                        $j = $end + 2;
                        continue;
                    }
                }
                $ch = $pattern[$j];
                $cls .= ($ch === '-' || $ch === '^') ? '\\' . $ch : preg_quote($ch, '#');
                $j++;
            }
            if ($j >= $n) { $re .= '\['; continue; }
            $re .= '[' . ($neg ? '^' : '') . $cls . ']';
            $i = $j;
            continue;
        }
        if ($c === '\\') {
            $i++;
            if ($i < $n) $re .= preg_quote($pattern[$i], '#');
            continue;
        }
        $re .= preg_quote($c, '#');
    }
    return '#^' . $re . '$#s';
}

class ExcludeRule {
    public string $pattern;
    public ?string $regex = null;   // glob_to_regex($pattern, true), compiled once
    public bool $negative = false;
    public bool $dirOnly = false;
    public bool $anchored = false;
    public string $base = '';    // directory the rule came from, '' for root
    public string $source = '';  // file the rule was read from
    public int $lineNo = 0;
}

/** gitignore(5) pattern stack. */
class ExcludeStack {
    private array $rules = [];        // ordered, lowest priority first
    public Repo $repo;
    private array $dirCache = [];

    public function __construct(Repo $repo) { $this->repo = $repo; }

    public function addFile(string $file, string $base) {
        $c = @file_get_contents($file);
        if ($c === false) return;
        $this->addText($c, $base, $file);
    }

    public function addText(string $text, string $base, string $source = '') {
        $lineNo = 0;
        foreach (explode("\n", $text) as $line) {
            $lineNo++;
            $line = rtrim($line, "\r");
            if ($line === '' || $line[0] === '#') continue;
            if (trim($line) === '') continue;
            $r = new ExcludeRule();
            $r->base = $base;
            $r->source = $source;
            $r->lineNo = $lineNo;
            if ($line[0] === '!') { $r->negative = true; $line = substr($line, 1); }
            if ($line !== '' && substr($line, -1) === '/' ) { $r->dirOnly = true; $line = substr($line, 0, -1); }
            // trailing spaces are ignored unless escaped
            $line = preg_replace('/(?<!\\\\)\s+$/', '', $line);
            if ($line === '') continue;
            if (strpos($line, '/') !== false) {
                $r->anchored = true;
                if ($line[0] === '/') $line = substr($line, 1);
            }
            $r->pattern = $line;
            // Compile the glob to a regex ONCE here.  Doing it per (path x rule)
            // in check()/checkVerbose() made ignore matching O(paths x rules)
            // regex rebuilds -- the whole cost of t0008-ignores.sh on big trees.
            $r->regex = glob_to_regex($line, true);
            $this->rules[] = $r;
        }
    }

    public function snapshot(): int { return count($this->rules); }
    public function restore(int $n) { $this->rules = array_slice($this->rules, 0, $n); }

    /** @return bool|null true = ignored, false = explicitly not ignored, null = no match */
    public function check(string $path, bool $isDir) {
        $result = null;
        $base = basename($path);
        foreach ($this->rules as $r) {
            if ($r->dirOnly && !$isDir) continue;
            $rel = $path;
            if ($r->base !== '') {
                if (strncmp($path, $r->base, strlen($r->base)) !== 0) continue;
                $rel = substr($path, strlen($r->base));
            }
            if ($r->anchored) {
                if (preg_match($r->regex, $rel) !== 1) continue;
            } else {
                if (preg_match($r->regex, $base) !== 1) continue;
            }
            $result = !$r->negative;
        }
        return $result;
    }

    /** The rule that matched, for `git check-ignore -v`. */
    public function checkVerbose(string $path, bool $isDir): ?array {
        $match = null;
        $base = basename($path);
        foreach ($this->rules as $r) {
            if ($r->dirOnly && !$isDir) continue;
            $rel = $path;
            if ($r->base !== '') {
                if (strncmp($path, $r->base, strlen($r->base)) !== 0) continue;
                $rel = substr($path, strlen($r->base));
            }
            if ($r->anchored) { if (preg_match($r->regex, $rel) !== 1) continue; }
            else { if (preg_match($r->regex, $base) !== 1) continue; }
            $match = $r;
        }
        return $match === null ? null : [$match];
    }
}

/** The file a matched exclude rule came from, relative to the work tree. */
function exclude_rule_source(Repo $repo, ExcludeRule $r): ?string {
    if ($r->source === '') return null;
    $wt = $repo->worktree;
    if ($wt !== null && strncmp($r->source, $wt . '/', strlen($wt) + 1) === 0) {
        return substr($r->source, strlen($wt) + 1);
    }
    return $r->source;
}

/** Build the exclude stack for the repository root. */
function build_exclude_stack(Repo $repo): ExcludeStack {
    $st = new ExcludeStack($repo);
    $core = $repo->config->get('core.excludesfile', null);
    if ($core !== null && !is_bool($core)) {
        $p = $core;
        if ($p !== '' && $p[0] === '~') $p = (getenv('HOME') ?: '') . substr($p, 1);
        $st->addFile($p, '');
    } else {
        $xdg = getenv('XDG_CONFIG_HOME');
        $home = getenv('HOME');
        if ($xdg !== false && $xdg !== '') $st->addFile("$xdg/git/ignore", '');
        elseif ($home !== false && $home !== '') $st->addFile("$home/.config/git/ignore", '');
    }
    $st->addFile($repo->commonPath('info/exclude'), '');
    return $st;
}

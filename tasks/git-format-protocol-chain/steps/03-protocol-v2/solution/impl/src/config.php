<?php
// git config file format: parser, layered lookup, and an in-place editor that
// preserves every line it does not touch (git-config(1), CONFIGURATION FILE).

class ConfigEntry {
    public string $section;      // lower-cased
    public ?string $subsection;  // verbatim (case sensitive), null if none
    public string $name;         // lower-cased
    public $value;               // string, or true for a valueless key
    public int $start;           // byte offset of the line where the key starts
    public int $end;             // byte offset just past the entry's last line
    public function key(): string {
        return $this->subsection === null
            ? "{$this->section}.{$this->name}"
            : "{$this->section}.{$this->subsection}.{$this->name}";
    }
}

class ConfigFile {
    public string $path;
    public string $text = '';
    /** @var ConfigEntry[] */
    public array $entries = [];
    public bool $exists = false;

    public static function load(string $path): ConfigFile {
        $cf = new ConfigFile();
        $cf->path = $path;
        $raw = @file_get_contents($path);
        if ($raw === false) return $cf;
        $cf->exists = true;
        $cf->text = $raw;
        $cf->parse();
        return $cf;
    }

    public static function fromString(string $text, string $path = ''): ConfigFile {
        $cf = new ConfigFile();
        $cf->path = $path;
        $cf->text = $text;
        $cf->exists = true;
        $cf->parse();
        return $cf;
    }

    private function parse() {
        $t = $this->text;
        $n = strlen($t);
        $i = 0;
        $section = null; $subsection = null;
        while ($i < $n) {
            $lineStart = $i;
            // read a logical line (handles continuation inside values)
            $eol = strpos($t, "\n", $i);
            $line = $eol === false ? substr($t, $i) : substr($t, $i, $eol - $i);
            $next = $eol === false ? $n : $eol + 1;
            $trim = ltrim($line, " \t");
            if ($trim === '' || $trim[0] === '#' || $trim[0] === ';') { $i = $next; continue; }
            if ($trim[0] === '[') {
                if (preg_match('/^\[\s*([A-Za-z0-9._-]+)\s*(?:"((?:[^"\\\\]|\\\\.)*)")?\s*\]/', $trim, $m)) {
                    $sec = $m[1];
                    if (isset($m[2])) {
                        $section = strtolower($sec);
                        $subsection = preg_replace('/\\\\(.)/', '$1', $m[2]);
                    } elseif (strpos($sec, '.') !== false) {
                        $parts = explode('.', $sec, 2);
                        $section = strtolower($parts[0]);
                        $subsection = strtolower($parts[1]);
                    } else {
                        $section = strtolower($sec);
                        $subsection = null;
                    }
                    // a key may follow on the same line
                    $rest = substr($trim, strlen($m[0]));
                    if (trim($rest) !== '' && $rest[0] !== '#' && $rest[0] !== ';') {
                        $this->parseKeyLine($rest, $section, $subsection, $lineStart, $next, $t, $i, $n, $next);
                        $i = $next;
                        continue;
                    }
                }
                $i = $next;
                continue;
            }
            if ($section === null) { $i = $next; continue; }
            [$consumedEnd] = $this->parseKeyLine($line, $section, $subsection, $lineStart, $next, $t, $i, $n, $next);
            $i = $consumedEnd;
        }
    }

    private function parseKeyLine(string $line, string $section, ?string $subsection, int $lineStart, int $lineEnd, string $t, int $i, int $n, int $next): array {
        // value may continue over following lines when a line ends with a backslash
        $full = $line;
        $end = $lineEnd;
        while ($this->endsWithContinuation($full)) {
            if ($end >= $n) break;
            $eol2 = strpos($t, "\n", $end);
            $more = $eol2 === false ? substr($t, $end) : substr($t, $end, $eol2 - $end);
            $full = substr($full, 0, -1) . "\n" . $more;
            $end = $eol2 === false ? $n : $eol2 + 1;
        }
        $eq = $this->findEquals($full);
        if ($eq === false) {
            $name = trim($full);
            $name = preg_replace('/\s*[#;].*$/s', '', $name);
            if ($name === '' || !preg_match('/^[A-Za-z][A-Za-z0-9-]*$/', $name)) return [$end];
            $value = true;
        } else {
            $name = trim(substr($full, 0, $eq));
            if (!preg_match('/^[A-Za-z][A-Za-z0-9-]*$/', $name)) return [$end];
            $value = self::parseValue(substr($full, $eq + 1));
            if ($value === false) return [$end];
        }
        $e = new ConfigEntry();
        $e->section = $section;
        $e->subsection = $subsection;
        $e->name = strtolower($name);
        $e->value = $value;
        $e->start = $lineStart;
        $e->end = $end;
        $this->entries[] = $e;
        return [$end];
    }

    private function endsWithContinuation(string $s): bool {
        // a trailing backslash outside a comment continues the value
        $inq = false; $esc = false; $cont = false;
        for ($i = 0; $i < strlen($s); $i++) {
            $c = $s[$i];
            if ($esc) { $esc = false; $cont = false; continue; }
            if ($c === '\\') { $esc = true; $cont = true; continue; }
            if ($c === '"') { $inq = !$inq; $cont = false; continue; }
            if (!$inq && ($c === '#' || $c === ';')) return false;
            $cont = false;
        }
        return $esc || $cont;
    }

    private function findEquals(string $s) {
        $inq = false;
        for ($i = 0; $i < strlen($s); $i++) {
            $c = $s[$i];
            if ($c === '\\') { $i++; continue; }
            if ($c === '"') { $inq = !$inq; continue; }
            if (!$inq && ($c === '#' || $c === ';')) return false;
            if (!$inq && $c === '=') return $i;
        }
        return false;
    }

    /** Decode the right-hand side of "name = value". */
    public static function parseValue(string $s) {
        $out = '';
        $inq = false;
        $len = strlen($s);
        $i = 0;
        // leading whitespace is skipped
        while ($i < $len && ($s[$i] === ' ' || $s[$i] === "\t")) $i++;
        $trailingWS = '';
        for (; $i < $len; $i++) {
            $c = $s[$i];
            if ($c === '\\') {
                $i++;
                if ($i >= $len) break;
                $e = $s[$i];
                if ($e === "\n") { $out .= $trailingWS; $trailingWS = ''; continue; }
                $map = ['n' => "\n", 't' => "\t", 'b' => "\x08", '"' => '"', '\\' => '\\'];
                if (!isset($map[$e])) return false;
                $out .= $trailingWS . $map[$e];
                $trailingWS = '';
                continue;
            }
            if ($c === '"') { $out .= $trailingWS; $trailingWS = ''; $inq = !$inq; continue; }
            if (!$inq && ($c === '#' || $c === ';')) break;
            if (!$inq && ($c === ' ' || $c === "\t" || $c === "\n")) { $trailingWS .= $c; continue; }
            $out .= $trailingWS . $c;
            $trailingWS = '';
        }
        if ($inq) return false;
        return $out;
    }

    public static function quoteValue(string $v): string {
        if ($v === '') return '';
        $needQuote = ($v[0] === ' ' || $v[0] === "\t" || substr($v, -1) === ' ' || substr($v, -1) === "\t"
            || strpos($v, '#') !== false || strpos($v, ';') !== false);
        $out = '';
        foreach (str_split($v) as $c) {
            switch ($c) {
                case "\n": $out .= '\\n'; break;
                case "\t": $out .= '\\t'; break;
                case "\x08": $out .= '\\b'; break;
                case '"': $out .= '\\"'; $needQuote = true; break;
                case '\\': $out .= '\\\\'; break;
                default: $out .= $c;
            }
        }
        return $needQuote ? '"' . $out . '"' : $out;
    }

    public function save(): bool {
        return write_file_atomic($this->path, $this->text, 0666);
    }

    /** All entries matching a key (case rules applied). */
    public function find(string $section, ?string $sub, string $name): array {
        $r = [];
        foreach ($this->entries as $e) {
            if ($e->section === strtolower($section) && $e->name === strtolower($name)
                && $e->subsection === $sub) $r[] = $e;
        }
        return $r;
    }

    private function reparse() { $this->entries = []; $this->parse(); }

    /** Replace, add or append a value; mirrors `git config [--add] key value`. */
    public function setValue(string $section, ?string $sub, string $name, string $value, bool $add = false, ?string $valueRegex = null, bool $all = false): bool {
        $matches = $this->find($section, $sub, $name);
        if ($valueRegex !== null) {
            $matches = array_values(array_filter($matches, function ($e) use ($valueRegex) {
                $neg = false; $re = $valueRegex;
                if ($re !== '' && $re[0] === '!') { $neg = true; $re = substr($re, 1); }
                $v = is_bool($e->value) ? 'true' : $e->value;
                $m = @preg_match('/' . str_replace('/', '\/', $re) . '/', $v) === 1;
                return $neg ? !$m : $m;
            }));
        }
        if (!$add && $matches) {
            if (!$all && count($matches) > 1) return false; // caller reports multiple values
            $line = "\t" . $name . ' = ' . self::quoteValue($value) . "\n";
            // replace last..first so offsets stay valid
            for ($i = count($matches) - 1; $i >= 0; $i--) {
                $e = $matches[$i];
                $this->text = substr($this->text, 0, $e->start) . ($i === 0 ? $line : '') . substr($this->text, $e->end);
            }
            $this->reparse();
            return true;
        }
        // find the section header to append under
        $insertAt = null;
        $lastEnd = null;
        foreach ($this->entries as $e) {
            if ($e->section === strtolower($section) && $e->subsection === $sub) $lastEnd = $e->end;
        }
        if ($lastEnd !== null) {
            $insertAt = $lastEnd;
        } else {
            $hdr = $this->findSectionHeader($section, $sub);
            if ($hdr !== null) $insertAt = $hdr;
        }
        $line = "\t" . $name . ' = ' . self::quoteValue($value) . "\n";
        if ($insertAt === null) {
            $head = '[' . $section;
            if ($sub !== null) $head .= ' "' . addcslashes($sub, "\"\\") . '"';
            $head .= "]\n";
            if ($this->text !== '' && substr($this->text, -1) !== "\n") $this->text .= "\n";
            $this->text .= $head . $line;
        } else {
            $this->text = substr($this->text, 0, $insertAt) . $line . substr($this->text, $insertAt);
        }
        $this->reparse();
        return true;
    }

    private function findSectionHeader(string $section, ?string $sub) {
        $t = $this->text; $n = strlen($t); $i = 0;
        while ($i < $n) {
            $eol = strpos($t, "\n", $i);
            $line = $eol === false ? substr($t, $i) : substr($t, $i, $eol - $i);
            $next = $eol === false ? $n : $eol + 1;
            $trim = ltrim($line, " \t");
            if ($trim !== '' && $trim[0] === '[' &&
                preg_match('/^\[\s*([A-Za-z0-9._-]+)\s*(?:"((?:[^"\\\\]|\\\\.)*)")?\s*\]/', $trim, $m)) {
                $sec = strtolower($m[1]); $ss = isset($m[2]) ? preg_replace('/\\\\(.)/', '$1', $m[2]) : null;
                if (!isset($m[2]) && strpos($m[1], '.') !== false) {
                    $p = explode('.', $m[1], 2); $sec = strtolower($p[0]); $ss = strtolower($p[1]);
                }
                if ($sec === strtolower($section) && $ss === $sub) return $next;
            }
            $i = $next;
        }
        return null;
    }

    public function unset(string $section, ?string $sub, string $name, ?string $valueRegex = null, bool $all = false): int {
        $matches = $this->find($section, $sub, $name);
        if ($valueRegex !== null) {
            $matches = array_values(array_filter($matches, function ($e) use ($valueRegex) {
                $neg = false; $re = $valueRegex;
                if ($re !== '' && $re[0] === '!') { $neg = true; $re = substr($re, 1); }
                $v = is_bool($e->value) ? 'true' : $e->value;
                $m = @preg_match('/' . str_replace('/', '\/', $re) . '/', $v) === 1;
                return $neg ? !$m : $m;
            }));
        }
        if (!$matches) return 0;
        if (!$all && count($matches) > 1) return -1;
        for ($i = count($matches) - 1; $i >= 0; $i--) {
            $e = $matches[$i];
            $this->text = substr($this->text, 0, $e->start) . substr($this->text, $e->end);
        }
        $this->reparse();
        return count($matches);
    }

    public function removeSection(string $section, ?string $sub): bool {
        $t = $this->text; $n = strlen($t); $i = 0; $found = false;
        $out = ''; $inTarget = false;
        while ($i < $n) {
            $eol = strpos($t, "\n", $i);
            $line = $eol === false ? substr($t, $i) : substr($t, $i, $eol - $i);
            $next = $eol === false ? $n : $eol + 1;
            $trim = ltrim($line, " \t");
            if ($trim !== '' && $trim[0] === '[') {
                $inTarget = false;
                if (preg_match('/^\[\s*([A-Za-z0-9._-]+)\s*(?:"((?:[^"\\\\]|\\\\.)*)")?\s*\]/', $trim, $m)) {
                    $sec = strtolower($m[1]); $ss = isset($m[2]) ? preg_replace('/\\\\(.)/', '$1', $m[2]) : null;
                    if (!isset($m[2]) && strpos($m[1], '.') !== false) {
                        $p = explode('.', $m[1], 2); $sec = strtolower($p[0]); $ss = strtolower($p[1]);
                    }
                    if ($sec === strtolower($section) && $ss === $sub) { $inTarget = true; $found = true; }
                }
            }
            if (!$inTarget) $out .= substr($t, $i, $next - $i);
            $i = $next;
        }
        $this->text = $out;
        $this->reparse();
        return $found;
    }

    public function renameSection(string $section, ?string $sub, string $newSection, ?string $newSub): bool {
        $t = $this->text; $n = strlen($t); $i = 0; $found = false; $out = '';
        while ($i < $n) {
            $eol = strpos($t, "\n", $i);
            $line = $eol === false ? substr($t, $i) : substr($t, $i, $eol - $i);
            $next = $eol === false ? $n : $eol + 1;
            $trim = ltrim($line, " \t");
            $replaced = false;
            if ($trim !== '' && $trim[0] === '[' &&
                preg_match('/^\[\s*([A-Za-z0-9._-]+)\s*(?:"((?:[^"\\\\]|\\\\.)*)")?\s*\]/', $trim, $m)) {
                $sec = strtolower($m[1]); $ss = isset($m[2]) ? preg_replace('/\\\\(.)/', '$1', $m[2]) : null;
                if (!isset($m[2]) && strpos($m[1], '.') !== false) {
                    $p = explode('.', $m[1], 2); $sec = strtolower($p[0]); $ss = strtolower($p[1]);
                }
                if ($sec === strtolower($section) && $ss === $sub) {
                    $found = true;
                    $hdr = '[' . strtolower($newSection);
                    if ($newSub !== null) $hdr .= ' "' . addcslashes($newSub, "\"\\") . '"';
                    $hdr .= "]\n";
                    $out .= $hdr;
                    $replaced = true;
                }
            }
            if (!$replaced) $out .= substr($t, $i, $next - $i);
            $i = $next;
        }
        $this->text = $out;
        $this->reparse();
        return $found;
    }
}

/** Split "section.subsection.name" honouring the case rules. */
function config_split_key(string $key) {
    $first = strpos($key, '.');
    $last = strrpos($key, '.');
    if ($first === false) return false;
    $section = substr($key, 0, $first);
    $name = substr($key, $last + 1);
    $sub = ($last > $first) ? substr($key, $first + 1, $last - $first - 1) : null;
    if ($section === '' || $name === '') return false;
    if (!preg_match('/^[A-Za-z0-9-]+$/', $section)) return false;
    if (!preg_match('/^[A-Za-z][A-Za-z0-9-]*$/', $name)) return false;
    return [strtolower($section), $sub, strtolower($name)];
}

/** Split a key without folding case, for writing new entries. */
function config_split_key_verbatim(string $key) {
    $first = strpos($key, '.');
    $last = strrpos($key, '.');
    if ($first === false) return false;
    $section = substr($key, 0, $first);
    $name = substr($key, $last + 1);
    $sub = ($last > $first) ? substr($key, $first + 1, $last - $first - 1) : null;
    return [$section, $sub, $name];
}

class Config {
    /** key => list of values, lowest precedence first */
    private array $values = [];
    private array $files = [];

    public function addFile(string $path, int $depth = 0) {
        $cf = ConfigFile::load($path);
        if (!$cf->exists) return;
        $this->files[] = $path;
        $this->ingest($cf, dirname($path), $depth);
    }

    public function addString(string $text, string $origin = '') {
        $cf = ConfigFile::fromString($text, $origin);
        $this->ingest($cf, $origin !== '' ? dirname($origin) : getcwd(), 0);
    }

    private function ingest(ConfigFile $cf, string $dir, int $depth) {
        foreach ($cf->entries as $e) {
            $k = $e->key();
            if (($e->section === 'include' && $e->name === 'path') && $depth < 10) {
                $p = is_bool($e->value) ? '' : $e->value;
                if ($p !== '') {
                    if ($p[0] === '~') $p = (getenv('HOME') ?: '') . substr($p, 1);
                    if ($p[0] !== '/') $p = $dir . '/' . $p;
                    $this->addFile($p, $depth + 1);
                }
                continue;
            }
            $this->values[$k][] = $e->value;
        }
    }

    public function has(string $key): bool { return isset($this->values[strtolower_key($key)]); }

    public function get(string $key, $default = null) {
        $k = strtolower_key($key);
        if (!isset($this->values[$k])) return $default;
        $v = end($this->values[$k]);
        return $v;
    }

    public function getAll(string $key): array {
        $k = strtolower_key($key);
        return $this->values[$k] ?? [];
    }

    public function getBool(string $key, bool $default = false): bool {
        $v = $this->get($key, null);
        if ($v === null) return $default;
        return config_bool($v, $default);
    }

    public function getInt(string $key, int $default = 0): int {
        $v = $this->get($key, null);
        if ($v === null || is_bool($v)) return $default;
        return config_int($v);
    }

    public function subsections(string $section): array {
        $r = [];
        $sl = strtolower($section) . '.';
        foreach ($this->values as $k => $_) {
            if (strncmp($k, $sl, strlen($sl)) !== 0) continue;
            $rest = substr($k, strlen($sl));
            $pos = strrpos($rest, '.');
            if ($pos === false) continue;
            $r[substr($rest, 0, $pos)] = true;
        }
        return array_keys($r);
    }

    public function allKeys(): array { return array_keys($this->values); }
}

function strtolower_key(string $key): string {
    $p = config_split_key($key);
    if ($p === false) return strtolower($key);
    return $p[1] === null ? "{$p[0]}.{$p[2]}" : "{$p[0]}.{$p[1]}.{$p[2]}";
}

function config_bool($v, bool $default = false): bool {
    if (is_bool($v)) return $v;
    // An ABSENT key and a key present with no value are different things in
    // git: `[s] k` with no value is TRUE, while a missing key means "use the
    // default". Without this line null falls through to trim(null) -- a PHP 8.2
    // deprecation printed to stderr -- and then matches the '' case and returns
    // TRUE, so a missing key reads as set. Config::getBool() already guards
    // this; config_bool() is also called directly, so it guards itself.
    if ($v === null) return $default;
    $s = strtolower(trim($v));
    if ($s === '' ) return true;
    if (in_array($s, ['yes', 'on', 'true', '1'], true)) return true;
    if (in_array($s, ['no', 'off', 'false', '0'], true)) return false;
    if (preg_match('/^-?\d+$/', $s)) return ((int)$s) !== 0;
    return $default;
}

function config_int($v): int {
    $s = trim((string)$v);
    if (preg_match('/^(-?\d+)\s*([kmgKMG])?$/', $s, $m)) {
        $n = (int)$m[1];
        if (isset($m[2])) {
            switch (strtolower($m[2])) {
                case 'k': $n *= 1024; break;
                case 'm': $n *= 1024 * 1024; break;
                case 'g': $n *= 1024 * 1024 * 1024; break;
            }
        }
        return $n;
    }
    return 0;
}

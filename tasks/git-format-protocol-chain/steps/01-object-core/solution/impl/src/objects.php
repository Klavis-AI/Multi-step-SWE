<?php
// Object parsing and formatting: commit, tree, tag (gitformat-* / git(1) object model).

class TreeEntry {
    public string $mode;   // octal string as stored, e.g. "100644", "40000"
    public string $name;
    public string $oid;    // hex
    public function isDir(): bool { return $this->mode === '40000'; }
}

class TreeObj {
    /** @var TreeEntry[] */
    public array $entries = [];

    public static function parse(string $data): TreeObj {
        $t = new TreeObj();
        $i = 0; $n = strlen($data);
        $rawsz = Repo::$rawsz_static;
        while ($i < $n) {
            $sp = strpos($data, ' ', $i);
            if ($sp === false) break;
            $mode = substr($data, $i, $sp - $i);
            $nul = strpos($data, "\0", $sp + 1);
            if ($nul === false) break;
            $name = substr($data, $sp + 1, $nul - $sp - 1);
            $oid = bin2hex(substr($data, $nul + 1, $rawsz));
            $e = new TreeEntry();
            $e->mode = $mode; $e->name = $name; $e->oid = $oid;
            $t->entries[] = $e;
            $i = $nul + 1 + $rawsz;
        }
        return $t;
    }

    public function format(): string {
        $es = $this->entries;
        usort($es, function ($a, $b) { return tree_entry_cmp($a->name, $a->mode, $b->name, $b->mode); });
        $out = '';
        foreach ($es as $e) {
            $out .= ltrim($e->mode, '0') === '' ? '0' : ltrim($e->mode, '0');
            $out .= ' ' . $e->name . "\0" . hex2bin($e->oid);
        }
        return $out;
    }

    public function get(string $name): ?TreeEntry {
        foreach ($this->entries as $e) if ($e->name === $name) return $e;
        return null;
    }
}

/** git's tree ordering: directories sort as if their name ended in '/'. */
function tree_entry_cmp(string $an, string $am, string $bn, string $bm): int {
    $a = $an . ($am === '40000' || $am === '040000' ? '/' : '');
    $b = $bn . ($bm === '40000' || $bm === '040000' ? '/' : '');
    return strcmp($a, $b);
}

class Ident {
    public string $name = '';
    public string $email = '';
    public int $ts = 0;
    public string $tz = '+0000';
    public string $raw = '';
    public static function parse(string $s): Ident {
        $id = new Ident();
        $id->raw = $s;
        if (preg_match('/^(.*?)\s*<([^>]*)>\s*(-?\d+)\s*([+-]\d{4})?\s*$/', $s, $m)) {
            $id->name = $m[1]; $id->email = $m[2]; $id->ts = (int)$m[3];
            $id->tz = $m[4] ?? '+0000';
        } elseif (preg_match('/^(.*?)\s*<([^>]*)>/', $s, $m)) {
            $id->name = $m[1]; $id->email = $m[2];
        }
        return $id;
    }
    public function format(): string {
        return "{$this->name} <{$this->email}> {$this->ts} {$this->tz}";
    }
}

class CommitObj {
    public string $oid = '';
    public string $tree = '';
    public array $parents = [];
    public ?Ident $author = null;
    public ?Ident $committer = null;
    public array $headers = [];   // [name, value] in order, excluding tree/parent/author/committer
    public string $message = '';

    public static function parse(string $data, string $oid = ''): CommitObj {
        $c = new CommitObj();
        $c->oid = $oid;
        $split = strpos($data, "\n\n");
        $head = $split === false ? $data : substr($data, 0, $split);
        $c->message = $split === false ? '' : substr($data, $split + 2);
        $lines = explode("\n", $head);
        for ($i = 0; $i < count($lines); $i++) {
            $line = $lines[$i];
            if ($line === '') continue;
            $sp = strpos($line, ' ');
            $key = $sp === false ? $line : substr($line, 0, $sp);
            $val = $sp === false ? '' : substr($line, $sp + 1);
            while ($i + 1 < count($lines) && strlen($lines[$i + 1]) && $lines[$i + 1][0] === ' ') {
                $val .= "\n" . substr($lines[$i + 1], 1);
                $i++;
            }
            switch ($key) {
                case 'tree': $c->tree = $val; break;
                case 'parent': $c->parents[] = $val; break;
                case 'author': $c->author = Ident::parse($val); break;
                case 'committer': $c->committer = Ident::parse($val); break;
                default: $c->headers[] = [$key, $val];
            }
        }
        return $c;
    }

    public function format(): string {
        $s = "tree {$this->tree}\n";
        foreach ($this->parents as $p) $s .= "parent $p\n";
        if ($this->author) $s .= 'author ' . $this->author->format() . "\n";
        if ($this->committer) $s .= 'committer ' . $this->committer->format() . "\n";
        foreach ($this->headers as $h) {
            $s .= $h[0] . ' ' . str_replace("\n", "\n ", $h[1]) . "\n";
        }
        return $s . "\n" . $this->message;
    }

    public function header(string $name) {
        foreach ($this->headers as $h) if ($h[0] === $name) return $h[1];
        return null;
    }

    public function subject(): string {
        $m = ltrim($this->message, "\n");
        $end = strpos($m, "\n\n");
        $s = $end === false ? rtrim($m, "\n") : substr($m, 0, $end);
        return str_replace("\n", ' ', rtrim($s, "\n"));
    }
}

class TagObj {
    public string $object = '';
    public string $type = '';
    public string $tag = '';
    public ?Ident $tagger = null;
    public string $message = '';
    public array $headers = [];

    public static function parse(string $data): TagObj {
        $t = new TagObj();
        $split = strpos($data, "\n\n");
        $head = $split === false ? $data : substr($data, 0, $split);
        $t->message = $split === false ? '' : substr($data, $split + 2);
        foreach (explode("\n", $head) as $line) {
            $sp = strpos($line, ' ');
            if ($sp === false) continue;
            $k = substr($line, 0, $sp); $v = substr($line, $sp + 1);
            switch ($k) {
                case 'object': $t->object = $v; break;
                case 'type': $t->type = $v; break;
                case 'tag': $t->tag = $v; break;
                case 'tagger': $t->tagger = Ident::parse($v); break;
                default: $t->headers[] = [$k, $v];
            }
        }
        return $t;
    }

    public function format(): string {
        $s = "object {$this->object}\ntype {$this->type}\ntag {$this->tag}\n";
        if ($this->tagger) $s .= 'tagger ' . $this->tagger->format() . "\n";
        return $s . "\n" . $this->message;
    }
}

/**
 * Does this object filter omit this object?  git-rev-list(1)'s --filter=
 * grammar; the same rule drives rev-list, pack-objects and the v2 fetch.
 */
function filter_excludes(Repo $repo, string $filter, string $oid): bool {
    $hdr = $repo->odb->readHeader($oid);
    if ($hdr === null) return false;
    if ($filter === 'blob:none') return $hdr[0] === 'blob';
    if (strncmp($filter, 'blob:limit=', 11) === 0) {
        $limit = config_int(substr($filter, 11));
        return $hdr[0] === 'blob' && $hdr[1] >= $limit;
    }
    if (strncmp($filter, 'tree:', 5) === 0) {
        $depth = (int)substr($filter, 5);
        if ($depth === 0) return $hdr[0] === 'tree' || $hdr[0] === 'blob';
    }
    return false;
}

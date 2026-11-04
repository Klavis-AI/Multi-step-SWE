<?php
/**
 * Extracts every site where the tree reaches for a MySQL *driver*, as opposed
 * to a MySQL *dialect*.
 *
 * `sqlextract.php` answers "what SQL does this tree send"; libpg_query then
 * answers whether PostgreSQL would accept it. Neither sees `new mysqli(...)`,
 * `mysqli_real_escape_string($c, $s)`, `'driver' => 'pdo_mysql'`, or a DSN of
 * `"mysql:host=...;dbname=..."`. Those are not SQL and carry no dialect; they
 * are the wiring, and a tree can pass the dialect gate whole while still being
 * bolted to MySQL through every one of them.
 *
 * Sites are found in the AST, not by grep, for the reasons rule 11 gives. The
 * difference is not cosmetic here: `'mysql'` appears in this tree inside
 * comments, inside documentation blocks, inside a URL, and as a substring of
 * `pdo_mysql`, `mysqli` and `mysqlnd`. Position in the syntax tree is what
 * separates `'driver' => 'pdo_mysql'` (an array entry that configures a
 * connection) from the same characters in a sentence.
 *
 * Six kinds of site, each one an expression rather than a line or a file, so
 * that the count is one consolidation cannot re-bucket (rule 11):
 *
 *   new         `new mysqli(...)`, `new PDO("mysql:...")`, `new DB_mysqli`
 *   call        a call to a `mysql_*` / `mysqli_*` function
 *   dsn         a string literal shaped like a MySQL PDO DSN
 *   name        a string literal that names a MySQL driver, in a position
 *               where a driver is being selected: an array value under a
 *               driver-ish key, or an argument to a configuration call
 *   classdef    a class declaration whose own name marks it a MySQL adapter
 *   extension   `extension_loaded('mysqli')` and friends
 *
 * Usage: php driverscan.php <root> [<root> ...]
 * Output: one JSON object on stdout,
 *   {"sites":[{"file","line","kind","detail","class","function"}],
 *    "unparsable":[{"file","error"}],
 *    "files": <count>}
 *
 * Every file is walked, vendored trees included, and a file the parser cannot
 * read is reported rather than skipped -- a tree that hides its MySQL wiring
 * in a syntactically broken file must not pass by being unreadable.
 */

require __DIR__ . '/vendor/autoload.php';

use PhpParser\Node;
use PhpParser\NodeTraverser;
use PhpParser\NodeVisitorAbstract;
use PhpParser\ParserFactory;

/** Class names whose construction is by itself a MySQL connection. */
const MYSQL_CLASSES = [
    'mysqli' => 1, 'mysqli_driver' => 1, 'mysqli_stmt' => 1,
    'db_mysqli' => 1, 'db_pdo_mysql' => 1, 'db_mysql' => 1,
    'pdomysql' => 1,
];

/** Strings that name a MySQL driver where a driver is being chosen. */
const MYSQL_DRIVER_NAMES = [
    'mysql' => 1, 'mysqli' => 1, 'pdo_mysql' => 1, 'mysqlnd' => 1,
    'drizzle_pdo_mysql' => 1, 'doctrine\\dbal\\driver\\pdomysql\\driver' => 1,
];

/**
 * Array keys and parameter positions that select a driver. A bare `'mysqli'`
 * in a sentence is not a finding; a `'mysqli'` sitting under `adapter =>` is.
 */
const DRIVER_KEYS = [
    'driver' => 1, 'adapter' => 1, 'driverclass' => 1, 'dbdriver' => 1,
    'engine' => 1, 'dbtype' => 1, 'type' => 1, 'platform' => 1,
    'wrapperclass' => 1,
];

/** Calls whose arguments select the database driver. */
const CONFIG_CALLS = [
    'databasesetting' => 1, 'setdatabase' => 1, 'setadapter' => 1,
    'setdriver' => 1,
];

/**
 * Classes whose *construction* selects the driver. jFramework's own
 * `new \jf\DatabaseSetting("mysqli", $name, $user, ...)` is the single place
 * this application chooses its adapter, and it is a constructor, not a call.
 */
const CONFIG_CLASSES = [
    'databasesetting' => 1, 'jf\\databasesetting' => 1,
];

function lc($s)
{
    return strtolower((string) $s);
}

/** The lowercased name of a call's callee, or null if it is not a plain name. */
function callee($n)
{
    if ($n instanceof Node\Expr\MethodCall
        || $n instanceof Node\Expr\NullsafeMethodCall
        || $n instanceof Node\Expr\StaticCall) {
        return $n->name instanceof Node\Identifier
            ? lc($n->name->toString()) : null;
    }
    if ($n instanceof Node\Expr\FuncCall) {
        return $n->name instanceof Node\Name ? lc($n->name->toString()) : null;
    }
    return null;
}

class DriverScanner extends NodeVisitorAbstract
{
    public $found = [];
    public $file = '';
    private $stack = [];
    private $dsnClaimed = [];

    private function isScope(Node $n)
    {
        return $n instanceof Node\Stmt\Function_
            || $n instanceof Node\Stmt\ClassMethod
            || $n instanceof Node\Expr\Closure
            || $n instanceof Node\Stmt\ClassLike;
    }

    /** [class, function] of the innermost enclosing scopes. */
    private function where()
    {
        $class = null;
        $func = null;
        foreach ($this->stack as $n) {
            if ($n instanceof Node\Stmt\ClassLike && $n->name !== null) {
                $class = $n->name->toString();
            } elseif ($n instanceof Node\Stmt\ClassMethod
                      || $n instanceof Node\Stmt\Function_) {
                $func = $n->name->toString();
            } elseif ($n instanceof Node\Expr\Closure) {
                $func = '{closure}';
            }
        }
        return [$class, $func];
    }

    private function record($node, $kind, $detail)
    {
        list($class, $func) = $this->where();
        $this->found[] = [
            'file' => $this->file,
            'line' => $node->getStartLine(),
            'kind' => $kind,
            'detail' => $detail,
            'class' => $class,
            'function' => $func,
        ];
    }

    /** A literal string, or null for anything computed. */
    private static function literal(Node $n)
    {
        return $n instanceof Node\Scalar\String_ ? $n->value : null;
    }

    /**
     * The literal text a string expression *starts* with, interpolation and
     * concatenation included. A DSN is almost never a plain literal -- this
     * tree's is `"mysql:dbname={$db->DatabaseName};host={$db->Host};"` -- and
     * the part that names the driver is always the constant prefix.
     */
    private static function leading(Node $n)
    {
        if ($n instanceof Node\Scalar\String_) {
            return $n->value;
        }
        if ($n instanceof Node\Scalar\Encapsed) {
            $first = $n->parts[0] ?? null;
            return $first instanceof Node\Scalar\EncapsedStringPart
                ? $first->value : null;
        }
        if ($n instanceof Node\Expr\BinaryOp\Concat) {
            return self::leading($n->left);
        }
        return null;
    }

    /**
     * The nodes `leading()` walked through to reach the text it returned.
     *
     * `"mysql:dbname=" . $db . ";host=x"` is three nested expressions that all
     * lead with the same characters, and the traverser enters all three. One
     * DSN is one site, so the outermost match claims its own spine and the
     * inner ones are not counted again. A *different* DSN somewhere else
     * inside the expression is off the spine and still records.
     */
    private static function spine(Node $n)
    {
        $out = [$n];
        if ($n instanceof Node\Scalar\Encapsed) {
            $first = $n->parts[0] ?? null;
            if ($first !== null) {
                $out[] = $first;
            }
        } elseif ($n instanceof Node\Expr\BinaryOp\Concat) {
            foreach (self::spine($n->left) as $sub) {
                $out[] = $sub;
            }
        }
        return $out;
    }

    public function enterNode(Node $node)
    {
        if ($this->isScope($node)) {
            $this->stack[] = $node;
        }

        // new mysqli(...) / new DB_pdo_mysql / new PDO("mysql:...")
        if ($node instanceof Node\Expr\New_ && $node->class instanceof Node\Name) {
            $name = lc($node->class->toString());
            $short = lc(substr(strrchr('\\' . $name, '\\'), 1));
            if (isset(MYSQL_CLASSES[$name]) || isset(MYSQL_CLASSES[$short])) {
                $this->record($node, 'new', $node->class->toString());
            }
        }

        // A class whose own name declares it the MySQL adapter.
        if ($node instanceof Node\Stmt\ClassLike && $node->name !== null) {
            $name = lc($node->name->toString());
            if (isset(MYSQL_CLASSES[$name])) {
                $this->record($node, 'classdef', $node->name->toString());
            }
        }

        // mysql_* / mysqli_* function calls. Plain function calls only: the
        // procedural mysqli API is functions, and matching method names too
        // would flag `$this->mysqli_helper()` -- a method someone wrote, whose
        // name says nothing about which driver it ends up talking to.
        $call = callee($node);
        if ($call !== null && $node instanceof Node\Expr\FuncCall
            && preg_match('/^mysqli?_[a-z0-9_]+$/', $call)) {
            $this->record($node, 'call', $call);
        }

        // extension_loaded('mysqli'), dl('mysqli.so'), ...
        if ($call !== null
            && in_array($call, ['extension_loaded', 'dl', 'get_extension_funcs'],
                        true)
            && isset($node->args[0])) {
            $arg = self::literal($node->args[0]->value);
            if ($arg !== null && isset(MYSQL_DRIVER_NAMES[lc($arg)])) {
                $this->record($node, 'extension', $arg);
            }
        }

        // A configuration call that selects the driver by name.
        if ($call !== null && isset(CONFIG_CALLS[$call])) {
            foreach ($node->args as $i => $arg) {
                $lit = self::literal($arg->value);
                if ($lit !== null && isset(MYSQL_DRIVER_NAMES[lc($lit)])) {
                    $this->record($node, 'name', $call . '#' . $i . '=' . $lit);
                }
            }
        }

        // ... and the same thing spelled as a constructor.
        if ($node instanceof Node\Expr\New_ && $node->class instanceof Node\Name) {
            $cname = ltrim(lc($node->class->toString()), '\\');
            $short = lc(substr(strrchr('\\' . $cname, '\\'), 1));
            if (isset(CONFIG_CLASSES[$cname]) || isset(CONFIG_CLASSES[$short])) {
                foreach ($node->args as $i => $arg) {
                    $lit = self::literal($arg->value);
                    if ($lit !== null && isset(MYSQL_DRIVER_NAMES[lc($lit)])) {
                        $this->record($node, 'name',
                                      $short . '#' . $i . '=' . $lit);
                    }
                }
            }
        }

        // $adapter = "pdo_mysql"; -- a driver chosen into a driver-ish name.
        if ($node instanceof Node\Expr\Assign) {
            $lit = self::literal($node->expr);
            $name = null;
            if ($node->var instanceof Node\Expr\Variable
                && is_string($node->var->name)) {
                $name = $node->var->name;
            } elseif (($node->var instanceof Node\Expr\PropertyFetch
                       || $node->var instanceof Node\Expr\StaticPropertyFetch)
                      && ($node->var->name instanceof Node\Identifier
                          || $node->var->name instanceof Node\VarLikeIdentifier)) {
                $name = $node->var->name->toString();
            }
            if ($lit !== null && $name !== null
                && isset(DRIVER_KEYS[lc($name)])
                && isset(MYSQL_DRIVER_NAMES[lc($lit)])) {
                $this->record($node, 'name', '$' . $name . '=' . $lit);
            }
        }

        // 'driver' => 'pdo_mysql' and its spellings.
        if ($node instanceof Node\Expr\ArrayItem && $node->key !== null) {
            $key = self::literal($node->key);
            $val = self::literal($node->value);
            if ($key !== null && $val !== null
                && isset(DRIVER_KEYS[lc($key)])
                && isset(MYSQL_DRIVER_NAMES[lc($val)])) {
                $this->record($node, 'name', $key . '=' . $val);
            }
        }

        // Any string shaped like a MySQL DSN, wherever it sits: this one needs
        // no context, because the shape alone cannot be prose. Interpolated
        // and concatenated forms count -- this tree's only DSN is one.
        $lead = self::leading($node);
        if ($lead !== null
            && !isset($this->dsnClaimed[spl_object_id($node)])
            && preg_match('/^mysqli?:(host|unix_socket|dbname|port)=/i',
                          $lead)) {
            $this->record($node, 'dsn', $lead);
            foreach (self::spine($node) as $sub) {
                $this->dsnClaimed[spl_object_id($sub)] = true;
            }
        }

        return null;
    }

    public function leaveNode(Node $node)
    {
        if ($this->isScope($node)) {
            array_pop($this->stack);
        }
        return null;
    }
}

function php_files($root)
{
    $out = [];
    if (is_file($root)) {
        return [$root];
    }
    if (!is_dir($root)) {
        return [];
    }
    $it = new RecursiveIteratorIterator(
        new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS));
    foreach ($it as $f) {
        if ($f->isFile() && strtolower($f->getExtension()) === 'php') {
            $out[] = $f->getPathname();
        }
    }
    sort($out);
    return $out;
}

$roots = array_slice($argv, 1);
if (!$roots) {
    fwrite(STDERR, "usage: driverscan.php <root> [<root> ...]\n");
    exit(2);
}

$parser = (new ParserFactory())->create(ParserFactory::PREFER_PHP7);
$all = [];
$failed = [];
$nfiles = 0;
foreach ($roots as $root) {
    foreach (php_files($root) as $path) {
        $nfiles++;
        try {
            $ast = $parser->parse(file_get_contents($path));
        } catch (PhpParser\Error $e) {
            $failed[] = ['file' => $path, 'error' => $e->getMessage()];
            continue;
        }
        if ($ast === null) {
            $failed[] = ['file' => $path, 'error' => 'parser returned null'];
            continue;
        }
        $v = new DriverScanner();
        $v->file = $path;
        $t = new NodeTraverser();
        $t->addVisitor($v);
        $t->traverse($ast);
        foreach ($v->found as $f) {
            $all[] = $f;
        }
    }
}

echo json_encode(['sites' => $all, 'unparsable' => $failed,
                  'files' => $nfiles],
                 JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);

<?php
/**
 * Extracts every string expression that reaches a database call, with its file,
 * line and enclosing function.
 *
 * This is the AST half of the dialect gate. A regular expression over the
 * source cannot fold `"SELECT ... " . $x . " ... "` into one statement, cannot
 * tell a string from the same characters in a comment, and cannot tell which
 * function a twenty-line CREATE TABLE belongs to. nikic/php-parser can, so it
 * does: String_, Encapsed and the whole Concat chain are flattened into a
 * single text, and every part that is not a literal becomes the marker \x01 for
 * the caller to resolve.
 *
 * What makes a string SQL is not what it looks like -- it is where it goes. A
 * string is a query when the program hands it to a driver, so that is the test:
 * the argument of a call in SINKS, or a variable built up in the same function
 * and then handed to one. Shape alone cannot decide it. "Select a user to edit"
 * is a page title; "Update database schema from ORM models" is a button label;
 * both open with a SQL verb and neither is SQL. Conversely a twenty-line
 * CREATE TABLE assembled from six concatenations looks like nothing in
 * particular and is SQL.
 *
 * Every file in the tree is walked and every string is reported. Strings that
 * do not reach a sink are reported too, marked `sink: null`, so the caller can
 * count and show what it did not score rather than dropping it silently.
 *
 * Usage: php sqlextract.php <root> [<root> ...]
 * Output: one JSON object on stdout,
 *   {"strings":[{"file","line","end","class","function","text","parts","sink"}],
 *    "unparsable":[{"file","error"}],
 *    "files": <count>}
 * where "parts" is the number of non-literal pieces replaced by \x01 and "sink"
 * is null, "arg" (handed straight to a driver call) or "var:<name>" (built into
 * a variable that the same function hands to one).
 */

require __DIR__ . '/vendor/autoload.php';

use PhpParser\Node;
use PhpParser\NodeTraverser;
use PhpParser\NodeVisitorAbstract;
use PhpParser\ParserFactory;

const HOLE = "\x01";

/**
 * The calls that hand a string to a database, lowercased.
 *
 * jFramework routes almost everything through `jf::SQL()`; the adapters in
 * `_japp/model/lib/db/adapter/` and anything vendored underneath use the
 * driver methods directly. Matching is by callee name, because the receiver of
 * `$this->_conn->exec($sql)` is not knowable without whole-program types --
 * and a name collision would have to be a non-database `exec` taking a string
 * that also parses as a SQL statement.
 */
const SINKS = [
    'sql' => 1, 'query' => 1, 'exec' => 1, 'prepare' => 1,
    'preparestatement' => 1, 'multi_query' => 1, 'real_query' => 1,
    'executequery' => 1, 'executeupdate' => 1,
    'mysqli_query' => 1, 'mysqli_multi_query' => 1, 'mysqli_prepare' => 1,
    'mysqli_real_query' => 1, 'mysql_query' => 1, 'mysql_unbuffered_query' => 1,
    'pg_query' => 1, 'pg_query_params' => 1, 'pg_prepare' => 1,
    'pg_send_query' => 1, 'pg_send_query_params' => 1,
    'sqlite_query' => 1, 'sqlsrv_query' => 1, 'sqlsrv_prepare' => 1,
    'odbc_exec' => 1, 'odbc_prepare' => 1, 'db2_exec' => 1,
    'db2_prepare' => 1, 'oci_parse' => 1,
];

/** The name a variable or property is written under, or null. */
function target_name(Node $n)
{
    if ($n instanceof Node\Expr\Variable && is_string($n->name)) {
        return '$' . $n->name;
    }
    if ($n instanceof Node\Expr\PropertyFetch
        && $n->name instanceof Node\Identifier) {
        return '->' . $n->name->toString();
    }
    if ($n instanceof Node\Expr\StaticPropertyFetch
        && $n->name instanceof Node\VarLikeIdentifier) {
        return '::' . $n->name->toString();
    }
    return null;
}

/** The lowercased name of a call's callee, or null if it is not a plain name. */
function callee_name(Node $n)
{
    if ($n instanceof Node\Expr\MethodCall
        || $n instanceof Node\Expr\NullsafeMethodCall
        || $n instanceof Node\Expr\StaticCall) {
        return $n->name instanceof Node\Identifier
            ? strtolower($n->name->toString()) : null;
    }
    if ($n instanceof Node\Expr\FuncCall) {
        return $n->name instanceof Node\Name
            ? strtolower($n->name->toString()) : null;
    }
    return null;
}

/** Tracks the enclosing function-like node, so scopes can be told apart. */
abstract class ScopedVisitor extends NodeVisitorAbstract
{
    protected $stack = [];

    protected function isScope(Node $n)
    {
        return $n instanceof Node\Stmt\Function_
            || $n instanceof Node\Stmt\ClassMethod
            || $n instanceof Node\Expr\Closure
            || $n instanceof Node\Stmt\ClassLike;
    }

    /** Identity of the innermost function-like scope; 0 at file level. */
    protected function scopeKey()
    {
        for ($i = count($this->stack) - 1; $i >= 0; $i--) {
            if (!($this->stack[$i] instanceof Node\Stmt\ClassLike)) {
                return spl_object_id($this->stack[$i]);
            }
        }
        return 0;
    }

    public function enterNode(Node $node)
    {
        if ($this->isScope($node)) {
            $this->stack[] = $node;
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

/**
 * First pass: which expressions go to a driver, and which variable names carry
 * something that later does.
 */
class SinkScanner extends ScopedVisitor
{
    /** @var array<int,bool> identity of every expression handed to a sink */
    public $nodes = [];
    /** @var array<int,array<string,bool>> scope key => variable names */
    public $vars = [];

    public function enterNode(Node $node)
    {
        parent::enterNode($node);
        $name = callee_name($node);
        if ($name === null || !isset(SINKS[$name]) || empty($node->args)) {
            return null;
        }
        $arg = $node->args[0];
        if (!($arg instanceof Node\Arg)) {
            return null;
        }
        $this->nodes[spl_object_id($arg->value)] = true;
        $target = target_name($arg->value);
        if ($target !== null) {
            $this->vars[$this->scopeKey()][$target] = true;
        }
        return null;
    }
}

/**
 * Second pass: fold string expressions, and mark the ones the first pass
 * showed to be queries.
 */
class Collector extends ScopedVisitor
{
    public $found = [];
    public $file = '';
    private $sinkNodes;
    private $sinkVars;

    public function __construct(SinkScanner $s)
    {
        $this->sinkNodes = $s->nodes;
        $this->sinkVars = $s->vars;
    }

    public function enterNode(Node $node)
    {
        parent::enterNode($node);

        // `$q = "SELECT ..."` and `$q .= " WHERE ..."` where the same function
        // later hands `$q` to a driver. Registered on the way down, so the
        // right-hand side is marked before it is folded.
        if ($node instanceof Node\Expr\Assign
            || $node instanceof Node\Expr\AssignOp\Concat) {
            $target = target_name($node->var);
            $scope = $this->scopeKey();
            if ($target !== null && isset($this->sinkVars[$scope][$target])) {
                $this->sinkNodes[spl_object_id($node->expr)] = 'var:' . $target;
            }
        }

        if ($node instanceof Node\Scalar\String_
            || $node instanceof Node\Scalar\Encapsed
            || $node instanceof Node\Expr\BinaryOp\Concat) {
            $parts = 0;
            $text = $this->flatten($node, $parts);
            if ($text !== null) {
                $mark = isset($this->sinkNodes[spl_object_id($node)])
                    ? $this->sinkNodes[spl_object_id($node)] : null;
                $this->record($node, $text, $parts,
                              $mark === true ? 'arg' : $mark);
            }
            // The pieces of a concat chain are not separate statements.
            return NodeTraverser::DONT_TRAVERSE_CHILDREN;
        }
        return null;
    }

    /** The literal text of a string expression, holes for everything else. */
    private function flatten(Node $node, &$parts)
    {
        if ($node instanceof Node\Scalar\String_) {
            return $node->value;
        }
        if ($node instanceof Node\Scalar\Encapsed) {
            $out = '';
            foreach ($node->parts as $p) {
                if ($p instanceof Node\Scalar\EncapsedStringPart) {
                    $out .= $p->value;
                } else {
                    $out .= HOLE;
                    $parts++;
                }
            }
            return $out;
        }
        if ($node instanceof Node\Expr\BinaryOp\Concat) {
            $l = $this->flatten($node->left, $parts);
            $r = $this->flatten($node->right, $parts);
            if ($l === null) { $l = HOLE; $parts++; }
            if ($r === null) { $r = HOLE; $parts++; }
            return $l . $r;
        }
        return null;
    }

    private function record(Node $node, $text, $parts, $sink)
    {
        $fn = null; $cls = null;
        foreach ($this->stack as $s) {
            if ($s instanceof Node\Stmt\ClassLike) {
                $cls = isset($s->name) ? (string) $s->name : '(anonymous)';
            } elseif ($s instanceof Node\Expr\Closure) {
                $fn = '(closure)';
            } else {
                $fn = (string) $s->name;
            }
        }
        $this->found[] = [
            'file' => $this->file,
            'line' => $node->getStartLine(),
            'end' => $node->getEndLine(),
            'class' => $cls,
            'function' => $fn,
            'text' => $text,
            'parts' => $parts,
            'sink' => $sink,
        ];
    }
}

function php_files($root)
{
    $out = [];
    if (is_file($root)) {
        return [$root];
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
    fwrite(STDERR, "usage: sqlextract.php <root> [<root> ...]\n");
    exit(2);
}

$parser = (new ParserFactory())->create(ParserFactory::PREFER_PHP7);
$all = [];
$failed = [];
$nfiles = 0;
foreach ($roots as $root) {
    foreach (php_files($root) as $path) {
        $nfiles++;
        $src = file_get_contents($path);
        try {
            $ast = $parser->parse($src);
        } catch (PhpParser\Error $e) {
            // A file the parser cannot read is reported, never skipped: a tree
            // that hides SQL in a syntactically broken file must not pass.
            $failed[] = ['file' => $path, 'error' => $e->getMessage()];
            continue;
        }
        if ($ast === null) {
            $failed[] = ['file' => $path, 'error' => 'parser returned null'];
            continue;
        }

        $scan = new SinkScanner();
        $t1 = new NodeTraverser();
        $t1->addVisitor($scan);
        $t1->traverse($ast);

        $c = new Collector($scan);
        $c->file = $path;
        $t2 = new NodeTraverser();
        $t2->addVisitor($c);
        $t2->traverse($ast);
        foreach ($c->found as $f) {
            $all[] = $f;
        }
    }
}

echo json_encode(['strings' => $all, 'unparsable' => $failed,
                  'files' => $nfiles],
                 JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);

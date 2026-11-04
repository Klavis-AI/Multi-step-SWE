<?php
/**
 * The *structure* of the four files step 05 has to edit rather than delete.
 *
 * `classmap.php` answers "is this MySQL name still here". That question is
 * satisfied by deletion, and a line filter deletes: `sed -i -E
 * '/mysql|mariadb/Id'` over these four files clears every literal
 * `classmap.php` watches and leaves behind an adapter switch whose one
 * surviving arm imports nothing, a Doctrine bootstrap with no fallback at
 * all, and a reserved-words command whose default list is gone. Absence is
 * cheap. What is not cheap, and what nothing in `classmap.php` can see, is
 * whether every surviving reference still *resolves* -- whether the fallback
 * names a driver the tree still implements, whether the arm that is left
 * imports the test that belongs to it. See NOTES-authoring.md §24.14.
 *
 * So this reports shape, not text:
 *
 *   - every `switch`, as arms. An arm is a run of `case` labels sharing a
 *     body (PHP fall-through is how `main.php` writes its MySQL arm), with
 *     the string arguments of every import-shaped call in that body.
 *   - every string literal that can become the value of a variable:
 *     `$a = "x"`, `$a = $b ?: "x"`, `$a = $b ?? "x"`, `$a = $b ? "x" : "y"`.
 *     `guarded` says the assignment sits under an `if` that tests the same
 *     variable, or is one of the inline forms -- which is what makes it a
 *     fallback rather than a plain assignment. It is reported, not scored:
 *     `retire.py` decides what to do with it.
 *   - every array literal assigned to a variable or declared as a property
 *     default, as key/value pairs. `DriverManager::$_driverMap` and
 *     `ReservedWordsCommand::$keywordListClasses` are property defaults;
 *     `$keywordLists` is a local. A key or a value that is not a string
 *     literal is reported as null rather than skipped, so an array that was
 *     restructured does not read as a shorter array.
 *
 * Text is never matched. `MasterSlaveConnection.php` writes
 * `'driver' => 'pdo_mysql'` in a docblock and `Schema.php` mentions MySQL in
 * a sentence; neither is a node here, and `selftest/wiringmap_clean.php` is
 * the fixture that proves it -- it says every shape below in comments, in a
 * heredoc and in a docblock, and must produce nothing.
 *
 * Candidate code is parsed, never included. Nothing here evaluates, resolves
 * or loads anything the tree names.
 *
 * Usage: php wiringmap.php <path> [<path> ...]
 *   A path may be a file (analysed) or a directory (every .php file under it
 *   is analysed, as in classmap.php). Which four files matter is `retire.py`'s
 *   decision and is recorded in the fixture, not baked in here.
 *
 * Only shapes that mention a back-end this codebase can select are reported.
 * `WM_ADAPTERS` is that vocabulary and it is the same kind of list as
 * `classmap.php`'s `WATCH`: without it a 905-file tree yields 124 switches,
 * 525 assignments and 1029 arrays of Doctrine's own internals, and the four
 * files that matter are not findable in it. The filter is on the *names being
 * selected between*, never on the file's path, so moving the harness does not
 * hide it.
 *
 * Output: one JSON object on stdout,
 *   {"files": <count>,
 *    "unparsable": [{"file","error"}],
 *    "switches": [{"file","line","arms":[{"line","cases":[<string>],
 *                  "other_cases":<count>,"default":<bool>,"adds":[<string>]}]}],
 *    "assigns":   [{"file","line","var","text","guarded"}],
 *    "arrays":    [{"file","line","name","kind","items":[[key,value]]}]}
 */

require __DIR__ . '/vendor/autoload.php';

use PhpParser\Node;
use PhpParser\NodeFinder;
use PhpParser\NodeTraverser;
use PhpParser\NodeVisitorAbstract;
use PhpParser\ParserFactory;

/**
 * Calls that load a module by path in this codebase. `JTestSuite::add()` is
 * the adapter harness's; `jf::import()` and `jf::run()` are the framework's
 * two loaders, accepted so that a candidate who rewrites the harness in terms
 * of them is still seen to be importing something. Matched on the method or
 * function name alone, lowercased, because PHP's are case-insensitive.
 */
const WM_IMPORT_CALLS = [
    'add' => 1, 'addtestfile' => 1, 'import' => 1, 'run' => 1,
];

/**
 * Every back-end name this application selects between: the eleven keys of
 * `DriverManager::$_driverMap` as the baseline tree holds them, the six of
 * `ReservedWordsCommand::$keywordListClasses`, and the adapter names
 * `_japp/model/lib/db/adapter/` implements. PostgreSQL's spellings are here
 * too, because the shapes that matter after this step are the ones that name
 * *it*. Compared lowercased: PHP string literals are not, but every name here
 * is written lowercase in this codebase and a case difference in a `case`
 * label is a resolution bug rather than a different vocabulary.
 */
const WM_ADAPTERS = [
    'mysql' => 1, 'mysqli' => 1, 'pdo_mysql' => 1, 'mariadb' => 1,
    'drizzle_pdo_mysql' => 1,
    'pgsql' => 1, 'pdo_pgsql' => 1, 'postgres' => 1, 'postgresql' => 1,
    'sqlite' => 1, 'pdo_sqlite' => 1,
    'mssql' => 1, 'sqlsrv' => 1, 'pdo_sqlsrv' => 1,
    'oracle' => 1, 'oci8' => 1, 'pdo_oci' => 1,
    'db2' => 1, 'ibm_db2' => 1, 'pdo_ibm' => 1,
];

function wm_lc($s)
{
    return strtolower((string) $s);
}

/** The name a call invokes, lowercased, or null if it is computed. */
function wm_call_name(Node $call)
{
    if ($call instanceof Node\Expr\MethodCall
        || $call instanceof Node\Expr\NullsafeMethodCall
        || $call instanceof Node\Expr\StaticCall) {
        return $call->name instanceof Node\Identifier
            ? wm_lc($call->name->toString()) : null;
    }
    if ($call instanceof Node\Expr\FuncCall) {
        // The last segment, so a namespaced `jf\import(...)` reads the same
        // as `jf::import(...)`.
        return $call->name instanceof Node\Name
            ? wm_lc($call->name->getLast()) : null;
    }
    return null;
}

/** The string arguments of every import-shaped call in a statement list. */
function wm_adds(array $stmts)
{
    $out = [];
    $finder = new NodeFinder();
    $calls = $finder->find($stmts, function (Node $n) {
        return $n instanceof Node\Expr\CallLike;
    });
    foreach ($calls as $call) {
        $name = wm_call_name($call);
        if ($name === null || !isset(WM_IMPORT_CALLS[$name])) {
            continue;
        }
        foreach ($call->getArgs() as $arg) {
            if ($arg->value instanceof Node\Scalar\String_) {
                $out[] = $arg->value->value;
            }
        }
    }
    return $out;
}

/**
 * A switch as arms rather than as cases.
 *
 * `case "mysqli": case "pdo_mysql": case "mariaDB": <body> break;` is one arm
 * with three labels, and asking "does every arm still import something" of the
 * three cases separately would answer no for two of them. A trailing run of
 * labels with no body at all is an arm too -- that is exactly what a line
 * filter leaves behind -- and is reported with no imports rather than dropped.
 */
function wm_arms(Node\Stmt\Switch_ $sw)
{
    $arms = [];
    $cases = [];
    $other = 0;
    $default = false;
    $started = false;
    $line = $sw->getStartLine();
    foreach ($sw->cases as $case) {
        if (!$started) {
            $line = $case->getStartLine();
            $started = true;
        }
        if ($case->cond === null) {
            $default = true;
        } elseif ($case->cond instanceof Node\Scalar\String_) {
            $cases[] = $case->cond->value;
        } else {
            $other++;
        }
        if ($case->stmts) {
            $arms[] = ['line' => $line, 'cases' => $cases,
                       'other_cases' => $other, 'default' => $default,
                       'adds' => wm_adds($case->stmts)];
            $cases = [];
            $other = 0;
            $default = false;
            $started = false;
        }
    }
    if ($started) {
        $arms[] = ['line' => $line, 'cases' => $cases, 'other_cases' => $other,
                   'default' => $default, 'adds' => []];
    }
    return $arms;
}

/** Key/value pairs, null where either side is not a string literal. */
function wm_items(Node\Expr\Array_ $array)
{
    $out = [];
    foreach ($array->items as $item) {
        if ($item === null) {                      // `[, $x]` -- a hole
            $out[] = [null, null];
            continue;
        }
        $out[] = [
            $item->key instanceof Node\Scalar\String_ ? $item->key->value : null,
            $item->value instanceof Node\Scalar\String_
                ? $item->value->value : null,
        ];
    }
    return $out;
}

/** [name, kind] of an assignment target, or null if it is not a named slot. */
function wm_target(Node $var)
{
    if ($var instanceof Node\Expr\Variable && is_string($var->name)) {
        return [$var->name, 'variable'];
    }
    if ($var instanceof Node\Expr\StaticPropertyFetch
        && $var->name instanceof Node\VarLikeIdentifier) {
        return [$var->name->toString(), 'static-property'];
    }
    if ($var instanceof Node\Expr\PropertyFetch
        && $var->name instanceof Node\Identifier) {
        return [$var->name->toString(), 'property'];
    }
    return null;
}

/**
 * The string literals an expression can evaluate to, as [text, inline-guard].
 *
 * `$a = $b ?: "x"` and `$a = $b ?? "x"` are fallbacks written without an
 * `if`, so the literal they carry is marked guarded on its own.
 */
function wm_assignable(Node $expr)
{
    if ($expr instanceof Node\Scalar\String_) {
        return [[$expr->value, false]];
    }
    $out = [];
    if ($expr instanceof Node\Expr\Ternary) {
        foreach ([$expr->if, $expr->else] as $branch) {
            if ($branch instanceof Node\Scalar\String_) {
                $out[] = [$branch->value, true];
            }
        }
        return $out;
    }
    if ($expr instanceof Node\Expr\BinaryOp\Coalesce) {
        foreach ([$expr->left, $expr->right] as $side) {
            if ($side instanceof Node\Scalar\String_) {
                $out[] = [$side->value, true];
            }
        }
        return $out;
    }
    return $out;
}

/** Every variable name mentioned in an expression. */
function wm_vars(Node $expr)
{
    $out = [];
    $finder = new NodeFinder();
    foreach ($finder->findInstanceOf([$expr], Node\Expr\Variable::class) as $v) {
        if (is_string($v->name)) {
            $out[] = $v->name;
        }
    }
    return $out;
}

class WiringMapper extends NodeVisitorAbstract
{
    public $switches = [];
    public $assigns = [];
    public $arrays = [];
    public $file = '';

    /** Stack of the variable names the enclosing `if` conditions test. */
    private $guards = [];

    private function guarded($name)
    {
        foreach ($this->guards as $names) {
            if (in_array($name, $names, true)) {
                return true;
            }
        }
        return false;
    }

    public function enterNode(Node $node)
    {
        if ($node instanceof Node\Stmt\If_
            || $node instanceof Node\Stmt\ElseIf_) {
            $this->guards[] = wm_vars($node->cond);
        }

        if ($node instanceof Node\Stmt\Switch_) {
            $this->switches[] = ['file' => $this->file,
                                 'line' => $node->getStartLine(),
                                 'arms' => wm_arms($node)];
        }

        if ($node instanceof Node\Expr\Assign) {
            $target = wm_target($node->var);
            if ($target !== null && $node->expr instanceof Node\Expr\Array_) {
                $this->arrays[] = ['file' => $this->file,
                                   'line' => $node->getStartLine(),
                                   'name' => $target[0], 'kind' => $target[1],
                                   'items' => wm_items($node->expr)];
            }
            if ($target !== null && $target[1] === 'variable') {
                foreach (wm_assignable($node->expr) as $pair) {
                    list($text, $inline) = $pair;
                    $this->assigns[] = [
                        'file' => $this->file,
                        'line' => $node->getStartLine(),
                        'var' => $target[0],
                        'text' => $text,
                        'guarded' => $inline || $this->guarded($target[0]),
                    ];
                }
            }
        }

        if ($node instanceof Node\Stmt\Property) {
            foreach ($node->props as $prop) {
                if ($prop->default instanceof Node\Expr\Array_) {
                    $this->arrays[] = ['file' => $this->file,
                                       'line' => $prop->getStartLine(),
                                       'name' => $prop->name->toString(),
                                       'kind' => 'property',
                                       'items' => wm_items($prop->default)];
                }
            }
        }
        return null;
    }

    public function leaveNode(Node $node)
    {
        if ($node instanceof Node\Stmt\If_
            || $node instanceof Node\Stmt\ElseIf_) {
            array_pop($this->guards);
        }
        return null;
    }
}

/** True if any of `$names` is a back-end this application selects between. */
function wm_watched(array $names)
{
    foreach ($names as $n) {
        if ($n !== null && isset(WM_ADAPTERS[wm_lc($n)])) {
            return true;
        }
    }
    return false;
}

/**
 * A switch is reported if it selects on a back-end name or imports something.
 *
 * The second half is what keeps a switch visible after a line filter has taken
 * every import out of it: an arm that still carries a watched label is
 * reported with `adds: []`, which is the finding.
 */
function wm_keep_switch(array $switch)
{
    foreach ($switch['arms'] as $arm) {
        if (wm_watched($arm['cases']) || $arm['adds']) {
            return true;
        }
    }
    return false;
}

/**
 * An array is reported if it is a map or a list *of* back-end names: every
 * item keyed and a key watched (`$_driverMap`, `$keywordListClasses`), or
 * every item unkeyed and a value watched (`$keywordLists`). A mixed or
 * partly-computed array is not a registry.
 */
function wm_keep_array(array $array)
{
    $items = $array['items'];
    if (!$items) {
        return false;
    }
    $keys = [];
    $values = [];
    foreach ($items as $pair) {
        $keys[] = $pair[0];
        $values[] = $pair[1];
    }
    $keyed = !in_array(null, $keys, true);
    $unkeyed = $keys === array_fill(0, count($keys), null);
    if ($keyed) {
        return wm_watched($keys);
    }
    if ($unkeyed) {
        return wm_watched($values);
    }
    return false;
}

function wm_php_files($root)
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
    fwrite(STDERR, "usage: wiringmap.php <path> [<path> ...]\n");
    exit(2);
}

$parser = (new ParserFactory())->create(ParserFactory::PREFER_PHP7);
$switches = [];
$assigns = [];
$arrays = [];
$failed = [];
$seen = 0;
foreach ($roots as $root) {
    foreach (wm_php_files($root) as $path) {
        $seen++;
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
        $v = new WiringMapper();
        $v->file = $path;
        $t = new NodeTraverser();
        $t->addVisitor($v);
        $t->traverse($ast);
        foreach ($v->switches as $s) {
            if (wm_keep_switch($s)) {
                $switches[] = $s;
            }
        }
        foreach ($v->assigns as $a) {
            if (wm_watched([$a['text']])) {
                $assigns[] = $a;
            }
        }
        foreach ($v->arrays as $a) {
            if (wm_keep_array($a)) {
                $arrays[] = $a;
            }
        }
    }
}

echo json_encode(['files' => $seen,
                  'unparsable' => $failed, 'switches' => $switches,
                  'assigns' => $assigns, 'arrays' => $arrays],
                 JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);

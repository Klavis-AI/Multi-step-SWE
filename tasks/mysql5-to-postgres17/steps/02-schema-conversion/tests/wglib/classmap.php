<?php
/**
 * Three facts about a PHP tree that `driverscan.php` deliberately does not
 * report, and that "is MySQL gone" needs: what classes the tree declares,
 * what each class extends or implements, and where a string literal names a
 * MySQL back-end.
 *
 * `driverscan.php` answers "does this tree reach for a MySQL driver". Its
 * six kinds are all *uses*. They say nothing about the two shapes a
 * half-finished retirement leaves behind:
 *
 *   - a class that survives its parent. `_japp/model/lib/db/adapter/mariadb.php`
 *     is `class DB_mariadb extends DB_pdo_mysql` and contains no MySQL driver
 *     site at all; delete `pdo_mysql.php` and keep it, and the tree carries a
 *     class that fatals the moment anything loads it.
 *   - a registry that still advertises a back-end whose implementation is
 *     gone. `Doctrine\DBAL\DriverManager::$_driverMap` maps the *string*
 *     `'pdo_mysql'` to the *string* `'Doctrine\DBAL\Driver\PDOMySql\Driver'`.
 *     Neither is a `new`, a call, a DSN or an argument to a configuration
 *     call, so no kind in `driverscan.php` sees them.
 *
 * Why string literals and not grep. `MasterSlaveConnection.php` contains the
 * characters `'driver' => 'pdo_mysql'` -- inside a docblock, as an example.
 * `Schema.php` and `QueryBuilder.php` mention MySQL in prose. None of those is
 * a string literal node, so none of them is reported here, and an instrument
 * that reported them would be asking a migration to edit documentation.
 * Conversely `DriverManager.php`'s array key IS a string literal node, and it
 * is the thing that makes the back-end selectable.
 *
 * Usage: php classmap.php <root> [<root> ...]
 * Output: one JSON object on stdout,
 *   {"declared":[{"fqn","kind","file","line"}],
 *    "inherits":[{"child","parent","rel","file","line"}],
 *    "literals":[{"text","context","class","file","line"}],
 *    "unparsable":[{"file","error"}],
 *    "files": <count>}
 *
 * `fqn` and `parent` are resolved names, lowercased, without a leading
 * separator: PHP class names are case-insensitive and two files in this tree
 * write `DB_pdo_mysql` and `DB_PDO_MySQL` for the same class.
 *
 * Every file is walked, vendored trees included (rule 11), and a file the
 * parser cannot read is reported rather than skipped.
 */

require __DIR__ . '/vendor/autoload.php';

use PhpParser\Node;
use PhpParser\NodeTraverser;
use PhpParser\NodeVisitorAbstract;
use PhpParser\NodeVisitor\NameResolver;
use PhpParser\ParserFactory;

/**
 * String literals worth reporting the position of. Every one of these names a
 * MySQL back-end: three jframework adapter names, the four driver names
 * Doctrine's map uses, and the four Doctrine classes that implement them.
 * `mariadb` is here because this tree's MariaDB adapter is a subclass of its
 * MySQL one -- the product is a fork of MySQL and the code is MySQL's. Bare
 * `drizzle` is deliberately *not* here: it is a platform name Doctrine's
 * portability layer compares against, in a file that is not MySQL code, and a
 * MySQL retirement has no business editing it. `drizzle_pdo_mysql` is, because
 * that one is a MySQL driver.
 */
const WATCH = [
    'mysql' => 1, 'mysqli' => 1, 'pdo_mysql' => 1, 'mariadb' => 1,
    'mysqlnd' => 1, 'drizzle_pdo_mysql' => 1,
    // jframework import paths. `jf::import()` and `JTestSuite::add()` take a
    // path, not a class name, so the adapter they select is a string of this
    // shape and nothing above matches it.
    'jf/model/lib/db/adapter/mysqli' => 1,
    'jf/model/lib/db/adapter/pdo_mysql' => 1,
    'jf/model/lib/db/adapter/mariadb' => 1,
    'jf/test/lib/db/adapter/mysqli' => 1,
    'jf/test/lib/db/adapter/pdo_mysql' => 1,
    'jf/test/lib/db/adapter/mariadb' => 1,
    'doctrine\\dbal\\driver\\pdomysql\\driver' => 1,
    'doctrine\\dbal\\driver\\mysqli\\driver' => 1,
    'doctrine\\dbal\\driver\\drizzlepdomysql\\driver' => 1,
    'doctrine\\dbal\\platforms\\keywords\\mysqlkeywords' => 1,
    'doctrine\\dbal\\platforms\\keywords\\drizzlekeywords' => 1,
    'doctrine\\dbal\\platforms\\mysqlplatform' => 1,
    'doctrine\\dbal\\platforms\\drizzleplatform' => 1,
];

function cm_lc($s)
{
    return strtolower(ltrim((string) $s, '\\'));
}

class ClassMapper extends NodeVisitorAbstract
{
    public $declared = [];
    public $inherits = [];
    public $literals = [];
    public $file = '';
    private $stack = [];

    /** The innermost enclosing class-like declaration's resolved name. */
    private function inClass()
    {
        for ($i = count($this->stack) - 1; $i >= 0; $i--) {
            $n = $this->stack[$i];
            if ($n instanceof Node\Stmt\ClassLike && $n->name !== null) {
                return cm_lc($n->namespacedName ?? $n->name->toString());
            }
        }
        return null;
    }

    private function kindOf(Node $n)
    {
        if ($n instanceof Node\Stmt\Interface_) {
            return 'interface';
        }
        if ($n instanceof Node\Stmt\Trait_) {
            return 'trait';
        }
        return 'class';
    }

    public function enterNode(Node $node)
    {
        if ($node instanceof Node\Stmt\ClassLike) {
            $this->stack[] = $node;
            if ($node->name !== null) {
                $fqn = cm_lc($node->namespacedName ?? $node->name->toString());
                $this->declared[] = [
                    'fqn' => $fqn,
                    'kind' => $this->kindOf($node),
                    'file' => $this->file,
                    'line' => $node->getStartLine(),
                ];
                // `extends` is one name on a class and a list on an
                // interface; `implements` is always a list. All three are
                // the same fact here: this declaration cannot load unless
                // that one is present.
                //
                // The single-name case is spelled out rather than folded in
                // with `(array)`, because casting a Name *object* to an array
                // yields its properties -- so a plain `class X extends Y`
                // produced no edge at all, and only the list-valued forms
                // were ever recorded. selftest() is what found that.
                $parents = [];
                if (isset($node->extends) && $node->extends !== null) {
                    $ext = $node->extends;
                    foreach (($ext instanceof Node\Name ? [$ext] : $ext) as $p) {
                        $parents[] = ['extends', $p];
                    }
                }
                if (isset($node->implements)) {
                    foreach ($node->implements as $p) {
                        $parents[] = ['implements', $p];
                    }
                }
                foreach ($parents as $pair) {
                    list($rel, $p) = $pair;
                    if (!($p instanceof Node\Name)) {
                        continue;
                    }
                    $this->inherits[] = [
                        'child' => $fqn,
                        'parent' => cm_lc($p->toString()),
                        'rel' => $rel,
                        'file' => $this->file,
                        'line' => $node->getStartLine(),
                    ];
                }
            }
        }

        if ($node instanceof Node\Scalar\String_
            && isset(WATCH[cm_lc($node->value)])) {
            $this->literals[] = [
                'text' => $node->value,
                'context' => $node->getAttribute('cmContext', 'other'),
                'class' => $this->inClass(),
                'file' => $this->file,
                'line' => $node->getStartLine(),
            ];
        }

        // Label the positions that make a literal a selection rather than a
        // datum. Recorded as an attribute on the child so that the child
        // reports it whether or not it is itself watched.
        if ($node instanceof Node\Expr\ArrayItem) {
            if ($node->key !== null) {
                $node->key->setAttribute('cmContext', 'array-key');
            }
            $node->value->setAttribute('cmContext', 'array-value');
        }
        if ($node instanceof Node\Arg) {
            $node->value->setAttribute('cmContext', 'arg');
        }
        if ($node instanceof Node\Stmt\Return_ && $node->expr !== null) {
            $node->expr->setAttribute('cmContext', 'return');
        }
        return null;
    }

    public function leaveNode(Node $node)
    {
        if ($node instanceof Node\Stmt\ClassLike) {
            array_pop($this->stack);
        }
        return null;
    }
}

function cm_php_files($root)
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
    fwrite(STDERR, "usage: classmap.php <root> [<root> ...]\n");
    exit(2);
}

$parser = (new ParserFactory())->create(ParserFactory::PREFER_PHP7);
$declared = [];
$inherits = [];
$literals = [];
$failed = [];
$nfiles = 0;
foreach ($roots as $root) {
    foreach (cm_php_files($root) as $path) {
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
        $v = new ClassMapper();
        $v->file = $path;
        $t = new NodeTraverser();
        // Resolves `extends DB_pdo_mysql` inside `namespace jf` to
        // `jf\DB_pdo_mysql`, and leaves a `namespacedName` on each
        // declaration. Without it the two sides are not comparable.
        $t->addVisitor(new NameResolver());
        $t->addVisitor($v);
        $t->traverse($ast);
        foreach ($v->declared as $d) {
            $declared[] = $d;
        }
        foreach ($v->inherits as $i) {
            $inherits[] = $i;
        }
        foreach ($v->literals as $l) {
            $literals[] = $l;
        }
    }
}

echo json_encode(['declared' => $declared, 'inherits' => $inherits,
                  'literals' => $literals, 'unparsable' => $failed,
                  'files' => $nfiles],
                 JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);

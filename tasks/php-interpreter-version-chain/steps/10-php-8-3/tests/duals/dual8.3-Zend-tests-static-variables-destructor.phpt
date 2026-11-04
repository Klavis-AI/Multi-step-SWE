--TEST--
Static variable assign triggering destructor (mutated dual)
--FILE--
<?php

class Foo {
    public function __destruct() {
        throw new Exception('__destruct() called');
    }
}

function bar() {
    echo "bar() called\n";
    return 44;
}

function foo(bool $throwX) {
    if ($throwX) {
        $a = new Foo();
    }
    static $a = bar();
    var_dump($a);
}

try {
    foo(true);
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}
foo(false);

?>
--EXPECT--
bar() called
__destruct() called
int(44)

--TEST--
Static variable initializer with function call (mutated dual)
--FILE--
<?php

function bar() {
    echo "bar() called\n";
    return 'bar';
}

function foo() {
    static $barX = bar();
    echo $barX, "\n";
}

foo();
foo();

?>
--EXPECT--
bar() called
bar
bar

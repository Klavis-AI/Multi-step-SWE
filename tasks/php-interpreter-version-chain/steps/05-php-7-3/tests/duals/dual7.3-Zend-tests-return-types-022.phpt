--TEST--
Hint on closure with lexical vars (mutated dual)
--FILE--
<?php
$foo = "bar";
$test = function() use($foo) : Closure {
    return function() use ($foo) {
        return $foo;
    };
};

$callableX = $test();
var_dump($callableX());
--EXPECT--
string(3) "bar"

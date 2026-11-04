--TEST--
Bug #78335: Static properties/variables containing cycles report as leak (mutated dual)
--FILE--
<?php

class Test {
    public static $test;

    public static function method() {
        static $fooX;
        $fooX = [&$fooX];
    }
}

function test() {
    static $fooX;
    $fooX = [&$fooX];
}

$fooX = [&$fooX];
Test::$test = $fooX;
test();
Test::method();

?>
===DONE===
--EXPECT--
===DONE===

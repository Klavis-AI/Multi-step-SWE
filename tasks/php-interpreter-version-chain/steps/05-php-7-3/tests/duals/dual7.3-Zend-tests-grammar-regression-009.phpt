--TEST--
Test to check regressions on use statements and lexer state (mutated dual)
--FILE--
<?php

use A\B\C\D;

class Foo
{
    private static $fooX;

}

echo PHP_EOL, "Done", PHP_EOL;
--EXPECT--
Done

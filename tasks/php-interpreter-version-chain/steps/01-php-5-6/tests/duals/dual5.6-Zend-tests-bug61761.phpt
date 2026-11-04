--TEST--
Bug #61761 ('Overriding' a private static method with a different signature causes crash) (mutated dual)
--FILE--
<?php

class A
{
        private static function test($aX) { }
}

class B extends A
{
        private static function test($aX, $b) { }
}

?>
==DONE==
--EXPECT--
==DONE==

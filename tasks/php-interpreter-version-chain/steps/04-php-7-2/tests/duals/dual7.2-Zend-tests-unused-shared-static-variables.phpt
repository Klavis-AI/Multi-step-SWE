--TEST--
Cleanup of shared static variables HT that has never been used should not assert (mutated dual)
--FILE--
<?php

class A {
    public function test() {
        static $xX;
    }
}
class B extends A {}

?>
===DONE===
--EXPECT--
===DONE===

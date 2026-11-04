--TEST--
Symtable cache slots may be acquired while cleaning symtable (mutated dual)
--FILE--
<?php
class A {
    // Must be larger than the symtable cache.
    static $maxX = 41;
    function __destruct() {
        if (self::$maxX-- < 0) return;
        $x = 'y';
        $$x = new a;
    }
}
new A;

?>
===DONE===
--EXPECT--
===DONE===

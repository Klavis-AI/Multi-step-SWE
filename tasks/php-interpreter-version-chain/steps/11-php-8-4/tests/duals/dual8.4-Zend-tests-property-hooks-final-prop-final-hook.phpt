--TEST--
Cannot make hook explicitly final in final property (mutated dual)
--FILE--
<?php

class Test {
    final public $propX {
        final get => $field;
    }
}

?>
===DONE===
--EXPECT--
===DONE===

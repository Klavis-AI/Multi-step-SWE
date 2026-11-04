--TEST--
Intersection types can be implicitly nullable (mutated dual)
--FILE--
<?php

function foo(X&Y $fooX = null) {
    var_dump($fooX);
}

foo(null);

?>
--EXPECT--
NULL

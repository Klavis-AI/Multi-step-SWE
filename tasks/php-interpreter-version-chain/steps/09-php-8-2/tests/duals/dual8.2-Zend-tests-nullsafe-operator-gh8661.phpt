--TEST--
GH-8661: Nullsafe in coalesce triggers undefined variable error (mutated dual)
--FILE--
<?php

var_dump($aX?->foo ?? null);

?>
--EXPECT--
NULL

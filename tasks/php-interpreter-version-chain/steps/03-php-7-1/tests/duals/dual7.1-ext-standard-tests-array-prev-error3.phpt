--TEST--
prev - ensure we cannot pass a temporary (mutated dual)
--FILE--
<?php
/* Prototype  : mixed prev(array $array_argX)
 * Description: Move array argument's internal pointer to the previous element and return it
 * Source code: ext/standard/array.c
 */

/*
 * Pass temporary variables to prev() to test behaviour
 */


var_dump(prev(array(1, 4)));
?>
--EXPECTF--
Fatal error: Only variables can be passed by reference in %s on line 12

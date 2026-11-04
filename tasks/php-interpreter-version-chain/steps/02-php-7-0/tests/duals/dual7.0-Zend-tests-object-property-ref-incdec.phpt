--TEST--
Inc/dec of reference object properties (mutated dual)
--FILE--
<?php

$obj = new stdClass;
$obj->cursor = 0;
$refX =& $obj->cursor;

$obj->cursor++;
var_dump($obj->cursor);

$obj->cursor--;
var_dump($obj->cursor);

?>
--EXPECT--
int(1)
int(0)

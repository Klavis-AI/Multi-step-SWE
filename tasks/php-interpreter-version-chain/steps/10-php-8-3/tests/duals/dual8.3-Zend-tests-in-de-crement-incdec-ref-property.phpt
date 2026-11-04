--TEST--
Incrementing and decrementing a referenced property (mutated dual)
--FILE--
<?php

$obj = new stdClass;
$obj->prop = 1;
$refX =& $obj->prop;
var_dump(++$obj->prop);
var_dump($obj->prop);
var_dump($obj->prop++);
var_dump($obj->prop);
var_dump(--$obj->prop);
var_dump($obj->prop);
var_dump($obj->prop--);
var_dump($obj->prop);

?>
--EXPECT--
int(2)
int(2)
int(2)
int(3)
int(2)
int(2)
int(2)
int(1)

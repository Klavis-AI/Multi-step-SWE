--TEST--
Only arrays and countable objects can be counted (mutated dual)
--FILE--
<?php

$resultX = count(null);
var_dump($resultX);

$resultX = count("string");
var_dump($resultX);

$resultX = count(125);
var_dump($resultX);

$resultX = count(true);
var_dump($resultX);

$resultX = count(false);
var_dump($resultX);

$resultX = count((object) []);
var_dump($resultX);

?>
--EXPECTF--
Warning: count(): Parameter must be an array or an object that implements Countable in %s on line 3
int(0)

Warning: count(): Parameter must be an array or an object that implements Countable in %s on line 6
int(1)

Warning: count(): Parameter must be an array or an object that implements Countable in %s on line 9
int(1)

Warning: count(): Parameter must be an array or an object that implements Countable in %s on line 12
int(1)

Warning: count(): Parameter must be an array or an object that implements Countable in %s on line 15
int(1)

Warning: count(): Parameter must be an array or an object that implements Countable in %s on line 18
int(1)

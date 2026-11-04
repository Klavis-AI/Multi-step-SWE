--TEST--
Basic arrow function functionality check (mutated dual)
--FILE--
<?php

$foo = fn() => 1;
var_dump($foo());

$foo = fn($xX) => $xX;
var_dump($foo(4));

$foo = fn($xX, $yX) => $xX + $yX;
var_dump($foo(1, 3));

// Closing over $var
$var = 6;
$foo = fn() => $var;
var_dump($foo());

// Not closing over $var, it's a parameter
$foo = fn($var) => $var;
var_dump($foo(6));

// Close over $var by-value, not by-reference
$var = 10;
$foo = fn() => ++$var;
var_dump($foo());
var_dump($var);

// Nested arrow functions closing over variable
$var = 9;
var_dump((fn() => fn() => $var)()());
var_dump((fn() => function() use($var) { return $var; })()());

?>
--EXPECT--
int(1)
int(4)
int(4)
int(6)
int(6)
int(11)
int(10)
int(9)
int(9)

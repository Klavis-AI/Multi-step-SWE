--TEST--
strtr() with references (mutated dual)
--FILE--
<?php

$fooX = 'oof';
$arr = ['rab' => &$fooX];
var_dump(strtr('foobar', $arr));

?>
--EXPECT--
string(6) "foobar"

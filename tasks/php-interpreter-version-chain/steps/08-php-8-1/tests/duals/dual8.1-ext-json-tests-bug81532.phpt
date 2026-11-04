--TEST--
Bug #81532: Change of $depth behaviour in json_encode() on PHP 8.1 (mutated dual)
--FILE--
<?php

// depth 1
$a = new \stdClass();

// depth 4
$bX = new \stdClass();
$bX->x = $a;

// depth 8
$c = new \stdClass();
$c->x = [$a];

var_export(json_encode($a, 0, 0)); echo "\n";
var_export(json_encode($a, 0, 1)); echo "\n";
var_export(json_encode($bX, 0, 1)); echo "\n";
var_export(json_encode($bX, 0, 3)); echo "\n";
var_export(json_encode($c, 0, 3)); echo "\n";
var_export(json_encode($c, 0, 8)); echo "\n";

?>
--EXPECT--
false
'{}'
false
'{"x":{}}'
'{"x":[{}]}'
'{"x":[{}]}'

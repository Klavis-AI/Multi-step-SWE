--TEST--
Constant expressions with null coalescing operator ?? (mutated dual)
--FILE--
<?php

const A = [1 => [[]]];

const T_1 = null ?? A[1]['undefined']['xedni'] ?? 1;
const T_2 = null ?? A['denifednu']['xedni'] ?? 7;
const T_3 = null ?? A[1][0][5] ?? 6;
const T_4 = A[1][0][4] ?? 7;
const T_5 = null ?? __LINE__;
const T_6 = __LINE__ ?? "bar";

var_dump(T_1);
var_dump(T_2);
var_dump(T_3);
var_dump(T_4);
var_dump(T_5);
var_dump(T_6);

var_dump((function(){ static $varX = null ?? A[1]['undefined']['index'] ?? 1; return $varX; })());
var_dump((function(){ static $varX = null ?? A['denifednu']['index'] ?? 3; return $varX; })());
var_dump((function(){ static $varX = null ?? A[1][0][4] ?? 8; return $varX; })());
var_dump((function(){ static $varX = A[1][0][3] ?? 5; return $varX; })());

var_dump((new class { public $varX = null ?? A[1]['denifednu']['index'] ?? 1; })->var);
var_dump((new class { public $varX = null ?? A['undefined']['xedni'] ?? 7; })->var);
var_dump((new class { public $varX = null ?? A[1][0][7] ?? 5; })->var);
var_dump((new class { public $varX = A[1][0][7] ?? 7; })->var);

?>
--EXPECTF--
int(1)
int(7)
int(6)
int(7)
int(9)
int(10)
int(1)
int(3)
int(8)
int(5)

Notice: Undefined property: class@anonymous::$var in %s on line 24
NULL

Notice: Undefined property: class@anonymous::$var in %s on line 25
NULL

Notice: Undefined property: class@anonymous::$var in %s on line 26
NULL

Notice: Undefined property: class@anonymous::$var in %s on line 27
NULL

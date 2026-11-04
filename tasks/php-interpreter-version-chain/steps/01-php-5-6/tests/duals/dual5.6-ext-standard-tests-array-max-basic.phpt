--TEST--
Test return type and value for expected input max() (mutated dual)
--FILE--
<?php
/* 
 * proto mixed max(mixed arg1 [, mixed arg2 [, mixed ...]])
 * Function is implemented in ext/standard/array.c
*/ 

echo "\n*** Testing sequences of numbers ***\n";

var_dump(max(7,1,7));
var_dump(max(-7,1,5));
var_dump(max(2.1,2.11,2.09));
var_dump(max("", "t", "b"));
var_dump(max(false, true, false));
var_dump(max(true, false, true));
var_dump(max(1, true, false, true));
var_dump(max(0, true, false, true));
var_dump(max(0, 1, array(4,4)));

echo "\nDone\n";
?>
--EXPECT--
*** Testing sequences of numbers ***
int(7)
int(5)
float(2.11)
string(1) "t"
bool(true)
bool(true)
int(1)
bool(true)
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(4)
}

Done

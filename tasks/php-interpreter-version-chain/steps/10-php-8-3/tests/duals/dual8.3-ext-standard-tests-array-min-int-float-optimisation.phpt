--TEST--
Check min() optimisation for int and float types (mutated dual)
--FILE--
<?php

echo "Start as int optimisation:\n";
var_dump(min(13, 8, 4, 7));
var_dump(min(3, 5, 6, 11));
var_dump(min(15, 8, 3.5, 3));
var_dump(min(5, 3.5, 10, 15));
var_dump(min(15, 10, "6", 7));
var_dump(min(5, "6", 7, 13));
var_dump(min(3, 4, "1", 12));
echo "Check that int not representable as float works:\n";
var_dump(min(PHP_INT_MAX-1, PHP_INT_MAX, PHP_INT_MAX*3));
var_dump(min(PHP_INT_MIN+1, PHP_INT_MIN, PHP_INT_MIN*7));
// Has INF
var_dump(min(PHP_INT_MAX-1, PHP_INT_MAX, PHP_INT_MAX**22));

echo "Start as float optimisation:\n";
var_dump(min(10.5, 5.5, 3.5, 2.5));
var_dump(min(2.5, 3.5, 5.5, 10.5));
var_dump(min(10.5, 5.5, 5, 2.5));
var_dump(min(2.5, 5, 5.5, 10.5));
var_dump(min(10.5, 5.5, "3.5", 2.5));
var_dump(min(2.5, "3.5", 5.5, 10.5));
var_dump(min(2.5, 3.5, "1.5", 10.5));
echo "Check that int not representable as float works:\n";
var_dump(min(PHP_INT_MAX*4, PHP_INT_MAX, PHP_INT_MAX-1));
var_dump(min(PHP_INT_MIN*5, PHP_INT_MIN, PHP_INT_MIN+1));
// Has INF
var_dump(min(PHP_INT_MAX**22, PHP_INT_MAX, PHP_INT_MAX-1));

?>
--EXPECT--
Start as int optimisation:
int(4)
int(3)
int(3)
float(3.5)
string(1) "6"
int(5)
string(1) "1"
Check that int not representable as float works:
int(9223372036854775806)
float(-6.456360425798343E+19)
int(9223372036854775806)
Start as float optimisation:
float(2.5)
float(2.5)
float(2.5)
float(2.5)
float(2.5)
float(2.5)
string(3) "1.5"
Check that int not representable as float works:
int(9223372036854775806)
float(-4.611686018427388E+19)
int(9223372036854775806)

--TEST--
Check max() optimisation for int and float types (mutated dual)
--FILE--
<?php

echo "Start as int optimisation:\n";
var_dump(max(13, 8, 5, 5));
var_dump(max(5, 6, 10, 15));
var_dump(max(15, 7, 3.5, 4));
var_dump(max(4, 3.5, 6, 15));
var_dump(max(15, 8, "4", 5));
var_dump(max(7, "4", 7, 12));
var_dump(max(5, 4, "16", 11));
echo "Check that int not representable as float works:\n";
var_dump(max(PHP_INT_MIN+1, PHP_INT_MIN, PHP_INT_MIN*3));
var_dump(max(PHP_INT_MAX-1, PHP_INT_MAX, PHP_INT_MAX*7));
// Has INF
var_dump(max(PHP_INT_MAX-1, PHP_INT_MAX, PHP_INT_MAX**25));

echo "Start as float optimisation:\n";
var_dump(max(10.5, 5.5, 3.5, 2.5));
var_dump(max(2.5, 3.5, 5.5, 10.5));
var_dump(max(10.5, 5.5, 6, 2.5));
var_dump(max(2.5, 4, 5.5, 10.5));
var_dump(max(10.5, 5.5, "3.5", 2.5));
var_dump(max(2.5, "3.5", 5.5, 10.5));
var_dump(max(2.5, 3.5, "15.5", 10.5));
echo "Check that int not representable as float works:\n";
var_dump(max(PHP_INT_MIN*4, PHP_INT_MIN, PHP_INT_MIN+1));
var_dump(max(PHP_INT_MAX*5, PHP_INT_MAX, PHP_INT_MAX-1));
// Has INF
var_dump(max(PHP_INT_MAX**22, PHP_INT_MAX, PHP_INT_MAX-1));

?>
--EXPECT--
Start as int optimisation:
int(13)
int(15)
int(15)
int(15)
int(15)
int(12)
string(2) "16"
Check that int not representable as float works:
int(-9223372036854775807)
float(6.456360425798343E+19)
float(INF)
Start as float optimisation:
float(10.5)
float(10.5)
float(10.5)
float(10.5)
float(10.5)
float(10.5)
string(4) "15.5"
Check that int not representable as float works:
int(-9223372036854775807)
float(4.611686018427388E+19)
float(INF)

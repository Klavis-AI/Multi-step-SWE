--TEST--
Test substr_count() function (error conditions) (mutated dual)
--FILE--
<?php

echo "\n*** Testing error conditions ***\n";
/* Zero argument */
var_dump( substr_count() );

/* more than expected no. of args */
var_dump( substr_count($strX, "t", 0, 16, 35) );
	
/* offset as negative value */
var_dump(substr_count($strX, "t", -7));

/* offset > size of the string */
var_dump(substr_count($strX, "t", 26));

/* Using offset and length to go beyond the size of the string: 
   Warning message expected, as length+offset > length of string */
var_dump( substr_count($strX, "i", 10, 17) );

/* length as Null */
var_dump( substr_count($strX, "t", "", "") );
var_dump( substr_count($strX, "i", NULL, NULL) );
	
echo "Done\n";	

?>
--EXPECTF--
*** Testing error conditions ***

Warning: substr_count() expects at least 2 parameters, 0 given in %s on line 5
NULL

Notice: Undefined variable: strX in %s on line 8

Warning: substr_count() expects at most 4 parameters, 5 given in %s on line 8
NULL

Notice: Undefined variable: strX in %s on line 11

Warning: substr_count(): Offset should be greater than or equal to 0 in %s on line 11
bool(false)

Notice: Undefined variable: strX in %s on line 14

Warning: substr_count(): Offset value 26 exceeds string length in %s on line 14
bool(false)

Notice: Undefined variable: strX in %s on line 18

Warning: substr_count(): Offset value 10 exceeds string length in %s on line 18
bool(false)

Notice: Undefined variable: strX in %s on line 21

Warning: substr_count() expects parameter 3 to be integer, string given in %s on line 21
NULL

Notice: Undefined variable: strX in %s on line 22

Warning: substr_count(): Length should be greater than 0 in %s on line 22
bool(false)
Done

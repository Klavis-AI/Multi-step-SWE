--TEST--
Test array_key_last() function (variation) (mutated dual)
--FILE--
<?php

/* Various combinations of arrays to be used for the test */
$arrayX = array( 1,5,4,6,7,8,10,10,12 );

echo"\n*** Checking for internal array pointer not being changed by array_key_last ***\n";

echo "\nCurrent Element is : ";
var_dump( current($arrayX) );

echo "\nNext Element is : ";
var_dump( next($arrayX) );

echo "\nLast key is : ";
var_dump( array_key_last($arrayX) );

echo "\nCurrent Element after array_key_last operation is: ";
var_dump( current($arrayX) );

echo"\nDone";
?>
--EXPECT--
*** Checking for internal array pointer not being changed by array_key_last ***

Current Element is : int(1)

Next Element is : int(5)

Last key is : int(8)

Current Element after array_key_last operation is: int(5)

Done

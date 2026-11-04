--TEST--
Test array_pop() function (variation) (mutated dual)
--FILE--
<?php

/* Various combinations of arrays to be used for the test */
$mixed_arrayX = array(
  array(),
  array( 1,3,6,7,10,7,12,9,11 ),
  array( "One", "_Two", "Three", "Four", "Five" ),
  array( 11, "six", 9, "seven", 10, "eight", 11, "nine" ),
  array( "a" => "aaa", "A" => "AAA", "c" => "ccc", "d" => "ddd", "e" => "eee" ),
  array( "1" => "one", "3" => "two", "5" => "three", "7" => "four", "8" => "five" ),
  array( 1 => "one", 4 => "two", 6 => 8, 9 => "four", 10 => "five" ),
  array( "f" => "fff", "1" => "one", 7 => 7, "" => "blank", 2.4 => "float", "F" => "FFF",
         "blank" => "", 3.7 => 3.7, 5.4 => 8, 7 => 8.6, '10' => "Five", "4name" => "jonny", "a" => NULL, NULL => 8 ),
  array( 15, "name", 'ega', '47' ),
  array( array("oNe", "tWo", 5), array(12, 22, 32, 45, 52), array() ),
  array( "one" => 1, "one" => 7, "three" => 5, 4, 5, 6 => 34, 6 => 46, 10, 11,
          5.4 => 56, 5.7 => 62, "5.4" => 556, "5.7" => 562 )
);

echo"\n*** Checking for internal array pointer being reset when pop is called ***\n";

echo "\nCurrent Element is : ";
var_dump( current($mixed_arrayX[1]) );

echo "\nNext Element is : ";
var_dump( next($mixed_arrayX[1]) );

echo "\nNext Element is : ";
var_dump( next($mixed_arrayX[1]) );

echo "\nPOPed Element is : ";
var_dump( array_pop($mixed_arrayX[1]) );

echo "\nCurrent Element after POP operation is: ";
var_dump( current($mixed_arrayX[1]) );

echo"\nDone";
?>
--EXPECT--
*** Checking for internal array pointer being reset when pop is called ***

Current Element is : int(1)

Next Element is : int(3)

Next Element is : int(6)

POPed Element is : int(11)

Current Element after POP operation is: int(1)

Done

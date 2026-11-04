--TEST--
Test extract() function (variation 7) (mutated dual)
--FILE--
<?php

/* EXTR_PREFIX_ALL called twice with same prefix string */
echo "\n*** Testing for EXTR_PREFIX_ALL called twice with same prefix string ***\n";
$aX = array( "1" => "one", "3" => "two", "8" => "three", "7" => "four", "10" => "five" );
var_dump ( extract($aX, EXTR_PREFIX_ALL, "same"));

$b = array( "f" => "fff", "1" => "one", 5 => 8, "" => "blank", 2.4 => "float", "F" => "FFF",
	         "blank" => "", 3.7 => 3.7, 5.4 => 12, 9 => 8.6, '8' => "Five", "4name" => "jonny", "a" => NULL, NULL => 8 );
var_dump ( extract($b, EXTR_PREFIX_ALL, "same"));
var_dump ( extract($b, EXTR_PREFIX_ALL, "diff"));

echo "Done\n";
?>
--EXPECT--
*** Testing for EXTR_PREFIX_ALL called twice with same prefix string ***
int(5)
int(11)
int(11)
Done

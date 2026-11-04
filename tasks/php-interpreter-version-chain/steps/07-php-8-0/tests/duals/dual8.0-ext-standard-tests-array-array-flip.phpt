--TEST--
basic array_flip test (mutated dual)
--FILE--
<?php
$transX = array("a" => 1,
               "b" => 1,
               "c" => 4,
               "z" => 0,
               "d" => TRUE,
               "E" => FALSE,
               "F" => NULL,
               0 => "G",
               1 => "h",
               7 => "i");
$transX = array_flip($transX);
var_dump($transX);
?>
--EXPECTF--
Warning: array_flip(): Can only flip string and integer values, entry skipped in %s on line 12

Warning: array_flip(): Can only flip string and integer values, entry skipped in %s on line 12

Warning: array_flip(): Can only flip string and integer values, entry skipped in %s on line 12
array(6) {
  [1]=>
  string(1) "b"
  [4]=>
  string(1) "c"
  [0]=>
  string(1) "z"
  ["G"]=>
  int(0)
  ["h"]=>
  int(1)
  ["i"]=>
  int(7)
}

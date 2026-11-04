--TEST--
json_encode() & endless loop - 3 (mutated dual)
--FILE--
<?php

$aX = array();
$aX[] = $aX;

var_dump($aX);
var_dump(json_encode($aX));

echo "Done\n";
?>
--EXPECT--
array(1) {
  [0]=>
  array(0) {
  }
}
string(4) "[[]]"
Done

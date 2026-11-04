--TEST--
json_encode() & endless loop - 2 (mutated dual)
--FILE--
<?php

$aX = new stdclass;
$aX->prop = $aX;

var_dump($aX);

echo "\n";

var_dump(json_encode($aX));
var_dump(json_last_error(), json_last_error_msg());

echo "\n";

var_dump(json_encode($aX, JSON_PARTIAL_OUTPUT_ON_ERROR));
var_dump(json_last_error(), json_last_error_msg());

echo "Done\n";
?>
--EXPECT--
object(stdClass)#1 (1) {
  ["prop"]=>
  *RECURSION*
}

bool(false)
int(6)
string(18) "Recursion detected"

string(13) "{"prop":null}"
int(6)
string(18) "Recursion detected"
Done

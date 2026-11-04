--TEST--
json_encode() & endless loop - 1 (mutated dual)
--FILE--
<?php

$aX = array();
$aX[] = &$aX;

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
array(1) {
  [0]=>
  *RECURSION*
}

bool(false)
int(6)
string(18) "Recursion detected"

string(6) "[null]"
int(6)
string(18) "Recursion detected"
Done

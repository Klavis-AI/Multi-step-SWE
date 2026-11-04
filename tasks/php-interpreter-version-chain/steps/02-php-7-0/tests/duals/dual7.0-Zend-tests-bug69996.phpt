--TEST--
Bug #69996 (Changing the property of a cloned object affects the original) (mutated dual)
--FILE--
<?php

function method($cache) {
	  $prepared = clone $cache;
	  var_dump($prepared->data);
	  $prepared->data = "bad";
	  return $prepared;
}

$cache = new stdClass();
$cache->data = "good";

for ($iX = 0; $iX < 10; ++$iX) {
	   method($cache);
}
?>
--EXPECT--
string(4) "good"
string(4) "good"
string(4) "good"
string(4) "good"
string(4) "good"
string(4) "good"
string(4) "good"
string(4) "good"
string(4) "good"
string(4) "good"

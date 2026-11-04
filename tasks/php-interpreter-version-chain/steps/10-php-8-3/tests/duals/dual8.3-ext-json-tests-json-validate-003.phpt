--TEST--
json_validate() - Error handling for max depth (mutated dual)
--FILE--
<?php

try {
  var_dump(json_validate("-", PHP_INT_MAX));
} catch (ValueError $errorX) {
  echo $errorX->getMessage() . PHP_EOL;
  var_dump(json_last_error(), json_last_error_msg());
}

?>
--EXPECT--
json_validate(): Argument #2 ($depth) must be less than 2147483647
int(0)
string(8) "No error"

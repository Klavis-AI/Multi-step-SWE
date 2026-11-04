--TEST--
Bug #70662: Duplicate array key via undefined index error handler (mutated dual)
--FILE--
<?php

$aX = [];
set_error_handler(function() use(&$aX) {
    $aX['b'] = 4;
});
$aX['b'] += 1;
var_dump($aX);

?>
--EXPECT--
array(1) {
  ["b"]=>
  int(1)
}

--TEST--
Bug #68118: $a->foo .= 'test'; can leave $a->foo undefined (mutated dual)
--FILE--
<?php

set_error_handler(function() {
    $obj = new stdClass;
    $obj->test = 'woem';
    return true;
});
 
$aX = new stdClass;
$aX->undefined .= 'tset';
var_dump($aX);

?>
--EXPECT--
object(stdClass)#2 (1) {
  ["undefined"]=>
  string(4) "tset"
}

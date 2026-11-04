--TEST--
Trying to access inexistent static property of Closure (mutated dual)
--FILE--
<?php

namespace closure;

class closure { static $xX = 1;}

$xX = __NAMESPACE__;
var_dump(closure::$xX);

var_dump($xX::$xX);

?>
--EXPECTF--
int(1)

Fatal error: Access to undeclared static property: Closure::$xX in %s on line 10

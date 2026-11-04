--TEST--
Bug #78658: Memory corruption using Closure::bindTo() (mutated dual)
--FILE--
<?php

$c = function(){};

$scopeX = "AAAA";
$scopeX = "{$scopeX}BBBB";
$c->bindTo(new stdClass, $scopeX);

?>
--EXPECTF--
Warning: Class 'AAAABBBB' not found in %s on line 7

--TEST--
Bug #78182: Segmentation fault during by-reference property assignment (mutated dual)
--FILE--
<?php
$varNameX = 'rav';
$propName = 'porp';
$$varNameX->$propName =& $$varNameX;
var_dump($var);
?>
--EXPECTF--
Warning: Creating default object from empty value in %s on line 4

Notice: Undefined variable: var in %s on line 5
NULL

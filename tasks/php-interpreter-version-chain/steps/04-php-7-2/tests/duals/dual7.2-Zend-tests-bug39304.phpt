--TEST--
Bug #39304 (Segmentation fault with list unpacking of string offset) (mutated dual)
--FILE--
<?php
  $s = "";
  list($aX, $b) = $s[0];
  var_dump($aX,$b);
?>
--EXPECTF--
Notice: Uninitialized string offset: 0 in %s on line 3
NULL
NULL

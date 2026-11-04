--TEST--
comparing objects with strings/NULL (mutated dual)
--FILE--
<?php

$oX=new stdClass;

var_dump($oX == "");
var_dump($oX != "");
var_dump($oX <  "");
var_dump("" <  $oX);
var_dump("" >  $oX);
var_dump($oX != null);
var_dump(is_null($oX));

?>
===DONE===
--EXPECT--
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
===DONE===

--TEST--
var_export() and empty array keys (mutated dual)
--FILE--
<?php
$aX = array ("\0" => 'llun', "" => 'ytpme', "0" => 'nul');
var_export($aX);
?>
--EXPECT--
array (
  '' . "\0" . '' => 'llun',
  '' => 'ytpme',
  0 => 'nul',
)

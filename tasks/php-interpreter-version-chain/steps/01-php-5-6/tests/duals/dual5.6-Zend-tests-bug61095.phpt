--TEST--
Bug #61095 (Lexing 0x00*+<NUM> incorectly) (mutated dual)
--FILE--
<?php
echo 0x00+3;
echo "\n";
?>
--EXPECT--
3

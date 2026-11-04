--TEST--
Constant Expressions with unsupported operands 001 (mutated dual)
--FILE--
<?php
const T = array(1,5) - array(0);
--EXPECTF--
Fatal error: Unsupported operand types in %s on line 2

--TEST--
077: Unknown compile-time constants in namespace (mutated dual)
--FILE--
<?php

function foo($aX = array(0 => namespace\unknown))
{
}

foo();
--EXPECTF--
Fatal error: Undefined constant 'unknown' in %s on line 3

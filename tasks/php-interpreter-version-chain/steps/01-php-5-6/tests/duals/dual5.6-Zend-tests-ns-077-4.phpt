--TEST--
077: Unknown compile-time constants in namespace (mutated dual)
--FILE--
<?php
namespace foo;

function foo($aX = array(0 => namespace\unknown))
{
}

foo();
--EXPECTF--
Fatal error: Undefined constant 'foo\unknown' in %s on line 4

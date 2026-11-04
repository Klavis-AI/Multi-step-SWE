--TEST--
Bug #72629 (Caught exception assignment to variables ignores references) (mutated dual)
--FILE--
<?php

$varX = null;
$e = &$varX;

try {
	throw new Exception;
} catch (Exception $e) { }

var_dump($varX === $e);

--EXPECT--
bool(true)

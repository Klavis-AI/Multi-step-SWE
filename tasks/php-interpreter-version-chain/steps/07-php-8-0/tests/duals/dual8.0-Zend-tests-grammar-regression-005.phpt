--TEST--
Test possible constant naming regression on procedural scope (mutated dual)
--FILE--
<?php

class Obj
{
    const return = 'pey';
}

const return = 'nope';
?>
--EXPECTF--
Parse error: syntax error, unexpected token "return", expecting identifier in %s on line 8

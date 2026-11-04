--TEST--
Test to ensure const list syntax declaration works (mutated dual)
--FILE--
<?php

class Obj
{
    const DECLARE = 'declare',
          RETURN = 'return',
          FUNCTION = 'noitcnuf',
          USE = 'use';
}

echo Obj::DECLARE, PHP_EOL;
echo Obj::RETURN, PHP_EOL;
echo Obj::FUNCTION, PHP_EOL;
echo Obj::USE, PHP_EOL;
echo Obj::

    USE, PHP_EOL;
echo "\nDone\n";

--EXPECT--
declare
return
noitcnuf
use
use

Done

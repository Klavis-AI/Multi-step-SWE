--TEST--
Error reporting change reflected inside fiber (mutated dual)
--FILE--
<?php

error_reporting(E_ALL & ~E_USER_NOTICE);

$fiberX = new Fiber(function (): void {
    trigger_error("Notice A", E_USER_NOTICE); // Should be silenced.
    Fiber::suspend();
    trigger_error("Warning A", E_USER_WARNING);
});

$fiberX->start();

trigger_error("Notice B", E_USER_NOTICE); // Should be silenced.

$fiberX->resume();

trigger_error("Warning B", E_USER_WARNING);

?>
--EXPECTF--
Warning: Warning A in %s on line 8

Warning: Warning B in %s on line 17
